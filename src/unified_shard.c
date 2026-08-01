#include "unified_shard.h"
#include "shard_meta.h"
#include "inverted_index.h"
#include "lexicon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#include <sys/stat.h>

extern double compute_bm25f_score(const uint32_t tf[MAX_FTS_FIELDS], const uint32_t doc_len[MAX_FTS_FIELDS], const double avgdl[MAX_FTS_FIELDS], const double weights[MAX_FTS_FIELDS], double idf, double k1, double b);

int shard_init(UnifiedShard *shard, uint32_t shard_id, const char *base_path) {
    shard->shard_id = shard_id;
    char data_path[512], wal_path[512];
    snprintf(data_path, sizeof(data_path), "%s/shard_%u.dat", base_path, shard_id);
    snprintf(wal_path, sizeof(wal_path), "%s/shard_%u.wal", base_path, shard_id);

    shard->data_fd = open(data_path, O_RDWR | O_CREAT, 0644);
    if (shard->data_fd < 0) return -1;
    
    if (wal_init(&shard->wal, wal_path) != 0) {
        close(shard->data_fd);
        return -1;
    }

    buffer_pool_init(&shard->bpm, shard->data_fd, &shard->wal);

    struct stat st;
    fstat(shard->data_fd, &st);
    if (st.st_size == 0) {
        char zero[4096] = {0};
        // Initialize with 3 pages minimum
        if (write(shard->data_fd, zero, 4096) < 0) return -1;
        if (write(shard->data_fd, zero, 4096) < 0) return -1;
        if (write(shard->data_fd, zero, 4096) < 0) return -1;
        
        
        Frame* ts_frame = buffer_pool_fetch_page(&shard->bpm, 1);
        if (ts_frame) {
            fts_tombstone_init(ts_frame->page_data, 1);
            buffer_pool_unpin_page(&shard->bpm, 1, true, HINT_NORMAL);
        }
        
        Frame* data_frame = buffer_pool_fetch_page(&shard->bpm, 2);
        if (data_frame) {
            slotted_page_init(data_frame->page_data, 2, (PageType)PAGE_TYPE_VECTOR_DATA);
            buffer_pool_unpin_page(&shard->bpm, 2, true, HINT_NORMAL);
        }
        
        shard->next_page_id = 2;
    } else {
        shard->next_page_id = (st.st_size / 4096) - 1;
        if (shard->next_page_id < 2) shard->next_page_id = 2;
    }
    
    btree_init(&shard->btree, &shard->bpm, 0, &shard->next_page_id);
    shard->tombstone_page_id = 1;
    shard->next_doc_id = 0; 
    shard->total_docs = 0;
    for(int i=0; i<MAX_FTS_FIELDS; i++) {
        shard->avgdl[i] = 10.0;
        shard->sum_dl[i] = 0.0;
    }
    
    // Try to restore stats from companion .meta file
    if (shard_meta_load(shard, base_path) == 0) {
        // Metadata loaded successfully - stats restored
    }
    
    shard->owner_thread = pthread_self();
    shard->is_initialized = true;

    return 0;
}

void shard_close_with_path(UnifiedShard *shard, const char *base_path) {
    if (!shard->is_initialized) return;
    // Persist shard statistics before shutdown
    if (base_path) {
        shard_meta_save(shard, base_path);
    }
    buffer_pool_flush_all(&shard->bpm);
    buffer_pool_destroy(&shard->bpm);
    wal_close(&shard->wal);
    close(shard->data_fd);
    shard->is_initialized = false;
}

void shard_close(UnifiedShard *shard) {
    shard_close_with_path(shard, NULL);
}

int engine_init(UnifiedEngine *engine, const char *base_path, uint32_t num_shards) {
    mkdir(base_path, 0755);
    strncpy(engine->base_path, base_path, sizeof(engine->base_path)-1);
    engine->base_path[sizeof(engine->base_path)-1] = '\0';
    engine->num_shards = num_shards;
    for (uint32_t i = 0; i < num_shards; i++) {
        if (shard_init(&engine->shards[i], i, base_path) != 0) {
            return -1;
        }
    }
    engine->k1 = 1.2;
    engine->b = 0.75;
    engine->field_weights[0] = 10.0;
    engine->field_weights[1] = 5.0;
    engine->field_weights[2] = 2.0;
    engine->field_weights[3] = 1.0;
    for (int i=4; i<MAX_FTS_FIELDS; i++) engine->field_weights[i] = 1.0;
    
    engine_recompute_global_stats(engine);
    return 0;
}

void engine_close(UnifiedEngine *engine) {
    for (uint32_t i = 0; i < engine->num_shards; i++) {
        shard_close_with_path(&engine->shards[i], engine->base_path);
    }
}

