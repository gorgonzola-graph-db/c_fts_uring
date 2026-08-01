#include "inverted_index.h"
#include "slotted_page.h"
#include "buffer_pool.h"
#include "btree.h"
#include "fts_types.h"
#include "fts_indexer.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

int inverted_index_add_posting(UnifiedShard *shard,
                               uint32_t term_id,
                               uint32_t doc_id,
                               uint32_t data_page_id,
                               uint16_t data_slot_id) {
    uint32_t posting_page_id;
    uint16_t slot_id;
    bool found = btree_search(&shard->btree, (uint64_t)term_id, &posting_page_id, &slot_id);

    if (found) {
        Frame *f = buffer_pool_fetch_page(&shard->bpm, posting_page_id);
        if (!f) return -1;
        
        InvertedIndexPage *page = (InvertedIndexPage *)f->page_data;
        if (page->posting_count < MAX_POSTINGS_PER_PAGE) {
            page->postings[page->posting_count].doc_id = doc_id;
            page->postings[page->posting_count].data_page_id = data_page_id;
            page->postings[page->posting_count].data_slot_id = data_slot_id;
            page->posting_count++;
            buffer_pool_unpin_page(&shard->bpm, posting_page_id, true, HINT_NORMAL);
        } else {
            uint32_t new_page_id = shard->next_page_id++;
            char zero[4096] = {0};
            lseek(shard->data_fd, new_page_id * 4096, SEEK_SET);
            if (write(shard->data_fd, zero, 4096) < 0) {
                buffer_pool_unpin_page(&shard->bpm, posting_page_id, false, HINT_NORMAL);
                return -1;
            }
            
            Frame *f_new = buffer_pool_fetch_page(&shard->bpm, new_page_id);
            if (!f_new) {
                buffer_pool_unpin_page(&shard->bpm, posting_page_id, false, HINT_NORMAL);
                return -1;
            }
            
            InvertedIndexPage *new_page = (InvertedIndexPage *)f_new->page_data;
            new_page->term_id = term_id;
            new_page->posting_count = 1;
            new_page->next_posting_page = INVALID_PAGE_ID;
            new_page->postings[0].doc_id = doc_id;
            new_page->postings[0].data_page_id = data_page_id;
            new_page->postings[0].data_slot_id = data_slot_id;
            
            page->next_posting_page = new_page_id;
            
            // Unpin BOTH pages before btree_insert to free buffer pool frames
            buffer_pool_unpin_page(&shard->bpm, posting_page_id, true, HINT_NORMAL);
            buffer_pool_unpin_page(&shard->bpm, new_page_id, true, HINT_NORMAL);
            
            btree_insert(&shard->btree, (uint64_t)term_id, new_page_id, 0, 0);
        }
    } else {
        uint32_t new_page_id = shard->next_page_id++;
        char zero[4096] = {0};
        lseek(shard->data_fd, new_page_id * 4096, SEEK_SET);
        if (write(shard->data_fd, zero, 4096) < 0) return -1;
        
        Frame *f = buffer_pool_fetch_page(&shard->bpm, new_page_id);
        if (!f) return -1;
        
        InvertedIndexPage *new_page = (InvertedIndexPage *)f->page_data;
        new_page->term_id = term_id;
        new_page->posting_count = 1;
        new_page->next_posting_page = INVALID_PAGE_ID;
        new_page->postings[0].doc_id = doc_id;
        new_page->postings[0].data_page_id = data_page_id;
        new_page->postings[0].data_slot_id = data_slot_id;
        
        // Unpin BEFORE btree_insert to free buffer pool frames for the B-Tree
        buffer_pool_unpin_page(&shard->bpm, new_page_id, true, HINT_NORMAL);
        
        btree_insert(&shard->btree, (uint64_t)term_id, new_page_id, 0, 0);
    }

    return 0;
}

