#ifndef LEXICON_H
#define LEXICON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define LEXICON_BUCKET_COUNT 65536  // 64K buckets
#define LEXICON_MAX_TERM_LEN 255

typedef struct LexiconEntry {
    char term[LEXICON_MAX_TERM_LEN + 1];
    uint32_t term_id;
    uint64_t document_frequency;  // Number of docs containing this term
    struct LexiconEntry *next;    // Chaining for hash collisions
} LexiconEntry;

typedef struct {
    LexiconEntry *buckets[LEXICON_BUCKET_COUNT];
    uint32_t next_term_id;
    uint32_t total_terms;
} Lexicon;

// Lifecycle
void lexicon_init(Lexicon *lex);
void lexicon_destroy(Lexicon *lex);

// Insert a term. Returns the term_id (existing or newly assigned).
// If the term already exists, increments document_frequency and returns existing id.
uint32_t lexicon_insert(Lexicon *lex, const char *term);

// Lookup a term. Returns true if found, setting *out_term_id and *out_doc_freq.
bool lexicon_lookup(const Lexicon *lex, const char *term, uint32_t *out_term_id, uint64_t *out_doc_freq);

// Get the document frequency for a term_id. Returns 0 if not found.
uint64_t lexicon_get_doc_freq(const Lexicon *lex, uint32_t term_id);

// Simple tokenizer: splits text on whitespace and punctuation, lowercases.
// Returns number of tokens written to out_tokens (up to max_tokens).
// Each token is a null-terminated string in out_buf, and out_tokens[i] points into out_buf.
int lexicon_tokenize(const char *text, char *out_buf, size_t buf_size, char **out_tokens, int max_tokens);

// Hash function (FNV-1a)
uint32_t lexicon_hash(const char *term);

#endif // LEXICON_H
