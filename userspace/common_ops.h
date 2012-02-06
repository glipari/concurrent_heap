#ifndef __COMMON_OPS__
#define __COMMON_OPS__

#include <linux/types.h>
#include <stdio.h>

struct data_struct_ops {
	void (*data_init) (void *s, int nproc);
	void (*data_cleanup) (void *s, int nproc);

	/*
	 * Update CPU state inside the data structure
	 */
	int (*data_set) (void *s, int cpu, __u64 dline);
	/*
	 * data_find should find the best CPU where to push
	 * a task and/or find the best task to pull from
	 * another CPU
	 */
	int (*data_find) (void *s);
	int (*data_max) (void *s);

	void (*data_load) (void *s, FILE *f);
	void (*data_save) (void *s, FILE *f);
	void (*data_print) (void *s);

	int (*data_check) (void *s);
};

#endif /*__COMMON_OPS__ */
