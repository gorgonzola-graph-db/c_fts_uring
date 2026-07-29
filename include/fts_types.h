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

// A single posting representing an occurrence of a term in a node
typedef struct {
    uint8_t node_id[16]; // 128-bit UUID for graph DB integration
    uint32_t term_frequency;
    uint32_t document_length;
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
double compute_bm25_score(uint32_t tf, uint32_t doc_len, double avgdl, double idf, double k1, double b);
void process_shard_buffer(const char *buffer, size_t bytes_read, double idf, double avgdl, double k1, double b);

#endif // FTS_TYPES_H
