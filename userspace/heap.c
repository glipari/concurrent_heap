#include <stdlib.h>
#include <limits.h>
#include <stdio.h>

#include "heap.h"
#include "common_ops.h"
#include "parameters.h"

#ifndef SEQUENTIAL 
#define LOCK(h,x)                                                       \
    do {                                                                \
        if (x < h->nproc) { pthread_mutex_lock(&h->array[x].m);         \
            h->array[x].locked = 1;                                     \
            h->array[x].id = (pthread_self() % 100);}                   \
    } while(0)

#define UNLOCK(h,x)                                                     \
    do {                                                                \
        if (x < h->nproc) { h->array[x].locked = 0;                     \
            pthread_mutex_unlock(&h->array[x].m); }                     \
    } while(0)
#else
#define LOCK(h,x)                                                       
#define UNLOCK(h,x)                                                      
#endif


#ifdef __DEBUG__
#define PRINT(x) printf(x)
#else
#define PRINT(x)
#endif

dline_t DLINE_MAX = {0, DL_MAX};
dline_t DLINE_MIN = {0, DL_MIN};

int dl_time_before(dline_t a, dline_t b)
{
	if (a.special == DL_MIN) {
		if (b.special == DL_MIN) return 0;
		else return 1;
	} else if (a.special == DL_MAX) {
		return 0;
	} else {
		if (b.special == DL_MIN) return 0;
		else if (b.special == DL_MAX) return 1;
		else return ((s64)(a.value - b.value) < 0);
	}
}

void heap_swap_nodes(heap_t *h, int n, int p)
{
    node_t *tmp = h->array[n].node;
    h->array[n].node = h->array[p].node;
    h->array[p].node = tmp;
    h->array[n].node->position = n;
    h->array[p].node->position = p;
}

int max_dline_proc(heap_t *h, int a, int b, int c) 
{
    int proc = dl_time_before(DLINE(h, b), DLINE(h, a)) ? a : b;
    return dl_time_before(DLINE(h, c), DLINE(h, proc)) ? proc : c;
}

void heap_init(void *s, int nproc)
{
    int i;
    heap_t *h = (heap_t*) s;
    h->array = (heap_node_t *)malloc(sizeof(heap_node_t)*nproc);
    h->nproc = nproc;
    h->nodes = (node_t*)malloc(sizeof(node_t)*nproc);
    for (i=0; i<nproc; i++) {
        h->nodes[i].proc_index = i;
        h->nodes[i].deadline = DLINE_MAX;
        h->nodes[i].position = i;

        h->array[i].node = &h->nodes[i];
        pthread_mutex_init(&(h->array[i].m), 0);
        h->array[i].locked = 0;
    }    
}

void heap_delete(void *s)
{
    int i;
    heap_t *h = (heap_t*) s;
    for (i=0; i<h->nproc; i++) 
        pthread_mutex_destroy(&(h->array[i].m));
    free(h->nodes);
    free(h->array);
}

int heap_preempt(void *s, int proc, __u64 newdl, int is_valid)
{
    heap_t *h = (heap_t*) s;
    dline_t newdline;
    newdline.value = newdl;
    newdline.special = DL_NORMAL;
    LOCK(h, 0);
    /* check if still the same */
    if (proc != h->array[0].node->proc_index) {
        UNLOCK(h, 0);
        return 0;
    }
    /* still the same, do the update */
    h->array[0].node->deadline = newdline;
    int n = 0;
    int l = 1;
    int r = 2;
    /* see if we must go down */
    while (n < h->nproc) {
        LOCK(h, l);
        LOCK(h, r);
        int p = max_dline_proc(h,n,l,r);
        if (p == n) {
            UNLOCK(h, r);
            UNLOCK(h, l);
            UNLOCK(h, n);
            return 1;
        }
        else if (p == r) UNLOCK(h, l);
        else             UNLOCK(h, r);

        /* p and n are still locked    */
        /* now p and n must be swapped */
        heap_swap_nodes(h, n, p);

        UNLOCK(h, n);   /* p is still locked */

        n = p;
        l = heap_left(n);
        r = heap_right(n);
    }
    printf("SHOULD NEVER GO OUT FROM HERE!\n");
    exit(-1);
    UNLOCK(h, n);
    return 1;
}

