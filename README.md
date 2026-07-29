# Gorgonzola Database Engine

This repository contains the core C-based storage engine modules for the Gorgonzola Database Engine, an ACID-compliant single-node and multi-shard storage engine written in C using `libuv`.

## Subsystem Architecture & Features

### 1. Storage Subsystem & Slotted Pages (`slotted_page.[ch]`)
- **Fixed Page Size**: 4KB pages with 24-byte page headers containing LSNs, CRC32 checksums, slot counts, free space pointers, and page types (`PAGE_TYPE_ROW`, `PAGE_TYPE_BTREE_LEAF`, `PAGE_TYPE_BTREE_INTERNAL`, `PAGE_TYPE_LENGTH_MAP`, `PAGE_TYPE_TOMBSTONE_BITSET`).
- **Slot Array & Defragmentation**: Dynamic tuple insertion, lookup, and deletion with slotted page defragmentation.

### 2. Buffer Pool Manager & Eviction Policy (`buffer_pool.[ch]`)
- **Clock Eviction Algorithm**: 8-frame page cache managing disk page fetching, dirty bit tracking, frame pinning, and page flushing.
- **Eviction Hints**: Support for `HINT_SEQUENTIAL_SCAN` to prevent cache thrashing during long scans.

### 3. Write-Ahead Logging & Recovery (`wal.[ch]`)
- **WAL Journaling**: Append-only log manager recording micro-transaction operations before dirty frames are written to disk.
- **Crash Recovery**: Sequential WAL log record replay engine reconstructing database state.

### 4. POSIX Shared Memory (`shm_manager.[ch]`)
- **Zero-Copy IPC**: Shared memory allocations (`shm_open`) exposing buffer pool memory regions across process boundaries.

### 5. Primary Key B-Tree Indexing (`btree.[ch]`)
- **Primary Key Storage**: Multi-level B-Tree indexing mapping primary keys to page and slot locations.
- **Node Splitting & Logging**: Automatic leaf splitting on page overflow with WAL transaction integration.

### 6. Full-Text Search (FTS) Indexer & Scoring (`fts_indexer.[ch]`, `lexicon.c`, `shard_worker.c`)
- **Quantized Length Maps**: $O(1)$ logarithmic length quantization mapping uint32 field lengths to uint8 bytes for fast BM25 document length evaluation without parsing slotted page tuples.
- **Tombstone Bitsets**: Bitset-backed document deletion tracking providing $O(1)$ lookup for deleted document filtering.
- **Scatter-Gather Engine**: Asynchronous multi-shard BM25 score evaluation driven by `libuv` event loops.

### 7. Tabular Query Engine & Shard Pruning (`catalog.c`, `tabular_worker.c`)
- **Catalog & Range Statistics**: Per-shard min/max key range metadata used by the query planner to prune non-matching shards before executing I/O.
- **Split-Apply-Combine Aggregates**: Parallel scatter-gather aggregate execution for SQL queries (`COUNT`, `SUM`).

---

## Build & Test

### Dependencies
- `libuv` (`libuv1-dev` on Debian/Ubuntu, `libuv-devel` on Fedora, `brew install libuv` on macOS)

### Compilation
```bash
make clean
make
```

### Running Test Harnesses
```bash
./c_fts_poc        # Async libuv BM25 scatter-gather FTS demo
./c_tabular_poc    # Tabular query planner & shard pruning demo
./c_wal_bp_poc     # WAL logging & buffer pool recovery benchmark
./c_phase2_3_poc   # B-Tree indexing & FTS length map / tombstone bitset verification
```
