#ifndef PGRAFT_STATE_H
#define PGRAFT_STATE_H

#include "postgres.h"
#include "storage/shmem.h"
#include "storage/spin.h"

/* Go library state persistence */
typedef struct pgraft_go_state
{
	/* Library status */
	int32_t		go_lib_loaded;
	int32_t		go_initialized;
	int32_t		go_running;
	
	/* Node configuration */
	int32_t		go_node_id;
	char		go_address[256];
	int32_t		go_port;
	
	/* Raft state persistence */
	int64_t		go_current_term;
	int64_t		go_voted_for;
	int64_t		go_commit_index;
	int64_t		go_last_applied;
	int64_t		go_last_index;
	int64_t		go_leader_id;
	char		go_raft_state[64];
	
	/* Node configuration */
	int32_t		num_nodes;
	int32_t		node_ids[16];
	char		node_addresses[16][256];
	int32_t		node_ports[16];
	
	/* Performance metrics */
	int64_t		go_messages_processed;
	int64_t		go_log_entries_committed;
	int64_t		go_heartbeats_sent;
	int64_t		go_elections_triggered;
	
	/* Mutex for thread safety */
	slock_t		mutex;
}			pgraft_go_state_t;

/* State management functions */
void		pgraft_state_init_shared_memory(void);
pgraft_go_state_t *pgraft_state_get_shared_memory(void);
void		pgraft_worker_init_shared_memory(void);

/* Go state persistence functions */
void		pgraft_state_save_go_library_state(void);
void		pgraft_state_restore_go_library_state(void);
void		pgraft_state_save_go_raft_state(void);
void		pgraft_state_restore_go_raft_state(void);

/* Node configuration persistence */
void		pgraft_state_save_node_config(int32_t node_id, const char *address, int32_t port);
void		pgraft_state_restore_node_config(int32_t *node_id, char *address, int32_t *port);
void		pgraft_state_save_cluster_nodes(int32_t num_nodes, int32_t *node_ids,
											char node_addresses[][256], int32_t *node_ports);
void		pgraft_state_restore_cluster_nodes(int32_t *num_nodes, int32_t *node_ids,
											   char node_addresses[][256], int32_t *node_ports);

/* State validation */
bool		pgraft_state_is_go_lib_loaded(void);
bool		pgraft_state_is_go_initialized(void);
bool		pgraft_state_is_go_running(void);
void		pgraft_state_set_go_lib_loaded(bool loaded);
void		pgraft_state_set_go_initialized(bool initialized);
void		pgraft_state_set_go_running(bool running);

#endif
