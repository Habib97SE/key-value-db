#pragma once

#include "storage/FileFormat.h"

#include <algorithm>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sdb {

namespace layout {

inline constexpr std::size_t kPageTypeOffset = 0;
inline constexpr std::size_t kFlagsOffset = 1;
inline constexpr std::size_t kParentPageIdOffset = 4;
inline constexpr std::size_t kKeyCountOffset = 8;
inline constexpr std::size_t kLeafNextPageIdOffset = 12;
inline constexpr std::size_t kInternalRightChildOffset = 12;

template <typename T>
T readScalar(const PageBuffer& page, std::size_t offset) {
    T value {};
    std::memcpy(&value, page.data() + offset, sizeof(T));
    return value;
}

template <typename T>
void writeScalar(PageBuffer& page, std::size_t offset, T value) {
    std::memcpy(page.data() + offset, &value, sizeof(T));
}

inline std::uint8_t flags(const PageBuffer& page) {
    return readScalar<std::uint8_t>(page, kFlagsOffset);
}

inline PageType pageType(const PageBuffer& page) {
    return static_cast<PageType>(readScalar<std::uint8_t>(page, kPageTypeOffset));
}

inline void setPageType(PageBuffer& page, PageType type) {
    writeScalar<std::uint8_t>(page, kPageTypeOffset, static_cast<std::uint8_t>(type));
}

inline bool isRoot(const PageBuffer& page) {
    return (flags(page) & 0x1U) != 0;
}

inline void setIsRoot(PageBuffer& page, bool root) {
    std::uint8_t current = flags(page);
    current = root ? static_cast<std::uint8_t>(current | 0x1U) : static_cast<std::uint8_t>(current & ~0x1U);
    writeScalar<std::uint8_t>(page, kFlagsOffset, current);
}

inline std::uint32_t parentPageId(const PageBuffer& page) {
    return readScalar<std::uint32_t>(page, kParentPageIdOffset);
}

inline void setParentPageId(PageBuffer& page, std::uint32_t pageId) {
    writeScalar<std::uint32_t>(page, kParentPageIdOffset, pageId);
}

inline std::uint16_t keyCount(const PageBuffer& page) {
    return readScalar<std::uint16_t>(page, kKeyCountOffset);
}

inline void setKeyCount(PageBuffer& page, std::uint16_t count) {
    writeScalar<std::uint16_t>(page, kKeyCountOffset, count);
}

inline std::uint32_t nextLeafPageId(const PageBuffer& page) {
    return readScalar<std::uint32_t>(page, kLeafNextPageIdOffset);
}

inline void setNextLeafPageId(PageBuffer& page, std::uint32_t pageId) {
    writeScalar<std::uint32_t>(page, kLeafNextPageIdOffset, pageId);
}

inline std::uint32_t rightChildPageId(const PageBuffer& page) {
    return readScalar<std::uint32_t>(page, kInternalRightChildOffset);
}

inline void setRightChildPageId(PageBuffer& page, std::uint32_t pageId) {
    writeScalar<std::uint32_t>(page, kInternalRightChildOffset, pageId);
}

inline std::size_t leafEntryOffset(std::size_t index) {
    return kLeafHeaderSize + index * kLeafEntrySize;
}

inline std::uint64_t leafKeyAt(const PageBuffer& page, std::size_t index) {
    return readScalar<std::uint64_t>(page, leafEntryOffset(index));
}

inline ValueBytes leafValueAt(const PageBuffer& page, std::size_t index) {
    ValueBytes value {};
    std::memcpy(value.data(), page.data() + leafEntryOffset(index) + sizeof(std::uint64_t), value.size());
    return value;
}

inline void writeLeafEntry(PageBuffer& page, std::size_t index, std::uint64_t key, const ValueBytes& value) {
    writeScalar<std::uint64_t>(page, leafEntryOffset(index), key);
    std::memcpy(page.data() + leafEntryOffset(index) + sizeof(std::uint64_t), value.data(), value.size());
}

inline void eraseLeafEntry(PageBuffer& page, std::size_t index) {
    const auto count = keyCount(page);
    for (std::size_t i = index + 1; i < count; ++i) {
        std::memcpy(page.data() + leafEntryOffset(i - 1), page.data() + leafEntryOffset(i), kLeafEntrySize);
    }
    std::memset(page.data() + leafEntryOffset(count - 1), 0, kLeafEntrySize);
    setKeyCount(page, static_cast<std::uint16_t>(count - 1));
}

inline std::size_t internalCellOffset(std::size_t index) {
    return kInternalHeaderSize + index * kInternalCellSize;
}

inline std::uint32_t internalLeftChildAt(const PageBuffer& page, std::size_t index) {
    return readScalar<std::uint32_t>(page, internalCellOffset(index));
}

inline std::uint64_t internalKeyAt(const PageBuffer& page, std::size_t index) {
    return readScalar<std::uint64_t>(page, internalCellOffset(index) + sizeof(std::uint32_t));
}

inline void writeInternalCell(PageBuffer& page, std::size_t index, std::uint32_t leftChild, std::uint64_t key) {
    writeScalar<std::uint32_t>(page, internalCellOffset(index), leftChild);
    writeScalar<std::uint64_t>(page, internalCellOffset(index) + sizeof(std::uint32_t), key);
}

inline std::uint32_t internalChildAt(const PageBuffer& page, std::size_t childIndex) {
    const auto count = keyCount(page);
    if (childIndex < count) {
        return internalLeftChildAt(page, childIndex);
    }
    if (childIndex == count) {
        return rightChildPageId(page);
    }
    throw std::runtime_error("internalChildAt: child index out of range");
}

inline void initializeLeafPage(PageBuffer& page, bool root, std::uint32_t parentPageId, std::uint32_t nextLeafId) {
    page.fill(std::byte {0});
    setPageType(page, PageType::Leaf);
    setIsRoot(page, root);
    setParentPageId(page, parentPageId);
    setKeyCount(page, 0);
    setNextLeafPageId(page, nextLeafId);
}

inline void initializeInternalPage(PageBuffer& page, bool root, std::uint32_t parentPageId, std::uint32_t rightChildId) {
    page.fill(std::byte {0});
    setPageType(page, PageType::Internal);
    setIsRoot(page, root);
    setParentPageId(page, parentPageId);
    setKeyCount(page, 0);
    setRightChildPageId(page, rightChildId);
}

inline ValueBytes encodeValue(std::string_view value) {
    ValueBytes bytes {};
    const auto copySize = std::min(value.size(), bytes.size());
    std::memcpy(bytes.data(), value.data(), copySize);
    return bytes;
}

inline std::string decodeValue(const ValueBytes& value) {
    auto end = std::find(value.begin(), value.end(), '\0');
    return std::string(value.begin(), end);
}

inline std::size_t leafFindKeyIndex(const PageBuffer& page, std::uint64_t key, bool* found = nullptr) {
    const auto count = keyCount(page);
    std::size_t low = 0;
    std::size_t high = count;

    while (low < high) {
        const auto mid = low + (high - low) / 2;
        const auto currentKey = leafKeyAt(page, mid);
        if (currentKey < key) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    if (found != nullptr) {
        *found = low < count && leafKeyAt(page, low) == key;
    }
    return low;
}

inline std::size_t internalFindChildIndex(const PageBuffer& page, std::uint64_t key) {
    const auto count = keyCount(page);
    for (std::size_t i = 0; i < count; ++i) {
        if (key < internalKeyAt(page, i)) {
            return i;
        }
    }
    return count;
}

struct ScanItem {
    std::uint64_t key;
    std::string value;
};

struct ChildReference {
    std::uint32_t pageId;
    std::uint64_t maxKey;
};

inline std::uint64_t maxKeyInPage(const PageBuffer& page) {
    const auto count = keyCount(page);
    if (count == 0) {
        throw std::runtime_error("cannot read max key from empty page");
    }
    if (pageType(page) == PageType::Leaf) {
        return leafKeyAt(page, count - 1);
    }
    return internalKeyAt(page, count - 1);
}

}  // namespace layout

}  // namespace sdb
