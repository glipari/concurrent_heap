#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <semaphore.h>
#include <pthread.h>

#include "heap.h"
#include "array_heap.h"
#include "dl_skiplist.h"
#include "fc_dl_skiplist.h"
#include "bm_fc_skiplist.h" 
#include "common_ops.h"
#include "rq_heap.h"
#include "measure.h"
#include "parameters.h"

#ifdef VERBOSE
#define PRINT_OP(i, op, dline) printf("%d) %s, dline %llu\n", i, op, dline)
#else 
#define PRINT_OP(i, op, dline) 
#endif

heap_t push_heap;
heap_t pull_heap;

array_heap_t push_array_heap;
array_heap_t pull_array_heap;

dl_skiplist_t push_dl_skiplist;
dl_skiplist_t pull_dl_skiplist;

fc_dl_skiplist_t push_fc_skiplist;
fc_dl_skiplist_t pull_fc_skiplist;

fc_sl_t push_bm_fc_skiplist;
fc_sl_t pull_bm_fc_skiplist;

pthread_t threads[NPROCESSORS];
sem_t start_barrier_sem, end_barrier_sem;
unsigned int barrier_count;
int last_pid = 0; /* operations on this MUST be ATOMIC */

/*
 * global data structures for push
 * and pull operations
 */
struct data_struct_ops *dso;

void *push_data_struct, *pull_data_struct;

extern struct data_struct_ops array_heap_ops;
extern struct data_struct_ops heap_ops;
extern struct data_struct_ops dl_skiplist_ops;
extern struct data_struct_ops fc_dl_skiplist_ops;
extern struct data_struct_ops bm_fc_skiplist_ops;

struct rq *cpu_to_rq[NPROCESSORS];

typedef enum {HEAP=0, ARRAY_HEAP=1, SKIPLIST=2, FC_SKIPLIST=3, BM_FC_SKIPLIST=4} data_struct_t;
typedef enum {ARRIVAL=0, FINISH=1, NOTHING=2} operation_t;
/*
 * 20% probability of new arrival
 * 10% of finish earlier that dline
 * 70% of doing nothing
 */
double prob[3] = {.2, .3, 1};

/*
 * usec_to_timespec - conversion from usec to 
 * struct timespec, return the value converted
 * @usec: number of usec
 */
struct timespec
usec_to_timespec(unsigned long usec)
{
	struct timespec ts;

	ts.tv_sec = usec / 1000000;
	ts.tv_nsec = (usec % 1000000) * 1000;

	return ts;
}

/*
 * timespec_add - sum two struct timespec value,
 * return the sum
 * @t1: pointer to first struct timespec value
 * @t2: pointer to second struct timespec value
 */
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

/*
 * select_operation - extract an operation type
 * at random, return the type selected
 */
operation_t select_operation()
{
	operation_t i = 0;
	double p = ((double)rand()) / (double)INT_MAX;
	for (i = ARRIVAL; i < NOTHING + 1; i++) {
		if (p < prob[i]) return i;
	}
	
	return NOTHING;
}

/*
 * arrival_process - randomize a deadline value
 * return the selected value
 * @curr_clock: current time
 */
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
int num_push[NPROCESSORS];
int num_pull[NPROCESSORS];

/*
 * signal_handler - a signal handler for SIGINT signal
 * @sig: signal id
 */
void signal_handler(int sig)
{
	int i;
	printf("\nEXITING!\n");

	printf("----Push Data Structure----\n");
	dso->data_print(push_data_struct, NPROCESSORS);
	printf("----Pull Data Structure----\n");
	dso->data_print(pull_data_struct, NPROCESSORS);
	for (i = 0; i < NPROCESSORS; i++) 
		printf("Index %d, ID %ld\n", i, (threads[i] % 100));
	exit(-1);
}

/*
 * processor - thread body for threads simulating CPUs
 * @arg: a pointer to a thread argument structure
 */
