#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <linux/types.h>
#include <time.h>
#include <pthread.h>

#include "common_ops.h"
#include "parameters.h"
#include "dl_skiplist.h"

/*! \brief Numero massimo di livelli della skiplist
 */
#define	MAX_LEVEL           8

/*! \brief Probabilità sorteggio livello superiore nella skiplist
 */
#define LEVEL_PROB_VALUE    0.20

/*! \brief Nodo di una doubly-linked skiplist */
struct dl_sl_node{
	/*! \brief deadline del task */
	__u64 dline;
	/*! \brief Livello del nodo */
	int level;
	/*! \brief Puntatori ai nodi successivi nei vari livelli */
	struct dl_sl_node *next[MAX_LEVEL];
	/*! \brief Puntatori ai nodi precedenti nei vari livelli */
	struct dl_sl_node *prev[MAX_LEVEL];
	/*! \brief Indice della runqueue associata al nodo */
	unsigned int rq_idx;
};

/************************************
*					FUNZIONI STATICHE					*
*************************************/

/*! \brief Confronta 2 deadline
 *
 * Effettua il confronto tra 2 deadline con 3 test condizionali,
 * Non si ritorna direttamente la differenza (__s64)(a - b) per
 * evitare problemi di overflow nel cast a int
 * \param [in] a deadline 1
 * \param [in] b deadline 2
 * \return torna 1 se a > b, 0 se a == b, -1 se a < b 
 */
#if 0
static inline int compare_dline(const __u64 a, const __u64 b){
	if(a > b)
		return 1;
	if(a < b)
		return -1;
	return 0;
}
#endif

/*! \brief Sgancia un nodo dalla skiplist
 *
 * Sgancia un nodo dalla skiplist list, modificando opportunamente i puntatori
 * degli elementi precedenti e successivi. Il nodo non viene deallocato e rimane 
 * associato alla medesima runqueue per un futuro accodamento di un task.
 * \param [in] list puntatore alla skiplist
 * \param [in] p puntatore al nodo da sganciare
 * \return valore della deadline contenuta nel nodo sganciato
 */
static inline __u64 dl_sl_detach(struct dl_sl *list, struct dl_sl_node *p){
	unsigned int i;

	/* sgancio del task dalla skip list */
	for(i = 0; i <= p->level; i++){
		p->prev[i]->next[i] = p->next[i];
		if(p->next[i])	/* il nodo non è l'ultimo della lista */
			p->next[i]->prev[i] = p->prev[i];
	}

	/* 
	 * se il nodo era l'unico a quel livello si aggiorna level 
	 * occorre un ciclo perchè tutti gli altri nodi potrebbero
	 * stare a livelli < p->level - 1
	 */
	while(!list->head->next[list->level] && list->level > 0)
		list->level--;

	/* si marca il nodo come sganciato dalla skiplist */
	p->level = -1;

	return p->dline;
}

/*! \brief Sgancia il nodo contenente il task associato alla runqueue di indice rq_idx
 *
 * Sgancia il nodo associato alla runqueue di indice rq_idx.
 * L'operazione ha complessità computazione O(1) grazie all'accesso diretto
 * tramite l'array di mapping list->rq_to_node[]
 * \param [in] list puntatore alla skiplist
 * \param [in] rq_idx indice della runqueue associata al nodo da sganciare
 * \return valore della deadline contenuta nel nodo sganciato
 */
static __u64 dl_sl_remove_idx(struct dl_sl *list, const unsigned int rq_idx){
	struct dl_sl_node *p;
	
	/* puntatore al nodo */
	p = list->rq_to_node[rq_idx];

	/* nodo non inserito nella skiplist */
	if(p->level < 0)
		return 0;

	/* sgancio del nodo dalla skiplist */
	return dl_sl_detach(list, p);
}

/*! \brief Sorteggio del livello
 *
 * Sorteggio di un livello per un nuovo nodo. Il valore sorteggiato apparterrà
 * all'intervallo [0, max] oppure [0, MAX_LEVEL - 1] se max >= MAX_LEVEL.
 * \param [in, out] seed puntatore allo stato del generatore pseudo-random
 * \param [in] max massimo valore ammesso per il livello sorteggiato
 * \return livello sorteggiato
 */
