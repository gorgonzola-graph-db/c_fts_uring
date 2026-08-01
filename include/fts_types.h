#ifndef FTS_TYPES_H
#define FTS_TYPES_H

#include <stdint.h>
#include <stddef.h>

#define MAX_TERM_LEN 255

// Centralized lexicon entry tracking global frequencies
typedef struct {
    char term[MAX_TERM_LEN + 1];
    uint32_t term_id;
    uint64_t total_document_count; // Required for global IDF computation
    uint64_t total_corpus_docs;    // Total docs in the entire collection (N)
} GlobalLexiconEntry;

#ifndef MAX_FTS_FIELDS
#define MAX_FTS_FIELDS 4
#endif

// A single posting representing an occurrence of a term in a node
typedef struct {
    uint8_t node_id[16]; // 128-bit UUID for graph DB integration
    uint32_t term_frequencies[MAX_FTS_FIELDS];
    uint32_t document_lengths[MAX_FTS_FIELDS];
} Posting;

// Distributed shard file entry tracking localized occurrences
typedef struct {
    uint32_t term_id;
    uint32_t num_postings;
    Posting postings[]; 
} ShardPostingList;

// A structure to hold the aggregated score for a specific node
typedef struct {
    uint8_t node_id[16];
    double bm25_score;
} NodeScore;

// Function declarations
double compute_idf(uint64_t total_corpus_docs, uint64_t term_doc_freq);
double compute_bm25f_score(const uint32_t tf[MAX_FTS_FIELDS], const uint32_t doc_len[MAX_FTS_FIELDS], const double avgdl[MAX_FTS_FIELDS], const double weights[MAX_FTS_FIELDS], double idf, double k1, double b);
void process_shard_buffer(const char *buffer, size_t bytes_read, double idf, const double avgdl[MAX_FTS_FIELDS], const double weights[MAX_FTS_FIELDS], double k1, double b);

#endif // FTS_TYPES_H
