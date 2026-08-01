#include "db_engine_ext.h"
SQLITE_EXTENSION_INIT1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/unified_shard.h"
#include "../include/lexicon.h"
#include "../include/inverted_index.h"
#include "../include/async_search.h"
#include "../include/shard_meta.h"

// Global lexicon shared across the engine lifetime
static Lexicon g_lexicon;
static bool g_lexicon_initialized = false;

typedef struct {
    sqlite3_vtab base;
    UnifiedEngine *engine;
} fts_uring_vtab;

typedef struct {
    sqlite3_vtab_cursor base;
    UnifiedSearchResult results[MAX_RESULTS_PER_SHARD * MAX_SHARDS];
    int total_results;
    int current_row;
} fts_uring_cursor;

static int ftsConnect(sqlite3 *db, void *pAux, int argc, const char *const*argv, sqlite3_vtab **ppVtab, char **pzErr) {
    int rc = sqlite3_declare_vtab(db, "CREATE TABLE x(node_id TEXT, bm25f_score REAL, cosine_score REAL, rrf_score REAL, query HIDDEN)");
    if( rc==SQLITE_OK ){
        fts_uring_vtab *pNew = sqlite3_malloc( sizeof(*pNew) );
        if (!pNew) return SQLITE_NOMEM;
        memset(pNew, 0, sizeof(*pNew));
        
        pNew->engine = sqlite3_malloc( sizeof(UnifiedEngine) );
        if (!pNew->engine) {
            sqlite3_free(pNew);
            return SQLITE_NOMEM;
        }
        
        engine_init(pNew->engine, "./fts_uring_data", 4);
        
        // Initialize global lexicon once
        if (!g_lexicon_initialized) {
            lexicon_init(&g_lexicon);
            g_lexicon_initialized = true;
        }
        
        // Initialize async search subsystem
        async_search_init();
        
        *ppVtab = (sqlite3_vtab*)pNew;
    }
    return rc;
}

static int ftsDisconnect(sqlite3_vtab *pVtab) {
    fts_uring_vtab *p = (fts_uring_vtab*)pVtab;
    if( p ){
        if( p->engine ){
            engine_close(p->engine);
            sqlite3_free(p->engine);
        }
        sqlite3_free(p);
    }
    async_search_shutdown();
    if (g_lexicon_initialized) {
        lexicon_destroy(&g_lexicon);
        g_lexicon_initialized = false;
    }
    return SQLITE_OK;
}

static int ftsOpen(sqlite3_vtab *pVTab, sqlite3_vtab_cursor **ppCursor) {
    fts_uring_cursor *pCur = sqlite3_malloc( sizeof(*pCur) );
    if (!pCur) return SQLITE_NOMEM;
    memset(pCur, 0, sizeof(*pCur));
    *ppCursor = (sqlite3_vtab_cursor*)pCur;
    return SQLITE_OK;
}

static int ftsClose(sqlite3_vtab_cursor *cur) {
    sqlite3_free(cur);
    return SQLITE_OK;
}

static int ftsBestIndex(sqlite3_vtab *tab, sqlite3_index_info *pIdxInfo) {
    for(int i = 0; i < pIdxInfo->nConstraint; i++){
        if( pIdxInfo->aConstraint[i].iColumn == 4 && pIdxInfo->aConstraint[i].op == SQLITE_INDEX_CONSTRAINT_EQ ){
            pIdxInfo->aConstraintUsage[i].argvIndex = 1;
            pIdxInfo->aConstraintUsage[i].omit = 1;
            pIdxInfo->estimatedCost = 10.0;
            return SQLITE_OK;
        }
    }
    return SQLITE_CONSTRAINT;
}

