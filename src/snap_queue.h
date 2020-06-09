#ifndef SNAP_SYS_QUEUE_H_
#define SNAP_SYS_QUEUE_H_

#include <sys/queue.h>

#define SNAP_TAILQ_REMOVE_SAFE(head, elm, field) do {   \
    if (((elm)->field.tqe_next) != NULL)                \
        (elm)->field.tqe_next->field.tqe_prev =         \
            (elm)->field.tqe_prev;                      \
    else                                                \
        (head)->tqh_last = (elm)->field.tqe_prev;       \
    *(elm)->field.tqe_prev = (elm)->field.tqe_next;     \
    (elm)->field.tqe_prev = NULL;                       \
} while (/*CONSTCOND*/0)

#define SNAP_TAILQ_FOREACH_SAFE(var, head, field, next)                  \
        for ((var) = ((head)->tqh_first);                                \
                (var) != NULL && ((next) = TAILQ_NEXT((var), field), 1); \
                    (var) = (next))
#endif
