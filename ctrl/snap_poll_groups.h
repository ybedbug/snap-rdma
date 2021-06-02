#ifndef _SNAP_POLL_GROUPS_H
#define _SNAP_POLL_GROUPS_H

#include <pthread.h>
#include <sys/queue.h>

struct snap_pg_q_entry {
	TAILQ_ENTRY(snap_pg_q_entry) entry;
};

struct snap_pg {
	int id;
	TAILQ_HEAD(, snap_pg_q_entry) q_list;
	pthread_spinlock_t lock;
};

struct snap_pg_ctx {
    /* Polling groups */
	struct snap_pg *pgs;
	int npgs;
};

void snap_pgs_free(struct snap_pg_ctx *ctx);
int snap_pgs_alloc(struct snap_pg_ctx *ctx, int nthreads);
void snap_pgs_suspend(struct snap_pg_ctx *ctx);
void snap_pgs_resume(struct snap_pg_ctx *ctx);
struct snap_pg *snap_pg_get_next(struct snap_pg_ctx *ctx);
void snap_pg_usage_decrease(size_t pg_index);

#endif
