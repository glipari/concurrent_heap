#ifndef __HEAP_H__
#define __HEAP_H__

#include <pthread.h>
#include <stdio.h>
#include <inttypes.h>

typedef unsigned long long u64;
typedef long long s64;

#define DLINE_MAX (LONG_MAX - 10)

typedef struct _node {
    int proc_index;
    u64 deadline;
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

#define DLINE(h,x) ((x<h->nproc) ? h->array[x].node->deadline : 0)
#define ARRAY_PNODE(h, i) (h->array[i].node)
#define PNODE_DLINE(h, p) (h->nodes[p].deadline)

int dl_time_before(u64 a, u64 b);

void heap_init(heap_t *h, int nproc);
void heap_delete(heap_t *h);

#define heap_left(index) (2*(index)+1)
#define heap_right(index) (2*(index)+2)
#define heap_parent(index) (((index)-1)>>1)
#define heap_get_max_proc(h) ((h)->array[0].node->proc_index)
#define heap_get_max_dline(h) ((h)->array[0].node->deadline)
#define heap_get_max_node(h) ((h)->array[0].node)

int heap_preempt(heap_t *h, int proc, u64 newdline);
int heap_finish(heap_t *h, int proc, u64 deadline);
void heap_print(heap_t *h);
int heap_check(heap_t *h);

int heap_save(heap_t *h, FILE *f);
int heap_load(heap_t *h, FILE *f);

#endif