static inline unsigned int dl_sl_rand_level(unsigned int *seed, unsigned int max){
	unsigned int level = 0;

	/* max è limitato superiormente a MAX_LEVEL */
	max = max > MAX_LEVEL - 1 ? MAX_LEVEL - 1 : max;

	while(rand_r(seed) > ((1 - LEVEL_PROB_VALUE) * RAND_MAX) && level < max)
		level++;

	return level;
}

/*! \brief Inserisce il task t, associato alla runqueue rq_idx, nella skiplist list
 *
 * Il puntatore al task t viene memorizzato nel nodo associato alla runqueue rq_idx,
 * recuperato mediante l'array di mapping list->rq_to_node[].
 * Non si ha quindi un'allocazione di un nuovo nodo ma un riutilizzo del nodo preallocato
 * in fase iniziale per la runqueue.
 * \param [in] list puntatore alla skiplist
 * \param [in] rq_idx indice della runqueue associata al task da inserire
 * \param [in] t puntatore al task da inserire
 * \return 0 in caso di successo, -1 altrimenti
 */
static int dl_sl_insert(struct dl_sl *list, const unsigned int rq_idx, __u64 dline, int (*cmp_dl)(__u64 a, __u64 b)){
	struct dl_sl_node *p;
	struct dl_sl_node *update[MAX_LEVEL];
	struct dl_sl_node *new_node;
	int cmp_res, level;
	unsigned int i, rand_level;	

	new_node = list->rq_to_node[rq_idx];

	/* memorizzazione deadline */
	new_node->dline = dline;

	/* ricerca multilivello del punto di inserimento */
	p = list->head;
	level = list->level;
	while(level >= 0){
		/* costruzione array update */
		update[level] = p;

		/* siamo in fondo alla lista, si scende un livello */
		if(!p->next[level]){
			level--;
			continue;
		}
		
		/* abbiamo un elemento davanti, lo confrontiamo con l'elemento da inserire */
		cmp_res = cmp_dl(p->next[level]->dline, new_node->dline);
		
		if(cmp_res > 0)		/* se l'elemento esaminato è minore del nuovo si prosegue in orizzontale */
			p = p->next[level];
		else				/* altrimenti si scende un livello */
			level--;
	}

	/* sorteggio del livello del nuovo nodo */
	rand_level = dl_sl_rand_level(&list->seed, list->level + 1);
	new_node->level = rand_level;

	/* il nuovo nodo aggiunge un livello */
	if(rand_level > list->level)
		update[++list->level] = list->head;

	/* inserimento */
	for(i = 0; i <= rand_level; i++){
		new_node->next[i] = update[i]->next[i];
		update[i]->next[i] = new_node;
		new_node->prev[i] = update[i];
		/* se il nodo non è l'ultimo della lista occorre settare il campo prev[i] del nodo successivo */
		if(new_node->next[i])
			new_node->next[i]->prev[i] = new_node;
	}

	return 0;
}

void dl_sl_init_load(struct dl_sl *list, int (*cmp_dl)(__u64 a, __u64 b)){
	unsigned int i;
	__u64 dline;

	dline = MAX_DL;
	
	for(i = 0; i < list->rq_num; i++)
		dl_sl_insert(list, i, dline, cmp_dl);
}

/************************************
*					FUNZIONI PUBBLICHE				*
*************************************/

/*! \brief Allocazione e inizializzazione skiplist
 * 
 * Crea ed inizializza una skip_list list.
 * Si preallocano una quantità len di nodi liberi e si costruisce
 * un array di mapping <indice_runqueue -> indirizzo_nodo>,
 * tale associazione è costante per tutta la durata del programma.
 * Si richiede che il generatore di numeri casuali sia
 * già stato correttamente inizializzato.
 * Non acquisisce alcun lock.
 * \param [in, out] list indirizzo del puntatore alla skiplist
 * \param [in] len numero di nodi della skiplist preallocati
 * \return 0 in caso di successo, -1 altrimenti
 */
