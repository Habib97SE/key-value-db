#include "storage/Pager.h"

#include <stdexcept>
#include <vector>

namespace sdb {

namespace {

void throwIoError(const std::string& message) {
    throw std::runtime_error(message);
}

}  // namespace

Pager::Pager(const std::filesystem::path& path) : path_(path) {
    ensureFileInitialized();
}

Pager::~Pager() {
    flush();
}

PageBuffer Pager::readPage(std::uint32_t pageId) {
    if (pageId >= pageCount_) {
        throw std::runtime_error("readPage: page id out of range");
    }

    PageBuffer page {};
    file_.seekg(static_cast<std::streamoff>(pageOffset(pageId)), std::ios::beg);
    file_.read(reinterpret_cast<char*>(page.data()), static_cast<std::streamsize>(page.size()));
    if (file_.gcount() != static_cast<std::streamsize>(page.size())) {
        throwIoError("failed to read full page");
    }
    file_.clear();
    return page;
}

void Pager::writePage(std::uint32_t pageId, const PageBuffer& page) {
    if (pageId >= pageCount_) {
        throw std::runtime_error("writePage: page id out of range");
    }

    file_.seekp(static_cast<std::streamoff>(pageOffset(pageId)), std::ios::beg);
    file_.write(reinterpret_cast<const char*>(page.data()), static_cast<std::streamsize>(page.size()));
    if (!file_) {
        throwIoError("failed to write page");
    }
    file_.flush();
    file_.clear();
}

std::uint32_t Pager::allocatePage() {
    const std::uint32_t pageId = pageCount_;
    PageBuffer blank {};
    file_.seekp(static_cast<std::streamoff>(pageOffset(pageId)), std::ios::beg);
    file_.write(reinterpret_cast<const char*>(blank.data()), static_cast<std::streamsize>(blank.size()));
    if (!file_) {
        throwIoError("failed to allocate page");
    }
    file_.flush();
    file_.clear();
    ++pageCount_;
    return pageId;
}

std::uint32_t Pager::pageCount() const {
    return pageCount_;
}

void Pager::flush() {
    if (file_.is_open()) {
        file_.flush();
    }
}

void Pager::ensureFileInitialized() {
    if (!std::filesystem::exists(path_)) {
        std::ofstream createFile(path_, std::ios::binary);
        if (!createFile) {
            throwIoError("failed to create database file");
        }
    }

    file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file_) {
        throwIoError("failed to open database file");
    }

    const auto fileSize = std::filesystem::file_size(path_);
    if (fileSize % kPageSize != 0) {
        throw std::runtime_error("database file size is not page aligned");
    }

    pageCount_ = static_cast<std::uint32_t>(fileSize / kPageSize);
}

std::uint64_t Pager::pageOffset(std::uint32_t pageId) const {
    return static_cast<std::uint64_t>(pageId) * kPageSize;
}

}  // namespace sdb
