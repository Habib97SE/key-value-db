#include "util/InvariantChecker.h"

#include "storage/PageLayout.h"

#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

namespace sdb {

namespace {

struct ValidationResult {
    std::uint64_t minKey = 0;
    std::uint64_t maxKey = 0;
    std::size_t leafDepth = 0;
    bool hasLeafDepth = false;
    bool hasKeys = false;
};

ValidationResult validateNode(
    Pager& pager,
    std::uint32_t pageId,
    std::uint32_t expectedParent,
    std::size_t depth,
    std::optional<std::uint64_t>& previousLeafMax,
    std::set<std::uint32_t>& visited) {
    if (!visited.insert(pageId).second) {
        throw std::runtime_error("cycle detected in tree");
    }

    const auto page = pager.readPage(pageId);
    if (layout::parentPageId(page) != expectedParent) {
        throw std::runtime_error("parent page id mismatch");
    }

    if (layout::pageType(page) == PageType::Leaf) {
        for (std::size_t i = 1; i < layout::keyCount(page); ++i) {
            if (layout::leafKeyAt(page, i - 1) > layout::leafKeyAt(page, i)) {
                throw std::runtime_error("leaf keys out of order");
            }
        }

        if (layout::keyCount(page) == 0) {
            return ValidationResult {
                .leafDepth = depth,
                .hasLeafDepth = true,
                .hasKeys = false,
            };
        }

        if (previousLeafMax.has_value() && previousLeafMax.value() > layout::leafKeyAt(page, 0)) {
            throw std::runtime_error("leaf chain order mismatch");
        }
        previousLeafMax = layout::leafKeyAt(page, layout::keyCount(page) - 1);

        return ValidationResult {
            .minKey = layout::leafKeyAt(page, 0),
            .maxKey = layout::leafKeyAt(page, layout::keyCount(page) - 1),
            .leafDepth = depth,
            .hasLeafDepth = true,
            .hasKeys = true,
        };
    }

    for (std::size_t i = 1; i < layout::keyCount(page); ++i) {
        if (layout::internalKeyAt(page, i - 1) >= layout::internalKeyAt(page, i)) {
            throw std::runtime_error("internal keys out of order");
        }
    }

    ValidationResult aggregate;
    bool firstChild = true;
    for (std::size_t i = 0; i <= layout::keyCount(page); ++i) {
        const auto childResult = validateNode(
            pager, layout::internalChildAt(page, i), pageId, depth + 1, previousLeafMax, visited);

        if (childResult.hasLeafDepth) {
            if (!aggregate.hasLeafDepth) {
                aggregate.leafDepth = childResult.leafDepth;
                aggregate.hasLeafDepth = true;
            } else if (aggregate.leafDepth != childResult.leafDepth) {
                throw std::runtime_error("leaves are not at the same depth");
            }
        }

        if (childResult.hasKeys && firstChild) {
            aggregate.minKey = childResult.minKey;
            firstChild = false;
            aggregate.hasKeys = true;
        }
        if (childResult.hasKeys) {
            aggregate.maxKey = childResult.maxKey;
        }

        if (!childResult.hasKeys) {
            continue;
        }

        if (i < layout::keyCount(page)) {
            if (childResult.maxKey >= layout::internalKeyAt(page, i)) {
                throw std::runtime_error("left child max key exceeds separator");
            }
        } else if (childResult.maxKey < layout::internalKeyAt(page, i - 1)) {
            throw std::runtime_error("right child max key below last separator");
        }
    }

    return aggregate;
}

}  // namespace

void InvariantChecker::validateTree(Pager& pager, std::uint32_t rootPageId) {
    std::optional<std::uint64_t> previousLeafMax;
    std::set<std::uint32_t> visited;
    (void)validateNode(pager, rootPageId, kInvalidPageId, 0, previousLeafMax, visited);
}

}  // namespace sdb
