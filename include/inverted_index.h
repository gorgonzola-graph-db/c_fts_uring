#ifndef INVERTED_INDEX_H
#define INVERTED_INDEX_H

#include "unified_shard.h"
#include <stdint.h>
#include <stdbool.h>

#define MAX_POSTINGS_PER_PAGE 128

// A posting entry stored in inverted index pages
// Maps (term_id) -> list of (doc_id, page_id, slot_id) tuples
#pragma pack(push, 1)
typedef struct {
    uint32_t doc_id;
    uint32_t data_page_id;   // Page where the UnifiedDocRecord lives
    uint16_t data_slot_id;   // Slot within that page
} InvertedPosting;
#pragma pack(pop)

// An inverted index page stores postings for a single term_id
#pragma pack(push, 1)
typedef struct {
    uint32_t term_id;
    uint32_t posting_count;
    uint32_t next_posting_page;  // Overflow chain (INVALID_PAGE_ID if none)
    InvertedPosting postings[MAX_POSTINGS_PER_PAGE];
} InvertedIndexPage;
#pragma pack(pop)

// Add a posting to the inverted index for a given term.
// Uses the shard's B-Tree to map term_id -> posting_page_id.
// If no page exists for this term, allocates a new one.
int inverted_index_add_posting(UnifiedShard *shard,
                               uint32_t term_id,
                               uint32_t doc_id,
                               uint32_t data_page_id,
                               uint16_t data_slot_id);

// Retrieve all postings for a given term_id.
// Returns the number of postings found.
// Postings are written to out_postings (up to max_postings).
int inverted_index_get_postings(UnifiedShard *shard,
                                uint32_t term_id,
                                InvertedPosting *out_postings,
                                uint32_t max_postings);

// Search using the inverted index instead of sequential scan.
// This is the fast replacement for shard_search_bm25f.
int shard_search_bm25f_indexed(UnifiedShard *shard,
                                uint32_t term_id,
                                double idf,
                                const double avgdl[MAX_FTS_FIELDS],
                                const double weights[MAX_FTS_FIELDS],
                                double k1, double b,
                                UnifiedSearchResult *results,
                                uint32_t max_results);

#endif // INVERTED_INDEX_H
