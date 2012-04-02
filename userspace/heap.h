#ifndef __HEAP_H__
#define __HEAP_H__

#include <pthread.h>
#include <stdio.h>
#include <inttypes.h>
#include <linux/types.h>
#include "common_ops.h"

typedef unsigned long long u64;
typedef long long s64;

typedef enum{DL_MIN, DL_NORMAL, DL_MAX} dl_t;

typedef struct __dline_struct {
    u64 value;
    dl_t special;
} dline_t;

int dl_time_before(dline_t a, dline_t b);

typedef struct _node {
    int proc_index;
    dline_t deadline;
    int position;
} node_t;

typedef struct _heap_node {
    node_t *node;
    pthread_mutex_t m;
    char locked;
    pthread_t id;
} heap_node_t;

typedef struct _heap {
    int nproc;
    heap_node_t *array;

    node_t *nodes;
} heap_t;

#define DLINE(h,x) ((x<h->nproc) ? h->array[x].node->deadline : DLINE_MIN)
#define ARRAY_PNODE(h, i) (h->array[i].node)
#define PNODE_DLINE(h, p) (h->nodes[p].deadline)

void heap_init(void *s, int nproc);
void heap_delete(void *s);

#define heap_left(index) (2*(index)+1)
#define heap_right(index) (2*(index)+2)
#define heap_parent(index) (((index)-1)>>1)
#define heap_get_max_proc(h) ((h)->array[0].node->proc_index)
#define heap_get_max_dline(h) ((h)->array[0].node->deadline)
#define heap_get_max_node(h) ((h)->array[0].node)

int heap_preempt(void *s, int proc, __u64 newdline, int is_valid);
int heap_preempt_local(void *s, int proc, __u64 newdline, int is_valid);
int heap_finish(void *s, int proc, __u64 deadline, int is_valid);
void heap_print(void *s, int nproc);
int heap_check(void *s, int nproc);

void heap_save(void *s, int nproc, FILE *f);
void heap_load(void *s, FILE *f);

#endif
