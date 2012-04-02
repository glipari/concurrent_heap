#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <linux/types.h>
#include <time.h>
#include <pthread.h>

#include "common_ops.h"
#include "parameters.h"
#include "bm_flat_combining.h"
#include "bm_fc_skiplist.h"

/* skiplist parameters */
#define	MAX_LEVEL           8
#define LEVEL_PROB_VALUE    0.20
#define OUT_OF_LIST					-1
#define NO_CACHED_CPU				-1

struct fc_sl_node{
	/* task deadline */
	__u64 dline;
	/* node level */
	int level;
	/* pointers to next and previous nodes */
	struct fc_sl_node *next[MAX_LEVEL];
	struct fc_sl_node *prev[MAX_LEVEL];
	/* CPU index */
	unsigned int cpu_idx;
};

static inline unsigned int sl_rand_level(unsigned int *seed, unsigned int max)
{
	unsigned int level = 0;

	max = max > MAX_LEVEL - 1 ? MAX_LEVEL - 1 : max;

	while(rand_r(seed) > ((1 - LEVEL_PROB_VALUE) * RAND_MAX) && level < max)
		level++;

	return level;
}

static __u64 sl_detach(fc_sl_t *list, struct fc_sl_node *p)
{
	unsigned int i;

	for(i = 0; i <= p->level; i++){
		p->prev[i]->next[i] = p->next[i];
		if(p->next[i])
			p->next[i]->prev[i] = p->prev[i];
	}

	while(!list->head->next[list->level] && list->level > 0)
		list->level--;

	p->level = OUT_OF_LIST;

	return p->dline;
}

static inline __u64 sl_remove_idx(fc_sl_t *list, const unsigned int cpu_idx)
{
	struct fc_sl_node *p;

	p = list->cpu_to_node[cpu_idx];

	if(p->level == OUT_OF_LIST)
		return 0;

	return sl_detach(list, p);
}

static void sl_insert(fc_sl_t *list, const unsigned int cpu_idx, __u64 dline)
{
	struct fc_sl_node *p;
	struct fc_sl_node *update[MAX_LEVEL];
	struct fc_sl_node *new_node;
	int cmp_res, level;
	unsigned int i, rand_level;

	new_node = list->cpu_to_node[cpu_idx];
	new_node->dline = dline;

	p = list->head;
	level = list->level;
	while(level >= 0){
		update[level] = p;

		if(!p->next[level]){
			level--;
			continue;
		}
		
		cmp_res = list->cmp_dl(p->next[level]->dline, new_node->dline);
		if(cmp_res > 0)
			p = p->next[level];
		else
			level--;
	}

	rand_level = sl_rand_level(&list->seed, list->level + 1);
	new_node->level = rand_level;
	if(rand_level > list->level)
		update[++list->level] = list->head;

	for(i = 0; i <= rand_level; i++){
		new_node->next[i] = update[i]->next[i];
		update[i]->next[i] = new_node;
		new_node->prev[i] = update[i];
		if(new_node->next[i])
			new_node->next[i]->prev[i] = new_node;
	}
}

/*
 * since we have to do different things if is_valid
 * is set or cleared, we need one more level of
 * indirection to call the correct handler function
 */
static void sl_dispatcher(void *s, int cpu, __u64 dline, int is_valid)
{
	sl_remove_idx((fc_sl_t *)s, cpu);

	if(is_valid)
		sl_insert((fc_sl_t *)s, cpu, dline);
}

void fc_sl_init(void *s, int nproc, int (*cmp_dl)(__u64 a, __u64 b))
{
	fc_sl_t *p = (fc_sl_t *)s;
	unsigned int i;

	p->cmp_dl = cmp_dl;
	p->head = (struct fc_sl_node *)calloc(1, sizeof(*p->head));
	p->seed = time(NULL);
	p->cpu_to_node = (struct fc_sl_node **)calloc(nproc, sizeof(*p->cpu_to_node));

	/* nodes preallocation */
	for(i = 0; i < nproc; i++){
		p->cpu_to_node[i] = (struct fc_sl_node *)calloc(1, sizeof(*p->cpu_to_node[i]));
		p->cpu_to_node[i]->level = OUT_OF_LIST;
		p->cpu_to_node[i]->cpu_idx = i;
	}

	p->cpu_num = nproc;
	p->cached_cpu = NO_CACHED_CPU;

	/* flat combining inizialization */
	p->fc = fc_create(p);
}

void fc_sl_cleanup(void *s)
{
	fc_sl_t *p = (fc_sl_t *)s;
	unsigned int i;

	for(i = 0; i < p->cpu_num; i++)
		free(p->cpu_to_node[i]);
	free(p->cpu_to_node);
	free(p->head);
	
	fc_destroy(p->fc);
}

int fc_sl_preempt(void *s, int cpu, __u64 dline, int is_valid)
{
	fc_sl_t *p = (fc_sl_t *)s;
	struct pub_record *rec;

	/* 
	 * if is_valid is set we may have 
	 * to update the cached CPU 
	 */
	if(is_valid && (p->cached_cpu == NO_CACHED_CPU || p->cmp_dl(dline, p->cpu_to_node[p->cached_cpu]->dline)))
		while(!__sync_bool_compare_and_swap(&p->cached_cpu, p->cached_cpu, cpu))
			;

	/*
	 * if is_valid is clear we may have
	 * to clear the cached CPU
	 */
	if(!is_valid && p->cached_cpu == cpu)
		while(!__sync_bool_compare_and_swap(&p->cached_cpu, p->cached_cpu, NO_CACHED_CPU))
			;

	rec = fc_get_record(p->fc, cpu);
	rec->req = PREEMPT;
	rec->par.preempt_p.cpu = cpu;
	rec->par.preempt_p.dline = dline;
	rec->par.preempt_p.is_valid = is_valid;
	rec->h.preempt_h.function = sl_dispatcher;
	fc_publish_record(p->fc, cpu);

	return 1;
}

