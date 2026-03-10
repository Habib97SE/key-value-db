#pragma once

#include "storage/FileFormat.h"

#include <cstdint>
#include <filesystem>
#include <fstream>

namespace sdb {

class Pager {
public:
    explicit Pager(const std::filesystem::path& path);
    ~Pager();

    Pager(const Pager&) = delete;
    Pager& operator=(const Pager&) = delete;

    PageBuffer readPage(std::uint32_t pageId);
    void writePage(std::uint32_t pageId, const PageBuffer& page);
    std::uint32_t allocatePage();
    std::uint32_t pageCount() const;
    void flush();

private:
    void ensureFileInitialized();
    std::uint64_t pageOffset(std::uint32_t pageId) const;

    std::filesystem::path path_;
    std::fstream file_;
    std::uint32_t pageCount_ = 0;
};

}  // namespace sdb
