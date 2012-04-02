#ifndef __DL_SKIPLIST_H
#define __DL_SKIPLIST_H

#include <stdio.h>
#include <inttypes.h>
#include <linux/types.h>
#include "common_ops.h"
#include "measure.h"

/* doubly-linked skiplist */
typedef struct _dl_skiplist {
	struct dl_sl *list;
	int (*cmp_dl)(__u64 a, __u64 b);
} dl_skiplist_t;

struct dl_sl{
	/*
	 * La testa della skip list è un nodo di struttura 
	 * che non contiene informazione.
	 * Permette inoltre di trovare immediatamente il nodo avente
	 * la chiave più piccola: head->next[0]
	 */
	struct dl_sl_node *head;
	/*
	 * L'array implementa il mapping tra le runqueue, identificate
 	 * con il loro indice, e i nodi della skiplist.
	 */
	struct dl_sl_node **rq_to_node;
  /* Seme per la generazione di numeri pseudo-random */
	unsigned int seed;
	/* Livello attuale della skiplist */
	unsigned int level;
	/* Dimensione della skiplist */
	unsigned int rq_num;
  /* Skiplist lock */
  pthread_rwlock_t lock;
};

void dl_sl_init(void *s, int nproc, int (*cmp_dl)(__u64 a, __u64 b));
void dl_sl_cleanup(void *s);

/*
 * Update CPU state inside the data structure
 * after a preemption
 */
int dl_sl_preempt(void *s, int cpu, __u64 dline, int is_valid);
/*
 * Update CPU state inside the data structure
 * after a task finished 
 */
int dl_sl_finish(void *s, int cpu, __u64 dline, int is_valid);
/*
 * data_find should find the best CPU where to push
 * a task and/or find the best task to pull from
 * another CPU
 */
int dl_sl_find(void *s);
int dl_sl_max(void *s);

void dl_sl_load(void *s, FILE *f);
void dl_sl_save(void *s, int nproc, FILE *f);
void dl_sl_print(void *s, int nproc);

int dl_sl_check(void *s, int nproc);

#endif
