# pgraft Makefile
# PostgreSQL extension using etcd-io/raft for distributed consensus

MODULE_big = pgraft
OBJS = src/pgraft.o src/pgraft_core.o src/pgraft_go.o src/pgraft_state.o src/pgraft_log.o src/pgraft_kv.o src/pgraft_kv_sql.o src/pgraft_sql.o src/pgraft_guc.o src/pgraft_util.o src/pgraft_apply.o src/pgraft_go_callbacks.o src/pgraft_json.o

EXTENSION = pgraft
DATA = pgraft--1.0.sql
PGFILEDESC = "pgraft - PostgreSQL extension with etcd-io/raft integration"

# PostgreSQL configuration
# Use pg_config from PATH by default (allows version-specific builds)
# Can be overridden: make PG_CONFIG=/path/to/pg_config
PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# Compiler flags
CFLAGS += -std=c99 -Wall -Wextra -Werror
CFLAGS += -I./include

# Override the default CFLAGS to ensure our include path is used
override CFLAGS += -I./include

# Extension-specific linker flags
# Add PostgreSQL lib directory dynamically
PG_LIB_DIR := $(shell $(PG_CONFIG) --libdir)
SHLIB_LINK += -lpthread -lm -ldl -L$(PG_LIB_DIR) -ljson-c -L./src
# Add rpath for macOS (loader_path) and Linux ($ORIGIN)
ifeq ($(shell uname -s),Darwin)
    SHLIB_LINK += -Wl,-rpath,@loader_path
else
    SHLIB_LINK += -Wl,-rpath,'$$ORIGIN'
endif

# Go Raft library (platform-specific extension)
ifeq ($(shell uname -s),Darwin)
    GO_RAFT_LIB = src/pgraft_go.dylib
    GO_RAFT_EXT = dylib
else
    GO_RAFT_LIB = src/pgraft_go.so
    GO_RAFT_EXT = so
endif

# Build Go Raft library
$(GO_RAFT_LIB): src/pgraft_go.go src/go.mod
	cd src && go mod tidy
	cd src && go build -buildmode=c-shared -o pgraft_go.$(GO_RAFT_EXT) pgraft_go.go

# Dependencies
$(OBJS): $(GO_RAFT_LIB)

# Clean target - ensure it exists
clean: clean-extra

clean-extra:
	rm -f src/*.o
	rm -f src/pgraft_go.dylib src/pgraft_go.so
	rm -f src/pgraft_go.h

# Installation directory
DESTDIR ?= 

# Install targets
install: all install-go-lib

install-go-lib: $(GO_RAFT_LIB)
	$(INSTALL_SHLIB) $(GO_RAFT_LIB) '$(DESTDIR)$(pkglibdir)/pgraft_go.$(GO_RAFT_EXT)'

# Development flags
ifeq ($(DEBUG),1)
    CFLAGS += -g -O0 -DDEBUG
else
    CFLAGS += -O2 -DNDEBUG
endif

# Test target
test: all
	@echo "Running pgraft tests..."
	@echo "Tests would go here"

.PHONY: clean install test
