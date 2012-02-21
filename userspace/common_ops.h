#ifndef __COMMON_OPS__
#define __COMMON_OPS__

#include <linux/types.h>
#include <stdio.h>
#include <pthread.h>
#include "rq_heap.h"

struct data_struct_ops {
	void (*data_init) (void *s, int nproc);
	void (*data_cleanup) (void *s);

	/*
	 * Update CPU state inside the data structure
	 * after a preemption
	 */
	int (*data_preempt) (void *s, int cpu, __u64 dline, int is_valid);
	/*
	 * Update CPU state inside the data structure
	 * after a task finished 
	 */
	int (*data_finish) (void *s, int cpu, __u64 dline, int is_valid);
	/*
	 * data_find should find the best CPU where to push
	 * a task and/or find the best task to pull from
	 * another CPU
	 */
	int (*data_find) (void *s);
	int (*data_max) (void *s);

	void (*data_load) (void *s, FILE *f);
	void (*data_save) (void *s, FILE *f);
	void (*data_print) (void *s, int nproc);

	int (*data_check) (void *s, int nproc);
};

struct task_struct {
	int pid;
	__u64 deadline;
};

struct rq {
	struct rq_heap heap;
	pthread_spinlock_t lock;
	/* cache values */
	__u64 earliest, next;
	int nrunning, overloaded;
};

typedef struct rq_heap_node rq_node_struct;

void rq_init (struct rq *rq);

void rq_lock (struct rq *rq);

void rq_unlock (struct rq *rq);

rq_node_struct* rq_peek (struct rq *rq);

rq_node_struct* rq_take (struct rq *rq);

struct task_struct* rq_node_task_struct(rq_node_struct* h);

void add_task_rq(struct rq* rq, struct task_struct* task);
#endif /*__COMMON_OPS__ */