void *processor(void *arg)
{
	int index = *((int*)arg);
	int i, is_valid = 0;
	struct rq rq;
	struct rq_heap_node *min, *node;
	struct task_struct *min_tsk, *new_tsk;
	operation_t op;
	struct timespec t_sleep, t_period;
	FILE *log;
	char log_name[LOGNAME_LEN];

	sprintf(log_name, "log-%d", index);
	log = fopen(log_name, "w");

	fprintf(log, "*****SIMULATION START*****\n\n");

	/* 
	 * initialize CPU runqueue and 
	 * bind the runqueue address to
	 * CPU in cpu_to_rq global array
	 */
	rq_init(&rq, index, log);
	cpu_to_rq[index] = &rq;

#ifdef DEBUG
	fprintf(rq.log, "[%d]:\trq initialized\n", index);
#endif

	__u64 min_dl = 0, new_dl;
	__u64 curr_clock = 0;
	t_period = usec_to_timespec(10000);

	/* simulation start barrier */
	__sync_fetch_and_add(&barrier_count, 1);

	if(barrier_count == NPROCESSORS)
		sem_post(&start_barrier_sem);

	sem_wait(&start_barrier_sem);
	sem_post(&start_barrier_sem);

	/* get current time */
	clock_gettime(CLOCK_MONOTONIC, &t_sleep);

	/* simulation cycles */
	for (i = 0; i < NCYCLES; i++) {
#ifdef MEASURE_CYCLE
	THREAD_LOCAL_MEASURE_START(cycle, index)
#endif
		curr_clock++;

#ifdef DEBUG	
		fprintf(rq.log, "[%d]:\ttaking lock on runqueue #%d\n", index, index);
#endif

		/* lock runqueue */
		rq_lock(&rq);

		/* try to pull to simulate pre_schedule() in Linux Scheduler */
		num_pull[index] += rq_pull_tasks(&rq);

		/* peek for the earliest deadline task */
		min = rq_peek(&rq);
		if (min != NULL) {
			min_tsk = rq_heap_node_value(min);
			min_dl = min_tsk->deadline;

#ifdef DEBUG
			fprintf(rq.log, "[%d]:\tthere is a queued task (%d, %llu)\n",
					index, 
					min_tsk->pid , min_tsk->deadline);
#endif
		}

#ifdef DEBUG
		fprintf(rq.log, "[%d]: curr_clock = %llu, min_dl = %llu\n",
			index, curr_clock, min_dl);
#endif

		/* if min_dl is earlier than curr_clock we have a finish */
		if (__dl_time_before(min_dl, curr_clock) && min != NULL) {
			/*
			 * remove task from rq
			 * task finish
			 */
			node = rq_take(&rq);
			free((struct task *)rq_heap_node_value(node));
			free(node);

			min_dl = 0;
			is_valid = 0;
			min = rq_peek(&rq);
			if (min != NULL) {
				min_tsk = rq_heap_node_value(min);
				min_dl = min_tsk->deadline;
				is_valid = 1;
			}

			/*
			 * is_valid == 0 means the runqueue is now empty
			 * is_valid != 0 means the runqueue has another task enqueued
			 */
#ifdef DEBUG
			printf("[%d]: task finishes\n", index);
			if (!is_valid)
				printf("[%d]: rq empty!\n", index);
#endif

			if (!is_valid)
				num_empty[index]++;
			num_finish[index]++;
		}

		/* select an operation at random */
		op = select_operation();

		/* arrival of a new task */
		if (op == ARRIVAL) {
			num_arrivals[index]++;
			new_dl = arrival_process(curr_clock);
			PRINT_OP(index, "arrival", new_dl);
			new_tsk = (struct task_struct *)malloc(sizeof(*new_tsk));
			if(!new_tsk){
				fprintf(stderr, "out of memory!\n");
				fflush(stderr);
				sync();
				exit(1);			
			}
			new_tsk->deadline = new_dl;
			new_tsk->pid = __sync_fetch_and_add( &last_pid, 1 );
#ifdef DEBUG
			printf("[%d]: task arrival (%d, %llu)\n", index,
					new_tsk->pid, new_tsk->deadline);
#endif

			/* enqueue the task on runqueue */
			add_task_rq(&rq, new_tsk);

			/* 
			 * if new_dl is earlier than min_dl 
			 * or min_dl = 0 (that is, the runqueue is empty)
			 * we have a preempt 
			 */
			if (__dl_time_before(new_dl, min_dl)) {
#ifdef DEBUG
				printf("[%d]: preemption!\n", index);
#endif
				num_preemptions[index]++;
				min_dl = new_dl;
			} else if (min_dl == 0) {
				min_dl = new_dl;
#ifdef DEBUG
				printf("[%d]: no more empty\n", index);
#endif
			}
		} else if (op == FINISH) {
			/* we have a finish */
			min = rq_peek(&rq);
			if (min != NULL) {
				/*
				 * if rq is not empty take the first
				 * task
				 */
#ifdef DEBUG
				printf("[%d]: task finishes early\n", index);
#endif
				num_early_finish[index]++;
				node = rq_take(&rq);
				free((struct task_struct *)rq_heap_node_value(node));
				free(node);

				/*
				 * than see if the next task is to be scheduled
				 * or else the rq becomes empty
				 */
				min_dl = 0;
				is_valid = 0;
				min = rq_peek(&rq);
				if (min != NULL) {					
					min_tsk = rq_heap_node_value(min);
					min_dl = min_tsk->deadline;
					is_valid = 1;
				}

#ifdef DEBUG
				if (!is_valid)
					printf("[%d]: rq empty!\n", index);
#endif

				if (!is_valid)
					num_empty[index]++;
				num_finish[index]++;
			}
		}

		/* try to push tasks to simulate post_schedule() in Linux Scheduler */
		num_push[index] += rq_push_tasks(&rq);

#ifdef DEBUG	
		fprintf(rq.log, "[%d]:\treleasing lock on runqueue #%d\n", index, index);
#endif

		/* runqueue lock release */
		rq_unlock(&rq);

#ifdef DEBUG
		printf("----Push Data Structure----\n");
		dso->data_print(push_data_struct, NPROCESSORS);
		printf("----Pull Data Structure----\n");
		dso->data_print(pull_data_struct, NPROCESSORS);
#endif

		/* sleep for remaining time in t_period */
		t_sleep = timespec_add(&t_sleep, &t_period);
#ifdef MEASURE_SLEEP
		THREAD_LOCAL_MEASURE_START(sleep, index)
#endif
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t_sleep, NULL);
#ifdef MEASURE_SLEEP
		THREAD_LOCAL_MEASURE_END(sleep, index)
#endif

#ifdef MEASURE_CYCLE
	THREAD_LOCAL_MEASURE_END(cycle, index)
#endif
	}

	/* 
	 * this cpu has finished simulation
	 * therefore global data structures must
	 * detach his node
	 */
	dso->data_preempt(pull_data_struct, index, 0, 0);
	dso->data_preempt(push_data_struct, index, 0, 0);

	/* simulation end barrier */
	__sync_fetch_and_sub(&barrier_count, 1);

	if(barrier_count == 0)
		sem_post(&end_barrier_sem);

	sem_wait(&end_barrier_sem);
	sem_post(&end_barrier_sem);

	/* runqueue destruction */
	rq_lock(&rq);
	
	cpu_to_rq[index] = NULL;
	rq_destroy(&rq);
	rq_unlock(&rq);

	fprintf(log, "\n*****SIMULATION END*****\n");

	fclose(log);

	return 0;
}

