#ifndef __COMMON_OPS__
#define __COMMON_OPS__

#include <linux/types.h>
#include <stdio.h>
#include <pthread.h>
#include "rq_heap.h"

#define MAX_DL	~0ULL

struct data_struct_ops {
	void (*data_init) (void *s, int nproc, int (*cmp_dl)(__u64 a, __u64 b));
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
	int (*data_check_cpu) (void *s, int cpu, __u64 dline);
};

extern struct data_struct_ops *dso;
extern void *push_data_struct, *pull_data_struct;
extern struct rq *cpu_to_rq[];

struct task_struct {
	int pid;
	__u64 deadline;
};

struct rq {
	int cpu;
	struct rq_heap heap;
	pthread_spinlock_t lock;
	/* cache values */
	__u64 earliest, next;
	int nrunning, overloaded;
	FILE *log;
};

int __dl_time_before(__u64 a, __u64 b);

int __dl_time_after(__u64 a, __u64 b);

void rq_init (struct rq *rq, int cpu, FILE *f);

void rq_destroy (struct rq *rq);

void rq_lock (struct rq *rq);

void rq_unlock (struct rq *rq);

struct rq_heap_node* rq_peek (struct rq *rq);

struct rq_heap_node* rq_take (struct rq *rq);

struct task_struct* rq_node_task_struct(struct rq_heap_node* h);

void add_task_rq(struct rq* rq, struct task_struct* task);

int rq_pull_tasks(struct rq* this_rq);

int rq_push_tasks(struct rq* this_rq);

int rq_check(struct rq *rq);

void rq_print(struct rq *this_rq, FILE *out);

#endif /*__COMMON_OPS__ */
