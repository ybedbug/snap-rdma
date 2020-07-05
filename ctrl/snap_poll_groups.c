#include <stdlib.h>
#include "snap_poll_groups.h"

void snap_pgs_free(struct snap_pg_ctx *ctx)
{
	int i;

	if (ctx->npgs == 0)
		return;

	for (i = 0; i < ctx->npgs; i++)
		pthread_spin_destroy(&ctx->pgs[i].lock);

	free(ctx->pgs);
}

int snap_pgs_alloc(struct snap_pg_ctx *ctx, int npgs)
{
	int i;

	ctx->npgs = 0;
	ctx->next_pg = 0;
	ctx->pgs = calloc(npgs, sizeof(struct snap_pg));
	if (!ctx->pgs)
		return -1;

	ctx->npgs = npgs;
	ctx->next_pg = 0;
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

	pg = &ctx->pgs[ctx->next_pg];
	pg->id = ctx->next_pg;
	ctx->next_pg = (ctx->next_pg + 1) % ctx->npgs;
	return pg;
}
