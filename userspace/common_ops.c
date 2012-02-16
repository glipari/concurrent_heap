#include <pthread.h>
#include "common_ops.h"
#include "rq_heap.h"

void rq_init (struct rq *rq)
{
	rq_heap_init(&rq->heap);
	pthread_spin_init(&rq->lock, 0);
	rq->earliest = 0;
	rq->next = 0;
}

void rq_lock (struct rq *rq)
{
	pthread_spin_lock(&rq->lock);
}

void rq_unlock (struct rq *rq)
{
	pthread_spin_unlock(&rq->lock);
}
