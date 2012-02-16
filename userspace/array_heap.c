/* Max-deadline CPU queue implementation
 * using a max-heap built with a static array.
 * array_heap.c
 */
#include <stdio.h> 
#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <pthread.h>
#include <linux/types.h>
#include "array_heap.h"
#include "common_ops.h"

static inline int __dl_time_before(__u64 a, __u64 b)
{
        return (__s64)(a - b) < 0;
}

static inline int parent(int i) {
        return (i - 1) >> 1;
}

static inline int left_child(int i) {
        return (i << 1) + 1;
}

static inline int right_child(int i) {
        return (i << 1) + 2;
}

void exchange(array_heap_t *h, int a, int b) {
	int cpu_tmp;
	__u64 dl_a = h->elements[a].dl, dl_b = h->elements[b].dl;
	int cpu_a = h->elements[a].cpu, cpu_b = h->elements[b].cpu;

	h->elements[b].dl = dl_a;
	h->elements[b].cpu = cpu_a;
	h->elements[a].dl = dl_b;
	h->elements[a].cpu = cpu_b;

	cpu_tmp = h->cpu_to_idx[cpu_b];
	h->cpu_to_idx[cpu_b] = h->cpu_to_idx[cpu_a];
	h->cpu_to_idx[cpu_a] = cpu_tmp;
}

void array_heap_init(void *s, int nproc) {
	int i;
	array_heap_t *h = (array_heap_t*) s;

	pthread_spin_init(&h->lock, 0);
	h->size = 0;
	h->cpu_to_idx = (int*)malloc(sizeof(int)*nproc);
	h->elements = (item*)malloc(sizeof(item)*nproc);

	for (i = 0; i < nproc; i++) {
		h->cpu_to_idx[i] = IDX_INVALID;
	}
}

void print_array_heap(void *s, int nproc) {
	int i;
	array_heap_t *h = (array_heap_t*) s;

	pthread_spin_lock(&h->lock);
	printf("Heap (%d elements):\n", h->size);
	printf("[ ");
	for (i = 0; i < h->size; i++)
		printf("(%d, %llu) ", h->elements[i].cpu, h->elements[i].dl);
	printf("] ");
	/*for (i = h->size; i < nproc; i++)
		printf("(%d, %llu) ", h->elements[i].cpu, h->elements[i].dl);
	printf("\n");*/
	printf("Cpu_to_idx:");
	for (i = 0; i < nproc; i++)
		printf(" %d", h->cpu_to_idx[i]);
	printf("\n");
	pthread_spin_unlock(&h->lock);
}

/* 
 * Move down the item at position idx as to not violate the
 * max-heap property.
 */
void max_heapify(array_heap_t *h, int idx) {
	int l, r, largest;

	l = left_child(idx);
	r = right_child(idx);
#ifdef DEBUG
	printf("idx = (%d, %llu)\n", idx, h->elements[idx].dl);
	printf("l = (%d,%llu), r = (%d,%llu)\n", l, h->elements[l].dl,
			r, h->elements[r].dl);
#endif
	if ((l < h->size) && __dl_time_before(h->elements[idx].dl, h->elements[l].dl))
		largest = l;
	else
		largest = idx;
	if ((r < h->size) && __dl_time_before(h->elements[largest].dl,
				h->elements[r].dl))
		largest = r;
	if (largest != idx) {
#ifdef DEBUG
		printf("exchanging %d with %d\n", largest, idx);
#endif
		exchange(h, largest, idx);
		max_heapify(h, largest);
	}

	return;
}

/* 
 * Sets a new key for the element at position idx.
 */
void heap_change_key(array_heap_t *h, int idx, __u64 new_dl) {
	int cpu;

	if (__dl_time_before(new_dl, h->elements[idx].dl)) {
		h->elements[idx].dl = new_dl;
#ifdef DEBUG
		printf("decreasing key for element: %d\n", idx);
#endif
		max_heapify(h, idx);
	} else {
		h->elements[idx].dl = new_dl;
#ifdef DEBUG
		printf("increasing key for element: %d\n", idx);
#endif
		while (idx > 0 && __dl_time_before(h->elements[parent(idx)].dl,
					h->elements[idx].dl)) {
			exchange(h, idx, parent(idx));
			idx = parent(idx);
		}
	}
	
}

/* Inserts a new key in the heap.
 * Returns the position where the new element has been added.
 */
