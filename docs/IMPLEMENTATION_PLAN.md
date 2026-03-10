# The end goal

Your first serious version should support:

- open/create database file
- insert key/value
- get value by key
- update existing key
- delete key
- range scan
- persistent storage on disk
- B+ tree page splits

That is already a real database engine.

---

# Big picture architecture

Build it as 6 layers:

1. **File format**
2. **Pager**
3. **Page/node layout**
4. **B+ tree algorithms**
5. **Database API**
6. **Testing and validation**

Think in this direction:

```text
DB API
  -> B+ Tree
    -> Pager
      -> File on disk
```

---

# Important scope rules

To avoid getting lost, your first version should have these constraints:

- single process only
- no concurrency
- no SQL
- no transactions
- no WAL
- fixed page size: 4096 bytes
- fixed key type: `uint64_t`
- fixed value size initially, for example 128 or 256 bytes
- one file only

These constraints are what make the project finishable.

---

# Phase 0 — Understand the target before coding

Before writing code, you should be able to explain these concepts in your own words:

- what a **page** is
- why databases use **fixed-size pages**
- difference between **internal node** and **leaf node**
- why **B+ tree** is usually better than plain B-tree for storage
- why child references must be **page IDs** and not pointers
- what happens during a **split**
- why leaves are often linked for range scans

Your mental model should be:

- file is divided into pages
- each page stores one node
- internal pages guide search
- leaf pages store actual key/value data
- root page ID is stored in metadata

If you cannot explain that clearly yet, start there.

---

# Phase 1 — Design the file format on paper

Do this before coding.

## 1. Choose the page size

Use:

```text
4096 bytes
```

That is standard and easy.

## 2. Define page 0

Reserve page 0 for database metadata.

It should contain at least:

- magic number
- version
- page size
- root page id
- next free page id or total page count

Example conceptually:

```text
Page 0 = DatabaseHeader
```

## 3. Define page types

You need at least:

- internal node page
- leaf node page

## 4. Define common page header

Every page should start with a small header.

Example fields:

- page type
- number of keys
- parent page id
- maybe reserved bytes for future use

## 5. Define internal page layout

Internal pages should store:

- child pointers
- separator keys

Conceptually:

```text
child0, key0, child1, key1, child2, ...
```

## 6. Define leaf page layout

Leaf pages should store:

- keys
- values
- next leaf page id

Conceptually:

```text
key/value pairs + nextLeafPageId
```

## 7. Calculate capacities

Before coding, compute:

- how many key/value pairs fit in one leaf page
- how many keys/children fit in one internal page

This is critical because your split logic depends on it.

---

# Phase 2 — Build the pager first

Do not start with the tree logic.

The pager is the foundation.

## Goal of pager

It should let you:

- open the database file
- read a page by page ID
- write a page by page ID
- allocate a new page
- know how many pages exist

## What to implement

At this stage, think of pages as raw bytes.

Your pager should support operations like:

- `readPage(pageId)`
- `writePage(pageId, buffer)`
- `allocatePage()`

## What to test

Before building the tree, verify:

- creating a new file works
- page 0 can be written and read back
- page 1 can be written and read back
- reopening the file preserves bytes exactly

## Success criterion

If you write bytes to page 3, close the DB, reopen it, and read page 3, the bytes must match exactly.

If the pager is weak, everything above it breaks.

---

# Phase 3 — Build serialization and page helpers

Now build utilities that interpret raw page bytes as structured pages.

Important: this is not yet “tree logic.” It is page manipulation.

## Build page helper layer

You want helper functions/classes for:

- reading page header
- setting key count
- checking page type
- reading key at index
- writing key at index
- reading child page ID
- writing child page ID
- reading value at index
- writing value at index
- setting/getting next leaf page id

## Goal

Be able to say:

- “insert key into this leaf page buffer”
- “shift entries right”
- “read child 2 from this internal page”

without mixing that logic with file code.

## What to test

For a single in-memory page buffer:

- write 5 keys
- read them back
- verify ordering
- verify header fields are correct

This phase is about mastering your binary layout.

---

# Phase 4 — Implement a single leaf root only

Still do not implement internal nodes yet.

Start with the simplest possible real DB:

- the root is a leaf
- all records live in that one page
- no splits yet

## Support