static int ftsFilter(sqlite3_vtab_cursor *pVtabCursor, int idxNum, const char *idxStr, int argc, sqlite3_value **argv) {
    fts_uring_cursor *pCur = (fts_uring_cursor *)pVtabCursor;
    fts_uring_vtab *pVtab = (fts_uring_vtab *)pVtabCursor->pVtab;
    
    if( argc > 0 ){
        const char *search_term = (const char*)sqlite3_value_text(argv[0]);
        
        // Tokenize the query using the lexicon tokenizer
        char token_buf[4096];
        char *tokens[64];
        int num_tokens = lexicon_tokenize(search_term, token_buf, sizeof(token_buf), tokens, 64);
        
        // Insert/lookup each token in the lexicon to get term_ids
        // (For now we use lexicon_insert which auto-creates IDs for new terms)
        for (int t = 0; t < num_tokens; t++) {
            lexicon_insert(&g_lexicon, tokens[t]);
        }
        
        UnifiedQuery query;
        memset(&query, 0, sizeof(query));
        query.search_term = search_term;
        query.use_text = true;
        query.use_vector = false;
        query.top_k = 64;
        
        // Use async scatter-gather search across all shards
        pCur->total_results = engine_search_async(pVtab->engine, &query, pCur->results, MAX_RESULTS_PER_SHARD * MAX_SHARDS);
    } else {
        pCur->total_results = 0;
    }
    
    pCur->current_row = 0;
    return SQLITE_OK;
}

static int ftsNext(sqlite3_vtab_cursor *pVtabCursor) {
    fts_uring_cursor *pCur = (fts_uring_cursor *)pVtabCursor;
    pCur->current_row++;
    return SQLITE_OK;
}

static int ftsEof(sqlite3_vtab_cursor *pVtabCursor) {
    fts_uring_cursor *pCur = (fts_uring_cursor *)pVtabCursor;
    return pCur->current_row >= pCur->total_results;
}

static int ftsColumn(sqlite3_vtab_cursor *cur, sqlite3_context *ctx, int i) {
    fts_uring_cursor *pCur = (fts_uring_cursor*)cur;
    
    if (pCur->current_row >= pCur->total_results) return SQLITE_OK;
    
    switch(i) {
        case 0: {
            char buf[33];
            for(int j=0; j<16; j++) {
                snprintf(buf+j*2, 3, "%02x", pCur->results[pCur->current_row].node_id[j]);
            }
            sqlite3_result_text(ctx, buf, -1, SQLITE_TRANSIENT);
            break;
        }
        case 1:
            sqlite3_result_double(ctx, pCur->results[pCur->current_row].bm25f_score);
            break;
        case 2:
            sqlite3_result_double(ctx, pCur->results[pCur->current_row].cosine_score);
            break;
        case 3:
            sqlite3_result_double(ctx, pCur->results[pCur->current_row].rrf_score);
            break;
        default:
            break;
    }
    return SQLITE_OK;
}

static int ftsRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid) {
    fts_uring_cursor *pCur = (fts_uring_cursor*)cur;
    *pRowid = pCur->current_row;
    return SQLITE_OK;
}

static sqlite3_module fts_uring_module = {
    0,              /* iVersion */
    ftsConnect,     /* xCreate */
    ftsConnect,     /* xConnect */
    ftsBestIndex,   /* xBestIndex */
    ftsDisconnect,  /* xDisconnect */
    ftsDisconnect,  /* xDestroy */
    ftsOpen,        /* xOpen - open a cursor */
    ftsClose,       /* xClose - close a cursor */
    ftsFilter,      /* xFilter - configure scan constraints */
    ftsNext,        /* xNext - advance a cursor */
    ftsEof,         /* xEof - check for end of scan */
    ftsColumn,      /* xColumn - read data */
    ftsRowid,       /* xRowid - read data */
    0,              /* xUpdate */
    0,              /* xBegin */
    0,              /* xSync */
    0,              /* xCommit */
    0,              /* xRollback */
    0,              /* xFindFunction */
    0,              /* xRename */
    0,              /* xSavepoint */
    0,              /* xRelease */
    0               /* xRollbackTo */
};

#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_ftsuring_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi) {
    int rc = SQLITE_OK;
    SQLITE_EXTENSION_INIT2(pApi);
    rc = sqlite3_create_module(db, "fts_uring", &fts_uring_module, 0);
    return rc;
}
