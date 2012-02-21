#include <pthread.h>
#include <stdlib.h>
#include "common_ops.h"
#include "rq_heap.h"

static inline int __dl_time_before(__u64 a, __u64 b)
{
        return (__s64)(a - b) < 0;
}

static int task_compare(struct rq_heap_node* _a, struct rq_heap_node* _b)
{
	struct task_struct *a, *b;
	a = (struct task_struct*) rq_heap_node_value(_a);
	b = (struct task_struct*) rq_heap_node_value(_b);

	return __dl_time_before(a->deadline, b->deadline);
}

void rq_init (struct rq *rq)
{
	rq_heap_init(&rq->heap);
	pthread_spin_init(&rq->lock, 0);
	rq->earliest = 0;
	rq->next = 0;
	rq->nrunning = 0;
	rq->overloaded = 0;
}

void rq_lock (struct rq *rq)
{
	pthread_spin_lock(&rq->lock);
}

void rq_unlock (struct rq *rq)
{
	pthread_spin_unlock(&rq->lock);
}

rq_node_struct* rq_peek (struct rq *rq)
{
	return rq_heap_peek(task_compare, &rq->heap);
}

rq_node_struct* rq_take (struct rq *rq)
{
	rq_node_struct *ns_taken, *ns_next;
	struct task_struct* ts_next;

	if (rq->nrunning < 1) {
		printf("ERROR: dequeue on an empty queue!\n");
		exit(-1);
	}

	if (--rq->nrunning < 2)
		rq->overloaded = 0;
#ifdef DEBUG
	printf("nrunning = %d, overloaded = %d\n", rq->nrunning,
			rq->overloaded);
#endif
	ns_taken = rq_heap_take(task_compare, &rq->heap);
	rq->earliest = rq->next;
	ns_next = rq_heap_peek_next(task_compare, &rq->heap);
	if (ns_next != NULL) {
		ts_next = (struct task_struct*) rq_heap_node_value(ns_next);
		rq->next = ts_next->deadline;
	} else
		rq->next = 0;
#ifdef DEBUG
	printf("earliest = %llu, next = %llu\n", rq->earliest,
			rq->next);
#endif

	return ns_taken;
}

struct task_struct* rq_node_task_struct(rq_node_struct* h)
{
	struct task_struct* ts;
	
	ts = (struct task_struct*) rq_heap_node_value(h);
#ifdef DEBUG
	printf("node (%d,%llu)\n", ts->pid, ts->deadline);
#endif
	return ts;
}

void add_task_rq(struct rq* rq, struct task_struct* task)
{
	__u64 task_dl = task->deadline;
	__u64 old_earliest = rq->earliest, old_next = rq->next;
	struct rq_heap_node* hn = malloc(sizeof(struct rq_heap_node));
	struct task_struct* ts;
	

	rq_heap_node_init(hn, task);
	ts = (struct task_struct*) rq_heap_node_value(hn);
#ifdef DEBUG
	printf("inserting node (%d,%llu)\n", ts->pid, ts->deadline);
#endif
	rq_heap_insert(task_compare, &rq->heap, hn);
	/*
	 * cache update
	 */
	if (rq->nrunning == 0 || __dl_time_before(task_dl, old_earliest)) {
		rq->next = old_earliest;
		rq->earliest = task_dl;
	} else if (!rq->overloaded || __dl_time_before(task_dl, old_next))
		rq->next = task_dl;
#ifdef DEBUG
	printf("earliest = %llu, next = %llu\n", rq->earliest,
			rq->next);
#endif

	if (++rq->nrunning > 1)
		rq->overloaded = 1;
#ifdef DEBUG
	printf("nrunning = %d, overloaded = %d\n", rq->nrunning,
			rq->overloaded);
#endif
}

