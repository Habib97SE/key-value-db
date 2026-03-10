# Simple DB Technical Documentation

## 1. Overview

`simple_db` is a small embedded key/value database implemented in C++. It stores fixed-size records in a single file using a pager-backed B+ tree. The current system is intentionally limited in scope so the storage engine remains understandable and testable:

- single process
- single database file
- fixed page size
- fixed key type: `uint64_t`
- fixed value size: `256` bytes
- no SQL layer
- no concurrency control
- no transactions or WAL
- no crash recovery beyond basic file persistence

Even with those limits, the project already implements the core pieces of a real storage engine:

- persistent on-disk storage
- metadata page
- page allocator
- leaf and internal B+ tree nodes
- sorted insert/update
- point lookup
- range scan
- recursive page splitting
- simple delete without rebalance
- invariant checking and stress testing

## 2. Project Structure

The codebase is split by responsibility:

```text
main.cpp                     CLI entrypoint
src/db/Database.*            public database API and bootstrap logic
src/storage/FileFormat.h     global constants and on-disk metadata definitions
src/storage/Pager.*          fixed-size page IO and page allocation
src/storage/PageLayout.h     byte-level page read/write helpers
src/tree/BPlusTree.*         search, insert, split, delete, scan
src/util/DebugPrinter.*      human-readable tree output
src/util/InvariantChecker.*  runtime structural validation
tests/database_tests.cpp     persistence, split, and randomized correctness tests
docs/DESIGN.md               compact design summary
docs/IMPLEMENTATION_PLAN.md  original implementation roadmap
```

The runtime dependency chain is:

```text
CLI
  -> Database
    -> BPlusTree
      -> Pager
        -> database file on disk
```

## 3. Build and Execution

The project is built with the provided `Makefile`.

### Build

```sh
make
```

This produces the `simple_db` executable.

### Run tests

```sh
make test
```

This builds and runs the standalone test executable `simple_db_tests`.

### Clean

```sh
make clean
```

## 4. CLI Interface

The CLI is implemented in [main.cpp](/Users/salt-dev/Developments/c-plus-plus/tutorial/main.cpp).

### Supported commands

```sh
./simple_db put <key> <value>
./simple_db get <key>
./simple_db remove <key>
./simple_db scan [start] [end]
./simple_db debug
```

### Database selection

The database file can be selected in three ways:

1. `--database <path>`
2. `--database=<path>`
3. `SIMPLE_DB_DATABASE` environment variable

If none of those are provided, the default database path is `demo.db`.

### Examples

```sh
./simple_db --database=test-db put 42 hello
./simple_db --database=test-db get 42
./simple_db scan 10 100 --database=test-db
SIMPLE_DB_DATABASE=test-db ./simple_db debug
```

### Exit behavior

- `0`: success
- `1`: invalid usage or runtime error
- `2`: key not found for `get` or `remove`

### Invalid command fallback

If a user enters an unknown command, the CLI prints:

- the unknown command name
- the list of valid commands
- the full usage text

This makes the interface safer for manual use and scripts.

## 5. Storage Model

The database is a fixed-page file. Every page is exactly `4096` bytes.

### High-level file layout

- page `0`: database metadata
- page `1+`: B+ tree pages

Each tree page stores exactly one node:

- internal node page
- leaf node page

The root page ID is stored in the metadata page. Child relationships are represented by page IDs, not memory pointers, which allows the tree to persist across process restarts.

## 6. On-Disk Format

Core constants are defined in [src/storage/FileFormat.h](/Users/salt-dev/Developments/c-plus-plus/tutorial/src/storage/FileFormat.h).

### Global constants

- page size: `4096`
- value size: `256`
- invalid page id: `0`
- metadata page id: `0`
- initial root page id: `1`
- format version: `1`
- magic header: `SDBTREE1`

### Database header

The metadata page serializes `DatabaseHeader`:

- `magic[8]`
- `version`
- `pageSize`
- `rootPageId`
- `pageCount`

This metadata is written to page `0` and validated when a database is opened.

### File initialization

When a new database file is opened for the first time:

1. page `0` is allocated
2. page `1` is allocated
3. the database header is written to page `0`
4. page `1` is initialized as the root leaf page

