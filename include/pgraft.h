/*-------------------------------------------------------------------------
 *
 * pgraft.h
 *      Extension-wide definitions for pgraft.
 *
 * Copyright (c) 2024-2025, pgElephant, Inc.
 *
 *-------------------------------------------------------------------------
 */
#ifndef PGRAFT_H
#define PGRAFT_H

/*
 * Extension version.
 *
 * Keep these three in lockstep on every release:
 *   - PGRAFT_VERSION below,
 *   - default_version in pgraft.control (plus a matching pgraft--<v>.sql and
 *     an upgrade script from the previous version),
 *   - pgraftVersion in src/pgraft_go.go.
 *
 * pgraft_go_check_version() compares this against the Go library's own version
 * at load time and warns when a stale pgraft_go library is picked up.
 */
#define PGRAFT_VERSION "2.0.0"

#endif							/* PGRAFT_H */
