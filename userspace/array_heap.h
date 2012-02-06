/* Max-deadline CPU queue implementation
 * using a max-heap built with a static array.
 * array_heap.h
 */
#include <pthread.h>
#include <linux/types.h>

#define DL_INVALID	-1
#define IDX_INVALID	-1
#define MAX_CPU		-1

struct array_item {
	__u64 dl;
	int cpu;
};
typedef struct array_item item;

struct heap_struct {
	pthread_spinlock_t lock;
	int size;
	int *cpu_to_idx;
	item *elements;
};
typedef struct heap_struct array_heap_t;

void array_heap_init(array_heap_t *h, int nproc);

void print_array_heap(array_heap_t *h, int nproc);

void max_heapify(array_heap_t *h, int idx, int *new_idx);

int heap_insert(array_heap_t *h, long dl, int cpu);

int heap_maximum(array_heap_t *h);

int heap_extract_max(array_heap_t *h, int cpu);

int heap_increase_key(array_heap_t *h, int cpu, long new_dl);
