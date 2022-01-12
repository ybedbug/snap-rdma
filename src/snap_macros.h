/*
 * Copyright © 2021 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of Nvidia Corporation and its affiliates
 * (the "Company") and all right, title, and interest in and to the software
 * product, including all associated intellectual property rights, are and
 * shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */

#ifndef _SNAP_COMPILER_H_
#define _SNAP_COMPILER_H_
#include <assert.h>

#ifndef offsetof
#define offsetof(t, m) ((size_t) &((t *)0)->m)
#endif

#ifndef container_of
#define container_of(ptr, type, member) ({ \
		typeof(((type *)0)->member)*__mptr = (ptr); \
		(type *)((char *)__mptr - offsetof(type, member)); })
#endif

#define snap_min(a, b) (((a) < (b)) ? (a) : (b))
#define snap_max(a, b) (((a) > (b)) ? (a) : (b))

#define snap_likely(x) __builtin_expect(!!(x), 1)
#define snap_unlikely(x) __builtin_expect(!!(x), 0)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(n) (sizeof(n) / sizeof(*n))
#endif

#define SNAP_ALIGN_FLOOR(val, align) \
	(typeof(val))((val) & (~((typeof(val))((align) - 1))))

#define SNAP_ALIGN_CEIL(val, align) \
	SNAP_ALIGN_FLOOR(((val) + ((typeof(val)) (align) - 1)), align)

#define SNAP_IS_POW2_OR_ZERO(_n) (!((_n) & ((_n) - 1)))

#define SNAP_IS_POW2(_n) (((_n) > 0) && SNAP_IS_POW2_OR_ZERO(_n))

#define SNAP_ROUNDUP_POW2(_n) \
	({ \
	/* a hack to discard 'const' qualifier of _n */\
	typeof((_n) + 0) pow2; \
	/* weird indentation here to make style check happy */\
	for \
	(pow2 = 1; pow2 < (_n); pow2 <<= 1); \
	\
	pow2; \
	})

#define SNAP_ROUNDUP_POW2_OR0(_n) \
	(((_n) == 0) ? 0 : SNAP_ROUNDUP_POW2(_n))

#ifndef static_assert
#define static_assert(cond, msg)
#endif

#endif