/* 
 * checker - thread body of thread that check all data
 * structures integrity
 * @arg: a pointer to a thread argument structure
 */
void *checker(void *arg)
{
	int i, k = 0;
	int count = 0;
	__u64 dline;
	FILE *error_log;

	error_log = fopen("error_log.txt", "w");
	if(!error_log){
		perror("error while opening error log file");
		exit(1);
	}

  while(1) {
		usleep(50000);
		fprintf(stderr, "%d) Checker: OK!\r", ++count);

		/* acquire locks */
		if(k == NPROCESSORS)
			k = 0;
		for(; k < NPROCESSORS; k++){
			/*
			 * we have to wait the processor
			 * for updating cpu_to_rq array
			 * with the runqueue address
			 */
			if(!cpu_to_rq[k])
				break;

			rq_lock(cpu_to_rq[k]);
		}
		/*
		 * some processor hasn't
		 * updated cpu_to_rq,
		 * retry
		 */
		if(k < NPROCESSORS)
			continue;

#ifdef DEBUG
		fprintf(error_log, "*****CHECKER OUTPUT - COUNT %d*****", count);

		for(i = 0; i < NPROCESSORS; i++)
			rq_print(cpu_to_rq[i], error_log);
#endif

		/* check all runqueues */
		for(i = 0; i < NPROCESSORS; i++)
			if(!rq_check(cpu_to_rq[i])){
				fprintf(error_log, "\n***** rq_check found errors on runqueue %d *****\n\n", i);
				rq_print(cpu_to_rq[i], error_log);
				break;
			}

#ifdef DEBUG
		fprintf(error_log, "*****PUSH DATA STRUCTURE****\n");
		dso->data_save(push_data_struct, NPROCESSORS, error_log);
		fprintf(error_log, "*****END PUSH DATA STRUCTURE****\n\n");
		fprintf(error_log, "*****PULL DATA STRUCTURE****\n");
		dso->data_save(pull_data_struct, NPROCESSORS, error_log);
		fprintf(error_log, "*****END PULL DATA STRUCTURE****\n\n");
#endif

		/* check all global data structures */
		if (!dso->data_check(push_data_struct, NPROCESSORS)){
			fprintf(error_log, "\n***** data_check found errors on PUSH DATA STRUCTURE *****\n\n");
			break;
		}
		if (!dso->data_check(pull_data_struct, NPROCESSORS)){
			fprintf(error_log, "\n***** data_check found errors on PULL DATA STRUCTURE *****\n\n");
			break;
		}

		/* check runqueues and global data structures consistency */
		for(i = 0; i < NPROCESSORS; i++){
			dline = cpu_to_rq[i]->earliest;
			if(!dso->data_check_cpu(push_data_struct, i, dline)){
				fprintf(error_log, "\n***** data_check_cpu found errors on PUSH DATA STRUCTURE for runqueue #%d *****\n\n", i);
				break;
			}
			dline = cpu_to_rq[i]->next;
			if(!dso->data_check_cpu(pull_data_struct, i, dline)){
				fprintf(error_log, "\n***** data_check_cpu found errors on PULL DATA STRUCTURE for runqueue #%d *****\n\n", i);
				break;
			}
		}

		/* release locks */
		for(i = 0; i < NPROCESSORS; i++)
			rq_unlock(cpu_to_rq[i]);

#ifdef DEBUG
		fprintf(error_log, "*****END OUTPUT*****\n\n");
#endif

		fflush(error_log);
		sync();
	}

	fclose(error_log);
	exit(1);

	return NULL;
}

