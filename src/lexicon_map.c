#include "lexicon.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// FNV-1a constants
#define FNV_PRIME 16777619
#define FNV_OFFSET_BASIS 2166136261U

uint32_t lexicon_hash(const char *term) {
    uint32_t hash = FNV_OFFSET_BASIS;
    while (*term) {
        hash ^= (unsigned char)*term;
        hash *= FNV_PRIME;
        term++;
    }
    return hash % LEXICON_BUCKET_COUNT;
}

void lexicon_init(Lexicon *lex) {
    if (!lex) return;
    for (int i = 0; i < LEXICON_BUCKET_COUNT; i++) {
        lex->buckets[i] = NULL;
    }
    lex->next_term_id = 1; // 0 is reserved
    lex->total_terms = 0;
}

void lexicon_destroy(Lexicon *lex) {
    if (!lex) return;
    for (int i = 0; i < LEXICON_BUCKET_COUNT; i++) {
        LexiconEntry *entry = lex->buckets[i];
        while (entry != NULL) {
            LexiconEntry *next = entry->next;
            free(entry);
            entry = next;
        }
        lex->buckets[i] = NULL;
    }
    lex->next_term_id = 1;
    lex->total_terms = 0;
}

uint32_t lexicon_insert(Lexicon *lex, const char *term) {
    if (!lex || !term) return 0;
    
    uint32_t bucket = lexicon_hash(term);
    LexiconEntry *entry = lex->buckets[bucket];
    
    // Check if term already exists
    while (entry != NULL) {
        if (strcmp(entry->term, term) == 0) {
            entry->document_frequency++;
            return entry->term_id;
        }
        entry = entry->next;
    }
    
    // Create new entry
    LexiconEntry *new_entry = (LexiconEntry *)malloc(sizeof(LexiconEntry));
    if (!new_entry) return 0; // Handle malloc failure gracefully if needed
    
    strncpy(new_entry->term, term, LEXICON_MAX_TERM_LEN);
    new_entry->term[LEXICON_MAX_TERM_LEN] = '\0';
    new_entry->term_id = lex->next_term_id++;
    new_entry->document_frequency = 1;
    new_entry->next = lex->buckets[bucket];
    
    lex->buckets[bucket] = new_entry;
    lex->total_terms++;
    
    return new_entry->term_id;
}

bool lexicon_lookup(const Lexicon *lex, const char *term, uint32_t *out_term_id, uint64_t *out_doc_freq) {
    if (!lex || !term) return false;
    
    uint32_t bucket = lexicon_hash(term);
    LexiconEntry *entry = lex->buckets[bucket];
    
    while (entry != NULL) {
        if (strcmp(entry->term, term) == 0) {
            if (out_term_id) *out_term_id = entry->term_id;
            if (out_doc_freq) *out_doc_freq = entry->document_frequency;
            return true;
        }
        entry = entry->next;
    }
    return false;
}

uint64_t lexicon_get_doc_freq(const Lexicon *lex, uint32_t term_id) {
    if (!lex || term_id == 0) return 0;
    
    for (int i = 0; i < LEXICON_BUCKET_COUNT; i++) {
        LexiconEntry *entry = lex->buckets[i];
        while (entry != NULL) {
            if (entry->term_id == term_id) {
                return entry->document_frequency;
            }
            entry = entry->next;
        }
    }
    return 0;
}

int lexicon_tokenize(const char *text, char *out_buf, size_t buf_size, char **out_tokens, int max_tokens) {
    if (!text || !out_buf || buf_size == 0 || !out_tokens || max_tokens <= 0) return 0;
    
    size_t in_len = strlen(text);
    size_t copy_len = (in_len < buf_size - 1) ? in_len : (buf_size - 1);
    
    for (size_t i = 0; i < copy_len; i++) {
        if (isalnum((unsigned char)text[i])) {
            out_buf[i] = (char)tolower((unsigned char)text[i]);
        } else {
            out_buf[i] = '\0';
        }
    }
    out_buf[copy_len] = '\0'; // Ensure null-termination
    
    int token_count = 0;
    bool in_token = false;
    
    for (size_t i = 0; i <= copy_len; i++) {
        if (out_buf[i] != '\0') {
            if (!in_token) {
                if (token_count >= max_tokens) break;
                out_tokens[token_count++] = &out_buf[i];
                in_token = true;
            }
        } else {
            in_token = false;
        }
    }
    
    return token_count;
}
