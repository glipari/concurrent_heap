#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <signal.h>
#include <unistd.h>
#include "heap.h"
#include "array_heap.h"
#include "common_ops.h"

//#define VERBOSE 

#define NPROCESSORS    8    
#define NCYCLES        1000000
#define DMIN           10
#define DMAX           100
#define WAITCYCLE      10000

#ifdef VERBOSE
#define PRINT_OP(i, op, dline) printf("%d) %s, dline %llu (%d)\n", i, op, dline.value, dline.special)
#else 
#define PRINT_OP(i, op, dline) 
#endif

heap_t heap;
array_heap_t array_heap;
pthread_t threads[NPROCESSORS];
struct data_struct_ops *dso;
extern struct data_struct_ops array_heap_ops;
extern struct data_struct_ops heap_ops;

typedef enum {HEAP=0, ARRAY_HEAP=1, SKIPLIST=2} data_struct_t;
typedef enum {ARRIVAL=0, FINISH=1, SLEEP=2} operation_t;
double prob[3] = {.1, .2, 1}; // 10% probability of new arrival, 10% of finishing the job, 80% of sleeping for 1 usec

operation_t select_operation()
{
    operation_t i = 0;
    double p = ((double)rand()) / (double)INT_MAX;
    for (i=ARRIVAL; i<SLEEP+1; i++) {
        if (p<prob[i]) return i;
    }
    return SLEEP;
}

dline_t arrival_process(dline_t curr_clock)
{
    dline_t tmp = curr_clock;
    tmp.special = DL_NORMAL;
    tmp.value +=  (rand() % (DMAX - DMIN)) + DMIN;

    return tmp;
}

int num_preemptions[NPROCESSORS];
int num_finish[NPROCESSORS];

void signal_handler(int sig)
{
    int i;
    printf("\nEXITING!\n");
    heap_print(&heap);
    for (i=0; i<NPROCESSORS; i++) 
        printf("Index %d, ID %ld\n", i, (threads[i] % 100));
    exit(-1);
}

void *processor(void *arg)
{
    int index = *((int*)arg);
    int i;

    dline_t curr_deadline = DLINE_MIN;
    dline_t curr_clock = DLINE_MIN;
    
    for (i=0; i<NCYCLES; i++) {
        operation_t op = select_operation();
        switch(op) {
        case ARRIVAL:
        {
            dline_t dline = arrival_process(curr_clock);
            PRINT_OP(index, "arrival", dline);
            int res;
            int proc = -1;
            do {
                res = 1;
                node_t *pn = heap_get_max_node(&heap);
                dline_t latest = pn->deadline;
                proc = pn->proc_index;
                if (dl_time_before(dline, latest))  
                    //res = heap_preempt(&heap, proc, dline.value);
                    res = dso->data_set(&heap, proc, dline.value);
            } while (res == 0);
            if (proc == index) {
                curr_clock = curr_deadline;
                curr_deadline = dline;
            }
            num_preemptions[index] += res; 
            break;
        }
        case FINISH:
        {
            PRINT_OP(index, "finishing", curr_deadline);
            curr_clock = PNODE_DLINE((&heap), index);
            if (dl_time_before(curr_clock, DLINE_MAX)) {
                curr_deadline = arrival_process(curr_clock);
                if (heap_finish(&heap, index, curr_deadline)) 
                    curr_clock = curr_deadline;
                else 
                    curr_deadline = heap_get_max_node(&heap)->deadline;
                num_finish[index]++;
            }
            
            break;
        }
        case SLEEP:
        {
            unsigned long k;
            unsigned long delay = rand() % WAITCYCLE;
            for (k=0;k < delay;k++);  
            break;
        }
        default: 
            printf("???? Operation unkown");
            exit(-1);
        }
    }
    return 0;
}

void *checker(void *arg)
{
    int flag;
    int count = 0;
    data_struct_t* pdata_type = (data_struct_t*) arg;

    while(1) {
	    switch (*pdata_type) {
		    case HEAP:
        		//flag = heap_check(&heap);
        		flag = dso->data_check(&heap);
        		if (!flag) {
            			// lock has not been released!
            			printf("Errore!!!\n");
            			FILE *myfile = fopen("error_heap.txt", "w");
            			heap_save(&heap, myfile);
            			fclose(myfile);
            			exit(-1);
			}
			break;
		    case ARRAY_HEAP:
        		flag = array_heap_check(&array_heap);
        		if (!flag) {
            			// lock has not been released!
            			printf("Errore!!!\n");
            			FILE *myfile =
					fopen("error_array_heap.txt", "w");
            			array_heap_save(&array_heap, myfile);
            			fclose(myfile);
            			exit(-1);
			}
			break;
		    case SKIPLIST:
			printf("skiplist checker..\n");
			break;
		    default:
			exit(-1);
        }
        // lock released
        printf("%d) Checker: OK!\r", count++);
        usleep(10);
    }
}

data_struct_t parse_user_options(int argc, char **argv)
{
	data_struct_t data_type = HEAP;
	int c;

	while ((c = getopt(argc, argv, "has")) != -1)
		switch (c) {
			case 'h':
				data_type = HEAP;
				dso = &heap_ops;
				break;
			case 'a':
				data_type = ARRAY_HEAP;
				dso = &array_heap_ops;
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


    data_type = parse_user_options(argc, argv);

    switch (data_type) {
	    case HEAP:
    		printf("Initializing the heap\n");
    		//heap_init(&heap, NPROCESSORS);
		dso->data_init(&heap, NPROCESSORS);
		break;
	    case ARRAY_HEAP:
    		printf("Initializing the array heap\n");
		//array_heap_init(&array_heap, NPROCESSORS);
		dso->data_init(&array_heap, NPROCESSORS);
		break;
	    case SKIPLIST:
		printf("Not yet implemented!\n");
		exit(-1);
	    default:
		exit(-1);
    }
    
    printf("Creating Checker\n");

    pthread_create(&check, 0, checker, &data_type);

    //sleep(20);

    printf("Creating processors\n");

    for (i=0; i<NPROCESSORS; i++) {
        ind[i] = i;
        pthread_create(&threads[i], 0, processor, &ind[i]);
    }

    printf("Waiting for the end\n");

    for (i=0; i<NPROCESSORS; i++) {
        pthread_join(threads[i], 0);
        printf("Num Preemptions [%d]: %d\n", i, num_preemptions[i]);
        printf("Num Finishings  [%d]: %d\n", i, num_finish[i]);
    }
    printf("--------------EVERYTHING OK!---------------------\n");
    
    return 0;
}
