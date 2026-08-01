#include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1
#include <stdio.h>
#include <string.h>

// Include your custom engine headers here
// #include "../include/fts_types.h"
// extern void scatter_query_to_shards_uv(...);

/* 
** 1. THE VIRTUAL TABLE OBJECT
** This represents an instance of your table.
*/
typedef struct {
    sqlite3_vtab base;        // Base class. Must be first.
    // You can add your c_fts_uring context here (e.g. file descriptors, libuv loops)
} fts_uring_vtab;

/* 
** 2. THE CURSOR OBJECT
** This represents a single query execution (a single scan).
*/
typedef struct {
    sqlite3_vtab_cursor base; // Base class. Must be first.
    
    // State for yielding rows back to SQLite
    int current_row;
    int total_results;
    double *scores;           // Array of BM25F scores from your engine
    char **node_ids;          // Array of UUIDs from your engine
} fts_uring_cursor;


/* 
** xConnect: Called when the user types: CREATE VIRTUAL TABLE t1 USING fts_uring;
** We tell SQLite what our schema looks like.
*/
static int ftsConnect(sqlite3 *db, void *pAux, int argc, const char *const*argv, sqlite3_vtab **ppVtab, char **pzErr) {
    int rc = sqlite3_declare_vtab(db, "CREATE TABLE x(node_id TEXT, score NUMERIC, query HIDDEN)");
    if( rc==SQLITE_OK ){
        fts_uring_vtab *pNew = sqlite3_malloc( sizeof(*pNew) );
        memset(pNew, 0, sizeof(*pNew));
        *ppVtab = (sqlite3_vtab*)pNew;
        // Initialize your c_fts_uring libuv loop here
    }
    return rc;
}

static int ftsDisconnect(sqlite3_vtab *pVtab) {
    sqlite3_free(pVtab);
    return SQLITE_OK;
}

static int ftsOpen(sqlite3_vtab *pVTab, sqlite3_vtab_cursor **ppCursor) {
    fts_uring_cursor *pCur = sqlite3_malloc( sizeof(*pCur) );
    memset(pCur, 0, sizeof(*pCur));
    *ppCursor = (sqlite3_vtab_cursor*)pCur;
    return SQLITE_OK;
}

static int ftsClose(sqlite3_vtab_cursor *cur) {
    sqlite3_free(cur);
    return SQLITE_OK;
}

/*
** xBestIndex: The Query Planner Hook.
** SQLite asks: "The user wrote 'WHERE query = 'spaceship''. Can you handle this?"
*/
static int ftsBestIndex(sqlite3_vtab *tab, sqlite3_index_info *pIdxInfo) {
    for(int i = 0; i < pIdxInfo->nConstraint; i++){
        // If we see: `query` = 'something'
        if( pIdxInfo->aConstraint[i].iColumn == 2 && pIdxInfo->aConstraint[i].op == SQLITE_INDEX_CONSTRAINT_EQ ){
            pIdxInfo->aConstraintUsage[i].argvIndex = 1; // Pass this string to xFilter
            pIdxInfo->aConstraintUsage[i].omit = 1;      // Tell SQLite we will evaluate it
            pIdxInfo->estimatedCost = 10.0;              // Tell SQLite we are very fast!
            return SQLITE_OK;
        }
    }
    return SQLITE_CONSTRAINT;
}

/*
** xFilter: Execute the Query!
** SQLite hands us the actual string (e.g., "spaceship").
*/
static int ftsFilter(sqlite3_vtab_cursor *pVtabCursor, int idxNum, const char *idxStr, int argc, sqlite3_value **argv) {
    fts_uring_cursor *pCur = (fts_uring_cursor *)pVtabCursor;
    
    // 1. Get the search string from SQLite
    if( argc > 0 ){
        const char *search_term = (const char*)sqlite3_value_text(argv[0]);
        // 2. KICK OFF YOUR ENGINE!
        // printf("c_fts_uring searching for: %s\n", search_term);
        // scatter_query_to_shards_uv(..., search_term, ...);
        // gather_shard_results_uv();
    }
    
    // 3. Store the results in the cursor
    pCur->current_row = 0;
    pCur->total_results = 4; // Mock result count
    return SQLITE_OK;
}

/*
** xNext: Move to the next result row.
*/
static int ftsNext(sqlite3_vtab_cursor *pVtabCursor) {
    fts_uring_cursor *pCur = (fts_uring_cursor *)pVtabCursor;
    pCur->current_row++;
    return SQLITE_OK;
}

/*
** xEof: Tell SQLite if we are out of results.
*/
static int ftsEof(sqlite3_vtab_cursor *pVtabCursor) {
    fts_uring_cursor *pCur = (fts_uring_cursor *)pVtabCursor;
    return pCur->current_row >= pCur->total_results;
}

/*
** xColumn: SQLite is asking for the data of the current row!
*/
static int ftsColumn(sqlite3_vtab_cursor *cur, sqlite3_context *ctx, int i) {
    // fts_uring_cursor *pCur = (fts_uring_cursor*)cur;
    
    switch(i) {
        case 0: // node_id
            sqlite3_result_text(ctx, "01010000-0000-0000-0000-000000000000", -1, SQLITE_TRANSIENT);
            break;
        case 1: // score
            sqlite3_result_double(ctx, 8.0106);
            break;
    }
    return SQLITE_OK;
}

/*
** xRowid: SQLite is asking for the hidden rowid
*/
static int ftsRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid) {
    fts_uring_cursor *pCur = (fts_uring_cursor*)cur;
    *pRowid = pCur->current_row;
    return SQLITE_OK;
}

/*
** Register the Virtual Table module callbacks
*/
static sqlite3_module fts_uring_module = {
    0,              /* iVersion */
    ftsConnect,     /* xCreate */
    ftsConnect,     /* xConnect */
    ftsBestIndex,   /* xBestIndex */
    ftsDisconnect,  /* xDisconnect */
    ftsDisconnect,  /* xDestroy */
    ftsOpen,        /* xOpen (create cursor) */
    ftsClose,       /* xClose (destroy cursor) */
    ftsFilter,      /* xFilter */
    ftsNext,        /* xNext */
    ftsEof,         /* xEof */
    ftsColumn,      /* xColumn */
    ftsRowid,       /* xRowid */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// Entry point when loading the extension into SQLite
#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_ftsuring_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi) {
    SQLITE_EXTENSION_INIT2(pApi);
    return sqlite3_create_module(db, "fts_uring", &fts_uring_module, 0);
}
