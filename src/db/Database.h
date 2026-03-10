#pragma once

#include "storage/FileFormat.h"
#include "storage/Pager.h"
#include "tree/BPlusTree.h"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sdb {

class Database {
public:
    explicit Database(const std::filesystem::path& path);

    void put(std::uint64_t key, std::string_view value);
    std::optional<std::string> get(std::uint64_t key);
    bool remove(std::uint64_t key);
    std::vector<std::pair<std::uint64_t, std::string>> scan(
        std::optional<std::uint64_t> start = std::nullopt,
        std::optional<std::uint64_t> end = std::nullopt);
    std::string debugString();

    const DatabaseHeader& header() const;
    Pager& pager();

private:
    void initializeIfNeeded();
    void loadHeader();
    void flushHeader();

    Pager pager_;
    DatabaseHeader header_ {};
    BPlusTree tree_;
};

}  // namespace sdb
