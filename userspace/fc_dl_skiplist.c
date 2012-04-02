#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <linux/types.h>
#include <time.h>
#include <pthread.h>

#include "common_ops.h"
#include "parameters.h"
#include "flat_combining.h"
#include "fc_dl_skiplist.h"

/* Numero massimo di publication record per CPU */
#define P_RECORD_PER_CPU		10

/*! \brief Numero massimo di livelli della skiplist
 */
#define	MAX_LEVEL           8

/*! \brief Probabilità sorteggio livello superiore nella skiplist
 */
#define LEVEL_PROB_VALUE    0.20

/*! \brief Nodo di una doubly-linked skiplist */
struct fc_dl_sl_node{
	/*! \brief deadline del task */
	__u64 dline;
	/*! \brief Livello del nodo */
	int level;
	/*! \brief Puntatori ai nodi successivi nei vari livelli */
	struct fc_dl_sl_node *next[MAX_LEVEL];
	/*! \brief Puntatori ai nodi precedenti nei vari livelli */
	struct fc_dl_sl_node *prev[MAX_LEVEL];
	/*! \brief Indice della runqueue associata al nodo */
	unsigned int rq_idx;
};

/************************************
*					FUNZIONI STATICHE					*
*************************************/

/*! \brief Sgancia un nodo dalla skiplist
 *
 * Sgancia un nodo dalla skiplist list, modificando opportunamente i puntatori
 * degli elementi precedenti e successivi. Il nodo non viene deallocato e rimane 
 * associato alla medesima runqueue per un futuro accodamento di un task.
 * \param [in] list puntatore alla skiplist
 * \param [in] p puntatore al nodo da sganciare
 * \return valore della deadline contenuta nel nodo sganciato
 */
