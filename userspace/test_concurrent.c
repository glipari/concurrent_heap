#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include "heap.h"
#include "array_heap.h"
#include "common_ops.h"
#include "rq_heap.h"

//#define VERBOSE 

#define NPROCESSORS    8
#define NCYCLES        10000 /* 1 cycle = 1ms simulated time */
#define DMIN           10
#define DMAX           100
#define WAITCYCLE      10000

#ifdef VERBOSE
#define PRINT_OP(i, op, dline) printf("%d) %s, dline %llu\n", i, op, dline)
#else 
#define PRINT_OP(i, op, dline) 
#endif

void *data_struct;
heap_t heap;
array_heap_t array_heap;
pthread_t threads[NPROCESSORS];
int last_pid = 0; /* operations on this MUST be ATOMIC */
struct data_struct_ops *dso;
extern struct data_struct_ops array_heap_ops;
extern struct data_struct_ops heap_ops;

typedef enum {HEAP=0, ARRAY_HEAP=1, SKIPLIST=2} data_struct_t;
typedef enum {ARRIVAL=0, FINISH=1, NOTHING=2} operation_t;
/*
 * 20% probability of new arrival
 * 10% of finish earlier that dline
 * 70% of doing nothing
 */
double prob[3] = {.2, .3, 1};

static inline int __dl_time_before(__u64 a, __u64 b)
{
        return (__s64)(a - b) < 0;
}

struct timespec
usec_to_timespec(unsigned long usec)
{
	struct timespec ts;

	ts.tv_sec = usec / 1000000;
	ts.tv_nsec = (usec % 1000000) * 1000;

	return ts;
}

struct timespec
timespec_add(struct timespec *t1, struct timespec *t2)
{
	struct timespec ts;

	ts.tv_sec = t1->tv_sec + t2->tv_sec;
	ts.tv_nsec = t1->tv_nsec + t2->tv_nsec;

	while (ts.tv_nsec >= 1E9) {
		ts.tv_nsec -= 1E9;
		ts.tv_sec++;
	}

	return ts;
}

static int task_compare(struct rq_heap_node* _a, struct rq_heap_node* _b)
{
	struct task_struct *a, *b;
	a = (struct task_struct*) rq_heap_node_value(_a);
	b = (struct task_struct*) rq_heap_node_value(_b);

	return __dl_time_before(a->deadline, b->deadline);
}

static void add_task(struct rq_heap* heap, struct task_struct* task)
{
	struct rq_heap_node* hn = malloc(sizeof(struct rq_heap_node));
	rq_heap_node_init(hn, task);
	rq_heap_insert(task_compare, heap, hn);
}

operation_t select_operation()
{
	operation_t i = 0;
	double p = ((double)rand()) / (double)INT_MAX;
	for (i = ARRIVAL; i < NOTHING + 1; i++) {
		if (p < prob[i]) return i;
	}
	
	return NOTHING;
}

__u64 arrival_process(__u64 curr_clock)
{
    __u64 tmp = curr_clock;
    tmp +=  (rand() % (DMAX - DMIN)) + DMIN;

    return tmp;
}

int num_arrivals[NPROCESSORS];
int num_preemptions[NPROCESSORS];
int num_early_finish[NPROCESSORS];
int num_finish[NPROCESSORS];
int num_empty[NPROCESSORS];

void signal_handler(int sig)
{
    int i;
    printf("\nEXITING!\n");
    dso->data_print(data_struct, NPROCESSORS);
    for (i=0; i<NPROCESSORS; i++) 
        printf("Index %d, ID %ld\n", i, (threads[i] % 100));
    exit(-1);
}

