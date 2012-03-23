#include <stdlib.h>
#include <stdio.h>

#include "flat_combining.h"

static pub_record *pub_list_reverse(pub_record *head){
	pub_record *reversed_list, *i, *j;

	reversed_list = NULL;
	i = head;
	while(i){
		j = i->next;
		i->next = reversed_list;
		reversed_list = i;
		i = j;
	}

	return reversed_list;
}

pub_record *create_publication_record(){
	pub_record *r = (pub_record *)calloc(1, sizeof(*r));

	return r;
}

int destroy_publication_record(pub_record *rec){
	if(rec)
		free(rec);
	else
		return -1;

	return 0;
}

int enqueue_publication_record(pub_list *queue, pub_record *r){
	if(!queue || !r)
		return -1;

	pub_record *old_head;
	while(1){
		old_head = queue->head;
		r->next = old_head;
		if(__sync_bool_compare_and_swap(&queue->head, old_head, r))
			break;
	}

	return 0;
}

pub_record *dequeue_all_publication_record(pub_list *queue){
	pub_record *old_head;

	if(!queue)
		return NULL;

	while(1){
		old_head = queue->head;
		if(__sync_bool_compare_and_swap(&queue->head, old_head, NULL))
			break;
	}

	return pub_list_reverse(old_head);
}

pub_list *create_publication_list(){
	pub_list *list = (pub_list *)calloc(1, sizeof(*list));

	return list;
}

int destroy_publication_list(pub_list *queue){
	pub_record *list, *i, *j;

	if(!queue)
		return -1;

	list = dequeue_all_publication_record(queue);

	i = list;
	while(i){
		j = i->next;
		destroy_publication_record(i);
		i = j;
	}

	free(queue);

	return 1;		
}

void print_publication_list(pub_record *head, FILE *out){
	pub_record *i;

	if(!out)
		return;

	fprintf(out, "Publication List:\n");

	if(!head)
		fprintf(out, "empty queue!");

	for(i = head; i; i = i->next)
		fprintf(out, "request: %u ready: %d\t", i->req, i->ready);

	fprintf(out, "\n\n");
}

#if 0
int try_combiner(int *data_structure_lock){
	int lock_value = *data_structure_lock;

	if(lock_value && __sync_bool_compare_and_swap(data_structure_lock, lock_value, 0)){
		__sync_synchronize();
		return 1;
	}
	
	return 0;
}

void release_combiner_lock(int *data_structure_lock){
	if(!__sync_bool_compare_and_swap(data_structure_lock, 0, 1)){
		fprintf(stderr, "never should be here!\n");
		exit(1);
	}
	__sync_synchronize();
}
#endif
