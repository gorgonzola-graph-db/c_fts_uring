#ifndef UNIFIED_SHARD_H
#define UNIFIED_SHARD_H

#include "slotted_page.h"
#include "buffer_pool.h"
#include "wal.h"
#include "btree.h"
#include "fts_indexer.h"
#include "fts_types.h"
#include <pthread.h>

#define MAX_SHARDS 16
#define EMBEDDING_DIM 384
#define MAX_RESULTS_PER_SHARD 64
#define RRF_K 60  // Reciprocal Rank Fusion constant

// Add new page types for vector storage
// PAGE_TYPE_VECTOR_DATA = 7  (add to PageType enum via separate define)
#define PAGE_TYPE_VECTOR_DATA 7

// A unified document record stored in slotted pages.
// This is the tuple that gets inserted into pages.
// It colocates text metadata and vector embeddings in the same cache line.
#pragma pack(push, 1)
typedef struct {
    uint8_t  node_id[16];                        // 128-bit UUID
    uint32_t term_frequencies[MAX_FTS_FIELDS];   // TF per field: name, kind, summary, body
    uint32_t document_lengths[MAX_FTS_FIELDS];   // Doc length per field
    float    embedding[EMBEDDING_DIM];           // Colocated vector embedding (384 floats = 1536 bytes)
    uint32_t doc_id;                             // Local document ID within this shard
} UnifiedDocRecord;
#pragma pack(pop)

// Result from a single shard's search
typedef struct {
    uint8_t  node_id[16];
    double   bm25f_score;
    double   cosine_score;
    double   rrf_score;      // Fused score
    uint32_t bm25f_rank;     // Rank in BM25F ordering
    uint32_t cosine_rank;    // Rank in cosine ordering
} UnifiedSearchResult;

// A single shard: owns its own WAL, BufferPool, B-Tree, and data file.
// Each shard is accessed by exactly ONE writer thread (shared-nothing).
typedef struct {
    uint32_t          shard_id;
    int               data_fd;        // File descriptor for shard data file
    WALManager        wal;
    BufferPoolManager bpm;
    BTree             btree;
    
    // Shard-local statistics for BM25F
    uint32_t          total_docs;
    double            avgdl[MAX_FTS_FIELDS];    // Per-field average document length
    double            sum_dl[MAX_FTS_FIELDS];   // Running sum for incremental avgdl
    
    // Page allocation
    uint32_t          next_page_id;
    uint32_t          next_doc_id;
    
    // Tombstone tracking
    uint32_t          tombstone_page_id;  // Page ID of the tombstone bitset page
    
    // Thread ownership
    pthread_t         owner_thread;
    bool              is_initialized;
} UnifiedShard;

// The top-level engine managing all shards
typedef struct {
    UnifiedShard shards[MAX_SHARDS];
    uint32_t     num_shards;
    char         base_path[256];      // Directory for shard files
    
    // Global corpus statistics (aggregated from all shards)
    uint64_t     global_total_docs;
    double       global_avgdl[MAX_FTS_FIELDS];
    
    // BM25F tuning parameters
    double       field_weights[MAX_FTS_FIELDS];  // e.g., {10.0, 5.0, 2.0, 1.0}
    double       k1;
    double       b;
} UnifiedEngine;

// Query parameters for a unified search
typedef struct {
    const char *search_term;               // Text query
    float       query_embedding[EMBEDDING_DIM]; // Query vector
    bool        use_text;                  // Enable BM25F
    bool        use_vector;                // Enable cosine similarity
    uint32_t    top_k;                     // Number of results
} UnifiedQuery;

// ==================== Function Prototypes ====================

// Engine lifecycle
int  engine_init(UnifiedEngine *engine, const char *base_path, uint32_t num_shards, uint32_t max_ram_mb);
void engine_close(UnifiedEngine *engine);

// Shard lifecycle (called internally by engine_init)
int  shard_init(UnifiedShard *shard, uint32_t shard_id, const char *base_path, uint32_t pool_size);
void shard_close(UnifiedShard *shard);
void shard_close_with_path(UnifiedShard *shard, const char *base_path);

// Document ingestion (lock-free: each thread writes to its own shard)
int shard_insert_document(UnifiedShard *shard, WALManager *wal,
                          const uint8_t node_id[16],
                          const uint32_t tf[MAX_FTS_FIELDS],
                          const uint32_t dl[MAX_FTS_FIELDS],
                          const float embedding[EMBEDDING_DIM],
                          const uint32_t *term_ids,
                          uint32_t num_terms);

// MVCC: Mark a document as deleted (atomic bit flip, no locks)
int shard_delete_document(UnifiedShard *shard, uint32_t doc_id);
bool shard_is_deleted(UnifiedShard *shard, uint32_t doc_id);

// Search within a single shard
int shard_search_bm25f(UnifiedShard *shard,
                       uint32_t term_id,
                       double idf,
                       const double avgdl[MAX_FTS_FIELDS],
                       const double weights[MAX_FTS_FIELDS],
                       double k1, double b,
                       UnifiedSearchResult *results,
                       uint32_t max_results);

int shard_search_vector(UnifiedShard *shard,
                        const float query_vec[EMBEDDING_DIM],
                        UnifiedSearchResult *results,
                        uint32_t max_results);

// Scatter-gather: query all shards and fuse results
int engine_search(UnifiedEngine *engine,
                  const UnifiedQuery *query,
                  UnifiedSearchResult *results,
                  uint32_t max_results);

// Math utilities
double compute_cosine_similarity(const float *a, const float *b, uint32_t dim);
void   compute_rrf_fusion(UnifiedSearchResult *results, uint32_t count);

// Statistics
void engine_recompute_global_stats(UnifiedEngine *engine);

int engine_insert_document(UnifiedEngine *engine,
                           const uint8_t node_id[16],
                           const uint32_t tf[MAX_FTS_FIELDS],
                           const uint32_t dl[MAX_FTS_FIELDS],
                           const float embedding[EMBEDDING_DIM],
                           const uint32_t *term_ids,
                           uint32_t num_terms);

#endif // UNIFIED_SHARD_H