/*
 * parse_user_options - parse command line arguments
 * @argc: arguments count
 * @argv: arguments vector 
 */
data_struct_t parse_user_options(int argc, char **argv)
{
	data_struct_t data_type = HEAP;
	int c;

	if (argc < 2) {
		printf("usage: test_concurrent OPTION\n"
			"\n\tOPTION:\n"
			"\t  -a array_heap\n"
			"\t  -h heap\n"
			"\t  -s skiplist\n"
			"\t  -f flat_combining_skiplist\n"
			"\t  -f bitmap_flat_combining_skiplist\n");
		exit(-1);
	}
	while ((c = getopt(argc, argv, "hasfb")) != -1)
		switch (c) {
			case 'h':
				data_type = HEAP;
				dso = &heap_ops;
				push_data_struct = &push_heap;
				pull_data_struct = &pull_heap;
				break;
			case 'a':
				data_type = ARRAY_HEAP;
				dso = &array_heap_ops;
				push_data_struct = &push_array_heap;
				pull_data_struct = &pull_array_heap;
				break;
			case 's':
				data_type = SKIPLIST;
				dso = &dl_skiplist_ops;
				push_data_struct = &push_dl_skiplist;
				pull_data_struct = &pull_dl_skiplist;
				break;
			case 'f':
				data_type = FC_SKIPLIST;
				dso = &fc_dl_skiplist_ops;
				push_data_struct = &push_fc_skiplist;
				pull_data_struct = &pull_fc_skiplist;
				break;
			case 'b':
				data_type = BM_FC_SKIPLIST;
				dso = &bm_fc_skiplist_ops;
				push_data_struct = &push_bm_fc_skiplist;
				pull_data_struct = &pull_bm_fc_skiplist;
				break;
			default:
				printf("data_type is not valid!\n");
				exit(-1);
		}

	return data_type;
}