int shard_insert_document(UnifiedShard *shard, WALManager *wal,
                          const uint8_t node_id[16],
                          const uint32_t tf[MAX_FTS_FIELDS],
                          const uint32_t dl[MAX_FTS_FIELDS],
                          const float embedding[EMBEDDING_DIM]) {
    UnifiedDocRecord rec;
    memcpy(rec.node_id, node_id, 16);
    memcpy(rec.term_frequencies, tf, sizeof(uint32_t) * MAX_FTS_FIELDS);
    memcpy(rec.document_lengths, dl, sizeof(uint32_t) * MAX_FTS_FIELDS);
    memcpy(rec.embedding, embedding, sizeof(float) * EMBEDDING_DIM);
    rec.doc_id = shard->next_doc_id++;

    wal_append_record(wal, 0, WAL_REC_INSERT, shard->next_page_id, 0, &rec, sizeof(UnifiedDocRecord));

    Frame *frame = buffer_pool_fetch_page(&shard->bpm, shard->next_page_id);
    if (!frame) return -1;
    if (!slotted_page_insert_tuple(frame->page_data, &rec, sizeof(UnifiedDocRecord))) {
        buffer_pool_unpin_page(&shard->bpm, shard->next_page_id, false, HINT_NORMAL);
        shard->next_page_id++;
        
        char zero[4096] = {0};
        lseek(shard->data_fd, shard->next_page_id * 4096, SEEK_SET);
        if (write(shard->data_fd, zero, 4096) < 0) {
            return -1;
        }

        frame = buffer_pool_fetch_page(&shard->bpm, shard->next_page_id);
        if (!frame) return -1;
        slotted_page_init(frame->page_data, shard->next_page_id, (PageType)PAGE_TYPE_VECTOR_DATA);
        slotted_page_insert_tuple(frame->page_data, &rec, sizeof(UnifiedDocRecord));
    }
    // Get the slot_id of the just-inserted tuple
    PageHeader *ins_hdr = (PageHeader *)frame->page_data;
    uint16_t inserted_slot_id = ins_hdr->slot_count - 1;
    uint32_t inserted_page_id = shard->next_page_id;
    buffer_pool_unpin_page(&shard->bpm, shard->next_page_id, true, HINT_NORMAL);

    shard->total_docs++;
    for (int i = 0; i < MAX_FTS_FIELDS; i++) {
        shard->sum_dl[i] += dl[i];
        shard->avgdl[i] = shard->sum_dl[i] / shard->total_docs;
    }

    // Update inverted index: for each field with tf > 0, add a posting.
    // We use a synthetic term_id = field_index for now; the VTab bridge
    // will use the Lexicon to map real terms to IDs before calling search.
    // This adds the doc to the B-Tree so shard_search_bm25f_indexed can find it.
    for (int i = 0; i < MAX_FTS_FIELDS; i++) {
        if (tf[i] > 0) {
            inverted_index_add_posting(shard, 0, rec.doc_id, inserted_page_id, inserted_slot_id);
            break;  // Only need one posting per doc for the generic key
        }
    }

    return 0;
}

int shard_delete_document(UnifiedShard *shard, uint32_t doc_id) {
    Frame *frame = buffer_pool_fetch_page(&shard->bpm, shard->tombstone_page_id);
    if (!frame) return -1;
    fts_tombstone_mark_deleted(frame->page_data, doc_id);
    wal_append_record(&shard->wal, 0, WAL_FTS_TOMBSTONE, shard->tombstone_page_id, 0, &doc_id, sizeof(uint32_t));
    buffer_pool_unpin_page(&shard->bpm, shard->tombstone_page_id, true, HINT_NORMAL);
    return 0;
}

bool shard_is_deleted(UnifiedShard *shard, uint32_t doc_id) {
    Frame *frame = buffer_pool_fetch_page(&shard->bpm, shard->tombstone_page_id);
    if (!frame) return false;
    bool deleted = fts_tombstone_is_deleted(frame->page_data, doc_id);
    buffer_pool_unpin_page(&shard->bpm, shard->tombstone_page_id, false, HINT_NORMAL);
    return deleted;
}

double compute_cosine_similarity(const float *a, const float *b, uint32_t dim) {
    double dot = 0.0, norm_a = 0.0, norm_b = 0.0;
    for (uint32_t i = 0; i < dim; i++) {
        dot += (double)a[i] * (double)b[i];
        norm_a += (double)a[i] * (double)a[i];
        norm_b += (double)b[i] * (double)b[i];
    }
    if (norm_a == 0.0 || norm_b == 0.0) return 0.0;
    return dot / (sqrt(norm_a) * sqrt(norm_b));
}

