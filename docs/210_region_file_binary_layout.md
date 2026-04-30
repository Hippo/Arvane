# Region File Binary Layout

Region files store chunk data, region-local tables, and optional entity data.

This document defines the **byte-level structure** of `.arvr` region files.

Goals:

- fast chunk lookup
- minimal parsing overhead
- compact storage
- forward compatibility
- safe partial reads
- future extensibility

---

## File Overview

A region file consists of:

```text
[ Header ]
[ Chunk Location Table ]
[ Chunk Metadata Table ]
[ Region Tables Block ]
[ Chunk Data Blocks... ]
[ Optional Entity Block ]
[ Optional Footer / Checksum ]
```

All offsets are absolute from file start.

---

## Endianness

All integers are:

```text
little-endian
```

---

## Header (Fixed Size)

```text
Offset  Size  Field
0       4     magic = "ARVR"
4       2     format_version
6       2     header_size
8       4     region_x
12      4     region_z
16      4     chunk_count (max slots, e.g. 1024 for 32x32)
20      4     location_table_offset
24      4     metadata_table_offset
28      4     region_tables_offset
32      4     data_start_offset
36      4     file_size
40      4     flags
44      ...   reserved (pad to header_size)
```

### Notes

* `magic` identifies file type
* `format_version` enables parsing changes
* `header_size` allows header extension without breaking old readers
* `flags` reserved for compression/encryption/etc.

---

## Chunk Indexing

Regions use a fixed grid:

```text
REGION_SIZE = 32
chunk_index = local_x + local_z * 32
```

* `local_x = chunk_x mod 32`
* `local_z = chunk_z mod 32`

---

## Chunk Location Table

Stores where chunk data lives in the file.

```text
Offset: location_table_offset
Entry count: chunk_count

Each entry (8 bytes):

0   4   offset (0 = not present)
4   4   length (compressed size in bytes)
```

### Notes

* `offset` is absolute file offset
* `length` includes chunk header + payload
* `offset = 0` means chunk is not stored

---

## Chunk Metadata Table

Stores lightweight metadata for quick decisions without reading chunk data.

```text
Offset: metadata_table_offset
Entry count: chunk_count

Each entry (8 bytes):

0   2   section_mask
2   1   flags
3   1   version
4   4   reserved
```

### Fields

#### `section_mask` (16 bits)

For format v3 and newer, this field is a legacy quick-reject hint, not an
authoritative height limit.

If every stored section in the column falls inside the first 16 sections
relative to the world's `min_chunk_y`, each bit represents one vertical
section:

```text
bit i = section (chunk_y = min_chunk_y + i) exists
```

If any stored section is outside that 16-section window, the writer sets
`section_mask = 0xFFFF` to mean "non-empty column, inspect the chunk payload".
Readers must not reject or skip a chunk section only because its Y coordinate
cannot be represented in this mask.

Legacy format v1/v2 files used this field as an exact 16-section mask.

#### `flags`

Bitfield:

```text
bit 0 = has_block_entities
bit 1 = has_scheduled_ticks
bit 2 = lighting_valid
bit 3 = reserved
```

---

## Region Tables Block

```text
Offset: region_tables_offset
```

Contains all region-wide tables.

### Structure

```text
u32 table_block_size

repeat:
  u16 table_type
  u16 entry_count
  u32 data_size
  [data]
```

---

## Table Types

```text
1 = block_state_table
2 = item_table
3 = entity_type_table
4 = block_entity_type_table
5 = biome_table
```

---

## Table Encoding

Tables store **UTF-8 strings**.

```text
For each entry:

u16 string_length
[u8 * length] string bytes
```

Example:

```text
"arvane:stone"
```

---

## Chunk Data Block

Each chunk is stored independently.

```text
Offset: from location table
```

### Chunk Header

Format v3 and newer:

```text
Offset  Size  Field
0       1     compression_type
1       1     reserved
2       2     reserved
4       4     section_count
8       4     uncompressed_size
12      4     compressed_size
16      ...   compressed_payload
```

Legacy format v1/v2 files used an 8-bit section count:

```text
Offset  Size  Field
0       1     compression_type
1       1     section_count
2       2     reserved
4       4     uncompressed_size
8       4     compressed_size
12      ...   compressed_payload
```

---

## Compression Types

```text
0 = none
1 = zstd
```

---

## Chunk Payload (Uncompressed)

```text
[ Section Data... ]
[ Block Entities ]
[ Scheduled Ticks ]
```

---

## Section Data

Each section:

Format v3 and newer:

```text
i32 chunk_y
u8  bits_per_block
u8  reserved
u16 reserved

u32 block_data_size
[u8 * block_data_size] packed block data

u32 lighting_data_size
[u8 * lighting_data_size] lighting data

u32 fluid_data_size
[u8 * fluid_data_size] fluid data
```

`chunk_y` is the absolute signed chunk-section Y coordinate. Region files must
not impose a fixed world-height window.

Legacy format v1/v2 files used an 8-bit relative Y index:

```text
u8  y_index
u8  bits_per_block
u16 reserved

u32 block_data_size
[u8 * block_data_size] packed block data

u32 lighting_data_size
[u8 * lighting_data_size] lighting data

format v2 only:
u32 fluid_data_size
[u8 * fluid_data_size] fluid data
```

---

## Block Data Encoding

* 4096 entries per section
* each entry is index into block_state_table

Bit packing:

```text
values packed consecutively
no padding between entries
aligned to byte boundary at end
```

---

## Lighting Data

```text
16 bits per block (2 bytes)
4096 * 2 = 8192 bytes per section
```

May be compressed via chunk compression.

---

## Block Entities

```text
u32 count

repeat count times:
  u16 x, y, z (local position)
  u16 type_index (into block_entity_type_table)
  u16 version
  u32 payload_size
  [payload bytes]
```

Payload format:

```text
binary or JSON (engine-defined)
```

---

## Scheduled Ticks

```text
u32 count

repeat:
  u16 x, y, z
  u16 type_index
  u32 due_tick
  u8  priority
```

---

## Optional Entity Block (Region-Level)

Stored after chunk data.

```text
u32 entity_count

repeat:
  u128 uuid
  u16 type_index
  f32 position[3]
  f32 velocity[3]
  u16 version
  u32 payload_size
  [payload bytes]
```

---

## Footer (Optional)

```text
u32 checksum_type
u64 checksum_value
```

Example:

```text
checksum_type = 1 (xxhash64)
```

---

## Alignment Rules

* No global alignment required
* Chunk blocks may be tightly packed
* Tables are tightly packed
* All offsets must be valid absolute positions

---

## Write Strategy

To update a chunk:

```text
1. write new chunk block at end of file
2. update location table entry
3. optionally mark old block unused
```

Optional future optimization:

* region compaction pass

---

## Read Strategy

```text
1. read header
2. read tables
3. lookup chunk in location table
4. read chunk block
5. decompress if needed
6. decode sections
```

---

## Corruption Handling

If:

* offset invalid
* length invalid
* decompression fails

Then:

```text
- mark chunk as corrupted
- skip chunk
- optionally regenerate
```

Must not crash engine.

---

## Future Extensions

Reserved for:

* per-chunk checksums
* delta encoding
* streaming regions
* partial chunk updates
* multi-layer compression
* encryption

Header and block structures allow extension without breaking compatibility.

---

## Design Summary

* Fixed index tables → fast lookup
* Region tables → registry independence
* Chunk blocks → isolated, replaceable
* Compression → efficient storage
* Append-style writes → safe updates

---