void *processor(void *arg)
{
	int index = *((int*)arg);
	int i, is_valid = 0;
	struct rq_heap rq;
	struct rq_heap_node *min;
	struct task_struct *min_tsk, *new_tsk;
	operation_t op;
	struct timespec t_sleep, t_period;

	rq_heap_init(&rq);
#ifdef DEBUG
	printf("[%d]: rq initialized\n", index);
#endif

	__u64 min_dl = 0, new_dl;
	__u64 curr_clock = 0;
	t_period = usec_to_timespec(1000);

	clock_gettime(CLOCK_MONOTONIC, &t_sleep);
	for (i = 0; i < NCYCLES; i++) {
		curr_clock++;

		min = rq_heap_peek(task_compare, &rq);
		if (min != NULL) {
			min_tsk = (struct task_struct*) rq_heap_node_value(min);
			min_dl = min_tsk->deadline;
		}

#ifdef DEBUG
		printf("[%d]: curr_clock = %llu, min_dl = %llu\n",
			index, curr_clock, min_dl);
#endif

		if (__dl_time_before(min_dl, curr_clock) && min != NULL) {
			/*
			 * remove task from rq
			 * task finish
			 */
			rq_heap_take(task_compare, &rq);

			min_dl = 0;
			is_valid = 0;
			min = rq_heap_peek(task_compare, &rq);
			if (min != NULL) {
				min_tsk = (struct task_struct*) rq_heap_node_value(min);
				min_dl = min_tsk->deadline;
				is_valid = 1;
			}
			dso->data_finish(data_struct, index, min_dl, is_valid);
#ifdef DEBUG
			printf("[%d]: task finishes\n", index);
			if (!is_valid)
				printf("[%d]: rq empty!\n", index);
#endif

			if (!is_valid)
				num_empty[index]++;
			num_finish[index]++;
		}

		op = select_operation();

		if (op == ARRIVAL) {
			num_arrivals[index]++;
			new_dl = arrival_process(curr_clock);
			PRINT_OP(index, "arrival", dline);
			new_tsk = malloc(sizeof(struct task_struct));
			new_tsk->deadline = new_dl;
			new_tsk->pid = __sync_fetch_and_add( &last_pid, 1 );
#ifdef DEBUG
			printf("[%d]: task arrival (%d, %llu)\n", index,
					new_tsk->pid, new_tsk->deadline);
#endif

			add_task(&rq, new_tsk);
			if (__dl_time_before(new_dl, min_dl)) {
				dso->data_preempt(data_struct, index, new_dl, 1);
#ifdef DEBUG
				printf("[%d]: preemption!\n", index);
#endif
				num_preemptions[index]++;
			} else if (min_dl == 0) {
				dso->data_preempt(data_struct, index, new_dl, 1);
#ifdef DEBUG
				printf("[%d]: no more empty\n", index);
#endif
			}
		} else if (op == FINISH) {
			min = rq_heap_peek(task_compare, &rq);
			if (min != NULL) {
				/*
				 * if rq is not empty take the first
				 * task
				 */
#ifdef DEBUG
				printf("[%d]: task finishes early\n", index);
#endif
				num_early_finish[index]++;
				rq_heap_take(task_compare, &rq);

				/*
				 * than see if the next task is to be scheduled
				 * or else the rq becomes empty
				 */
				min_dl = 0;
				is_valid = 0;
				min = rq_heap_peek(task_compare, &rq);
				if (min != NULL) {
					min_tsk = (struct task_struct*) rq_heap_node_value(min);
					min_dl = min_tsk->deadline;
					is_valid = 1;
				}
				dso->data_finish(data_struct, index, min_dl, is_valid);

#ifdef DEBUG
				if (!is_valid)
					printf("[%d]: rq empty!\n", index);
#endif

				if (!is_valid)
					num_empty[index]++;
				num_finish[index]++;
			}
		}

#ifdef DEBUG
		dso->data_print(data_struct, NPROCESSORS);
#endif
		t_sleep = timespec_add(&t_sleep, &t_period);
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t_sleep, NULL);
	}

	return 0;
}

void *checker(void *arg)
{
    int flag;
    int count = 0;

    while(1) {
    	flag = dso->data_check(data_struct, NPROCESSORS);
	if (!flag) {
		// lock has not been released!
		printf("Errore!!!\n");
		FILE *myfile = fopen("error_heap.txt", "w");
		dso->data_save(data_struct, myfile);
		fclose(myfile);
		exit(-1);
	} else {
        	// lock released
#ifndef DEBUG
        	printf("%d) Checker: OK!\r", count++);
#endif
        	usleep(10);
	}
    }
}

data_struct_t parse_user_options(int argc, char **argv)
{
	data_struct_t data_type = HEAP;
	int c;

	if (argc < 2) {
		printf("usage: test_concurrent OPTION\n"
			"\n\tOPTION:\n"
			"\t  -a array_heap\n"
			"\t  -h heap\n"
			"\t  -s skiplist\n");
		exit(-1);
	}
	while ((c = getopt(argc, argv, "has")) != -1)
		switch (c) {
			case 'h':
				data_type = HEAP;
				dso = &heap_ops;
				data_struct = &heap;
				break;
			case 'a':
				data_type = ARRAY_HEAP;
				dso = &array_heap_ops;
				data_struct = &array_heap;
				break;
			case 's':
				data_type = SKIPLIST;
				break;
			default:
				printf("data_type is not valid!\n");
				exit(-1);
		}

	return data_type;
}

int main(int argc, char **argv)
{
    pthread_t check;
    data_struct_t data_type;
    int ind[NPROCESSORS];
    int i;

    signal(SIGINT, signal_handler);
#ifdef DEBUG
    srand(1);
#else
    srand(time(NULL));
#endif


    data_type = parse_user_options(argc, argv);

    switch (data_type) {
	    case HEAP:
    		printf("Initializing the heap\n");
		break;
	    case ARRAY_HEAP:
    		printf("Initializing the array_heap\n");
		break;
	    case SKIPLIST:
		printf("skiplist is not yet implemented!\n");
		exit(-1);
	    default:
		exit(-1);
    }
    dso->data_init(data_struct, NPROCESSORS);
    
    printf("Creating Checker\n");

    pthread_create(&check, 0, checker, 0);

    //sleep(20);

    printf("Creating processors\n");

    for (i=0; i<NPROCESSORS; i++) {
        ind[i] = i;
        pthread_create(&threads[i], 0, processor, &ind[i]);
    }

    printf("Waiting for the end\n");

    for (i=0; i<NPROCESSORS; i++) {
        pthread_join(threads[i], 0);
	printf("+++++++++++++++++++++++++++++++++\n");
        printf("Num Arrivals [%d]: %d\n", i, num_arrivals[i]);
        printf("Num Preemptions [%d]: %d\n", i, num_preemptions[i]);
        printf("Num Finishings [%d]: %d\n", i, num_finish[i]);
        printf("Num Early Finishings [%d]: %d\n", i, num_early_finish[i]);
        printf("Num queue empty events  [%d]: %d\n", i, num_empty[i]);
    }
    printf("--------------EVERYTHING OK!---------------------\n");
    
    return 0;
}