This logic is implemented in [src/db/Database.cpp](/Users/salt-dev/Developments/c-plus-plus/tutorial/src/db/Database.cpp).

## 7. Pager Layer

The pager is implemented in [src/storage/Pager.cpp](/Users/salt-dev/Developments/c-plus-plus/tutorial/src/storage/Pager.cpp).

### Responsibilities

- open an existing file or create a new one
- validate file size alignment against the page size
- read a page by page ID
- write a page by page ID
- allocate new blank pages at EOF
- track the current number of pages

### Pager operations

#### `readPage(pageId)`

- verifies the page ID is in range
- seeks to `pageId * kPageSize`
- reads exactly one page into a `PageBuffer`

#### `writePage(pageId, page)`

- verifies the page ID is in range
- writes one full page back to disk
- flushes the stream

#### `allocatePage()`

- appends a zero-filled page at the end of the file
- increments `pageCount_`
- returns the new page ID

### Pager assumptions

- no page cache
- no dirty-page batching
- no free-page reuse
- every allocation grows the file

This keeps the implementation simple, but makes future optimization opportunities obvious.

## 8. Page Layout Layer

The byte-level layout helpers are implemented in [src/storage/PageLayout.h](/Users/salt-dev/Developments/c-plus-plus/tutorial/src/storage/PageLayout.h).

This layer is intentionally separate from the tree algorithms. It provides deterministic read/write access to raw page bytes.

### Common page header

All tree pages begin with these logical fields:

- `pageType` at offset `0`
- `flags` at offset `1`
- `parentPageId` at offset `4`
- `keyCount` at offset `8`

The `flags` byte currently uses bit `0` to mark whether the page is the root.

### Leaf layout

Leaf-specific header:

- `nextLeafPageId` at offset `12`

Leaf body:

- repeated fixed-size key/value entries

Each entry is:

- key: `8` bytes
- value: `256` bytes

Leaf entry size:

- `264` bytes

Leaf capacity:

- `(4096 - 16) / 264 = 15` entries

### Internal layout

Internal-specific header:

- `rightChildPageId` at offset `12`

Internal body:

- repeated cells of:
  - `leftChildPageId` (`4` bytes)
  - separator `key` (`8` bytes)

Internal cell size:

- `12` bytes

Internal capacity:

- `(4096 - 16) / 12 = 340` keys

An internal page with `N` keys contains `N + 1` child pointers:

- `N` left-child pointers embedded in cells
- `1` right-child pointer in the header

### Why this internal format was chosen

This representation simplifies child lookup:

- for child index `< keyCount`, use the left child stored with that separator
- for child index `== keyCount`, use the dedicated right child field

It also makes split logic easier because the implementation can reconstruct a temporary `children` vector and `keys` vector, then rewrite the page from scratch.

## 9. Value Encoding

Values are fixed-size byte arrays. The conversion helpers are:

- `encodeValue(std::string_view)`
- `decodeValue(const ValueBytes&)`

Encoding behavior:

- up to `256` bytes are copied
- longer values are truncated
- shorter values are zero-padded

Decoding behavior:

- bytes are read until the first `'\0'`

This means the current system is suitable for small string values but is not yet a general binary-safe value store with explicit length tracking.

## 10. B+ Tree Architecture

The tree implementation lives in [src/tree/BPlusTree.cpp](/Users/salt-dev/Developments/c-plus-plus/tutorial/src/tree/BPlusTree.cpp).

### Core properties

- all records live in leaf nodes
- internal nodes store only routing separators
- leaves are linked by `nextLeafPageId`
- all navigation is done through page IDs
- inserts keep keys sorted
- duplicate inserts overwrite existing values

### Search path

For point lookup:

1. start at `header_.rootPageId`
2. if the page is internal, choose a child using separator comparison
3. repeat until a leaf is reached
4. binary search the leaf for the target key

Internal child selection uses this rule:

- descend to the first separator key greater than the target key
- if none matches, descend to the rightmost child

Leaf search uses a lower-bound style binary search implemented by `leafFindKeyIndex`.

## 11. Insert Algorithm

Insertion is recursive.

### High-level flow

