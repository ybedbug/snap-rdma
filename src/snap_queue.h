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

#ifndef SNAP_SYS_QUEUE_H_
#define SNAP_SYS_QUEUE_H_

#include <sys/queue.h>

#define SNAP_TAILQ_REMOVE_SAFE(head, elm, field) do {		\
	if (((elm)->field.tqe_next) != NULL)			\
		(elm)->field.tqe_next->field.tqe_prev =		\
		(elm)->field.tqe_prev;				\
	else							\
		(head)->tqh_last = (elm)->field.tqe_prev;	\
	*(elm)->field.tqe_prev = (elm)->field.tqe_next;		\
	(elm)->field.tqe_prev = NULL;				\
} while (/*CONSTCOND*/0)

#define SNAP_TAILQ_FOREACH_SAFE(var, head, field, next)		  \
	for ((var) = ((head)->tqh_first);				\
		(var) != NULL && ((next) = TAILQ_NEXT((var), field), 1); \
		(var) = (next))
#endif

/*
 * Override buggy implementations in sys/queue.h
 */
#undef TAILQ_REMOVE
#define TAILQ_REMOVE(head, elm, field) do {                \
	if ((elm) == (head)->tqh_first)                     \
		(head)->tqh_first = (elm)->field.tqe_next;      \
	if (((elm)->field.tqe_next) != NULL)                \
		(elm)->field.tqe_next->field.tqe_prev =         \
			(elm)->field.tqe_prev;                \
	else                                \
		(head)->tqh_last = (elm)->field.tqe_prev;        \
	*(elm)->field.tqe_prev = (elm)->field.tqe_next;            \
(elm)->field.tqe_prev = NULL;                       \
} while (/*CONSTCOND*/0)

