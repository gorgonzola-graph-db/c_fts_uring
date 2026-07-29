#include "fts_types.h"
#include <math.h>

// Calculates standard Okapi BM25 Inverse Document Frequency (IDF)
double compute_idf(uint64_t total_corpus_docs, uint64_t term_doc_freq) {
    if (term_doc_freq == 0 || total_corpus_docs == 0) {
        return 0.0;
    }
    double n = (double)term_doc_freq;
    double N = (double)total_corpus_docs;
    // Standard BM25 IDF with floor smoothing
    double idf = log((N - n + 0.5) / (n + 0.5) + 1.0);
    return idf < 0.0 ? 0.0 : idf;
}