int fc_sl_find(void *s)
{
	fc_sl_t *p = (fc_sl_t *)s;
	struct fc_sl_node *node;
	int candidate_cpu;

	/* read cached CPU*/
	candidate_cpu = p->cached_cpu;
	if(candidate_cpu == NO_CACHED_CPU){
		/* best CPU from data structure */
		node = p->head->next[0];
		if(node)
			candidate_cpu = node->cpu_idx;
	}

	return candidate_cpu;
}

/* FIXME */
void fc_sl_load(void *s, FILE *f){
	fc_sl_t *p = (fc_sl_t *)s;

	fc_data_structure_lock(p->fc);
	fc_data_structure_unlock(p->fc);
}

void fc_sl_save(void *s, int nproc, FILE *f){
	fc_sl_t *p = (fc_sl_t *)s;
	struct fc_sl_node *node;
	int i;

	fc_data_structure_lock(p->fc);

	fprintf(f, "\n----Skiplist----\n");

	for(i = p->level; i >= 0; i--){
		fprintf(f, "%u:\t", i);
		for(node = p->head->next[i]; node; node = node->next[i])
			fprintf(f, "%llu ", node->dline);
		fprintf(f, "\n");
	}

	for(i = 0; i < p->cpu_num; i++)
		if(p->cpu_to_node[i]->level == OUT_OF_LIST)
			fprintf(f, "[%d]:\tout of list\n", i);
		else
			fprintf(f, "[%d]:\t%llu\n", i, p->cpu_to_node[i]->dline);
	fprintf(f, "\n");

	fprintf(f, "----End Skiplist----\n\n");

	fc_data_structure_unlock(p->fc);
}

void fc_sl_print(void *s, int nproc){
	fc_sl_save(s, 0, stdout);
}

int fc_sl_check(void *s, int nproc){
	fc_sl_t *p = (fc_sl_t *)s;
	struct fc_sl_node *node, *next_node, *prev_node;
	unsigned int i, max_level = 0;
	int flag = 1;

	/* to check we need to obtain a lock on data structure */
	fc_data_structure_lock(p->fc);

	/* skiplist levels number check */
	for(i = 0; i < MAX_LEVEL; i++)
		if(p->head->next[i] != NULL)
			max_level = i;
	if(max_level != p->level){
		fprintf(stderr, "ERROR: skiplist levels number\n");
		fprintf(stderr, "list->level: %u max_level: %u\n", p->level, max_level);
		for(i = 0; i < MAX_LEVEL; i++)
			printf("level %u: %p\n", i, p->head->next[i]);
		flag = 0;	
	}

	/* correct ordering check */

	/* forward check */
	for(i = 0; i < p->level; i++){
		if(!(node = p->head->next[i]))
			continue;
		
		while((next_node = node->next[i])){
			if(p->cmp_dl(node->dline, next_node->dline) < 0){
				fprintf(stderr, "ERROR: forward check failed (level: %u) on nodes prev: %llu and next: %llu\n", i, node->dline, next_node->dline);
				flag = 0;
			}
			node = next_node;
		}
	}

	/* backward check */
	for(i = 0; i < p->level; i++){
		if(!(node = p->head->next[i]))
			continue;

		/* we reach the last node */
		while(node->next[i])
			node = node->next[i];
			
		/* check */
		while((prev_node = node->prev[i])){
			if(p->cmp_dl(prev_node->dline, node->dline) < 0){
				fprintf(stderr, "ERROR: backward check failed (level: %u) on nodes prev: %llu and next: %llu\n", i, node->dline, next_node->dline);
				flag = 0;
			}
			node = prev_node;
		}
	}

	fc_data_structure_unlock(p->fc);

	return flag;
}

/*
 * since the update of the data structure
 * is deferred, there's no guarantee that
 * the node in it is updated, so this check
 * makes no sense in case of asynchronous
 * flat combining
 */
int fc_sl_check_cpu(void *s, int cpu, __u64 dline){
	return 1;
#if 0
	fc_sl_t *p = (fc_sl_t *)s;
	struct fc_sl_node *node;
	int flag = 1;

	/* to check we need to obtain a lock on data structure */
	fc_data_structure_lock(p->fc);

	node = p->cpu_to_node[cpu];
	if(!node)
		return 0;
	
	if(!dline && node->level != OUT_OF_LIST)
		flag = 0;

	if(dline > 0 && dline != node->dline)
		flag = 0;
	
	fc_data_structure_unlock(p->fc);

	return flag;
#endif
}

/* abstract functions mapping struct */
const struct data_struct_ops bm_fc_skiplist_ops = {
	.data_init = fc_sl_init,
	.data_cleanup = fc_sl_cleanup,
	.data_preempt = fc_sl_preempt,
	.data_finish = fc_sl_preempt,
	.data_find = fc_sl_find,
	.data_max = fc_sl_find,
	.data_load = fc_sl_load,
	.data_save = fc_sl_save,
	.data_print = fc_sl_print,
	.data_check = fc_sl_check,
	.data_check_cpu = fc_sl_check_cpu
};
