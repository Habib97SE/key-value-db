#include "db/Database.h"
#include "util/InvariantChecker.h"

#include <filesystem>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using sdb::Database;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::filesystem::path tempPath(const std::string& name) {
    return std::filesystem::temp_directory_path() / name;
}

void testBasicPersistence() {
    const auto path = tempPath("simple_db_basic.db");
    std::filesystem::remove(path);
    {
        Database db(path);
        db.put(42, "forty-two");
        db.put(5, "five");
        db.put(42, "updated");
        require(db.get(42).value_or("") == "updated", "updated value should be readable");
        require(!db.get(999).has_value(), "missing key should not exist");
    }
    {
        Database db(path);
        require(db.get(42).value_or("") == "updated", "value should persist after reopen");
        require(db.get(5).value_or("") == "five", "second value should persist after reopen");
        sdb::InvariantChecker::validateTree(db.pager(), db.header().rootPageId);
    }
    std::filesystem::remove(path);
}

void testLeafAndInternalSplits() {
    const auto path = tempPath("simple_db_split.db");
    std::filesystem::remove(path);
    Database db(path);

    for (std::uint64_t i = 1; i <= 200; ++i) {
        db.put(i, "value-" + std::to_string(i));
    }

    for (std::uint64_t i = 1; i <= 200; ++i) {
        require(db.get(i).value_or("") == "value-" + std::to_string(i), "inserted key missing after splits");
    }

    const auto rows = db.scan(20, 35);
    require(rows.size() == 16, "range size mismatch");
    require(rows.front().first == 20 && rows.back().first == 35, "range boundaries mismatch");
    require(db.header().rootPageId != sdb::kInitialRootPageId, "root should have split");
    sdb::InvariantChecker::validateTree(db.pager(), db.header().rootPageId);
    std::filesystem::remove(path);
}

void testRandomOperationsAgainstMap() {
    const auto path = tempPath("simple_db_random.db");
    std::filesystem::remove(path);
    Database db(path);
    std::map<std::uint64_t, std::string> expected;
    std::mt19937_64 rng(1337);
    std::uniform_int_distribution<int> opDist(0, 2);
    std::uniform_int_distribution<std::uint64_t> keyDist(1, 300);

    for (int i = 0; i < 1000; ++i) {
        const auto key = keyDist(rng);
        const auto op = opDist(rng);
        if (op == 0) {
            const auto value = "v-" + std::to_string(i) + "-" + std::to_string(key);
            db.put(key, value);
            expected[key] = value;
        } else if (op == 1) {
            const auto removed = db.remove(key);
            const auto erased = expected.erase(key) > 0;
            require(removed == erased, "remove result mismatch");
        } else {
            require(db.get(key) == (expected.count(key) ? std::optional<std::string>(expected[key]) : std::nullopt),
                "get result mismatch");
        }

        if (i % 100 == 0) {
            sdb::InvariantChecker::validateTree(db.pager(), db.header().rootPageId);
        }
    }

    const auto rows = db.scan();
    require(rows.size() == expected.size(), "scan size mismatch");
    std::size_t index = 0;
    for (const auto& [key, value] : expected) {
        require(rows[index].first == key, "scan key mismatch");
        require(rows[index].second == value, "scan value mismatch");
        ++index;
    }

    sdb::InvariantChecker::validateTree(db.pager(), db.header().rootPageId);
    std::filesystem::remove(path);
}

}  // namespace

int main() {
    testBasicPersistence();
    testLeafAndInternalSplits();
    testRandomOperationsAgainstMap();
    std::cout << "All tests passed\n";
    return 0;
}