int heap_preempt_local(void *s, int proc, __u64 newdl, int is_valid)
{
    heap_t *h = (heap_t*) s;
    dline_t newdline;
    newdline.value = newdl;
    newdline.special = DL_NORMAL;
    int proc_pos = h->nodes[proc].position;
    LOCK(h, proc_pos);
    /* check if still the same */
    if (proc != h->array[proc_pos].node->proc_index) {
        UNLOCK(h, proc_pos);
        return 0;
    }
    /* still the same, do the update */
    h->array[proc_pos].node->deadline = newdline;
    int n = proc_pos;
    int l = heap_left(n);
    int r = heap_right(n);
    /* see if we must go down */
    while (n < h->nproc) {
        LOCK(h, l);
        LOCK(h, r);
        int p = max_dline_proc(h,n,l,r);
        if (p == n) {
            UNLOCK(h, r);
            UNLOCK(h, l);
            UNLOCK(h, n);
            return 1;
        }
        else if (p == r) UNLOCK(h, l);
        else             UNLOCK(h, r);

        /* p and n are still locked    */
        /* now p and n must be swapped */
        heap_swap_nodes(h, n, p);

        UNLOCK(h, n);   /* p is still locked */

        n = p;
        l = heap_left(n);
        r = heap_right(n);
    }
    printf("SHOULD NEVER GO OUT FROM HERE!\n");
    exit(-1);
    UNLOCK(h, n);
    return 1;
}

// the path is on the stack: now I should lock from top until proc
// the problem is that proc could move up in the meanwhile!
#define STACKSIZE  10   /* needs to be > log_2(nproc) */
#define STACKBASE  0   

int heap_finish(void *s, int proc, __u64 dl, int is_valid)
{
    int path[STACKSIZE];                     
    int top = STACKBASE, base = STACKBASE;              
    heap_t *h = (heap_t*) s;
    dline_t deadline;
    if (is_valid) {
        deadline.value = dl;
        deadline.special = DL_NORMAL;
    } else {
        deadline.value = 0;
        deadline.special = DL_MAX;
    }
    node_t *p_proc = &h->nodes[proc];

    int j = 0, k = 0;             /* node indexes       */
    int i;                        /* path index         */

    LOCK(h, 0);                   /* locks the root     */
    path[top++] = 0;              /* put node 0 on path */
    while (1) {
        j = p_proc->position;     /* reads position (no lock)  */
        if (j == k) break;        /* node already locked, exit */

        while (heap_parent(j) != k) 
            j = heap_parent(j);   /* climb up until k   */

        k = j;                    
        path[top++] = k;          /* put k on path      */
        LOCK(h, k);               
    }

    /* now, everything is locked from 0 to j included */
    /* assumption: dline > j->dline */
    /* we now check the assumption, otherwise abort */
    if (dl_time_before(deadline, p_proc->deadline)) {
        /* unlock everything and return */
        for (i=0; i<top; i++) UNLOCK(h, path[i]);
        return 0;
    }
    /* now the assumption holds */
    k = path[base];
    while (dl_time_before(deadline, DLINE(h, k))) { 
        UNLOCK(h, k);
        k = path[++base];          /* move to child on path */
    }
    
    /* now, everything locked from k to j included */ 
    node_t *p = ARRAY_PNODE(h,j);
    node_t *q = 0;
    ARRAY_PNODE(h,j)->deadline = deadline;
    for (i = base; i<top; i++) {
        q = ARRAY_PNODE(h, path[i]);
        ARRAY_PNODE(h, path[i]) = p;
        p = q;
        ARRAY_PNODE(h, path[i])->position = path[i];
        //UNLOCK(h,path[i]);
    }

    for (i = base; i<top; i++) {
        UNLOCK(h,path[i]);
    }
    return 1;
}


