# Key-Value Database

This is a small embedded key-value database implemented from scratch in modern C++. It is a hobby project, not a framework or teaching library: the goal is to have full control over how data is stored, indexed, and read back from disk. Under the hood it uses a pager-backed B+ tree and a fixed on-disk layout so that every key/value record and page transition is explicit and inspectable.

The database is intentionally opinionated and limited: it runs in a single process, uses a single database file, stores fixed-size values, and exposes a very small CLI API. That constraint keeps the storage engine understandable while still exercising real database concerns like page management, splits, range scans, and invariant checking.

## What it does

At a high level, the database:

- stores key/value pairs persistently in a single file
- uses a B+ tree to keep keys sorted and support efficient point lookups and range scans
- manages fixed-size pages on disk through a pager
- exposes a small CLI to `put`, `get`, `remove`, `scan`, and `debug` the data

Keys are 64-bit unsigned integers (`uint64_t`). Values are stored as fixed-size 256-byte blobs; shorter strings are padded, longer ones are truncated. There is no SQL layer, no transactions, and no concurrency control—this is deliberately a simple storage engine core.

## Why it exists

Building a database from scratch is one of the most direct ways to explore systems programming, on-disk formats, and real-world data structures. This project exists as a playground for that exploration:

- to experiment with B+ tree design, page layout, and splitting/merging strategies
- to see exactly how a pager talks to the filesystem and tracks pages
- to make invariants, debug output, and tests part of the design from the beginning

It is also a reference point for future-you: a compact codebase you can come back to when you want to remember how to wire up an embedded storage engine end to end.

## Project structure

The core pieces live in `src/`:

- `main.cpp` – CLI entrypoint
- `src/db/Database.*` – public database API and bootstrap logic
- `src/storage/FileFormat.h` – on-disk constants and metadata definitions
- `src/storage/Pager.*` – fixed-size page I/O and allocation
- `src/storage/PageLayout.h` – byte-level helpers for reading/writing pages
- `src/tree/BPlusTree.*` – search, insert, split, delete, scan
- `src/util/DebugPrinter.*` – human-readable tree printing
- `src/util/InvariantChecker.*` – structural validation
- `tests/database_tests.cpp` – persistence and randomized correctness tests

More detailed internals are described in `docs/TECHNICAL_DOCUMENTATION.md`, `docs/DESIGN.md`, and `docs/IMPLEMENTATION_PLAN.md`.

## Setup

You need:

- a C++17-compatible compiler (for example `g++` or `clang++`)
- `make`

Clone the repository:

```bash
git clone https://github.com/Habib97SE/key-value-db.git
cd key-value-db
```

Build the project with the provided `Makefile`:

```bash
make
```

This produces the `simple_db` executable in the project root.

To run the test suite:

```bash
make test
```

This builds and runs `simple_db_tests`, which exercises persistence, splits, and randomized operations against a reference `std::map`.

To clean build artifacts:

```bash
make clean
```

## How to use it

The database is used through the `simple_db` CLI.

### Basic commands

```bash
./simple_db put <key> <value>
./simple_db get <key>
./simple_db remove <key>
./simple_db scan [start] [end]
./simple_db debug
```

- **`put <key> <value>`**: insert or overwrite a key/value pair.  
  - `key` is a 64-bit unsigned integer (e.g. `42`, `1000`).  
  - `value` is a string; it will be stored as a fixed 256-byte value (truncated if too long, padded if shorter).

- **`get <key>`**: look up the value for `key`.  
  - exits with code `0` on success and prints the value  
  - exits with code `2` if the key is not found

- **`remove <key>`**: delete the given key if it exists.  
  - exits with code `0` on success  
  - exits with code `2` if the key is not found

- **`scan [start] [end]`**: iterate keys in sorted order.  
  - with no arguments, scans the entire keyspace  
  - with `start`, begins at the first key ≥ `start`  
  - with `start` and `end`, returns keys in `[start, end]`

- **`debug`**: prints a human-readable view of the B+ tree, including internal and leaf nodes, which is useful when reasoning about splits and structure.

### Selecting the database file

By default, the database uses a file called `demo.db` in the current directory. You can override this in three ways:

1. `--database <path>`
2. `--database=<path>`
3. `SIMPLE_DB_DATABASE` environment variable

Examples:

```bash
./simple_db --database=test-db put 42 hello
./simple_db --database=test-db get 42
./simple_db scan 10 100 --database=test-db
SIMPLE_DB_DATABASE=test-db ./simple_db debug
```

### Exit codes

- `0` – success  
- `1` – invalid usage or runtime error  
- `2` – key not found for `get` or `remove`

These codes make it practical to script around `simple_db` or integrate it into small tooling.

## Who this is for

This repository is for anyone who likes databases and low-level C++: people who want to see a compact storage engine end to end, experiment with on-disk structures, or have a concrete base to extend with features like variable-length values, better delete semantics, caching, or WAL. It is also for future-you, coming back to understand how your own key-value store works under the hood. 