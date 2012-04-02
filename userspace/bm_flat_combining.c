/* ffsll() feature test macro */
#define _GNU_SOURCE

#include <linux/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <strings.h>
#include <string.h>

#include "bm_flat_combining.h"

/* bitmap management helper functions */
inline void bitmap64_set(int64_t *bitmap, int n){	
	*bitmap |= ((int64_t)1 << n);
}

inline void bitmap32_set(int32_t *bitmap, int n){	
	*bitmap |= ((int32_t)1 << n);
}

inline void bitmap64_clear(int64_t *bitmap, int n){
	*bitmap &= ~((int64_t)1 << n);
}

inline void bitmap32_clear(int32_t *bitmap, int n){
	*bitmap &= ~((int32_t)1 << n);
}

inline int bitmap64_test(int64_t *bitmap, int n){
	return ((*bitmap & ((int64_t)1 << n)) > (int64_t)0);
}

inline int bitmap32_test(int32_t *bitmap, int n){
	return ((*bitmap & ((int32_t)1 << n)) > (int32_t)0);
}

inline int bitmap64_ffs(int64_t *bitmap){
	return ffsll(*bitmap) - 1;
}

inline int bitmap32_ffs(int32_t *bitmap){
	return ffs(*bitmap) - 1;
}

inline void bitmap64_print(int64_t *bitmap, FILE *out){
	fprintf(out, "Bitmap 64:\t");
	fprintf(out, "%lld\t", *bitmap);
	fprintf(out, "\n");
}

inline void bitmap32_print(int32_t *bitmap, FILE *out){
	fprintf(out, "Bitmap 32:\t");
	fprintf(out, "%d\t", *bitmap);
	fprintf(out, "\n");
}

/* data structure lock */
#define DS_LOCK_LOCKED				1
#define DS_LOCK_UNLOCKED			0

struct data_structure_lock{
	int lock;
};

/* data structure lock interface */
void fc_lock(struct data_structure_lock *ds_lock)
{
	while(!__sync_bool_compare_and_swap(&ds_lock->lock, 0, 1))
		;
}

int fc_trylock(struct data_structure_lock *ds_lock)
{
	if(__sync_bool_compare_and_swap(&ds_lock->lock, 0, 1))
		return 0;
	else
		return -1;
}

void fc_unlock(struct data_structure_lock *ds_lock)
{
	ds_lock->lock = 0;
	__sync_synchronize();
}

/* publication record list */
struct pub_list{
	/* publisher CPUs bitmap*/
	int64_t cpu_bitmap;
	/* active publication records bitmap */
	int32_t rec_bitmap[NPROCESSORS];
	/* publication record array*/
	struct pub_record rec_array[NPROCESSORS * PUB_RECORD_PER_CPU];
	/* last used per CPU publication record index */
	int last_used_idx[NPROCESSORS];
};

/* flat combining helper structure */
struct flat_combining{
	/* concurrent data struture */
	void *data_structure;
	/* publication list */
	struct pub_list map;
	/* data structure lock */
	struct data_structure_lock ds_lock;
};

/* flat combining interface */
static void fc_do_combiner(struct flat_combining *fc)
{
	struct pub_list *map = &fc->map;
	struct pub_record *rec;
	int cpu_index, rec_index;

	while((cpu_index = bitmap64_ffs(&map->cpu_bitmap)) >= 0){
		while((rec_index = bitmap32_ffs(&map->rec_bitmap[cpu_index])) >= 0){
			rec = &map->rec_array[cpu_index * PUB_RECORD_PER_CPU + rec_index];
			switch(rec->req){
				case PREEMPT:
					rec->h.preempt_h.function(fc->data_structure, rec->par.preempt_p.cpu, rec->par.preempt_p.dline, rec->par.preempt_p.is_valid);
					break;
			}
			bitmap32_clear(&map->rec_bitmap[cpu_index], rec_index);
		}
		bitmap64_clear(&map->cpu_bitmap, cpu_index);
	}
}

struct flat_combining *fc_create(void *data_structure)
{
	struct flat_combining *fc;

	fc = (struct flat_combining *)calloc(1, sizeof(*fc));
	fc->ds_lock.lock = DS_LOCK_UNLOCKED;
	fc->data_structure = data_structure;

	return fc;
}

int fc_destroy(struct flat_combining *fc)
{
	if(fc){
		free(fc);	
		return 0;
	}

	return -1;
}

struct pub_record *fc_get_record(struct flat_combining *fc, const int cpu)
{
	struct pub_list *map = &fc->map;
	int idx_to_use;

	/* next publication record to use */
	idx_to_use = map->last_used_idx[cpu];

	while(1){
		/* if not busy we use it */
		if(!bitmap32_test(&map->rec_bitmap[cpu], idx_to_use))
			return &map->rec_array[cpu * PUB_RECORD_PER_CPU + idx_to_use];

		/* no free record: 
		 * set bit in cpu_bitmap then 
		 * spin to become a combiner 
		 */
		while(bitmap32_test(&map->rec_bitmap[cpu], idx_to_use)){
			bitmap64_set(&map->cpu_bitmap, cpu);
			__sync_synchronize();
			fc_try_combiner(fc);
		}
	}

}

void fc_publish_record(struct flat_combining *fc, const int cpu)
{
	struct pub_list *map = &fc->map;
	int idx_to_use;

	idx_to_use = map->last_used_idx[cpu];
	map->last_used_idx[cpu] = (map->last_used_idx[cpu] + 1) % PUB_RECORD_PER_CPU;

	bitmap32_set(&map->rec_bitmap[cpu], idx_to_use);
	bitmap64_set(&map->cpu_bitmap, cpu);
}

void fc_try_combiner(struct flat_combining *fc)
{
	if(!fc_trylock(&fc->ds_lock)){
		fc_do_combiner(fc);
		fc_unlock(&fc->ds_lock);
	}
}

void fc_data_structure_lock(struct flat_combining *fc)
{
	fc_lock(&fc->ds_lock);
}

void fc_data_structure_unlock(struct flat_combining *fc)
{
	fc_unlock(&fc->ds_lock);
}

void fc_print_publication_list(struct flat_combining *fc, FILE *out)
{
	struct pub_list *map = &fc->map;
	int i;

	if(!out)
		return;

	bitmap64_print(&map->cpu_bitmap, out);
	for(i = 0; i < NPROCESSORS; i++){
		fprintf(out, "[%d] ", i);
		bitmap32_print(&map->rec_bitmap[i], out);
	}

	fprintf(out, "\n");
}
