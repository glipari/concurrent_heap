#ifndef __COMMON_OPS__
#define __COMMON_OPS__

#include <linux/types.h>
#include <stdio.h>

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
#endif /*__COMMON_OPS__ */
