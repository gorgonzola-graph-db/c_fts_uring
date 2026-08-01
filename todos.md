# c_fts_uring Roadmap & TODOs

This document tracks upcoming features and enhancements for the `c_fts_uring` C-engine. As we are shifting focus away from extreme-scale monorepo infrastructure, these goals prioritize engine intelligence, precision, flexibility, and cross-platform compatibility.

## 1. Hybrid Search (SIMD-Accelerated BM25F + Vector Fusion)
- **Goal**: Return a single, perfectly ranked list of code chunks combining text and semantic meaning.
- **Tasks**:
  - Implement SIMD (AVX2/NEON) cosine similarity calculations for the existing `embedding[EMBEDDING_DIM]` arrays.
  - Implement Reciprocal Rank Fusion (RRF) directly in C during the search phase to merge BM25F scores with Vector Similarity scores on the fly.

## 2. Advanced Query Syntax & Positional Indexing
- **Goal**: Support exact phrase matching and proximity queries (e.g., `"exact phrase"`, `term NEAR/3 term2`) to improve AI context precision.
- **Tasks**:
  - Upgrade the inverted index structure to store exact term positions within documents, not just frequencies.
  - Modify the `vtab_bridge` query parser to interpret phrase quotes and proximity operators, scoring documents based on term adjacency.

## 3. Dynamic Memory Sizing (Configurable Buffer Pools)
- **Goal**: Make the engine adaptable to different hardware constraints without hardcoding memory limits.
- **Tasks**:
  - Remove the hardcoded `BUFFER_POOL_SIZE` macro.
  - Modify `ftsConnect` to accept a `max_ram_mb` parameter in the SQLite `CREATE VIRTUAL TABLE` instantiation string (e.g., `USING fts_uring(path, max_ram_mb=512)`).
  - Dynamically calculate the number of buffer pool frames based on the requested memory limit at initialization.

## 4. Cross-Platform Compatibility (macOS / Windows Fallbacks)
- **Goal**: Allow developers to compile and run the engine natively on macOS and Windows, which do not support Linux's `io_uring`.
- **Tasks**:
  - Abstract the disk I/O layer currently tied strictly to `io_uring`.
  - Implement a fallback asynchronous I/O backend using `kqueue` (macOS) or POSIX AIO (Windows/macOS) guarded by compiler `#ifdef` directives.
