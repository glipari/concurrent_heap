#ifndef __FLAT_COMBINING_H
#define __FLAT_COMBINING_H

#include <linux/types.h>

/* publication record helper macro */
#define IS_ACTIVE(r) r->active > 0
#define ACTIVATE(r) r->active = 1
#define DEACTIVATE(r) r->active = 0

#define IS_READY(r) r->ready > 0
#define SET_READY(r) r->ready = 1
#define UNSET_READY(r) r->ready = 0

/* concurrent data structure operations type */
typedef enum {
	PREEMPT
} op_type;

/* concurrent data structure operations parameters */
typedef struct{
	int cpu;
	__u64 dline;
	int is_valid;
} preempt_params;

typedef union{
	preempt_params preempt_p;
} params;

/* concurrent data structure operations results */
typedef struct{
	int res;
} preempt_result;

typedef union{
	preempt_result preempt_r;
} result;

/* publication record */
typedef struct public_record pub_record;
struct public_record{
	op_type req;
	params par;
	result res;
	unsigned active;
	unsigned ready;
	pub_record *next;
};

/* publication record list */
typedef struct{
	pub_record *head;
} pub_list;

pub_list *create_publication_list();

int destroy_publication_list(pub_list *queue);

pub_record *create_publication_record();

int destroy_publication_record(pub_record *rec);

int enqueue_publication_record(pub_list *queue, pub_record *r);

pub_record *dequeue_all_publication_record(pub_list *queue);

void print_publication_list(pub_record *head, FILE *out);

/* combiner thread helper functions */

//int try_combiner(int *data_structure_lock);

//void release_combiner_lock(int *data_structure_lock);

#endif
