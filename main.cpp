#include "db/Database.h"
#include "util/InvariantChecker.h"

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kDefaultDatabasePath = "demo.db";
constexpr std::string_view kDatabaseEnvVar = "SIMPLE_DB_DATABASE";

struct CliOptions {
    std::filesystem::path databasePath = std::string(kDefaultDatabasePath);
    std::vector<std::string> positionalArgs;
};

enum class ParseStatus {
    Ok,
    HelpShown,
    Error,
};

struct ParseResult {
    ParseStatus status = ParseStatus::Ok;
    CliOptions options;
};

void printAvailableCommands(std::string_view programName) {
    std::cerr
        << "Available commands:\n"
        << "  " << programName << " put <key> <value>\n"
        << "  " << programName << " get <key>\n"
        << "  " << programName << " remove <key>\n"
        << "  " << programName << " scan [start] [end]\n"
        << "  " << programName << " load <count> [start_key]\n"
        << "  " << programName << " debug\n";
}

void printUsage(std::string_view programName) {
    std::cerr
        << "Usage:\n"
        << "  " << programName << " [--database <path>] put <key> <value>\n"
        << "  " << programName << " [--database <path>] get <key>\n"
        << "  " << programName << " [--database <path>] remove <key>\n"
        << "  " << programName << " [--database <path>] scan [start] [end]\n"
        << "  " << programName << " [--database <path>] load <count> [start_key]\n"
        << "  " << programName << " [--database <path>] debug\n"
        << "\n"
        << "Options:\n"
        << "  --database <path>     Use the specified database file\n"
        << "  --database=<path>     Same as above\n"
        << "  --help                Show this help message\n"
        << "\n"
        << "Environment:\n"
        << "  " << kDatabaseEnvVar << "    Default database file when --database is not provided\n"
        << "\n"
        << "Database selection precedence:\n"
        << "  1. --database\n"
        << "  2. " << kDatabaseEnvVar << '\n'
        << "  3. " << kDefaultDatabasePath << '\n';
}

std::optional<std::uint64_t> parseUnsigned(const char* text) {
    try {
        std::size_t consumed = 0;
        const auto value = std::stoull(text, &consumed, 10);
        if (text[consumed] != '\0') {
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(value);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::uint64_t> parseKey(const char* text) {
    return parseUnsigned(text);
}

ParseResult parseCliOptions(int argc, char** argv, std::string_view programName) {
    ParseResult result;
    auto& options = result.options;
    if (const char* envValue = std::getenv(std::string(kDatabaseEnvVar).c_str())) {
        if (*envValue != '\0') {
            options.databasePath = envValue;
        }
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") {
            printUsage(programName);
            result.status = ParseStatus::HelpShown;
            return result;
        }
        if (arg == "--database") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --database\n";
                printUsage(programName);
                result.status = ParseStatus::Error;
                return result;
            }
            options.databasePath = argv[++i];
            continue;
        }
        if (arg.rfind("--database=", 0) == 0) {
            const auto path = arg.substr(std::string("--database=").size());
            if (path.empty()) {
                std::cerr << "Missing value for --database\n";
                printUsage(programName);
                result.status = ParseStatus::Error;
                return result;
            }
            options.databasePath = path;
            continue;
        }

        options.positionalArgs.push_back(arg);
    }

    return result;
}

int runPut(sdb::Database& db, const std::vector<std::string>& args, std::string_view programName) {
    if (args.size() != 3) {
        printUsage(programName);
        return 1;
    }

    const auto key = parseKey(args[1].c_str());
    if (!key.has_value()) {
        std::cerr << "Invalid key: " << args[1] << '\n';
        return 1;
    }

    db.put(key.value(), args[2]);
    sdb::InvariantChecker::validateTree(db.pager(), db.header().rootPageId);
    std::cout << "ok\n";
    return 0;
}

int runGet(sdb::Database& db, const std::vector<std::string>& args, std::string_view programName) {
    if (args.size() != 2) {
        printUsage(programName);
        return 1;
    }

    const auto key = parseKey(args[1].c_str());
    if (!key.has_value()) {
        std::cerr << "Invalid key: " << args[1] << '\n';
        return 1;
    }

    const auto value = db.get(key.value());
    if (!value.has_value()) {
        std::cout << "not found\n";
        return 2;
    }

    std::cout << value.value() << '\n';
    return 0;
}

int runRemove(sdb::Database& db, const std::vector<std::string>& args, std::string_view programName) {
    if (args.size() != 2) {
        printUsage(programName);
        return 1;
    }

    const auto key = parseKey(args[1].c_str());
    if (!key.has_value()) {
        std::cerr << "Invalid key: " << args[1] << '\n';
        return 1;
    }

    const bool removed = db.remove(key.value());
    sdb::InvariantChecker::validateTree(db.pager(), db.header().rootPageId);
    std::cout << (removed ? "removed" : "not found") << '\n';
    return removed ? 0 : 2;
}

