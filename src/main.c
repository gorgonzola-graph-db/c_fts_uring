#include "../include/fts_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

extern int fts_init_uv();
extern void fts_cleanup_uv();
extern void scatter_query_to_shards_uv(int *shard_fds, int num_shards, char **buffers, size_t read_size, double idf, double avgdl, double k1, double b);
extern void gather_shard_results_uv();

#define NUM_SHARDS 4
#define BUFFER_SIZE 4096

int main() {
    printf("===================================================================\n");
    printf("Initializing Custom C-Based Async FTS Engine PoC (libuv + BM25)\n");
    printf("===================================================================\n");
    
    if (fts_init_uv() < 0) {
        return EXIT_FAILURE;
    }

    // 1. Calculate Global IDF from Global Lexicon Parameters
    uint64_t total_corpus_docs = 10000;
    uint64_t term_doc_freq = 250;
    double idf = compute_idf(total_corpus_docs, term_doc_freq);
    double avgdl = 120.0;
    double k1 = 1.2;
    double b = 0.75;

    printf("Global Lexicon Lookup:\n");
    printf("  Total Corpus Docs (N): %lu\n", total_corpus_docs);
    printf("  Term Doc Frequency (n_q): %lu\n", term_doc_freq);
    printf("  Computed Global IDF: %.4f\n\n", idf);

    // 2. Prepare Mock Binary Posting Data in Shard Files
    int shard_fds[NUM_SHARDS];
    char *buffers[NUM_SHARDS];
    
    for (int i = 0; i < NUM_SHARDS; i++) {
        char filename[32];
        snprintf(filename, sizeof(filename), "mock_shard_%d.dat", i);
        
        int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd < 0) {
            perror("Failed to create mock shard");
            return EXIT_FAILURE;
        }

        // Construct mock binary posting list with 2 postings per shard
        uint32_t num_postings = 2;
        size_t payload_size = sizeof(uint32_t) * 2 + num_postings * sizeof(Posting);
        char *binary_payload = malloc(payload_size);

        ShardPostingList *plist = (ShardPostingList *)binary_payload;
        plist->term_id = 42;
        plist->num_postings = num_postings;

        for (uint32_t p_idx = 0; p_idx < num_postings; p_idx++) {
            Posting *p = &plist->postings[p_idx];
            memset(p->node_id, 0, 16);
            p->node_id[0] = (uint8_t)(i + 1);
            p->node_id[1] = (uint8_t)(p_idx + 1);
            p->term_frequency = (p_idx + 1) * 2 + i;
            p->document_length = 100 + (p_idx * 30) - (i * 5);
        }

        write(fd, binary_payload, payload_size);
        free(binary_payload);
        close(fd);
        
        // Open for reading by scatter query engine
        shard_fds[i] = open(filename, O_RDONLY);
        if (shard_fds[i] < 0) {
            perror("Failed to open mock shard for reading");
            return EXIT_FAILURE;
        }
        
        buffers[i] = malloc(BUFFER_SIZE);
        memset(buffers[i], 0, BUFFER_SIZE);
    }
    
    // 3. Scatter-Gather Query Execution
    printf("Scattering query (Term ID: 42) across %d shards asynchronously...\n", NUM_SHARDS);
    scatter_query_to_shards_uv(shard_fds, NUM_SHARDS, buffers, BUFFER_SIZE, idf, avgdl, k1, b);
    
    printf("Gathering results via libuv Event Loop callbacks...\n");
    gather_shard_results_uv();
    
    // 4. Cleanup
    for (int i = 0; i < NUM_SHARDS; i++) {
        close(shard_fds[i]);
        free(buffers[i]);
        char filename[32];
        snprintf(filename, sizeof(filename), "mock_shard_%d.dat", i);
        unlink(filename);
    }
    
    fts_cleanup_uv();
    printf("\nPoC BM25 scatter-gather execution completed successfully.\n");
    return EXIT_SUCCESS;
}
