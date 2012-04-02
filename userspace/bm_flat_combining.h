#ifndef __BM_FLAT_COMBINING_H
#define __BM_FLAT_COMBINING_H

#include <linux/types.h>
#include <stdio.h>
#include <stdint.h>

#include "parameters.h"

/* flat combining parameters */
#define PUB_RECORD_PER_CPU		10	/* no more than 32 publication record per CPU allowed in this implementation */

/* data structure operations type */
typedef enum {
	PREEMPT
} op_type;

/* data structure operations parameters */
typedef struct{
	int cpu;
	__u64 dline;
	int is_valid;
} preempt_params;

typedef union{
	preempt_params preempt_p;
} params;

/* data structure operations handler */
typedef struct{
	void (*function)(void *data_structure, int cpu, __u64 dline, int is_valid);
} preempt_handler;

typedef union{
	preempt_handler preempt_h;
} handler;

/* publication record */
struct pub_record{
	/* operation type */
	op_type req;
	/* operation parameters */
	params par;
	/* operation handler */
	handler h;
};

/* flat combining helper structure */
struct flat_combining;

/* flat combining interface */
struct flat_combining *fc_create(void *data_structure);

int fc_destroy(struct flat_combining *fc);

struct pub_record *fc_get_record(struct flat_combining *fc, const int cpu);

void fc_publish_record(struct flat_combining *fc, const int cpu);

/*
 * if we use a totally asynchronous flat combining
 * implementation, this function is going to be
 * used only internally in this module.
 * Otherwise, when we want to stop deferring work,
 * we have to call it explicitly.
 */
void fc_try_combiner(struct flat_combining *fc);

/* 
 * if we want to ensure that a certain operation
 * will be executed synchronously and sequentially
 * we have to acquire and further release lock 
 * on data structure with these functions
 */
void fc_data_structure_lock(struct flat_combining *fc);

void fc_data_structure_unlock(struct flat_combining *fc);

/* print helper function useful for debugging purpose */
void fc_print_publication_list(struct flat_combining *fc, FILE *out);

#endif
