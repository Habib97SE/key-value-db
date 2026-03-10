# Database Design

## Scope

- Single-process embedded key/value store
- Fixed page size: `4096` bytes
- Fixed key type: `uint64_t`
- Fixed value size: `256` bytes
- One node per page
- Page IDs are `uint32_t`

## File Layout

- Page `0`: database metadata
- Page `1+`: B+ tree nodes

## Metadata Page

The metadata page stores:

- magic: `SDBTREE1`
- format version: `1`
- page size
- root page id
- total page count

## Common Page Header

Every tree page starts with this logical header:

- `pageType` (`uint8_t`)
- `flags` (`uint8_t`)
- `reserved` (`uint16_t`)
- `parentPageId` (`uint32_t`)
- `keyCount` (`uint16_t`)
- `reserved2` (`uint16_t`)

Common header size: `12` bytes

## Leaf Page Layout

Leaf header adds:

- `nextLeafPageId` (`uint32_t`)

Leaf header size: `16` bytes

Leaf entries are fixed size:

- key: `8` bytes
- value: `256` bytes

Leaf entry size: `264` bytes

Leaf capacity:

- `(4096 - 16) / 264 = 15`

## Internal Page Layout

Internal pages store:

- common header (`12` bytes)
- right child pointer (`uint32_t`) for alignment and simpler child lookup
- repeated cells of:
  - left child page id (`uint32_t`)
  - separator key (`uint64_t`)

Internal header size: `16` bytes

Internal cell size: `12` bytes

Internal capacity:

- `(4096 - 16) / 12 = 340`

An internal page with `N` keys stores:

- `N` left-child pointers inside cells
- `1` right-child pointer in the header
- `N + 1` total children

Child selection rule:

- Descend to the first separator key greater than the target key
- If none exists, descend to `rightChildPageId`

## Split Rules

Leaf split:

- Insert into a temporary sorted list
- Keep `leftCount = total / 2`
- Keep `rightCount = total - leftCount`
- New right leaf inherits the old `nextLeafPageId`
- Left leaf points to the new right leaf
- Promote the first key from the new right leaf

Internal split:

- Insert the incoming separator and child into a temporary sorted list
- Promote the middle key
- Left internal keeps keys before the promoted key
- Right internal gets keys after the promoted key
- Right page gets the child pointers to the right side of the promoted key

## API Shape

- `open(path)`
- `put(key, value)`
- `get(key)`
- `remove(key)`
- `scan(start, end)`

## Debugging Support

- Invariant checker validates sorted keys, parent links, and leaf chain ordering
- Tree printer renders a readable B+ tree
