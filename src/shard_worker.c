#include "fts_types.h"
#include <stdio.h>
#include <string.h>

// Calculates single document score for a term given TF array, doc length array, avgdl array, field weights, and IDF
double compute_bm25f_score(const uint32_t tf[MAX_FTS_FIELDS], const uint32_t doc_len[MAX_FTS_FIELDS], const double avgdl[MAX_FTS_FIELDS], const double weights[MAX_FTS_FIELDS], double idf, double k1, double b) {
    double f = 0.0;
    
    // Calculate normalized term frequency across all fields
    for (int i = 0; i < MAX_FTS_FIELDS; i++) {
        if (weights[i] > 0.0 && avgdl[i] > 0.0 && tf[i] > 0) {
            double len_norm = 1.0 - b + b * ((double)doc_len[i] / avgdl[i]);
            f += weights[i] * ((double)tf[i] / len_norm);
        }
    }
    
    if (f == 0.0) {
        return 0.0;
    }
    
    double numerator = f * (k1 + 1.0);
    double denominator = f + k1;
    return idf * (numerator / denominator);
}

// Deserializes buffer into ShardPostingList and computes BM25F scores for each posting
void process_shard_buffer(const char *buffer, size_t bytes_read, double idf, const double avgdl[MAX_FTS_FIELDS], const double weights[MAX_FTS_FIELDS], double k1, double b) {
    if (bytes_read < sizeof(uint32_t) * 2) {
        return;
    }

    const ShardPostingList *plist = (const ShardPostingList *)buffer;
    size_t expected_size = sizeof(uint32_t) * 2 + plist->num_postings * sizeof(Posting);
    if (bytes_read < expected_size) {
        // Partial read handling in production engine
    }

    for (uint32_t i = 0; i < plist->num_postings; i++) {
        const Posting *p = &plist->postings[i];
        double score = compute_bm25f_score(p->term_frequencies, p->document_lengths, avgdl, weights, idf, k1, b);

        // Print Node UUID byte representation and computed score for PoC demonstration
        printf("    [Node UUID ");
        for (int b_idx = 0; b_idx < 16; b_idx++) {
            printf("%02x", p->node_id[b_idx]);
        }
        printf("] BM25F Score: %.4f (TF: [", score);
        for (int f_idx = 0; f_idx < MAX_FTS_FIELDS; f_idx++) {
            printf("%u%s", p->term_frequencies[f_idx], f_idx == MAX_FTS_FIELDS - 1 ? "" : ", ");
        }
        printf("])\n");
    }
}