int heap_set(void *s, int cpu, __u64 dline, int is_valid) {
	int idx, old_idx, new_cpu;
	array_heap_t *h = (array_heap_t*) s;

	/*
	 * if (cpu > ELEM_NUM) {
	 *	printf("warning: cpu = %d\n", cpu);
	 * }
	 */

	pthread_spin_lock(&h->lock);
	old_idx = h->cpu_to_idx[cpu];
	if (!is_valid) {
		new_cpu = h->elements[h->size - 1].cpu;
		h->elements[old_idx].dl = h->elements[h->size - 1].dl;
		h->elements[old_idx].cpu = new_cpu;
		h->size--;
		h->cpu_to_idx[new_cpu] = old_idx;
		h->cpu_to_idx[cpu] = IDX_INVALID;
		while (old_idx > 0 &&
			__dl_time_before(h->elements[parent(old_idx)].dl,
				h->elements[old_idx].dl)) {
			exchange(h, old_idx, parent(old_idx));
			old_idx = parent(old_idx);
		}
		max_heapify(h, old_idx);

		pthread_spin_unlock(&h->lock);
		return -1;
	}

	if (old_idx == IDX_INVALID) {
		h->size++;
		h->elements[h->size - 1].dl = 0;
		h->elements[h->size - 1].cpu = cpu;
		h->cpu_to_idx[cpu] = h->size - 1;
		heap_change_key(h, h->size - 1, dline);
	} else {
		heap_change_key(h, old_idx, dline);
	}

	pthread_spin_unlock(&h->lock);
	return idx;
}

int heap_maximum(void *s) {
	array_heap_t *h = (array_heap_t*) s;

	return h->elements[0].cpu;
}

/* Extracts and returns the maximum of the heap.
 */
/*int heap_extract_max(array_heap_t *h, int cpu) {
	int max;

	if (h->size < 1) {
		printf("ERROR: heap underflow!\n");
		exit(1);
	}

	if (cpu == MAX_CPU) {
		max = h->elements[0].cpu;
		h->elements[0].dl = h->elements[h->size - 1].dl;
		h->elements[0].cpu = h->elements[h->size - 1].cpu;
		h->size--;
		h->cpu_to_idx[max] = IDX_INVALID;
		max_heapify(h, 0, NULL);
		if (h->size != 0) {
			int new_max_cpu = h->elements[0].cpu;
			h->cpu_to_idx[new_max_cpu] = 0;
		}
	} else {
		int new_cpu = h->elements[h->size - 1].cpu;
		max = h->cpu_to_idx[cpu];
		h->elements[max].dl = h->elements[h->size - 1].dl;
		h->elements[max].cpu = new_cpu;
		h->size--;
		h->cpu_to_idx[new_cpu] = max;
		h->cpu_to_idx[cpu] = IDX_INVALID;
		max_heapify(h, max, NULL);
	}

	return max;
}*/

int array_heap_check(void *s, int nproc)
{
	int i, flag = 1;
	array_heap_t *h = (array_heap_t*) s;

	pthread_spin_lock(&h->lock);

	for (i = 0; i < nproc; i++) {
		/* 
		 * check if CPU position in the heap is correctly
		 * set in cpu_to_idx array
		 */
		if (h->cpu_to_idx[i] != IDX_INVALID &&
			h->elements[h->cpu_to_idx[i]].cpu != i) {
			printf("CPU %d is wrongly registered at"
				" position %d!\n", i, h->cpu_to_idx[i]);
			flag = 0;
			goto out;
		}

		/*
		 * check if dline(i) > dline(left_child(i))
		 */
		if (left_child(i) < h->size && __dl_time_before(h->elements[i].dl,
				h->elements[left_child(i)].dl) &&
				h->cpu_to_idx[i] != IDX_INVALID) {
			printf("Node %d has deadline %llu which is smaller"
				" than its left child %d with deadline %llu\n",
				i, h->elements[i].dl, left_child(i),
				h->elements[left_child(i)].dl);
			flag = 0;
			goto out;
		}

		/*
		 * check if dline(i) > dline(right_child(i))
		 */
		if (right_child(i) < h->size && __dl_time_before(h->elements[i].dl,
				h->elements[right_child(i)].dl) &&
				h->cpu_to_idx[i] != IDX_INVALID) {
			printf("Node %d has deadline %llu which is smaller"
				" than its right child %d with deadline %llu\n",
				i, h->elements[i].dl, right_child(i),
				h->elements[right_child(i)].dl);
			flag = 0;
			goto out;
		}
	}

out:
	pthread_spin_unlock(&h->lock);
	if (flag == 0)
		print_array_heap(s, nproc);
	return flag;
}

void array_heap_save(void *s, FILE *f)
{
	return;
}

void array_heap_cleanup(void *s)
{
	return;
}

int array_heap_find(void *s)
{
	return 0;
}

const struct data_struct_ops array_heap_ops = {
	.data_init = array_heap_init,
	.data_cleanup = array_heap_cleanup,
	.data_preempt = heap_set,
	.data_finish = heap_set,
	.data_find = array_heap_find,
	.data_max = heap_maximum,
	//.data_load = heap_load,
	.data_save = array_heap_save,
	.data_check = array_heap_check,
	.data_print = print_array_heap,
};