1. descend to the target leaf
2. if the key already exists, overwrite the value
3. if the leaf has free space, insert in sorted order
4. otherwise split the leaf
5. promote a separator to the parent
6. if the parent overflows, split the parent
7. if the root overflows, create a new root

### Leaf insert without split

The implementation:

- reads all existing entries into a temporary vector
- inserts the new key/value in sorted position
- reinitializes the page
- rewrites the page body from the temporary vector

This is not the most write-efficient strategy, but it is simple and reliable.

### Leaf split

When a leaf overflows:

1. allocate a new right leaf
2. build a temporary sorted vector of entries
3. split at `entries.size() / 2`
4. rewrite the left leaf with the left half
5. rewrite the new right leaf with the right half
6. preserve the old leaf-chain by:
   - setting `left.nextLeaf = rightPageId`
   - setting `right.nextLeaf = oldNextLeaf`
7. promote the first key of the new right leaf

That promoted key becomes the separator inserted into the parent.

### Internal insert without split

The implementation reconstructs:

- a temporary `children` vector
- a temporary `keys` vector

It inserts the promoted key and right child at the correct position, then rewrites the page in normalized layout.

### Internal split

When an internal node overflows:

1. construct temporary `keys` and `children`
2. choose the middle separator as the promoted key
3. write the left half back into the original page
4. allocate and write a new right internal page
5. update parent pointers for all children moved to the new right page

### Root split

If a split reaches the root:

1. allocate a new internal root page
2. store:
   - left child page ID
   - promoted separator key
   - right child page ID
3. update both child pages to:
   - clear their old root flags
   - point to the new parent
4. update `header_.rootPageId`
5. flush the header back to page `0`

## 12. Delete Semantics

Delete is currently implemented as a simple leaf-only removal.

Behavior:

1. descend to the target leaf
2. locate the key
3. shift remaining leaf entries left
4. decrement `keyCount`

What is intentionally not implemented yet:

- borrow from sibling
- merge underfull leaves
- merge internal nodes
- parent separator repair
- root shrink

This means deletes preserve key correctness for the current tests, but can leave the tree less space-efficient over time.

## 13. Range Scans

Range scans are supported because leaves are linked.

### Full scan

If no start key is provided:

1. descend from the root to the leftmost leaf
2. iterate keys in that leaf
3. follow `nextLeafPageId` until the chain ends

### Bounded scan

If `start` is provided:

1. descend directly to the leaf that would contain `start`
2. skip keys smaller than `start`
3. stop once `end` is exceeded

This is the main reason a B+ tree is useful over a plain binary tree for storage: scans remain efficient and sorted without traversing internal pages repeatedly.

## 14. Database API

The public API is defined in [src/db/Database.h](/Users/salt-dev/Developments/c-plus-plus/tutorial/src/db/Database.h).

### Public methods

- `Database(path)`
- `put(key, value)`
- `get(key)`
- `remove(key)`
- `scan(start, end)`
- `debugString()`
- `header()`
- `pager()`

### Role of `Database`

The `Database` class is the integration boundary between persistence and tree logic. It is responsible for:

- opening the pager
- initializing new database files
- loading and validating the header
- flushing header changes when the tree updates root or page count
- exposing a small clean API to callers

It deliberately does not implement tree operations itself.

## 15. Invariant Checking

Structural validation is implemented in [src/util/InvariantChecker.cpp](/Users/salt-dev/Developments/c-plus-plus/tutorial/src/util/InvariantChecker.cpp).

### Checks performed

- no cycles in the tree
- parent page IDs match the actual traversal path
- keys inside each leaf are sorted
- keys inside each internal node are sorted
- leaf chain ordering is monotonic
- all leaves are at the same depth
- child ranges are compatible with parent separators

### Notes on empty pages

Because delete currently does not rebalance, empty non-root leaves may exist temporarily. The validator is tolerant of that simplified delete model and focuses on correctness rather than ideal space utilization.

## 16. Debug Tree Output

Human-readable tree printing is implemented in [src/util/DebugPrinter.cpp](/Users/salt-dev/Developments/c-plus-plus/tutorial/src/util/DebugPrinter.cpp).

The output format is hierarchical, for example:

