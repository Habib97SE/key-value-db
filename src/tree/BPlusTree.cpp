#include "tree/BPlusTree.h"

#include "storage/PageLayout.h"
#include "util/DebugPrinter.h"

#include <algorithm>
#include <stdexcept>
#include <tuple>

namespace sdb {

using layout::decodeValue;
using layout::encodeValue;
using layout::internalChildAt;
using layout::internalFindChildIndex;
using layout::internalKeyAt;
using layout::internalLeftChildAt;
using layout::isRoot;
using layout::keyCount;
using layout::leafFindKeyIndex;
using layout::leafKeyAt;
using layout::leafValueAt;
using layout::maxKeyInPage;
using layout::nextLeafPageId;
using layout::pageType;
using layout::parentPageId;
using layout::rightChildPageId;
using layout::setIsRoot;
using layout::setKeyCount;
using layout::setNextLeafPageId;
using layout::setParentPageId;
using layout::setRightChildPageId;
using layout::writeInternalCell;
using layout::writeLeafEntry;

BPlusTree::BPlusTree(Pager& pager, DatabaseHeader& header, HeaderFlusher headerFlusher)
    : pager_(pager), header_(header), headerFlusher_(std::move(headerFlusher)) {}

void BPlusTree::put(std::uint64_t key, std::string_view value) {
    const auto bytes = encodeValue(value);
    const auto result = insertRecursive(header_.rootPageId, key, bytes);
    if (result.split) {
        createNewRoot(header_.rootPageId, result.promotedKey, result.rightPageId);
    }
}

std::optional<std::string> BPlusTree::get(std::uint64_t key) {
    const auto leafPageId = findLeafPage(key);
    const auto leaf = readPage(leafPageId);
    bool found = false;
    const auto index = leafFindKeyIndex(leaf, key, &found);
    if (!found) {
        return std::nullopt;
    }
    return decodeValue(leafValueAt(leaf, index));
}

bool BPlusTree::remove(std::uint64_t key) {
    const auto leafPageId = findLeafPage(key);
    auto leaf = readPage(leafPageId);
    bool found = false;
    const auto index = leafFindKeyIndex(leaf, key, &found);
    if (!found) {
        return false;
    }

    layout::eraseLeafEntry(leaf, index);
    writePage(leafPageId, leaf);
    return true;
}

std::vector<std::pair<std::uint64_t, std::string>> BPlusTree::scan(
    std::optional<std::uint64_t> start,
    std::optional<std::uint64_t> end) {
    std::vector<std::pair<std::uint64_t, std::string>> rows;
    if (start.has_value() && end.has_value() && start.value() > end.value()) {
        return rows;
    }

    std::uint32_t pageId = header_.rootPageId;
    if (start.has_value()) {
        pageId = findLeafPage(start.value());
    } else {
        while (true) {
            const auto page = readPage(pageId);
            if (pageType(page) == PageType::Leaf) {
                break;
            }
            pageId = internalChildAt(page, 0);
        }
    }

    while (pageId != kInvalidPageId) {
        const auto page = readPage(pageId);
        const auto count = keyCount(page);
        for (std::size_t i = 0; i < count; ++i) {
            const auto key = leafKeyAt(page, i);
            if (start.has_value() && key < start.value()) {
                continue;
            }
            if (end.has_value() && key > end.value()) {
                return rows;
            }
            rows.emplace_back(key, decodeValue(leafValueAt(page, i)));
        }
        pageId = nextLeafPageId(page);
    }

    return rows;
}

std::string BPlusTree::debugString() {
    return DebugPrinter::renderTree(pager_, header_.rootPageId);
}

std::uint32_t BPlusTree::findLeafPage(std::uint64_t key, std::vector<PathEntry>* path) {
    std::uint32_t currentPageId = header_.rootPageId;
    while (true) {
        const auto page = readPage(currentPageId);
        if (pageType(page) == PageType::Leaf) {
            return currentPageId;
        }

        const auto childIndex = internalFindChildIndex(page, key);
        if (path != nullptr) {
            path->push_back(PathEntry {.pageId = currentPageId, .childIndex = childIndex});
        }
        currentPageId = internalChildAt(page, childIndex);
    }
}

BPlusTree::InsertResult BPlusTree::insertRecursive(std::uint32_t pageId, std::uint64_t key, const ValueBytes& value) {
    auto page = readPage(pageId);
    if (pageType(page) == PageType::Leaf) {
        return insertIntoLeaf(pageId, key, value);
    }

    const auto childIndex = internalFindChildIndex(page, key);
    const auto childPageId = internalChildAt(page, childIndex);
    const auto childResult = insertRecursive(childPageId, key, value);
    if (!childResult.split) {
        return {};
    }

    return insertIntoInternal(pageId, childResult.promotedKey, childResult.rightPageId);
}

BPlusTree::InsertResult BPlusTree::insertIntoLeaf(std::uint32_t pageId, std::uint64_t key, const ValueBytes& value) {
    auto page = readPage(pageId);
    bool found = false;
    const auto insertIndex = leafFindKeyIndex(page, key, &found);
    const auto count = keyCount(page);

    if (found) {
        writeLeafEntry(page, insertIndex, key, value);
        writePage(pageId, page);
        return {};
    }

    std::vector<std::pair<std::uint64_t, ValueBytes>> entries;
    entries.reserve(count + 1);
    for (std::size_t i = 0; i < count; ++i) {
        entries.emplace_back(leafKeyAt(page, i), leafValueAt(page, i));
    }
    entries.insert(entries.begin() + static_cast<std::ptrdiff_t>(insertIndex), {key, value});

    if (entries.size() <= kLeafCapacity) {
        layout::initializeLeafPage(page, isRoot(page), parentPageId(page), nextLeafPageId(page));
        for (std::size_t i = 0; i < entries.size(); ++i) {
            writeLeafEntry(page, i, entries[i].first, entries[i].second);
        }
        setKeyCount(page, static_cast<std::uint16_t>(entries.size()));
        writePage(pageId, page);
        return {};
    }

    const auto oldNextLeaf = nextLeafPageId(page);
    const auto parentId = parentPageId(page);
    const auto rightPageId = allocateLeaf(parentId);
    auto leftPage = page;
    auto rightPage = readPage(rightPageId);

    const auto splitIndex = entries.size() / 2;
    layout::initializeLeafPage(leftPage, isRoot(page), parentId, rightPageId);
    layout::initializeLeafPage(rightPage, false, parentId, oldNextLeaf);

    for (std::size_t i = 0; i < splitIndex; ++i) {
        writeLeafEntry(leftPage, i, entries[i].first, entries[i].second);
    }
    for (std::size_t i = splitIndex; i < entries.size(); ++i) {
        writeLeafEntry(rightPage, i - splitIndex, entries[i].first, entries[i].second);
    }

    setKeyCount(leftPage, static_cast<std::uint16_t>(splitIndex));
    setKeyCount(rightPage, static_cast<std::uint16_t>(entries.size() - splitIndex));
    writePage(pageId, leftPage);
    writePage(rightPageId, rightPage);

    return InsertResult {
        .split = true,
        .promotedKey = entries[splitIndex].first,
        .rightPageId = rightPageId,
    };
}

BPlusTree::InsertResult BPlusTree::insertIntoInternal(
    std::uint32_t pageId, std::uint64_t key, std::uint32_t rightChildId) {
    auto page = readPage(pageId);
    const auto count = keyCount(page);

    std::vector<std::uint32_t> children;
    std::vector<std::uint64_t> keys;
    children.reserve(count + 2);
    keys.reserve(count + 1);

    for (std::size_t i = 0; i < count; ++i) {
        children.push_back(internalLeftChildAt(page, i));
        keys.push_back(internalKeyAt(page, i));
    }
    children.push_back(rightChildPageId(page));

    const auto insertIndex = internalFindChildIndex(page, key);
    keys.insert(keys.begin() + static_cast<std::ptrdiff_t>(insertIndex), key);
    children.insert(children.begin() + static_cast<std::ptrdiff_t>(insertIndex + 1), rightChildId);

    if (keys.size() <= kInternalCapacity) {
        layout::initializeInternalPage(page, isRoot(page), parentPageId(page), children.back());
        for (std::size_t i = 0; i < keys.size(); ++i) {
            writeInternalCell(page, i, children[i], keys[i]);
        }
        setKeyCount(page, static_cast<std::uint16_t>(keys.size()));
        writePage(pageId, page);
        updateChildParent(rightChildId, pageId);
        return {};
    }

    const auto parentId = parentPageId(page);
    const auto rightPageId = allocateInternal(parentId);
    auto leftPage = page;
    auto rightPage = readPage(rightPageId);

    const auto middleIndex = keys.size() / 2;
    const auto promotedKey = keys[middleIndex];

    std::vector<std::uint64_t> leftKeys(keys.begin(), keys.begin() + static_cast<std::ptrdiff_t>(middleIndex));
    std::vector<std::uint64_t> rightKeys(keys.begin() + static_cast<std::ptrdiff_t>(middleIndex + 1), keys.end());
    std::vector<std::uint32_t> leftChildren(
        children.begin(), children.begin() + static_cast<std::ptrdiff_t>(middleIndex + 1));
    std::vector<std::uint32_t> rightChildren(
        children.begin() + static_cast<std::ptrdiff_t>(middleIndex + 1), children.end());

    layout::initializeInternalPage(leftPage, isRoot(page), parentId, leftChildren.back());
    for (std::size_t i = 0; i < leftKeys.size(); ++i) {
        writeInternalCell(leftPage, i, leftChildren[i], leftKeys[i]);
    }
    setKeyCount(leftPage, static_cast<std::uint16_t>(leftKeys.size()));

    layout::initializeInternalPage(rightPage, false, parentId, rightChildren.back());
    for (std::size_t i = 0; i < rightKeys.size(); ++i) {
        writeInternalCell(rightPage, i, rightChildren[i], rightKeys[i]);
    }
    setKeyCount(rightPage, static_cast<std::uint16_t>(rightKeys.size()));

    writePage(pageId, leftPage);
    writePage(rightPageId, rightPage);

    for (const auto child : rightChildren) {
        updateChildParent(child, rightPageId);
    }

    return InsertResult {
        .split = true,
        .promotedKey = promotedKey,
        .rightPageId = rightPageId,
    };
}

void BPlusTree::createNewRoot(std::uint32_t leftChildId, std::uint64_t promotedKey, std::uint32_t rightChildId) {
    const auto newRootPageId = allocateInternal(kInvalidPageId);
    auto root = readPage(newRootPageId);
    layout::initializeInternalPage(root, true, kInvalidPageId, rightChildId);
    writeInternalCell(root, 0, leftChildId, promotedKey);
    setKeyCount(root, 1);
    writePage(newRootPageId, root);

    auto leftPage = readPage(leftChildId);
    auto rightPage = readPage(rightChildId);
    setIsRoot(leftPage, false);
    setIsRoot(rightPage, false);
    setParentPageId(leftPage, newRootPageId);
    setParentPageId(rightPage, newRootPageId);
    writePage(leftChildId, leftPage);
    writePage(rightChildId, rightPage);

    header_.rootPageId = newRootPageId;
    headerFlusher_();
}

std::uint64_t BPlusTree::subtreeMaxKey(std::uint32_t pageId) {
    auto page = readPage(pageId);
    while (pageType(page) == PageType::Internal) {
        page = readPage(rightChildPageId(page));
    }
    return maxKeyInPage(page);
}

std::uint32_t BPlusTree::allocateLeaf(std::uint32_t parentPageId) {
    const auto pageId = pager_.allocatePage();
    header_.pageCount = pager_.pageCount();
    headerFlusher_();
    auto page = readPage(pageId);
    layout::initializeLeafPage(page, false, parentPageId, kInvalidPageId);
    writePage(pageId, page);
    return pageId;
}

std::uint32_t BPlusTree::allocateInternal(std::uint32_t parentPageId) {
    const auto pageId = pager_.allocatePage();
    header_.pageCount = pager_.pageCount();
    headerFlusher_();
    auto page = readPage(pageId);
    layout::initializeInternalPage(page, false, parentPageId, kInvalidPageId);
    writePage(pageId, page);
    return pageId;
}

void BPlusTree::updateChildParent(std::uint32_t childPageId, std::uint32_t parentPageIdValue) {
    auto child = readPage(childPageId);
    setParentPageId(child, parentPageIdValue);
    writePage(childPageId, child);
}

void BPlusTree::writePage(std::uint32_t pageId, const PageBuffer& page) {
    pager_.writePage(pageId, page);
}

PageBuffer BPlusTree::readPage(std::uint32_t pageId) {
    return pager_.readPage(pageId);
}

}  // namespace sdb
