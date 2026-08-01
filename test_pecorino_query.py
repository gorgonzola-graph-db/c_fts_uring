import sqlite3

print("[1] Opening Python SQLite3 connection...")
conn = sqlite3.connect(":memory:")

print("[2] Loading our custom fts_uring extension...")
conn.enable_load_extension(True)
conn.load_extension("./fts_uring.so")

print("[3] Creating the Virtual Table mapped to our C-Engine...")
# Using 'fts_uring' as the module name as defined in vtab_bridge.c
conn.execute("CREATE VIRTUAL TABLE pecorino_ast USING fts_uring();")

print("[4] Querying for the AST node snippet 'DataCollector' inserted by our C program...")
cursor = conn.execute("""
    SELECT node_id, bm25f_score, cosine_score, rrf_score 
    FROM pecorino_ast 
    WHERE query = 'DataCollector'
""")

for row in cursor.fetchall():
    print("--------------------------------------------------")
    print("Result from Python + SQLite VTab Bridge:")
    print(f"  Node ID:      {row[0]}")
    print(f"  BM25F Score:  {row[1]:.4f}")
    print(f"  Cosine Score: {row[2]:.4f}")
    print(f"  RRF Score:    {row[3]:.4f}")
    print("--------------------------------------------------")

print("Success!")