int runScan(sdb::Database& db, const std::vector<std::string>& args, std::string_view programName) {
    std::optional<std::uint64_t> start;
    std::optional<std::uint64_t> end;

    if (args.size() == 2 || args.size() == 3) {
        start = parseKey(args[1].c_str());
        if (!start.has_value()) {
            std::cerr << "Invalid start key: " << args[1] << '\n';
            return 1;
        }
    }
    if (args.size() == 3) {
        end = parseKey(args[2].c_str());
        if (!end.has_value()) {
            std::cerr << "Invalid end key: " << args[2] << '\n';
            return 1;
        }
    }
    if (args.size() > 3) {
        printUsage(programName);
        return 1;
    }

    const auto rows = db.scan(start, end);
    for (const auto& [key, value] : rows) {
        std::cout << key << " => " << value << '\n';
    }
    return 0;
}

int runLoad(sdb::Database& db, const std::vector<std::string>& args, std::string_view programName) {
    if (args.size() != 2 && args.size() != 3) {
        printUsage(programName);
        return 1;
    }

    const auto count = parseUnsigned(args[1].c_str());
    if (!count.has_value() || count.value() == 0) {
        std::cerr << "Invalid count: " << args[1] << '\n';
        return 1;
    }

    std::uint64_t startKey = 1;
    if (args.size() == 3) {
        const auto parsedStart = parseUnsigned(args[2].c_str());
        if (!parsedStart.has_value() || parsedStart.value() == 0) {
            std::cerr << "Invalid start key: " << args[2] << '\n';
            return 1;
        }
        startKey = parsedStart.value();
    }

    const auto startedAt = std::chrono::steady_clock::now();
    const auto progressStep = std::max<std::uint64_t>(count.value() / 10, 1);

    for (std::uint64_t offset = 0; offset < count.value(); ++offset) {
        const auto key = startKey + offset;
        db.put(key, "value-" + std::to_string(key));
        if ((offset + 1) % progressStep == 0 || offset + 1 == count.value()) {
            std::cerr << "loaded " << (offset + 1) << '/' << count.value() << '\n';
        }
    }

    sdb::InvariantChecker::validateTree(db.pager(), db.header().rootPageId);

    const auto endedAt = std::chrono::steady_clock::now();
    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(endedAt - startedAt).count();
    const auto elapsedSeconds = std::chrono::duration<double>(endedAt - startedAt).count();
    const auto rate = elapsedSeconds > 0.0 ? static_cast<double>(count.value()) / elapsedSeconds : 0.0;

    std::cout
        << "loaded " << count.value() << " records into " << db.header().pageCount << " pages\n"
        << "start_key=" << startKey << " end_key=" << (startKey + count.value() - 1) << '\n'
        << "elapsed_ms=" << elapsedMs << '\n'
        << "records_per_sec=" << static_cast<std::uint64_t>(rate) << '\n';
    return 0;
}

int runDebug(sdb::Database& db, const std::vector<std::string>& args, std::string_view programName) {
    if (args.size() != 1) {
        printUsage(programName);
        return 1;
    }

    sdb::InvariantChecker::validateTree(db.pager(), db.header().rootPageId);
    std::cout << db.debugString();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string programName = argc > 0 ? std::filesystem::path(argv[0]).filename().string() : "simple_db";
    if (argc < 2) {
        printUsage(programName);
        return 1;
    }

    try {
        const auto parseResult = parseCliOptions(argc, argv, programName);
        if (parseResult.status == ParseStatus::HelpShown) {
            return 0;
        }
        if (parseResult.status == ParseStatus::Error) {
            return 1;
        }
        if (parseResult.options.positionalArgs.empty()) {
            std::cerr << "No command provided.\n";
            printAvailableCommands(programName);
            std::cerr << '\n';
            printUsage(programName);
            return 1;
        }

        sdb::Database db(parseResult.options.databasePath);
        const std::string& command = parseResult.options.positionalArgs[0];

        if (command == "put") {
            return runPut(db, parseResult.options.positionalArgs, programName);
        }
        if (command == "get") {
            return runGet(db, parseResult.options.positionalArgs, programName);
        }
        if (command == "remove") {
            return runRemove(db, parseResult.options.positionalArgs, programName);
        }
        if (command == "scan") {
            return runScan(db, parseResult.options.positionalArgs, programName);
        }
        if (command == "load") {
            return runLoad(db, parseResult.options.positionalArgs, programName);
        }
        if (command == "debug") {
            return runDebug(db, parseResult.options.positionalArgs, programName);
        }

        std::cerr << "Unknown command: " << command << '\n';
        printAvailableCommands(programName);
        std::cerr << '\n';
        printUsage(programName);
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
}
