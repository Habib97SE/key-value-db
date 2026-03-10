#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sdb {

inline constexpr std::size_t kPageSize = 4096;
inline constexpr std::size_t kValueSize = 256;
inline constexpr std::uint32_t kInvalidPageId = 0;
inline constexpr std::uint32_t kMetadataPageId = 0;
inline constexpr std::uint32_t kInitialRootPageId = 1;
inline constexpr std::uint32_t kFormatVersion = 1;
inline constexpr std::string_view kMagic = "SDBTREE1";

using PageBuffer = std::array<std::byte, kPageSize>;
using ValueBytes = std::array<char, kValueSize>;

enum class PageType : std::uint8_t {
    Invalid = 0,
    Internal = 1,
    Leaf = 2,
};

struct DatabaseHeader {
    std::array<char, 8> magic {};
    std::uint32_t version = kFormatVersion;
    std::uint32_t pageSize = static_cast<std::uint32_t>(kPageSize);
    std::uint32_t rootPageId = kInitialRootPageId;
    std::uint32_t pageCount = 2;
};

inline constexpr std::size_t kCommonHeaderSize = 12;
inline constexpr std::size_t kLeafHeaderSize = 16;
inline constexpr std::size_t kInternalHeaderSize = 16;
inline constexpr std::size_t kLeafEntrySize = sizeof(std::uint64_t) + kValueSize;
inline constexpr std::size_t kLeafCapacity = (kPageSize - kLeafHeaderSize) / kLeafEntrySize;
inline constexpr std::size_t kInternalCellSize = sizeof(std::uint32_t) + sizeof(std::uint64_t);
inline constexpr std::size_t kInternalCapacity = (kPageSize - kInternalHeaderSize) / kInternalCellSize;

}  // namespace sdb
