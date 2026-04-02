<!--
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing,
  software distributed under the License is distributed on an
  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
  KIND, either express or implied.  See the License for the
  specific language governing permissions and limitations
  under the License.
-->

# PAX Column Encoding

## Table of Contents

- [Encoding Reference](#encoding-reference)
  - [DeltaDelta](#deltadelta)
  - [Gorilla](#gorilla)
  - [Bool](#bool)
  - [Zstd](#zstd)
  - [LZ4](#lz4)
  - [Zlib](#zlib)
  - [RLE\_V2](#rle_v2)
  - [Direct Delta](#direct-delta)
  - [Dictionary](#dictionary)
  - [None](#none)
- [Encoding Selection Guide](#encoding-selection-guide)
- [Compression Tuning for Time-Series Workloads](#compression-tuning-for-time-series-workloads)
- [References](#references)

---

PAX supports per-column encoding via the `ENCODING` clause. Each column
in a PAX table can independently choose an encoding type that best matches
its data characteristics.

```sql
CREATE TABLE metrics (
    ts    TIMESTAMPTZ ENCODING (compresstype=deltadelta),
    id    INT         ENCODING (compresstype=deltadelta),
    value FLOAT8      ENCODING (compresstype=gorilla),
    flag  BOOL        ENCODING (compresstype=bool),
    name  TEXT        ENCODING (compresstype=zstd)
) USING pax;
```

Table-level default compression can also be set via `WITH (compresstype=...)`,
which applies to all columns that do not have an explicit `ENCODING` clause.

## Encoding Reference

PAX supports 10 encoding types, registered in `ColumnEncoding.Kind`
(`src/cpp/storage/proto/pax.proto`):

| compresstype | Protobuf Kind | Applicable Types | Description |
|-------------|---------------|------------------|-------------|
| `none` | `NO_ENCODED (0)` | All | No encoding (default) |
| `rle` | `RLE_V2 (1)` | Integer types | ORC Run-Length Encoding v2 |
| `delta` | `DIRECT_DELTA (2)` | Integer types | ORC direct delta encoding |
| `zstd` | `COMPRESS_ZSTD (3)` | All | Zstandard generic compression |
| `zlib` | `COMPRESS_ZLIB (4)` | All | Zlib/deflate generic compression |
| `dict` | `DICTIONARY (5)` | Text, variable-length | ORC dictionary encoding |
| `lz4` | `COMPRESS_LZ4 (6)` | All | LZ4 fast generic compression |
| `gorilla` | `GORILLA (7)` | Float, integer types | XOR-based float compression |
| `deltadelta` | `DELTA_DELTA (8)` | Integer, timestamp types | Double-delta compression |
| `bool` | `BOOL_COMPRESS (9)` | Bool only | Simple8b-RLE boolean packing |

---

### DeltaDelta

| Property | Value |
|----------|-------|
| **compresstype** | `deltadelta` |
| **Applicable types** | `int2`, `int4`, `int8`, `timestamp`, `timestamptz`, `date` |
| **Lossless** | Yes (bit-exact roundtrip) |

**Algorithm.** Double-delta encoding with zigzag transformation and Simple8b-RLE
bit packing:

1. First-order deltas: `d[i] = v[i] - v[i-1]`
2. Second-order deltas: `dd[i] = d[i] - d[i-1]`
3. Zigzag encode signed to unsigned: `(dd << 1) ^ (dd >> 63)`
4. Pack with Simple8b-RLE, which selects the most compact bit-width for
   each block of values

For regular sequences (e.g., timestamps with constant 10s intervals), the
second-order deltas are all zero, compressing to near-zero overhead. For
cyclic integer patterns (e.g., `tags_id = i % 100`), deltas are small and
highly repetitive.

**Best for:** Timestamps with regular intervals, monotonic counters, device IDs.

**Usage.**
```sql
CREATE TABLE t (
    ts  TIMESTAMPTZ ENCODING (compresstype=deltadelta),
    id  INT         ENCODING (compresstype=deltadelta)
) USING pax;
```

**Reference.** Simple8b-RLE based on Vo Ngoc Anh & Alistair Moffat,
"Index compression using 64-bit words", Software: Practice and Experience, 2010.

---

### Gorilla

| Property | Value |
|----------|-------|
| **compresstype** | `gorilla` |
| **Applicable types** | `float4`, `float8` (also works on `int2`/`int4`/`int8`) |
| **Lossless** | Yes (bit-exact roundtrip) |

**Algorithm.** XOR-based floating-point compression:

1. XOR current value with predecessor: `xor = bits(v[i]) ^ bits(v[i-1])`
2. If XOR = 0 (identical values): encode as a single 0-bit
3. If XOR has the same leading/trailing zero window as previous XOR:
   encode only the meaningful bits (reuse the window)
4. Otherwise: encode new leading-zero count + meaningful-bit count + bits

The implementation stores five parallel streams (two tag streams via
Simple8b-RLE, leading-zero counts via Simple8b-RLE, meaningful-bit counts
via Simple8b-RLE, and XOR meaningful bits via BitArray) for cache-friendly
sequential decoding.

**Best for:** Slowly-changing float metrics (CPU usage, temperature, sensor
readings) where adjacent values share most of their IEEE 754 bit pattern.

**Usage.**
```sql
CREATE TABLE t (
    cpu  FLOAT8 ENCODING (compresstype=gorilla),
    mem  FLOAT4 ENCODING (compresstype=gorilla)
) USING pax;
```

**Reference.** T. Pelkonen et al., "Gorilla: A Fast, Scalable, In-Memory
Time Series Database", VLDB 2015.

---

### Bool

| Property | Value |
|----------|-------|
| **compresstype** | `bool` |
| **Applicable types** | `bool` only |
| **Lossless** | Yes |

**Algorithm.** Converts booleans to 0/1 and packs with Simple8b-RLE.
Up to 60 booleans fit in a single 64-bit word (selector 1: 60 x 1-bit each).
Long constant runs compress further via the RLE selector.

Standard PostgreSQL uses 1 byte per boolean; this encoding reduces storage
to approximately 1 bit per value.

**Best for:** Boolean flag columns (`active`, `is_deleted`, `alarm`).

**Usage.**
```sql
CREATE TABLE t (
    active BOOL ENCODING (compresstype=bool)
) USING pax;
```

**Note:** Bool encoding is not compatible with `porc_vec` storage format.

---

### Zstd

| Property | Value |
|----------|-------|
| **compresstype** | `zstd` |
| **Applicable types** | All types |

Facebook Zstandard compression. Treats column data as an opaque byte stream.
Good balance of compression ratio and speed.

**Best for:** `text`, `varchar`, `bytea`, `jsonb`, and other variable-length
types where the above type-specific encodings are not applicable.

```sql
CREATE TABLE t (name TEXT ENCODING (compresstype=zstd)) USING pax;
```

---

### LZ4

| Property | Value |
|----------|-------|
| **compresstype** | `lz4` |
| **Applicable types** | All types |

LZ4 fast compression. Lower compression ratio than zstd but significantly
faster compression/decompression speed.

**Best for:** Write-heavy workloads where compression speed matters more
than storage savings.

```sql
CREATE TABLE t (data BYTEA ENCODING (compresstype=lz4)) USING pax;
```

---

### Zlib

| Property | Value |
|----------|-------|
| **compresstype** | `zlib` |
| **Applicable types** | All types |

Zlib (deflate) compression. Generally superseded by zstd, which offers
better compression ratio at comparable speed.

```sql
CREATE TABLE t (payload TEXT ENCODING (compresstype=zlib)) USING pax;
```

---

### RLE_V2

| Property | Value |
|----------|-------|
| **compresstype** | `rle` |
| **Applicable types** | Integer types |

ORC Run-Length Encoding v2. Supports short repeat, direct, patched base,
and delta sub-encodings. Inherited from the original ORC format.

```sql
CREATE TABLE t (status INT ENCODING (compresstype=rle)) USING pax;
```

---

### Direct Delta

| Property | Value |
|----------|-------|
| **compresstype** | `delta` |
| **Applicable types** | Integer types |

ORC direct delta encoding. Stores a base value plus fixed-width deltas.
Used internally by PAX for offset columns.

```sql
CREATE TABLE t (seq INT ENCODING (compresstype=delta)) USING pax;
```

---

### Dictionary

| Property | Value |
|----------|-------|
| **compresstype** | `dict` |
| **Applicable types** | `text`, `varchar`, variable-length types |

ORC dictionary encoding. Builds a dictionary of unique values and replaces
each occurrence with its dictionary index. Effective when cardinality is low
(e.g., status columns with a few distinct values).

```sql
CREATE TABLE t (status TEXT ENCODING (compresstype=dict)) USING pax;
```

---

### None

| Property | Value |
|----------|-------|
| **compresstype** | `none` |
| **Applicable types** | All types |

No encoding. Data stored as raw values. This is the default when no
`ENCODING` clause is specified.

## Encoding Selection Guide

| Data pattern | Recommended | Reason |
|-------------|-------------|--------|
| Timestamps with regular intervals | `deltadelta` | dd ≈ 0, near-zero storage |
| Monotonic integer counters/IDs | `deltadelta` | Small constant deltas |
| Slowly-changing float metrics | `gorilla` | Adjacent XOR is sparse |
| Boolean flags | `bool` | 60 values per 64-bit word |
| Low-cardinality strings | `dict` | Dictionary index < full value |
| General text / variable-length | `zstd` | Best generic ratio |
| Write-heavy, latency-sensitive | `lz4` | Fastest compression speed |

## Compression Tuning for Time-Series Workloads

DeltaDelta and Gorilla encode each value relative to its predecessor
(delta-of-deltas and XOR respectively), so their compression ratio depends
heavily on whether adjacent rows within a tuple group have similar column
values. Two factors critically affect this: **write order** and **group size**.

### Sorted Write Order

Data must be sorted before writing (typically `ORDER BY tags_id, time`) to
ensure adjacent rows within each segment have similar column values. Without
sorting, MPP hash distribution scatters rows randomly, destroying the value
locality these encodings depend on.

```sql
-- Good: sorted write, adjacent rows have similar values
INSERT INTO encoded_table SELECT * FROM source ORDER BY tags_id, time;

-- Bad: unsorted write, adjacent values are random
INSERT INTO encoded_table SELECT * FROM source;
```

### Group Size

The GUC `pax.max_tuples_per_group` (default 16384) controls how many rows
are encoded together in each tuple group (stripe). Larger groups give the
encoder longer sequences to exploit patterns from, and amortize per-group
header overhead over more values. Generic compression (zstd, lz4, zlib)
is less sensitive to group size since it operates on raw byte streams.

```sql
-- Increase group size for better compression (more memory during write)
SET pax.max_tuples_per_group = 65536;
```

### Benchmark

**Data source:** TSBS (Time Series Benchmark Suite), `cpu` measurement table.

**Test setup:** 864,000 rows, 100 hosts, 12 columns (1 timestamp + 1 tags_id +
10 float8 metrics), 10s intervals over 24 hours, Cloudberry MPP cluster
(1 coordinator + 3 segments), default group size (16384).

#### Overall Table Size

| Storage | Size | vs Heap |
|---------|------|---------|
| Heap (row-store) | 102 MB | 1.0x |
| PAX (no encoding, sorted) | 76 MB | 1.4x |
| PAX + DeltaDelta/Gorilla (unsorted) | 19 MB | 5.4x |
| PAX + DeltaDelta/Gorilla (**sorted**) | **6.4 MB** | **15.9x** |

Sorted write triples the compression ratio (5.4x → 15.9x). DeltaDelta
benefits from constant-interval timestamps compressing to near-zero
delta-of-deltas, and Gorilla benefits from slowly-changing float metrics
producing sparse XOR values — both effects are maximized when rows within
each device are contiguous.

#### Group Size Impact (Sorted DeltaDelta + Gorilla, TSBS data)

| Group size | Size | vs Heap |
|-----------|------|---------|
| Heap | 102 MB | 1.0x |
| PAX (no encoding) | 76 MB | 1.3x |
| 1,024 | 7.0 MB | 14.6x |
| 4,096 | 6.2 MB | 16.6x |
| 16,384 (default) | 6.4 MB | **15.9x** |
| 65,536 | 6.1 MB | **16.7x** |

With real time-series data, group size from 1024 → 65536 improves
compression from 14.6x to 16.7x. The biggest jump is from 1024 → 4096;
beyond 4096 the returns diminish. The default 16384 is a good balance
between compression ratio and memory usage during encoding.

## References

- Vo Ngoc Anh, Alistair Moffat. "Index compression using 64-bit words."
  Software: Practice and Experience, 2010. *(Simple8b-RLE)*
- T. Pelkonen et al. "Gorilla: A Fast, Scalable, In-Memory Time Series
  Database." Proceedings of the VLDB Endowment, 2015. *(Gorilla XOR encoding)*
