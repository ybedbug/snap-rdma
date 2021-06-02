#include <stdlib.h>
#include "snap_poll_groups.h"

static size_t *virtio_pg_usage = NULL;
static size_t virtio_pg_ref_count = 0;

void snap_pgs_free(struct snap_pg_ctx *ctx)
{
	int i;

	if (ctx->npgs == 0)
		return;

	for (i = 0; i < ctx->npgs; i++)
		pthread_spin_destroy(&ctx->pgs[i].lock);

	free(ctx->pgs);

	virtio_pg_ref_count--;
	if (!virtio_pg_ref_count) {
		free(virtio_pg_usage);
		virtio_pg_usage = NULL;
	}
}

int snap_pgs_alloc(struct snap_pg_ctx *ctx, int npgs)
{
	int i;

	ctx->npgs = 0;
	ctx->pgs = calloc(npgs, sizeof(struct snap_pg));
	if (!ctx->pgs)
		return -1;

	if (!virtio_pg_usage) {
		virtio_pg_usage = calloc(npgs, sizeof(*virtio_pg_usage));
		if (!virtio_pg_usage) {
			free(ctx->pgs);
			return -1;
		}
		virtio_pg_ref_count = 0;
	}
	virtio_pg_ref_count++;

	ctx->npgs = npgs;
	for (i = 0; i < npgs; i++) {
		pthread_spin_init(&ctx->pgs[i].lock, PTHREAD_PROCESS_PRIVATE);
		TAILQ_INIT(&ctx->pgs[i].q_list);
	}
	return 0;
}

void snap_pgs_suspend(struct snap_pg_ctx *ctx)
{
	int i;

	for (i = 0; i < ctx->npgs; i++)
		pthread_spin_lock(&ctx->pgs[i].lock);
}

void snap_pgs_resume(struct snap_pg_ctx *ctx)
{
	int i;

	for (i = ctx->npgs - 1; i >= 0; i--)
		pthread_spin_unlock(&ctx->pgs[i].lock);
}

struct snap_pg *snap_pg_get_next(struct snap_pg_ctx *ctx)
{
	struct snap_pg *pg;
	size_t pg_index = 0, i;

	for (i = 1; i < ctx->npgs; i++)
		if (virtio_pg_usage[i] < virtio_pg_usage[pg_index])
			pg_index = i;
	pg = &ctx->pgs[pg_index];
	virtio_pg_usage[pg_index]++;
	pg->id = pg_index;

	return pg;
}

void snap_pg_usage_decrease(size_t pg_index)
{
	virtio_pg_usage[pg_index]--;
}
