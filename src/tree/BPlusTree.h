#pragma once

#include "storage/FileFormat.h"
#include "storage/Pager.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sdb {

class BPlusTree {
public:
    using HeaderFlusher = std::function<void()>;

    BPlusTree(Pager& pager, DatabaseHeader& header, HeaderFlusher headerFlusher);

    void put(std::uint64_t key, std::string_view value);
    std::optional<std::string> get(std::uint64_t key);
    bool remove(std::uint64_t key);
    std::vector<std::pair<std::uint64_t, std::string>> scan(
        std::optional<std::uint64_t> start,
        std::optional<std::uint64_t> end);
    std::string debugString();

private:
    struct InsertResult {
        bool split = false;
        std::uint64_t promotedKey = 0;
        std::uint32_t rightPageId = kInvalidPageId;
    };

    struct PathEntry {
        std::uint32_t pageId;
        std::size_t childIndex;
    };

    std::uint32_t findLeafPage(std::uint64_t key, std::vector<PathEntry>* path = nullptr);
    InsertResult insertRecursive(std::uint32_t pageId, std::uint64_t key, const ValueBytes& value);
    InsertResult insertIntoLeaf(std::uint32_t pageId, std::uint64_t key, const ValueBytes& value);
    InsertResult insertIntoInternal(std::uint32_t pageId, std::uint64_t key, std::uint32_t rightChildId);
    void createNewRoot(std::uint32_t leftChildId, std::uint64_t promotedKey, std::uint32_t rightChildId);
    std::uint64_t subtreeMaxKey(std::uint32_t pageId);
    std::uint32_t allocateLeaf(std::uint32_t parentPageId);
    std::uint32_t allocateInternal(std::uint32_t parentPageId);
    void updateChildParent(std::uint32_t childPageId, std::uint32_t parentPageId);
    void writePage(std::uint32_t pageId, const PageBuffer& page);
    PageBuffer readPage(std::uint32_t pageId);

    Pager& pager_;
    DatabaseHeader& header_;
    HeaderFlusher headerFlusher_;
};

}  // namespace sdb