int shard_search_bm25f(UnifiedShard *shard,
                       uint32_t term_id,
                       double idf,
                       const double avgdl[MAX_FTS_FIELDS],
                       const double weights[MAX_FTS_FIELDS],
                       double k1, double b,
                       UnifiedSearchResult *results,
                       uint32_t max_results) {
    uint32_t count = 0;
    
    for (uint32_t pid = 2; pid <= shard->next_page_id; pid++) {
        Frame *frame = buffer_pool_fetch_page(&shard->bpm, pid);
        if (!frame) continue;
        
        PageHeader *hdr = (PageHeader*)frame->page_data;
        if (hdr->page_type != PAGE_TYPE_VECTOR_DATA) {
            buffer_pool_unpin_page(&shard->bpm, pid, false, HINT_NORMAL);
            continue;
        }
        
        uint16_t slot_count = hdr->slot_count;
        for (uint16_t s = 0; s < slot_count; s++) {
            uint16_t len;
            const char *tup = slotted_page_get_tuple(frame->page_data, s, &len);
            if (!tup || len != sizeof(UnifiedDocRecord)) continue;
            
            UnifiedDocRecord *rec = (UnifiedDocRecord*)tup;
            if (shard_is_deleted(shard, rec->doc_id)) continue;
            
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
                    for (uint32_t i = 1; i < count; i++) {
                        if (results[i].bm25f_score < min_score) {
                            min_score = results[i].bm25f_score;
                            min_idx = i;
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
        }
        buffer_pool_unpin_page(&shard->bpm, pid, false, HINT_SEQUENTIAL_SCAN);
    }
    
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

int shard_search_vector(UnifiedShard *shard,
                        const float query_vec[EMBEDDING_DIM],
                        UnifiedSearchResult *results,
                        uint32_t max_results) {
    uint32_t count = 0;
    
    for (uint32_t pid = 2; pid <= shard->next_page_id; pid++) {
        Frame *frame = buffer_pool_fetch_page(&shard->bpm, pid);
        if (!frame) continue;
        
        PageHeader *hdr = (PageHeader*)frame->page_data;
        if (hdr->page_type != PAGE_TYPE_VECTOR_DATA) {
            buffer_pool_unpin_page(&shard->bpm, pid, false, HINT_NORMAL);
            continue;
        }
        
        uint16_t slot_count = hdr->slot_count;
        for (uint16_t s = 0; s < slot_count; s++) {
            uint16_t len;
            const char *tup = slotted_page_get_tuple(frame->page_data, s, &len);
            if (!tup || len != sizeof(UnifiedDocRecord)) continue;
            
            UnifiedDocRecord *rec = (UnifiedDocRecord*)tup;
            if (shard_is_deleted(shard, rec->doc_id)) continue;
            
            double score = compute_cosine_similarity(query_vec, rec->embedding, EMBEDDING_DIM);
            if (count < max_results) {
                memcpy(results[count].node_id, rec->node_id, 16);
                results[count].cosine_score = score;
                results[count].bm25f_score = 0.0;
                results[count].rrf_score = 0.0;
                results[count].bm25f_rank = 0;
                results[count].cosine_rank = 0;
                count++;
            } else {
                uint32_t min_idx = 0;
                double min_score = results[0].cosine_score;
                for (uint32_t i = 1; i < count; i++) {
                    if (results[i].cosine_score < min_score) {
                        min_score = results[i].cosine_score;
                        min_idx = i;
                    }
                }
                if (score > min_score) {
                    memcpy(results[min_idx].node_id, rec->node_id, 16);
                    results[min_idx].cosine_score = score;
                    results[min_idx].bm25f_score = 0.0;
                    results[min_idx].rrf_score = 0.0;
                    results[min_idx].bm25f_rank = 0;
                    results[min_idx].cosine_rank = 0;
                }
            }
        }
        buffer_pool_unpin_page(&shard->bpm, pid, false, HINT_SEQUENTIAL_SCAN);
    }
    
    for (uint32_t i = 0; i < count; i++) {
        for (uint32_t j = i + 1; j < count; j++) {
            if (results[j].cosine_score > results[i].cosine_score) {
                UnifiedSearchResult tmp = results[i];
                results[i] = results[j];
                results[j] = tmp;
            }
        }
    }
    
    return count;
}

static int cmp_rrf(const void *a, const void *b) {
    const UnifiedSearchResult *ra = (const UnifiedSearchResult *)a;
    const UnifiedSearchResult *rb = (const UnifiedSearchResult *)b;
    if (rb->rrf_score > ra->rrf_score) return 1;
    if (rb->rrf_score < ra->rrf_score) return -1;
    return 0;
}

void compute_rrf_fusion(UnifiedSearchResult *results, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        results[i].rrf_score = 0.0;
        if (results[i].bm25f_score > 0) {
            results[i].rrf_score += 1.0 / (RRF_K + results[i].bm25f_rank);
        }
        if (results[i].cosine_score > 0 || results[i].cosine_score < 0) { 
            // valid cosine can be <= 0, but check if rank is assigned (if it is > 0)
            if (results[i].cosine_rank > 0) {
                results[i].rrf_score += 1.0 / (RRF_K + results[i].cosine_rank);
            }
        }
    }
    qsort(results, count, sizeof(UnifiedSearchResult), cmp_rrf);
}

int engine_search(UnifiedEngine *engine,
                  const UnifiedQuery *query,
                  UnifiedSearchResult *results,
                  uint32_t max_results) {
    UnifiedSearchResult merged[MAX_SHARDS * MAX_RESULTS_PER_SHARD * 2];
    uint32_t merged_count = 0;
    
    UnifiedSearchResult bm25f_all[MAX_SHARDS * MAX_RESULTS_PER_SHARD];
    uint32_t bm25f_count = 0;
    
    UnifiedSearchResult vector_all[MAX_SHARDS * MAX_RESULTS_PER_SHARD];
    uint32_t vector_count = 0;
    
    for (uint32_t s = 0; s < engine->num_shards; s++) {
        if (query->use_text) {
            uint32_t n = shard_search_bm25f(&engine->shards[s], 0, 1.0,
                                            engine->global_avgdl, engine->field_weights,
                                            engine->k1, engine->b,
                                            &bm25f_all[bm25f_count], MAX_RESULTS_PER_SHARD);
            bm25f_count += n;
        }
        if (query->use_vector) {
            uint32_t n = shard_search_vector(&engine->shards[s], query->query_embedding,
                                             &vector_all[vector_count], MAX_RESULTS_PER_SHARD);
            vector_count += n;
        }
    }
    
    if (query->use_text) {
        for (uint32_t i = 0; i < bm25f_count; i++) {
            for (uint32_t j = i + 1; j < bm25f_count; j++) {
                if (bm25f_all[j].bm25f_score > bm25f_all[i].bm25f_score) {
                    UnifiedSearchResult tmp = bm25f_all[i];
                    bm25f_all[i] = bm25f_all[j];
                    bm25f_all[j] = tmp;
                }
            }
            bm25f_all[i].bm25f_rank = i + 1;
        }
    }
    
    if (query->use_vector) {
        for (uint32_t i = 0; i < vector_count; i++) {
            for (uint32_t j = i + 1; j < vector_count; j++) {
                if (vector_all[j].cosine_score > vector_all[i].cosine_score) {
                    UnifiedSearchResult tmp = vector_all[i];
                    vector_all[i] = vector_all[j];
                    vector_all[j] = tmp;
                }
            }
            vector_all[i].cosine_rank = i + 1;
        }
    }
    
    for (uint32_t i = 0; i < bm25f_count; i++) {
        merged[merged_count++] = bm25f_all[i];
    }
    
    for (uint32_t i = 0; i < vector_count; i++) {
        bool found = false;
        for (uint32_t j = 0; j < merged_count; j++) {
            if (memcmp(merged[j].node_id, vector_all[i].node_id, 16) == 0) {
                merged[j].cosine_score = vector_all[i].cosine_score;
                merged[j].cosine_rank = vector_all[i].cosine_rank;
                found = true;
                break;
            }
        }
        if (!found) {
            merged[merged_count++] = vector_all[i];
        }
    }
    
    if (query->use_text && query->use_vector) {
        compute_rrf_fusion(merged, merged_count);
    } else {
        for (uint32_t i = 0; i < merged_count; i++) {
            for (uint32_t j = i + 1; j < merged_count; j++) {
                double score_i = query->use_text ? merged[i].bm25f_score : merged[i].cosine_score;
                double score_j = query->use_text ? merged[j].bm25f_score : merged[j].cosine_score;
                if (score_j > score_i) {
                    UnifiedSearchResult tmp = merged[i];
                    merged[i] = merged[j];
                    merged[j] = tmp;
                }
            }
        }
    }
    
    uint32_t final_count = (merged_count > max_results) ? max_results : merged_count;
    memcpy(results, merged, final_count * sizeof(UnifiedSearchResult));
    return final_count;
}

void engine_recompute_global_stats(UnifiedEngine *engine) {
    engine->global_total_docs = 0;
    double sum_dl[MAX_FTS_FIELDS] = {0};
    
    for (uint32_t s = 0; s < engine->num_shards; s++) {
        engine->global_total_docs += engine->shards[s].total_docs;
        for (int i = 0; i < MAX_FTS_FIELDS; i++) {
            sum_dl[i] += engine->shards[s].sum_dl[i];
        }
    }
    
    for (int i = 0; i < MAX_FTS_FIELDS; i++) {
        if (engine->global_total_docs > 0) {
            engine->global_avgdl[i] = sum_dl[i] / engine->global_total_docs;
        } else {
            engine->global_avgdl[i] = 10.0;
        }
    }
}