- insert key/value into leaf
- find key in leaf
- update existing key
- keep keys sorted

## Why this matters

It lets you validate:

- file format
- page layout
- basic search
- sorted insert
- persistence

before the hard part begins.

## What to test

- insert keys in random order
- reopen DB and verify they remain readable
- update same key
- search missing key
- insert until leaf becomes full

## Success criterion

You have a working tiny persistent key-value DB with one leaf page.

That is your first major milestone.

---

# Phase 5 — Handle leaf splits and root split

This is where it starts feeling like a real B+ tree.

## What happens

When a leaf is full:

- allocate a new leaf page
- move half the records to the new leaf
- update leaf linked-list pointer
- if the old leaf was the root, create a new internal root
- otherwise promote separator key to parent

For your first split implementation, start with only this case:

- root is leaf
- root becomes full
- split it into two leaves
- create a new internal root

Do not jump to recursive multi-level insertion yet.

## What to understand deeply

You should be able to explain:

- which key is promoted
- why the parent stores a separator
- how lookups decide left vs right child
- how linked leaves preserve scan order

## What to test

- insert enough keys to force first split
- verify all keys remain searchable
- verify tree structure manually
- verify range scan across both leaves

This is your second major milestone.

---

# Phase 6 — Add internal node search

Now implement tree traversal.

## Goal

Starting from the root:

- if page is internal, choose the correct child
- descend
- continue until a leaf
- search within the leaf

## What to implement

- binary or linear search inside internal nodes
- child selection logic
- recursive or iterative descent

## Testing

Create trees with multiple leaves and verify:

- every inserted key is found
- missing keys return not found
- boundary keys route correctly
- smallest and largest keys work

At this phase, reads become truly tree-based.

---

# Phase 7 — Full insert with recursive splitting

Now you implement real B+ tree insertion for arbitrary depth.

## Cases to handle

1. insert into non-full leaf
2. split full leaf and insert separator into parent
3. if parent is full, split parent too
4. if root splits, create new root

This is the hardest core phase.

## Practical advice

Implement it in a disciplined way:

- first find target leaf
- insert if space exists
- otherwise split leaf
- then propagate separator upward
- repeat if necessary

Do not try to make it “clever.” Make it explicit.

## What to test

- insert ascending keys
- insert descending keys
- insert random keys
- large enough input to create depth 3 tree
- after each insert, verify invariants

## Invariants to check

- keys in each node are sorted
- internal nodes have valid child counts
- leaf linked list is correct
- all leaves are at same depth
- every key remains searchable

This is the core of your engine.

---

# Phase 8 — Add range scans

One big reason to use B+ tree is sorted access.

## Goal

Support something like:

- scan all keys
- scan from key A to key B

## How

- descend to the first relevant leaf
- iterate keys in that leaf
- follow `nextLeafPageId`
- continue until end condition

## Tests

- full scan returns sorted order
- partial scan returns correct range
- edge ranges behave correctly
- scan over multiple leaf pages works

Once this works, the database starts to feel powerful.

---

# Phase 9 — Add delete, but keep it simple first

Deletion is trickier than insertion.

So do it in two stages.

## Stage 1

Support **logical/simple deletion** inside a leaf only:

- if key exists in leaf, remove it
- shift remaining keys left
- do not rebalance yet

This means the tree may become less space-efficient, but correctness can still hold.

## Stage 2

Later, implement proper B+ tree delete:

- borrow from sibling
- merge nodes
- update parent separators
- shrink root when needed

I recommend postponing full rebalance until everything else is rock solid.

---

# Phase 10 — Build a clean external API

Once tree logic works, wrap it in a small public database interface.

Example conceptual API:

- `open(path)`
- `put(key, value)`
- `get(key)`
- `remove(key)`
- `scan(start, end)`

The public API should know almost nothing about page layouts.

That separation will keep your code clean.

---

# Phase 11 — Add correctness tooling

This is extremely important.

When building storage engines, debugging by “it seems to work” is dangerous.

## Build an invariant checker

Create a debug function that validates the tree.

It should verify things like:

- page types are valid
- keys sorted within every node
- parent-child relationships make sense
- internal separator ordering is valid
- linked leaf chain is sorted
- all leaves at same depth

Run this after many inserts during development.

## Build a tree printer

