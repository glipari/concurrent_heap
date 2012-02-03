#include "heap.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <signal.h>

//#define VERBOSE 

#define NPROCESSORS    8    
#define NCYCLES        1000000
#define DMIN           10
#define DMAX           100
#define WAITCYCLE      10000

#ifdef VERBOSE
#define PRINT_OP(i, op, dline) printf("%d) %s, dline %d\n", i, op, dline)
#else 
#define PRINT_OP(i, op, dline) 
#endif

heap_t heap;
pthread_t threads[NPROCESSORS];

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

int arrival_process(int curr_clock)
{
    return curr_clock + (rand() % (DMAX - DMIN)) + DMIN; 
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

    int curr_deadline = 0;
    int curr_clock = 0;
    
    for (i=0; i<NCYCLES; i++) {
        operation_t op = select_operation();
        switch(op) {
        case ARRIVAL: 
        {
            int dline = arrival_process(curr_clock);
            //if (dline < curr_clock) break;           
            PRINT_OP(index, "arrival", dline);
            int res;
            int proc = -1;
            do {
                res = 1;
                node_t *pn = heap_get_max_node(&heap);
                int latest = pn->deadline;
                proc = pn->proc_index;
                if (dline < latest)  
                    res = heap_preempt(&heap, proc, dline);
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
            curr_clock = PNODE_DLINE((&heap), index);
            if (curr_clock < DLINE_MAX) {
                curr_deadline = arrival_process(curr_clock);
                if (curr_deadline < curr_clock) break;
                
                PRINT_OP(index, "finishing", curr_deadline);
                heap_finish(&heap, index, curr_deadline);
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
}

void *checker(void *arg)
{
    int flag;
    int count = 0;
    while(1) {
        flag = heap_check(&heap);
        if (!flag) {
            // lock has not been released!
            printf("Errore!!!\n");
            FILE *myfile = fopen("error_heap.txt", "w");
            heap_save(&heap, myfile);
            fclose(myfile);
            exit(-1);
        }
        else 
            // lock released
            printf("%d) Checker: OK!\r", count++);
        usleep(10);
    }
}

int main()
{
    pthread_t check;
    int ind[NPROCESSORS];
    int i;

    signal(SIGINT, signal_handler);

    printf("Initializing the heap\n");
    
    heap_init(&heap, NPROCESSORS);
    
    printf("Creating processors\n");

    for (i=0; i<NPROCESSORS; i++) {
        ind[i] = i;
        pthread_create(&threads[i], 0, processor, &ind[i]);
    }

    printf("Creating Checker\n");

    pthread_create(&check, 0, checker, 0);

    printf("Waiting for the end\n");

    for (i=0; i<NPROCESSORS; i++) {
        pthread_join(threads[i], 0);
        printf("Num Preemptions [%d]: %d\n", i, num_preemptions[i]);
        printf("Num Finishings  [%d]: %d\n", i, num_finish[i]);
    }
    printf("--------------EVERYTHING OK!---------------------\n");
    
}
