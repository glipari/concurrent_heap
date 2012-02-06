#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "heap.h"

heap_t myheap;

dline_t ask_deadline() {
    dline_t deadline;
    int x;
    printf("Insert deadline.value and deadline.special (0 = min, 1 = normal, 2 = max): ");
    scanf("%llu", &deadline.value);
    scanf("%d", &x);
    deadline.special = x;
    return deadline;
}

void dline_insert()
{
    dline_t deadline = ask_deadline();
    int p = heap_get_max_proc(&myheap);
    if (dl_time_before(heap_get_max_dline(&myheap), deadline))
        printf("Too large!\n");
    else {
        heap_preempt(&myheap, p, deadline);
        heap_print(&myheap);
        if (!heap_check(&myheap)) exit(-1);
    }
}


void dline_update()
{
    printf("Select a processor (0-7): ");
    int proc;
    scanf("%d", &proc);
    if (proc >7 || proc <0) 
        printf("Wrong processor!\n");
    else {
        dline_t deadline = ask_deadline();

        if (dl_time_before(deadline, myheap.nodes[proc].deadline)) 
            printf("deadline too short\n");
        else {
            heap_finish(&myheap, proc, deadline);
            heap_print(&myheap);
            if (!heap_check(&myheap)) exit(-1);
        }
    }
}

void print_menu()
{
    printf("Select Operation: (1,p) preempt (2,f) finish (3,s) save (4,l) load (5,q) quit\n");
}

void save()
{
    char str[100];
    printf("Saving, enter file name: ");
    scanf("%s", str);
    struct stat s;
    if (stat(str, &s) == 0) {
        printf("File exists, do you want to overwrite (s/n)?");
        char r = getchar();
        if (r != 's') return;
    }
    FILE *myfile = fopen(str, "w");
    heap_save(&myheap, myfile);
    fclose(myfile);
}

void load() 
{
    char str[100];
    struct stat s;

    printf("Loading, enter file name : ");
    scanf("%s", str);
    if (stat(str, &s) != 0) {
        printf("File does not exist\n");
        return;
    }
    heap_delete(&myheap);
    FILE *myfile = fopen(str, "r");
    heap_load(&myheap, myfile);
    fclose(myfile);
    heap_print(&myheap);
}

int main()
{
    heap_init(&myheap, 8);
    heap_print(&myheap);
    char selection = 0;
    
    while (1) {
        print_menu();
        selection = getchar();
        switch (selection) {
        case '1':
        case 'p':
            dline_insert();
            break;
        case '2':
        case 'f':
            dline_update();
            break;
        case '3' : 
        case 's' : 
            save();
            break;
        case '4' :
        case 'l' : 
            load();
            break;
        case '5' :
        case 'q' :
            printf("Exit!");
            return 0;
        default:
            break;
        }
        getchar();
    }        
}