void heap_print(void *s, int nproc)
{
    int i;
    heap_t *h = (heap_t*) s;
    printf("[\n");
    for (i=0; i<h->nproc; i++) {
        printf("  pos: %d = (pr %d, dl %llu (%d), lk %d", i, 
               h->array[i].node->proc_index, 
               h->array[i].node->deadline.value,
               h->array[i].node->deadline.special,
               h->array[i].locked);
        if (h->array[i].locked) printf(" id %ld)\n", h->array[i].id);
        else printf(")\n");
    }
    printf("]\n");
}


int heap_check(void *s, int nproc)
{
    /* check order */
    int i;
    int sum=0;
    int flag = 1;
    heap_t *h = (heap_t*) s;
    /* lock everything */
    for (i=0; i<h->nproc; i++) 
        LOCK(h, i);
    
    for (i=0; i<h->nproc; i++) {
        if (ARRAY_PNODE(h, i)->position != i) {
            printf("Node %d contains processor %d, which is registered at position %d\n", 
                   i, ARRAY_PNODE(h, i)->proc_index, ARRAY_PNODE(h, i)->position);
            flag = 0; 
            break;
        } 
        if (dl_time_before(DLINE(h,i), DLINE(h,heap_left(i)))) {
            printf("Node %d has deadline %llu (%d) which is smaller than its left child (%d) deadline %llu (%d)\n", 
                   i, DLINE(h,i).value, DLINE(h,i).special , 
                   heap_left(i), DLINE(h,heap_left(i)).value, DLINE(h,heap_left(i)).special);
            flag = 0;
            break;
        }
        if (dl_time_before(DLINE(h,i), DLINE(h,heap_right(i)))) {
            printf("Node %d has deadline %llu (%d) which is smaller than its right child (%d) deadline %llu (%d)\n", 
                   i, DLINE(h,i).value, DLINE(h,i).special, heap_right(i), 
                   DLINE(h,heap_right(i)).value, DLINE(h,heap_right(i)).special);
            flag = 0;
            break;
        }
        sum += ARRAY_PNODE(h,i)->proc_index + 1; 
    }
    if (flag == 1 && sum != (h->nproc+1) * h->nproc / 2) {
        printf("Something strange's happening! Probably some node is duplicated! sum = %d\n", sum);
        flag = 0;
    }

    if (flag == 1) 
	for (i=0; i<h->nproc; i++) 
            UNLOCK(h, i);

    return flag;
}

void heap_save(void *s, int nproc, FILE *f)
{
    int i;
    heap_t *h = (heap_t*) s;
    fprintf(f, "N_Nodes: %d\n", h->nproc);
    
    for (i=0; i<h->nproc; i++) {
        fprintf(f, "index %d\tdeadline %llu type %d\n", 
                h->array[i].node->proc_index, 
                h->array[i].node->deadline.value,
                h->array[i].node->deadline.special
            );
    }
}

void heap_load(void *s, FILE *f)
{
    char str[100];
    int n, i;
    int k;
    dline_t d;
    int x;
    heap_t *h = (heap_t*) s;
    fscanf(f, "%s %d\n", str, &n);

    heap_init(h, n);
    
    for (i=0; i<h->nproc; i++) {
        fscanf(f, "%s %d", str, &k);
        fscanf(f, "%s %llu %d", str, &d.value, &x);
        d.special = x;
        h->nodes[k].deadline = d;
        h->nodes[k].position = i;
        h->array[i].node = &h->nodes[k];
    }
}

const struct data_struct_ops heap_ops = {
	.data_init = heap_init,
	.data_cleanup = heap_delete,
	.data_preempt = heap_preempt_local,
	.data_finish = heap_finish,
	//.data_find = array_heap_find,
	//.data_max = heap_maximum,
	.data_load = heap_load,
	.data_save = heap_save,
	.data_check = heap_check,
	.data_print = heap_print,
};