int main(int argc, char **argv)
{
#ifndef MEASURE
    pthread_t check;
#endif
    data_struct_t data_type;
    int ind[NPROCESSORS];
    int i;

#ifdef MEASURE
		set_tsc_cost();
#endif

    signal(SIGINT, signal_handler);
    srand(time(NULL));

    data_type = parse_user_options(argc, argv);

    switch (data_type) {
	    case HEAP:
    		printf("Initializing the heap\n");
		break;
	    case ARRAY_HEAP:
		dso->data_init(push_data_struct, NPROCESSORS, __dl_time_before);
		dso->data_init(pull_data_struct, NPROCESSORS, __dl_time_after);
    		printf("Initializing the array_heap\n");
		break;
	    case SKIPLIST:
		dso->data_init(push_data_struct, NPROCESSORS, __dl_time_after);
		dso->data_init(pull_data_struct, NPROCESSORS, __dl_time_before);
		printf("Initializing the skiplist\n");
		break;
	    case FC_SKIPLIST:
		dso->data_init(push_data_struct, NPROCESSORS, __dl_time_after);
		dso->data_init(pull_data_struct, NPROCESSORS, __dl_time_before);
		printf("Initializing the flat_combining_skiplist\n");
		break;
			case BM_FC_SKIPLIST:
		dso->data_init(push_data_struct, NPROCESSORS, __dl_time_after);
		dso->data_init(pull_data_struct, NPROCESSORS, __dl_time_before);
		printf("Initializing the bitmap_flat_combining_skiplist\n");
		break;
	    default:
		exit(-1);
    }

#ifndef MEASURE
    printf("Creating Checker\n");

    pthread_create(&check, 0, checker, 0);
#endif

    printf("Creating processors\n");

		barrier_count = 0;
		sem_init(&start_barrier_sem, 0, 0);
		sem_init(&end_barrier_sem, 0, 0);

    for (i = 0; i < NPROCESSORS; i++) {
        ind[i] = i;
        pthread_create(&threads[i], 0, processor, &ind[i]);
    }

    printf("Waiting for the end\n");

    for (i = 0; i < NPROCESSORS; i++) {
        pthread_join(threads[i], 0);
				printf("+++++++++++++++++++++++++++++++++\n");
        printf("Num Arrivals [%d]: %d\n", i, num_arrivals[i]);
        printf("Num Preemptions [%d]: %d\n", i, num_preemptions[i]);
        printf("Num Finishings [%d]: %d\n", i, num_finish[i]);
        printf("Num Early Finishings [%d]: %d\n", i, num_early_finish[i]);
        printf("Num queue empty events  [%d]: %d\n", i, num_empty[i]);
				printf("Num Push from runqueue [%d]: %d\n", i, num_push[i]);
				printf("Num Pull to runqueue [%d]: %d\n", i, num_pull[i]);
    }
    printf("--------------EVERYTHING OK!---------------------\n");
    
		dso->data_cleanup(push_data_struct);
		dso->data_cleanup(pull_data_struct);

		sem_destroy(&start_barrier_sem);
		sem_destroy(&end_barrier_sem);

#ifdef MEASURE
		printf("TSC read costs %llu cycles\n\n", get_tsc_cost());
#endif

#ifdef MEASURE_PUSH_FIND
		COMMON_MEASURE_PRINT(push_find);
		COMMON_MEASURE_OUTCOME_PRINT(push_find);
		printf("\n");
#endif

#ifdef MEASURE_PULL_FIND
		COMMON_MEASURE_PRINT(pull_find);
		COMMON_MEASURE_OUTCOME_PRINT(pull_find);
		printf("\n");
#endif

#ifdef MEASURE_PUSH_PREEMPT
		COMMON_MEASURE_PRINT(push_preempt);
		printf("\n");
#endif

#ifdef MEASURE_PUSH_PREEMPT
		COMMON_MEASURE_PRINT(pull_preempt);
		printf("\n");
#endif

#ifdef MEASURE_FIND_LOCK
		COMMON_MEASURE_PRINT(find_lock);
		printf("\n");
#endif

#ifdef MEASURE_FIND_UNLOCK
		COMMON_MEASURE_PRINT(find_unlock);
		printf("\n");
#endif

#ifdef MEASURE_CYCLE
		THREAD_LOCAL_MEASURE_PRINT(cycle);
		printf("\n");
#endif

#ifdef MEASURE_SLEEP
		THREAD_LOCAL_MEASURE_PRINT(sleep);
		printf("\n");
#endif

    return 0;
}