void dl_sl_init(void *s, int nproc, int (*cmp_dl)(__u64 a, __u64 b)){
	dl_skiplist_t *p = (dl_skiplist_t *)s;
	unsigned int i = 0;

	/* creazione skiplist e nodo di testa */
	p->list = (struct dl_sl *)calloc(sizeof(struct dl_sl), 1);
	p->cmp_dl = cmp_dl;
	p->list->head = (struct dl_sl_node *)calloc(sizeof(struct dl_sl_node), 1);
	p->list->head->rq_idx = -1;

	/* salvataggio seme generatore pseudo-random */
	p->list->seed = time(NULL);

	/* creazione array di mapping */
	p->list->rq_to_node = (struct dl_sl_node **)calloc(sizeof(struct dl_sl_node *), nproc);

	/* preallocazione di nproc nodi e inizializzazione array mapping */
	for(i = 0; i < nproc; i++){
		p->list->rq_to_node[i] = (struct dl_sl_node *)calloc(sizeof(struct dl_sl_node), 1);
		p->list->rq_to_node[i]->level = -1;
		p->list->rq_to_node[i]->rq_idx = i;
	}

	/* salvataggio dimensione skiplist */
	p->list->rq_num = nproc;

#if 0
	/* precaricamento skiplist con deadline fittizie */
	dl_sl_init_load(p->list, p->cmp_dl);
#endif

	/* inizializzazione lock */
	pthread_rwlock_init(&p->list->lock, NULL);
}

void dl_sl_cleanup(void *s){
	dl_skiplist_t *p = (dl_skiplist_t *)s;
	unsigned int i;

	/* distruzione nodi skiplist */
	for(i = 0; i < p->list->rq_num; i++)
		free(p->list->rq_to_node[i]);

	/* distruzione nodo testa */
	free(p->list->head);
  
	/* distruzione array di mapping */
	free(p->list->rq_to_node);

	/* distruzione lock */
  pthread_rwlock_destroy(&p->list->lock);

	/* distruzione skiplist */
	free(p->list);
}

int dl_sl_preempt(void *s, int cpu, __u64 dline, int is_valid){
	dl_skiplist_t *p = (dl_skiplist_t *)s;

	pthread_rwlock_wrlock(&p->list->lock);

	dl_sl_remove_idx(p->list, cpu);
	if(is_valid)
		dl_sl_insert(p->list, cpu, dline, p->cmp_dl);

	pthread_rwlock_unlock(&p->list->lock);

	return 0;
}

#if 0
int dl_sl_finish(void *s, int cpu, __u64 dline, int is_valid){
	dl_skiplist_t *p = (dl_skiplist_t *)s;

	pthread_rwlock_wrlock(&p->list->lock);

	dl_sl_remove_idx(p->list, cpu);
	if(is_valid)
		dl_sl_insert(p->list, cpu, dline, p->cmp_dl);

	pthread_rwlock_unlock(&p->list->lock);

	return 0;
}
#endif

/*
 * e' possibile implementare qui una ricerca lineare del primo nodo da sganciare nel caso
 * i precedenti contengano task non adatti all'operazione di pull (a causa della CPU affinity)
 */
int dl_sl_find(void *s){
	dl_skiplist_t *p = (dl_skiplist_t *)s;
	struct dl_sl_node *n;
	int cpu = -1;

#ifdef MEASURE_FIND_LOCK
	COMMON_MEASURE_START(find_lock)
#endif
	//pthread_rwlock_rdlock(&p->list->lock);
#ifdef MEASURE_FIND_LOCK
	COMMON_MEASURE_END(find_lock)
#endif

	/*
	 * we don't need to take a lock:
	 * if p->list->head->next[0] is not
	 * NULL, then we can fetch a node
	 * address, and that node will not
	 * be freed until the simulation ends.
	 */ 
	n = p->list->head->next[0];
	if(n)
		cpu = n->rq_idx;
/*
	if(p->list->head->next[0])
		cpu = p->list->head->next[0]->rq_idx;
*/
#ifdef MEASURE_FIND_UNLOCK
	COMMON_MEASURE_START(find_unlock)
#endif
	//pthread_rwlock_unlock(&p->list->lock);
#ifdef MEASURE_FIND_UNLOCK
	COMMON_MEASURE_END(find_unlock)
#endif

	return cpu;
}

/* ancora da implementare */
void dl_sl_load(void *s, FILE *f){
	//dl_skiplist_t *p = (dl_skiplist_t *)s;
}

