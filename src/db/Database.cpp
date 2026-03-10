#include "db/Database.h"

#include "storage/PageLayout.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace sdb {

namespace {

PageBuffer serializeHeader(const DatabaseHeader& header) {
    PageBuffer page {};
    std::memcpy(page.data(), header.magic.data(), header.magic.size());
    std::memcpy(page.data() + 8, &header.version, sizeof(header.version));
    std::memcpy(page.data() + 12, &header.pageSize, sizeof(header.pageSize));
    std::memcpy(page.data() + 16, &header.rootPageId, sizeof(header.rootPageId));
    std::memcpy(page.data() + 20, &header.pageCount, sizeof(header.pageCount));
    return page;
}

DatabaseHeader deserializeHeader(const PageBuffer& page) {
    DatabaseHeader header;
    std::memcpy(header.magic.data(), page.data(), header.magic.size());
    std::memcpy(&header.version, page.data() + 8, sizeof(header.version));
    std::memcpy(&header.pageSize, page.data() + 12, sizeof(header.pageSize));
    std::memcpy(&header.rootPageId, page.data() + 16, sizeof(header.rootPageId));
    std::memcpy(&header.pageCount, page.data() + 20, sizeof(header.pageCount));
    return header;
}

}  // namespace

Database::Database(const std::filesystem::path& path)
    : pager_(path), tree_(pager_, header_, [this]() { flushHeader(); }) {
    initializeIfNeeded();
    loadHeader();
}

void Database::put(std::uint64_t key, std::string_view value) {
    tree_.put(key, value);
}

std::optional<std::string> Database::get(std::uint64_t key) {
    return tree_.get(key);
}

bool Database::remove(std::uint64_t key) {
    return tree_.remove(key);
}

std::vector<std::pair<std::uint64_t, std::string>> Database::scan(
    std::optional<std::uint64_t> start, std::optional<std::uint64_t> end) {
    return tree_.scan(start, end);
}

std::string Database::debugString() {
    return tree_.debugString();
}

const DatabaseHeader& Database::header() const {
    return header_;
}

Pager& Database::pager() {
    return pager_;
}

void Database::initializeIfNeeded() {
    if (pager_.pageCount() != 0) {
        return;
    }

    pager_.allocatePage();
    pager_.allocatePage();

    std::copy(kMagic.begin(), kMagic.end(), header_.magic.begin());
    header_.version = kFormatVersion;
    header_.pageSize = static_cast<std::uint32_t>(kPageSize);
    header_.rootPageId = kInitialRootPageId;
    header_.pageCount = pager_.pageCount();
    flushHeader();

    PageBuffer root {};
    layout::initializeLeafPage(root, true, kInvalidPageId, kInvalidPageId);
    pager_.writePage(kInitialRootPageId, root);
}

void Database::loadHeader() {
    header_ = deserializeHeader(pager_.readPage(kMetadataPageId));

    const std::array<char, 8> expectedMagic = {'S', 'D', 'B', 'T', 'R', 'E', 'E', '1'};
    if (header_.magic != expectedMagic) {
        throw std::runtime_error("database header magic mismatch");
    }
    if (header_.version != kFormatVersion) {
        throw std::runtime_error("unsupported database version");
    }
    if (header_.pageSize != kPageSize) {
        throw std::runtime_error("page size mismatch");
    }
    if (header_.pageCount != pager_.pageCount()) {
        throw std::runtime_error("header page count mismatch");
    }
}

void Database::flushHeader() {
    pager_.writePage(kMetadataPageId, serializeHeader(header_));
}

}  // namespace sdb
