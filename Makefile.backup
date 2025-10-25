# pgraft Makefile
# PostgreSQL extension using etcd-io/raft for distributed consensus

MODULE_big = pgraft
OBJS = src/pgraft.o src/pgraft_core.o src/pgraft_go.o src/pgraft_state.o src/pgraft_log.o src/pgraft_kv.o src/pgraft_kv_sql.o src/pgraft_sql.o src/pgraft_guc.o src/pgraft_util.o src/pgraft_apply.o src/pgraft_go_callbacks.o

EXTENSION = pgraft
DATA = pgraft--1.0.sql
PGFILEDESC = "pgraft - PostgreSQL extension with etcd-io/raft integration"

# PostgreSQL configuration - use PostgreSQL 17
PG_CONFIG = /usr/local/pgsql.17/bin/pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# Compiler flags
CFLAGS += -std=c99 -Wall -Wextra -Werror
CFLAGS += -I./include

# Override the default CFLAGS to ensure our include path is used
override CFLAGS += -I./include

# Extension-specific linker flags
SHLIB_LINK += -lpthread -lm -ldl -L/usr/local/pgsql.17/lib

# Go Raft library
GO_RAFT_LIB = src/pgraft_go.dylib

# Build Go Raft library
$(GO_RAFT_LIB): src/pgraft_go.go src/go.mod
	cd src && go mod tidy
	cd src && go build -buildmode=c-shared -o pgraft_go.dylib pgraft_go.go

# Dependencies
$(OBJS): $(GO_RAFT_LIB)

# Clean target - ensure it exists
clean: clean-extra

clean-extra:
	rm -f src/*.o
	rm -f src/pgraft_go.dylib
	rm -f src/pgraft_go.h

# Installation directory
DESTDIR ?= 

# Install targets
install: all install-go-lib

install-go-lib: $(GO_RAFT_LIB)
	$(INSTALL_SHLIB) src/pgraft_go.dylib '$(DESTDIR)$(pkglibdir)/pgraft_go.dylib'

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