static inline __u64 fc_dl_sl_detach(struct fc_dl_sl *list, struct fc_dl_sl_node *p){
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
static __u64 fc_dl_sl_remove_idx(struct fc_dl_sl *list, const unsigned int rq_idx){
	struct fc_dl_sl_node *p;
	
	/* puntatore al nodo */
	p = list->rq_to_node[rq_idx];

	/* nodo non inserito nella skiplist */
	if(p->level < 0)
		return 0;

	/* sgancio del nodo dalla skiplist */
	return fc_dl_sl_detach(list, p);
}

/*! \brief Sorteggio del livello
 *
 * Sorteggio di un livello per un nuovo nodo. Il valore sorteggiato apparterrà
 * all'intervallo [0, max] oppure [0, MAX_LEVEL - 1] se max >= MAX_LEVEL.
 * \param [in, out] seed puntatore allo stato del generatore pseudo-random
 * \param [in] max massimo valore ammesso per il livello sorteggiato
 * \return livello sorteggiato
 */
static inline unsigned int fc_dl_sl_rand_level(unsigned int *seed, unsigned int max){
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
static int fc_dl_sl_insert(struct fc_dl_sl *list, const unsigned int rq_idx, __u64 dline, int (*cmp_dl)(__u64 a, __u64 b)){
	struct fc_dl_sl_node *p;
	struct fc_dl_sl_node *update[MAX_LEVEL];
	struct fc_dl_sl_node *new_node;
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
	rand_level = fc_dl_sl_rand_level(&list->seed, list->level + 1);
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

#if 0
static void fc_dl_sl_init_load(struct fc_dl_sl *list, int (*cmp_dl)(__u64 a, __u64 b)){
	unsigned int i;
	__u64 dline;

	dline = MAX_DL;
	
	for(i = 0; i < list->rq_num; i++)
		fc_dl_sl_insert(list, i, dline, cmp_dl);
}
#endif

static int old_fc_dl_sl_preempt(void *s, int cpu, __u64 dline, int is_valid){
	fc_dl_skiplist_t *p = (fc_dl_skiplist_t *)s;

	fc_dl_sl_remove_idx(p->list, cpu);
	if(is_valid && fc_dl_sl_insert(p->list, cpu, dline, p->cmp_dl) < 0){
		fprintf(stderr, "errore inserimento skiplist");
		exit(-1);
	}

	return 0;
}

#if 0
static int old_fc_dl_sl_finish(void *s, int cpu, __u64 dline, int is_valid){
	fc_dl_skiplist_t *p = (fc_dl_skiplist_t *)s;
	__u64 old_dline, fake_dline;

	fake_dline = MAX_DL;

	old_dline = fc_dl_sl_remove_idx(p->list, cpu);
	if(is_valid)
		fc_dl_sl_insert(p->list, cpu, dline, p->cmp_dl);
	else
		fc_dl_sl_insert(p->list, cpu, fake_dline, p->cmp_dl);

	return 0;
}
#endif

/*
 * e' possibile implementare qui una ricerca lineare del primo nodo da sganciare nel caso
 * i precedenti contengano task non adatti all'operazione di pull (a causa della CPU affinity)
 *
 * Si copia p->list->head->next[0] in una variabile locale per evitare modifiche concorrenti
 * la lettura di tale indirizzo è sicuramente atomica, poichè tale indirizzo è tornato da calloc()
 * che ritorna indirizzi allineati alla dimensione del tipo primitivo più grande nell'architettura
 * in uso (8 bytes per x86-32).
 * Se si vuole avere garanzie maggiori sull'allineamento usare posix_memalign() e successivamente
 * memset per azzerare la memoria in modo da ottenere un equivalente di calloc().
 */
static int old_fc_dl_sl_find(void *s){
	fc_dl_skiplist_t *p = (fc_dl_skiplist_t *)s;
	struct fc_dl_sl_node *node;
	int cpu = -1;

	node = p->list->head->next[0];
	if(node)
		cpu = node->rq_idx;

	return cpu;
}

/* ancora da implementare */
static void old_fc_dl_sl_load(void *s, FILE *f){
#if 0
	fc_dl_skiplist_t *p = (fc_dl_skiplist_t *)s;
#endif
}

static void old_fc_dl_sl_save(void *s, FILE *f){
	fc_dl_skiplist_t *p = (fc_dl_skiplist_t *)s;
	struct fc_dl_sl_node *node;
	int i;

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
}

/* nproc non è utilizzato qui */
static void old_fc_dl_sl_print(void *s, int nproc){
	old_fc_dl_sl_save(s, stdout);	
}

static int old_fc_dl_sl_check(void *s, int nproc){
	fc_dl_skiplist_t *p = (fc_dl_skiplist_t *)s;
	struct fc_dl_sl_node *node, *next_node, *prev_node;
	unsigned int i, max_level = 0;
	int flag = 1;

	/* check numero livelli della skiplist */
	for(i = 0; i < MAX_LEVEL; i++)
		if(p->list->head->next[i] != NULL)
			max_level = i;
	if(max_level != p->list->level){
		fprintf(stderr, "errore numero livelli skiplist\n");
		fprintf(stderr, "list->level: %u max_level: %u\n", p->list->level, max_level);
		for(i = 0; i < MAX_LEVEL; i++)
			printf("level %u: %p\n", i, p->list->head->next[i]);
		flag = 0;	
	}

#if 0
	/* check numero elementi nella skiplist */
	node = p->list->head;
	for(i = 0; i < p->list->rq_num; i++)
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
			if(p->cmp_dl(node->dline, next_node->dline) < 0){
				fprintf(stderr, "errore forward check (level: %u) per elementi prev: %llu e next: %llu\n", i, node->dline, next_node->dline);
				flag = 0;
			}
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
			if(p->cmp_dl(prev_node->dline, node->dline) < 0){
				fprintf(stderr, "errore backward check (level: %u) per elementi prev: %llu e next: %llu\n", i, node->dline, next_node->dline);
				flag = 0;
			}
			node = prev_node;
		}
	}

	/* stampa skiplist in caso di errore */
	if(!flag)
		old_fc_dl_sl_print(s, nproc);

	return flag;
}

int old_fc_dl_sl_check_cpu (void *s, int cpu, __u64 dline){
	fc_dl_skiplist_t *p = (fc_dl_skiplist_t *)s;
	struct fc_dl_sl_node *node;
	int flag = 1;

	node = p->list->rq_to_node[cpu];
	if(!node)
		return 0;
	
	if(!dline && node->level != -1)
		flag = 0;

	if(dline > 0 && dline != node->dline)
		flag = 0;
	
	return flag;
}

static void fc_dl_sl_do_combiner(fc_dl_skiplist_t *p){
	pub_record *i;
	int cpu, is_valid;
	__u64 dline;

	/* scansione publication list */
	for(i = dequeue_all_publication_record(p->p_list); i; i = i->next){
		/* evasione richiesta */
		switch(i->req){
			case PREEMPT:
				cpu = i->par.preempt_p.cpu;
				dline = i->par.preempt_p.dline;
				is_valid = i->par.preempt_p.is_valid;
				i->res.preempt_r.res = old_fc_dl_sl_preempt((void *)p, cpu, dline, is_valid);
				break;
		}

		SET_READY(i);
		/* FIXME: ??? */
		/* 
		 * attenzione: il publication record deve rimanere ACTIVE finchè non è stata
		 * letta la risposta. Poichè in questa simulazione le risposte vengono scartate
		 * lo poniamo inattivo qui.
		 */
		DEACTIVATE(i);
		__sync_synchronize();
	}

}

static void fc_dl_sl_wait_response(fc_dl_skiplist_t *p/*, pub_record *r*/){
	if(!pthread_spin_trylock(&p->lock)){
		fc_dl_sl_do_combiner(p);
		pthread_spin_unlock(&p->lock);
	}
}

/*
 * restituisce un puntatore ad un publication record preallocato
 * se tutti i P_RECORD_PER_CPU sono utilizzati si attende l'evasione di una
 * richiesta che liberi un publication record
 */
static pub_record *get_pub_record(fc_dl_skiplist_t *p, int cpu){
	pub_record **array = p->p_record_array[cpu];
	int idx = p->p_record_idx[cpu];

	while(1){
		if(!IS_ACTIVE(array[idx])){
			ACTIVATE(array[idx]);
			p->p_record_idx[cpu] = (p->p_record_idx[cpu] + 1) % P_RECORD_PER_CPU;
			return array[idx];
		}
		/* nessun publication record libero: tento di diventare un combiner */
		fc_dl_sl_wait_response(p);
	}
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
void fc_dl_sl_init(void *s, int nproc, int (*cmp_dl)(__u64 a, __u64 b)){
	fc_dl_skiplist_t *p = (fc_dl_skiplist_t *)s;
	p->cmp_dl = cmp_dl;
	unsigned int i, j;

	/* creazione skiplist e nodo di testa */
	p->list = (struct fc_dl_sl *)calloc(1, sizeof(*p->list));
	p->list->head = (struct fc_dl_sl_node *)calloc(1, sizeof(*p->list->head));

	/* salvataggio seme generatore pseudo-random */
	p->list->seed = time(NULL);

	/* creazione array di mapping */
	p->list->rq_to_node = (struct fc_dl_sl_node **)calloc(nproc, sizeof(*p->list->rq_to_node));

	/* preallocazione di nproc nodi e inizializzazione array mapping */
	for(i = 0; i < nproc; i++){
		p->list->rq_to_node[i] = (struct fc_dl_sl_node *)calloc(1, sizeof(*p->list->rq_to_node[i]));
		p->list->rq_to_node[i]->level = -1;
		p->list->rq_to_node[i]->rq_idx = i;
	}

	/* salvataggio dimensione skiplist */
	p->list->rq_num = nproc;

#if 0
	/* precaricamento skiplist con deadline fittizie */
	fc_dl_sl_init_load(p->list, p->cmp_dl);
#endif

	/* inizializzazione lock */
	pthread_spin_init(&p->lock, PTHREAD_PROCESS_SHARED);

	/* creazione publication_list */
	p->p_list = create_publication_list();

	/* creazione publication records */
	p->p_record_array = (pub_record ***)calloc(nproc, sizeof(*p->p_record_array));
	for(i = 0; i < nproc; i++){
		p->p_record_array[i] = (pub_record **)calloc(P_RECORD_PER_CPU, sizeof(*p->p_record_array[i]));
		for(j = 0; j < P_RECORD_PER_CPU; j++)
			p->p_record_array[i][j] = create_publication_record();
		p->p_record_idx = (int *)calloc(nproc, sizeof(*p->p_record_idx));
	}
}

void fc_dl_sl_cleanup(void *s){
	fc_dl_skiplist_t *p = (fc_dl_skiplist_t *)s;
	unsigned int i, j;

	/* evasione richieste pendenti */
	pthread_spin_lock(&p->lock);
	fc_dl_sl_do_combiner(p);
	pthread_spin_unlock(&p->lock);

	/* distruzione nodi skiplist */
	for(i = 0; i < p->list->rq_num; i++)
		free(p->list->rq_to_node[i]);

	/* distruzione nodo testa */
	free(p->list->head);
  
	/* distruzione array di mapping */
	free(p->list->rq_to_node);

	/* distruzione lock */
	pthread_spin_destroy(&p->lock);

	/* distruzione publication records */
	for(i = 0; i < p->list->rq_num; i++){
		for(j = 0; j < P_RECORD_PER_CPU; j++)
			destroy_publication_record(p->p_record_array[i][j]);
		free(p->p_record_array[i]);
	}
	free(p->p_record_array);

	/* distruzione skiplist */
	free(p->list);

	/* distruzione publication_list */
	destroy_publication_list(p->p_list);
}

int fc_dl_sl_preempt(void *s, int cpu, __u64 dline, int is_valid){
	fc_dl_skiplist_t *p = (fc_dl_skiplist_t *)s;
	int res;

	/* creazione publication record */
	pub_record *r = get_pub_record(p, cpu);
	r->req = PREEMPT;
	r->par.preempt_p.cpu = cpu;
	r->par.preempt_p.dline = dline;
	r->par.preempt_p.is_valid = is_valid;
	UNSET_READY(r);

	/* pubblicazione publication record */
	enqueue_publication_record(p->p_list, r);

	/* FIXME: non serve un'attesa, è un'operazione di modifica */
	/* attesa evasione richiesta */
	/* nessuna attesa: si tenta di diventare combiner, se non si riesce si ritorna */
	fc_dl_sl_wait_response(p/*, r*/);

	/* lettura risultato */
	res = r->res.preempt_r.res;

	return res;
}

int fc_dl_sl_find(void *s){
	fc_dl_skiplist_t *p = (fc_dl_skiplist_t *)s;
	pub_record *rec;
	__u64 candidate_dline = 0;
	__u64 data_struct_dline = 0;
	int candidate_cpu = -1;
	int data_struct_cpu = -1;
	
	/* previsione migliore cpu dalla scansione lista */
	rec = p->p_list->head;		/* lettura atomica (vedi old_dl_sl_find) */
	while(rec){
		/* il combiner thread potrebbe, nel frattempo, apportare le modifiche */
		if(!IS_READY(rec))
			switch(rec->req){
				case PREEMPT:
						if(candidate_cpu == -1 || p->cmp_dl(rec->par.preempt_p.dline, candidate_dline)){
							candidate_cpu = rec->par.preempt_p.cpu;
							candidate_dline = rec->par.preempt_p.dline;
						}
					break;
			}
		rec = rec->next;
	}

	/* lettura migliore cpu dalla struttura dati */
	data_struct_cpu = old_fc_dl_sl_find(s);
	if(data_struct_cpu >= 0)
		data_struct_dline = p->list->rq_to_node[data_struct_cpu]->dline;

	/* confronto */
	if(candidate_dline > 0 && data_struct_dline > 0)
		return p->cmp_dl(candidate_dline, data_struct_dline) ? candidate_cpu : data_struct_cpu;
	if(candidate_dline > 0)
		return candidate_cpu;
	else
		return data_struct_cpu;
}

void fc_dl_sl_load(void *s, FILE *f){
	fc_dl_skiplist_t *p = (fc_dl_skiplist_t *)s;

	pthread_spin_lock(&p->lock);
	old_fc_dl_sl_load(s, f);
	pthread_spin_unlock(&p->lock);
}

void fc_dl_sl_save(void *s, int nproc, FILE *f){
	fc_dl_skiplist_t *p = (fc_dl_skiplist_t *)s;
	
	pthread_spin_lock(&p->lock);
	old_fc_dl_sl_save(s, f);
	pthread_spin_unlock(&p->lock);
}

void fc_dl_sl_print(void *s, int nproc){
	fc_dl_skiplist_t *p = (fc_dl_skiplist_t *)s;
	
	pthread_spin_lock(&p->lock);
	old_fc_dl_sl_print(s, nproc);
	pthread_spin_unlock(&p->lock);
}

int fc_dl_sl_check(void *s, int nproc){
	fc_dl_skiplist_t *p = (fc_dl_skiplist_t *)s;
	int res;
	
	pthread_spin_lock(&p->lock);
	res = old_fc_dl_sl_check(s, nproc);
	pthread_spin_unlock(&p->lock);

	return res;
}

int fc_dl_sl_check_cpu(void *s, int cpu, __u64 dline){
	fc_dl_skiplist_t *p = (fc_dl_skiplist_t *)s;
	int res;
	
	pthread_spin_lock(&p->lock);
	res = old_fc_dl_sl_check_cpu(s, cpu, dline);
	pthread_spin_unlock(&p->lock);

	return res;
}

/* mapping per le chiamate astratte alle funzioni della struttura dati */
const struct data_struct_ops fc_dl_skiplist_ops = {
	.data_init = fc_dl_sl_init,
	.data_cleanup = fc_dl_sl_cleanup,
	.data_preempt = fc_dl_sl_preempt,
	.data_finish = fc_dl_sl_preempt,
	.data_find = fc_dl_sl_find,
	.data_max = fc_dl_sl_find,
	.data_load = fc_dl_sl_load,
	.data_save = fc_dl_sl_save,
	.data_print = fc_dl_sl_print,
	.data_check = fc_dl_sl_check,
	.data_check_cpu = fc_dl_sl_check_cpu
};