```text
Internal(3): [40, 90]
  Leaf(1): [10, 20, 30]
  Leaf(2): [40, 50, 70]
  Leaf(4): [90, 100, 120]
```

This is useful when validating split behavior manually.

## 17. Testing Strategy

The test suite is implemented in [tests/database_tests.cpp](/Users/salt-dev/Developments/c-plus-plus/tutorial/tests/database_tests.cpp).

### Test categories

#### Basic persistence

Verifies:

- inserts survive close/reopen
- updates overwrite correctly
- missing keys return empty

#### Leaf and internal splits

Verifies:

- large insert runs create multi-page trees
- all inserted keys remain readable
- range scans return the right boundaries
- the root page eventually changes from page `1`

#### Randomized operations against `std::map`

Uses an in-memory `std::map<uint64_t, std::string>` as a reference model.

Operations are randomized across:

- `put`
- `remove`
- `get`

The test continuously compares the database behavior to the reference model and periodically validates tree invariants.

This is the strongest correctness mechanism in the current project.

## 18. Error Handling

Error handling is currently exception-based.

Examples:

- invalid page access throws
- bad file size alignment throws
- invalid database header throws
- CLI catches exceptions and prints `error: ...`

This is adequate for a small embedded tool, but future versions may want more structured status/error types.

## 19. Performance Characteristics

The current implementation favors clarity over throughput.

### Current tradeoffs

- no page cache
- page flushes happen eagerly
- nodes are often rebuilt from temporary vectors during inserts
- no bulk loading
- no prefix compression
- no free-list reuse

### Expected behavior

- point lookups are logarithmic in tree height plus one leaf search
- range scans are efficient after locating the first leaf
- inserts are correct but not optimized for high write volume

Given the small leaf capacity of `15`, trees grow in height earlier than they would in systems with smaller values or variable-length payload handling.

## 20. Known Limitations

The current implementation is intentionally incomplete in several areas:

- values are truncated to `256` bytes
- values do not store an explicit length
- delete does not rebalance the tree
- there is no transaction support
- there is no WAL or crash recovery
- there is no page checksum or corruption detection beyond header validation
- there is no free-page reuse
- there is no concurrent reader/writer support
- there is no schema or SQL layer
- there is no binary compatibility guarantee across future versions

These are not bugs in the current scope; they are deferred features.

## 21. Extensibility Roadmap

The most natural next improvements are:

1. explicit `create` and `info` CLI commands
2. better output modes such as JSON
3. free-page reuse
4. variable-length values with a slotted-page layout
5. delete rebalancing and node merge
6. page cache
7. WAL and crash recovery
8. concurrency control
9. higher-level query interface

Each of these should be added only after preserving the current invariants and test coverage.

## 22. How to Read the Code

For a newcomer, the best reading order is:

1. [docs/DESIGN.md](/Users/salt-dev/Developments/c-plus-plus/tutorial/docs/DESIGN.md)
2. [src/storage/FileFormat.h](/Users/salt-dev/Developments/c-plus-plus/tutorial/src/storage/FileFormat.h)
3. [src/storage/PageLayout.h](/Users/salt-dev/Developments/c-plus-plus/tutorial/src/storage/PageLayout.h)
4. [src/storage/Pager.cpp](/Users/salt-dev/Developments/c-plus-plus/tutorial/src/storage/Pager.cpp)
5. [src/db/Database.cpp](/Users/salt-dev/Developments/c-plus-plus/tutorial/src/db/Database.cpp)
6. [src/tree/BPlusTree.cpp](/Users/salt-dev/Developments/c-plus-plus/tutorial/src/tree/BPlusTree.cpp)
7. [src/util/InvariantChecker.cpp](/Users/salt-dev/Developments/c-plus-plus/tutorial/src/util/InvariantChecker.cpp)
8. [tests/database_tests.cpp](/Users/salt-dev/Developments/c-plus-plus/tutorial/tests/database_tests.cpp)

That sequence moves from storage primitives to tree behavior to verification.

## 23. Summary

This project is a compact storage-engine implementation with a clean separation between:

- file and page persistence
- binary page layout
- B+ tree algorithms
- external API
- CLI integration
- validation and testing

It is already sufficient for educational use, experimentation, and incremental extension toward a more capable embedded database.