Print a human-readable representation of the tree:

```text
Root: [40, 90]
  Leaf: [10, 20, 30]
  Leaf: [40, 50, 70]
  Leaf: [90, 100, 120]
```

This will save you enormous time.

## Build a page hex dump tool

For serious bugs, being able to dump raw page bytes is very useful.

---

# Phase 12 — Stress testing

Once the engine works functionally, test it aggressively.

## Use these patterns

- ascending inserts
- descending inserts
- random inserts
- duplicate key updates
- repeated reopen/close cycles
- many scan tests
- many delete tests

## Strong strategy

Use a reference model in memory:

- keep a `std::map<uint64_t, std::string>` as the truth model
- every operation applied to your DB is also applied to the map
- compare outputs continuously

This is one of the best ways to catch subtle bugs.

---

# Suggested project structure

Something along these lines:

```text
src/
  db/
    Database.cpp
    Database.h
  storage/
    Pager.cpp
    Pager.h
    FileFormat.h
    PageLayout.h
  tree/
    BPlusTree.cpp
    BPlusTree.h
    LeafNode.cpp
    LeafNode.h
    InternalNode.cpp
    InternalNode.h
  util/
    Serializer.h
    DebugPrinter.cpp
    DebugPrinter.h
    InvariantChecker.cpp
    InvariantChecker.h
tests/
```

You may organize it differently, but keep these responsibilities separate.

---

# Recommended implementation order

Here is the exact order I would follow:

## Step 1

Write a one-page design doc:

- file layout
- page header
- internal page format
- leaf page format
- page capacities
- split rules

## Step 2

Implement pager.

## Step 3

Implement metadata page.

## Step 4

Implement leaf page helper functions.

## Step 5

Implement single-root-leaf insert/get.

## Step 6

Persist and reload successfully.

## Step 7

Implement first leaf split and new root creation.

## Step 8

Implement internal page helper functions.

## Step 9

Implement tree descent for search.

## Step 10

Implement recursive insert and internal splits.

## Step 11

Implement leaf linked-list scanning.

## Step 12

Implement delete without rebalance.

## Step 13

Implement invariant checker and stress tests.

## Step 14

Only then consider advanced features.

---

# Common mistakes to avoid

## 1. Starting with variable-length values

Do not do that first. It complicates layout badly.

## 2. Mixing pager and tree logic

Keep file access separate from node algorithms.

## 3. Using raw pointers in node relationships

Use page IDs, not pointers.

## 4. Skipping invariants

You need validation tools early.

## 5. Adding transactions too soon

Leave crash recovery and WAL for later.

## 6. No manual drawings

Draw split examples on paper. It helps a lot.

## 7. Coding before fixing page layout

Your page format should be stable before major coding.

---

# What to do after v1 works

Once the first version is correct, then you can improve it in this order:

- variable-length values
- free-page reuse
- page cache
- checksums
- WAL
- crash recovery
- concurrency
- simple query language

But these are phase 2 goals, not phase 1.

---

# Your study method

Since you want to code it yourself, use this working style:

For each phase:

1. define exactly what “done” means
2. write the data layout on paper
3. implement only that phase
4. test it hard
5. do not continue until stable

That discipline is what makes projects like this finishable.

---

# A very practical 8-week roadmap

## Week 1

Database theory for your project:

- pages
- B+ trees
- internal vs leaf nodes
- separator keys
- file format design

Deliverable: design doc

## Week 2

Pager + file header

Deliverable: can create/read/write/reopen pages

## Week 3

Leaf page layout + root-as-leaf DB

Deliverable: insert/get in one page

## Week 4

Leaf splitting + root split

Deliverable: first real tree

## Week 5

Internal nodes + search descent

Deliverable: multi-leaf searchable tree

## Week 6

Recursive insert + internal splits

Deliverable: arbitrary-depth insert

## Week 7

Range scan + delete without rebalance + invariant checker

Deliverable: robust usable engine

## Week 8

Stress tests + bug fixing + cleanup + documentation

Deliverable: stable v1

---

# Best definition of success

Do not measure success as:

“it looks like a database”

Measure success as:

- file persists correctly
- all inserted keys can be read back
- sorted order is maintained
- splits preserve invariants
- database survives process restart
- stress tests pass

That is real success.
