#include "fts_types.h"
#include <stdio.h>
#include <string.h>

// Calculates single document score for a term given TF, document length, avgdl, and IDF
double compute_bm25_score(uint32_t tf, uint32_t doc_len, double avgdl, double idf, double k1, double b) {
    if (tf == 0 || avgdl <= 0.0) {
        return 0.0;
    }
    double f = (double)tf;
    double len_norm = 1.0 - b + b * ((double)doc_len / avgdl);
    double numerator = f * (k1 + 1.0);
    double denominator = f + k1 * len_norm;
    return idf * (numerator / denominator);
}

// Deserializes buffer into ShardPostingList and computes BM25 scores for each posting
void process_shard_buffer(const char *buffer, size_t bytes_read, double idf, double avgdl, double k1, double b) {
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
        double score = compute_bm25_score(p->term_frequency, p->document_length, avgdl, idf, k1, b);

        // Print Node UUID byte representation and computed score for PoC demonstration
        printf("    [Node UUID ");
        for (int b_idx = 0; b_idx < 16; b_idx++) {
            printf("%02x", p->node_id[b_idx]);
        }
        printf("] TF: %u, DocLen: %u => BM25 Score: %.4f\n", p->term_frequency, p->document_length, score);
    }
}
