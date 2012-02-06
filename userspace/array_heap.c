/* Max-deadline CPU queue implementation
 * using a max-heap built with a static array.
 * array_heap.c
 */
#include <stdio.h> 
#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <pthread.h>
#include "array_heap.h"

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
	long dl_a = h->elements[a].dl, dl_b = h->elements[b].dl;
	int cpu_a = h->elements[a].cpu, cpu_b = h->elements[b].cpu;

	h->elements[b].dl = dl_a;
	h->elements[b].cpu = cpu_a;
	h->elements[a].dl = dl_b;
	h->elements[a].cpu = cpu_b;

	cpu_tmp = h->cpu_to_idx[cpu_b];
	h->cpu_to_idx[cpu_b] = h->cpu_to_idx[cpu_a];
	h->cpu_to_idx[cpu_a] = cpu_tmp;
}

void array_heap_init(array_heap_t *h, int nproc) {
	int i;

	pthread_spin_init(&h->lock, 0);
	h->size = 0;
	h->cpu_to_idx = (int*)malloc(sizeof(int)*nproc);
	h->elements = (item*)malloc(sizeof(item)*nproc);

	for (i = 0; i < nproc; i++) {
		h->cpu_to_idx[i] = IDX_INVALID;
	}
}

void print_array_heap(array_heap_t *h, int nproc) {
	int i;

	pthread_spin_lock(&h->lock);
	printf("Heap (%d elements):\n", h->size);
	printf("[ ");
	for (i = 0; i < h->size; i++)
		printf("(%d, %lu, %d %d %d) ", h->elements[i].cpu, h->elements[i].dl,
				parent(i), left_child(i), right_child(i));
	printf("] ");
	for (i = h->size; i < nproc; i++)
		printf("(%d, %lu) ", h->elements[i].cpu, h->elements[i].dl);
	printf("\n");
	printf("Cpu_to_idx:");
	for (i = 0; i < nproc; i++)
		printf(" %d", h->cpu_to_idx[i]);
	printf("\n");
	pthread_spin_unlock(&h->lock);
}

/* Move down the item at position idx as to not violate the
 * max-heap property.
 */
void max_heapify(array_heap_t *h, int idx, int *new_idx) {
	int l, r, largest;

	l = left_child(idx);
	r = right_child(idx);
	if ((l <= h->size) && (h->elements[l].dl > h->elements[idx].dl))
		largest = l;
	else
		largest = idx;
	if ((r <= h->size) && (h->elements[r].dl > h->elements[largest].dl))
		largest = r;
	if (largest != idx) {
		/*printf("exchanging %d with %d\n", largest, idx);*/
		exchange(h, largest, idx);
		max_heapify(h, largest, new_idx);
	} else if (new_idx != NULL)
		*new_idx = largest;

	return;
}

/* Inserts a new key in the heap.
 * Returns the position where the new element has been added.
 */
int heap_insert(array_heap_t *h, long dl, int cpu) {
	int idx, old_idx;

	/*
	 * if (cpu > ELEM_NUM) {
	 *	printf("warning: cpu = %d\n", cpu);
	 * }
	 */

	pthread_spin_lock(&h->lock);
	if (dl == DL_INVALID) {
		/*h->elements[old_idx].dl = dl;*/
		/*h->cpu_to_idx[cpu] = IDX_INVALID;*/
		/*h->size--;*/
		/*max_heapify(h, old_idx);*/
		/*max = h->cpu_to_idx[cpu];*/
		int new_cpu = h->elements[h->size - 1].cpu;
		h->elements[old_idx].dl = h->elements[h->size - 1].dl;
		h->elements[old_idx].cpu = new_cpu;
		h->size--;
		h->cpu_to_idx[new_cpu] = old_idx;
		h->cpu_to_idx[cpu] = IDX_INVALID;
		max_heapify(h, old_idx, NULL);

		pthread_spin_unlock(&h->lock);
		return -1;
	}

	old_idx = h->cpu_to_idx[cpu];
	if (old_idx == IDX_INVALID) {
		h->size++;
		h->elements[h->size - 1].dl = -1;
		h->elements[h->size - 1].cpu = cpu;
		h->cpu_to_idx[cpu] = h->size - 1;
		idx = heap_change_key(h, h->size - 1, dl);
		h->elements[idx].cpu = cpu;
		/*h->cpu_to_idx[cpu] = idx;*/
	} else {
		idx = heap_change_key(h, old_idx, dl);
		h->elements[idx].cpu = cpu;
	}

	pthread_spin_unlock(&h->lock);
	return idx;
}

int heap_maximum(array_heap_t *h) {
	return h->elements[0].cpu;
}

/* Extracts and returns the maximum of the heap.
 */
int heap_extract_max(array_heap_t *h, int cpu) {
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
}

/* Sets a new key for the element at position idx.
 * Returns the new idx for that element.
 */
int heap_change_key(array_heap_t *h, int idx, long new_dl) {
	/*
	 * if (idx > ELEM_NUM) {
	 *	printf("warning: idx = %d\n", idx);
	 * }
	 */

	if (new_dl < h->elements[idx].dl) {
		/*printf("decreasing key for element: %d\n", idx);*/
		h->elements[idx].dl = new_dl;
		max_heapify(h, idx, &idx);
	} else {
		h->elements[idx].dl = new_dl;
		while (idx > 0 && h->elements[parent(idx)].dl < h->elements[idx].dl) {
			exchange(h, idx, parent(idx));
			idx = parent(idx);
		}
	}

	return idx;
}