int inverted_index_get_postings(UnifiedShard *shard,
                                uint32_t term_id,
                                InvertedPosting *out_postings,
                                uint32_t max_postings) {
    uint32_t posting_page_id;
    uint16_t slot_id;
    bool found = btree_search(&shard->btree, (uint64_t)term_id, &posting_page_id, &slot_id);
    
    if (!found) return 0;
    
    uint32_t total_count = 0;
    uint32_t current_page_id = posting_page_id;
    
    while (current_page_id != INVALID_PAGE_ID && total_count < max_postings) {
        Frame *f = buffer_pool_fetch_page(&shard->bpm, current_page_id);
        if (!f) break;
        
        InvertedIndexPage *page = (InvertedIndexPage *)f->page_data;
        
        for (uint32_t i = 0; i < page->posting_count && total_count < max_postings; i++) {
            out_postings[total_count++] = page->postings[i];
        }
        
        uint32_t next_page = page->next_posting_page;
        buffer_pool_unpin_page(&shard->bpm, current_page_id, false, HINT_NORMAL);
        current_page_id = next_page;
    }
    
    return total_count;
}

int shard_search_bm25f_indexed(UnifiedShard *shard,
                                uint32_t term_id,
                                double idf,
                                const double avgdl[MAX_FTS_FIELDS],
                                const double weights[MAX_FTS_FIELDS],
                                double k1, double b,
                                UnifiedSearchResult *results,
                                uint32_t max_results) {
    uint32_t max_possible_postings = 1000000;
    InvertedPosting *postings = malloc(sizeof(InvertedPosting) * max_possible_postings);
    if (!postings) return 0;
    
    uint32_t num_postings = inverted_index_get_postings(shard, term_id, postings, max_possible_postings);
    uint32_t count = 0;
    
    for (uint32_t i = 0; i < num_postings; i++) {
        uint32_t data_page_id = postings[i].data_page_id;
        uint16_t data_slot_id = postings[i].data_slot_id;
        
        Frame *frame = buffer_pool_fetch_page(&shard->bpm, data_page_id);
        if (!frame) continue;
        
        uint16_t len;
        const char *tup = slotted_page_get_tuple(frame->page_data, data_slot_id, &len);
        if (!tup || len != sizeof(UnifiedDocRecord)) {
            buffer_pool_unpin_page(&shard->bpm, data_page_id, false, HINT_NORMAL);
            continue;
        }
        
        UnifiedDocRecord *rec = (UnifiedDocRecord*)tup;
        if (shard_is_deleted(shard, rec->doc_id)) {
            buffer_pool_unpin_page(&shard->bpm, data_page_id, false, HINT_NORMAL);
            continue;
        }
        
        double score = compute_bm25f_score(rec->term_frequencies, rec->document_lengths, avgdl, weights, idf, k1, b);
        if (score > 0) {
            if (count < max_results) {
                memcpy(results[count].node_id, rec->node_id, 16);
                results[count].bm25f_score = score;
                results[count].cosine_score = 0.0;
                results[count].rrf_score = 0.0;
                results[count].bm25f_rank = 0;
                results[count].cosine_rank = 0;
                count++;
            } else {
                uint32_t min_idx = 0;
                double min_score = results[0].bm25f_score;
                for (uint32_t r = 1; r < count; r++) {
                    if (results[r].bm25f_score < min_score) {
                        min_score = results[r].bm25f_score;
                        min_idx = r;
                    }
                }
                if (score > min_score) {
                    memcpy(results[min_idx].node_id, rec->node_id, 16);
                    results[min_idx].bm25f_score = score;
                    results[min_idx].cosine_score = 0.0;
                    results[min_idx].rrf_score = 0.0;
                    results[min_idx].bm25f_rank = 0;
                    results[min_idx].cosine_rank = 0;
                }
            }
        }
        
        buffer_pool_unpin_page(&shard->bpm, data_page_id, false, HINT_NORMAL);
    }
    
    free(postings);
    
    for (uint32_t i = 0; i < count; i++) {
        for (uint32_t j = i + 1; j < count; j++) {
            if (results[j].bm25f_score > results[i].bm25f_score) {
                UnifiedSearchResult tmp = results[i];
                results[i] = results[j];
                results[j] = tmp;
            }
        }
    }
    
    return count;
}
