CC = gcc
CFLAGS = -Wall -O2 -fPIC -Iinclude
LDFLAGS = -luv -lm -lrt

FTS_SRCS = src/lexicon.c src/shard_worker.c src/uv_loop.c src/main.c
FTS_OBJS = $(FTS_SRCS:.c=.o)

TAB_SRCS = src/catalog.c src/slotted_page.c src/tabular_worker.c src/main_tabular.c
TAB_OBJS = $(TAB_SRCS:.c=.o)

WAL_SRCS = src/slotted_page.c src/wal.c src/buffer_pool.c src/shm_manager.c src/main_wal_bp.c
WAL_OBJS = $(WAL_SRCS:.c=.o)

P23_SRCS = src/slotted_page.c src/wal.c src/buffer_pool.c src/btree.c src/fts_indexer.c src/main_phase2_3.c
P23_OBJS = $(P23_SRCS:.c=.o)

TARGETS = c_fts_poc c_tabular_poc c_wal_bp_poc c_phase2_3_poc fts_uring.so fts_shell

VTAB_SRCS = src/vtab_bridge.c src/unified_shard.c src/wal.c src/buffer_pool.c \
            src/slotted_page.c src/btree.c src/fts_indexer.c src/shard_worker.c \
            src/lexicon_map.c src/shard_meta.c src/async_search.c src/inverted_index.c
VTAB_OBJS = $(VTAB_SRCS:.c=.o)

all: $(TARGETS)

c_fts_poc: $(FTS_OBJS)
	$(CC) $(FTS_OBJS) -o c_fts_poc $(LDFLAGS)

c_tabular_poc: $(TAB_OBJS)
	$(CC) $(TAB_OBJS) -o c_tabular_poc $(LDFLAGS)

c_wal_bp_poc: $(WAL_OBJS)
	$(CC) $(WAL_OBJS) -o c_wal_bp_poc $(LDFLAGS)

c_phase2_3_poc: $(P23_OBJS)
	$(CC) $(P23_OBJS) -o c_phase2_3_poc $(LDFLAGS)

fts_uring.so: $(VTAB_OBJS)
	$(CC) -g -fPIC -shared $(VTAB_OBJS) -o fts_uring.so $(LDFLAGS)

fts_shell: src/db_engine.o src/db_shell.o
	$(CC) src/db_engine.o src/db_shell.o -o fts_shell -lpthread -ldl -lm

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o $(TARGETS) fts_uring.so mock_shard_*.dat tab_shard_*.dat wal_bp_*.dat wal_bp_*.wal test_phase2_3.* fts_uring_data/*
