#ifndef SNAP_TEST_H
#define SNAP_TEST_H

#include <stdlib.h>
#include <stdio.h>

#include "snap.h"

struct snap_context *snap_ctx_open(int emulated_types);
void snap_ctx_close(struct snap_context *sctx);

#endif