void dl_sl_save(void *s, int nproc, FILE *f){
	dl_skiplist_t *p = (dl_skiplist_t *)s;
	struct dl_sl_node *node;
	int i;

	pthread_rwlock_rdlock(&p->list->lock);

	fprintf(f, "\n----Skiplist----\n");

	/* lista nodi */
	for(i = p->list->level; i >= 0; i--){
		fprintf(f, "%u:\t", i);
		for(node = p->list->head->next[i]; node; node = node->next[i])
			fprintf(f, "%llu ", node->dline);
		fprintf(f, "\n");
	}

	/* lista CPU - nodi */
	for(i = 0; i < p->list->rq_num; i++)
		if(p->list->rq_to_node[i]->level == -1)
			fprintf(f, "[%d]:\tout of list\n", i);
		else
			fprintf(f, "[%d]:\t%llu\n", i, p->list->rq_to_node[i]->dline);
	fprintf(f, "\n");

	fprintf(f, "----End Skiplist----\n\n");	

	pthread_rwlock_unlock(&p->list->lock);
}

/* nproc non è utilizzato qui */
void dl_sl_print(void *s, int nproc){
	dl_sl_save(s, nproc, stdout);	
}

int dl_sl_check(void *s, int nproc){
	dl_skiplist_t *p = (dl_skiplist_t *)s;
	struct dl_sl_node *node, *next_node, *prev_node;
	unsigned int i, max_level = 0;
	int flag = 1;

	pthread_rwlock_rdlock(&p->list->lock);

	/* check numero livelli della skiplist */
	for(i = 0; i < MAX_LEVEL; i++)
		if(p->list->head->next[i] != NULL)
			max_level = i;
	if(max_level != p->list->level){
		fprintf(stderr, "errore numero livelli skiplist");
		fprintf(stderr, "list->level: %u max_level: %u\n", p->list->level, max_level);
		for(i = 0; i < MAX_LEVEL; i++)
			printf("level %u: %p\n", i, p->list->head->next[i]);
		flag = 0;	
	}

	/* check numero elementi nella skiplist */
#if 0
	node = p->list->head;
	for(i = 0; node && i < p->list->rq_num; i++)
		node = node->next[0];
	if(i != p->list->rq_num){
		fprintf(stderr, "errore numero elementi skiplist");
		flag = 0;
	}
#endif

	/* 
	 * check corretto ordinamento deadline
	 * e integrità degli array di puntatori next e prev
	 * per ogni livello della lista
	 */

	/* forward check */
	for(i = 0; i < p->list->level; i++){
		/* lista vuota o con un solo elemento */
		if(!(node = p->list->head->next[i]))
			continue;
		
		/* check */
		while((next_node = node->next[i])){
			if(p->cmp_dl(node->dline, next_node->dline) < 0)
				flag = 0;
			node = next_node;
		}
	}

	/* backward check */
	for(i = 0; i < p->list->level; i++){
		/* lista vuota o con un solo elemento */
		if(!(node = p->list->head->next[i]))
			continue;

		/* raggiungo ultimo nodo */
		while(node->next[i])
			node = node->next[i];
			
		/* check */
		while((prev_node = node->prev[i])){
			if(p->cmp_dl(prev_node->dline, node->dline) < 0)
				flag = 0;
			node = prev_node;
		}
	}

	/* stampa skiplist in caso di errore */
	if(!flag)
		dl_sl_print(s, nproc);

	pthread_rwlock_unlock(&p->list->lock);

	return flag;
}

int dl_sl_check_cpu (void *s, int cpu, __u64 dline){
	dl_skiplist_t *p = (dl_skiplist_t *)s;
	struct dl_sl_node *node;
	int flag = 1;

	pthread_rwlock_rdlock(&p->list->lock);

	node = p->list->rq_to_node[cpu];
	if(!node)
		return 0;
	
	if(!dline && node->level != -1)
		flag = 0;

	if(dline > 0 && dline != node->dline)
		flag = 0;

	pthread_rwlock_unlock(&p->list->lock);
	
	return flag;
}

/* mapping per le chiamate astratte alle funzioni della struttura dati */
const struct data_struct_ops dl_skiplist_ops = {
	.data_init = dl_sl_init,
	.data_cleanup = dl_sl_cleanup,
	.data_preempt = dl_sl_preempt,
	.data_finish = dl_sl_preempt,
	.data_find = dl_sl_find,
	.data_max = dl_sl_find,
	.data_load = dl_sl_load,
	.data_save = dl_sl_save,
	.data_print = dl_sl_print,
	.data_check = dl_sl_check,
	.data_check_cpu = dl_sl_check_cpu
};
