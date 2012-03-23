/* rq_heap.h -- Binomial Heaps for Simulated runqueues
 *
 * Copyright (c) 2008, Bjoern B. Brandenburg <bbb [at] cs.unc.edu>
 * Modified by Juri Lelli <juri.lelli [at] gmail.com>, 2012
 * Modified by Fabio Falzoi <falzoi [at] feanor.sssup.it>, 2012
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the University of North Carolina nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY  COPYRIGHT OWNER AND CONTRIBUTERS ``AS IS'' AND
 * ANY  EXPRESS OR  IMPLIED  WARRANTIES,  INCLUDING, BUT  NOT  LIMITED TO,  THE
 * IMPLIED WARRANTIES  OF MERCHANTABILITY AND FITNESS FOR  A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO  EVENT SHALL THE  COPYRIGHT OWNER OR  CONTRIBUTERS BE
 * LIABLE  FOR  ANY  DIRECT,   INDIRECT,  INCIDENTAL,  SPECIAL,  EXEMPLARY,  OR
 * CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT  LIMITED  TO,  PROCUREMENT  OF
 * SUBSTITUTE GOODS  OR SERVICES;  LOSS OF USE,  DATA, OR PROFITS;  OR BUSINESS
 * INTERRUPTION)  HOWEVER CAUSED  AND ON  ANY THEORY  OF LIABILITY,  WHETHER IN
 * CONTRACT,  STRICT LIABILITY,  OR  TORT (INCLUDING  NEGLIGENCE OR  OTHERWISE)
 * ARISING IN ANY WAY  OUT OF THE USE OF THIS SOFTWARE,  EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __RQ_HEAP_H__
#define __RQ_HEAP_H__

#include <limits.h>
#include <stdlib.h>

#define NOT_IN_HEAP UINT_MAX

struct rq_heap_node {
	struct rq_heap_node* 	parent;
	struct rq_heap_node* 	next;
	struct rq_heap_node* 	child;

	unsigned int 		degree;
	void*			value;
	struct rq_heap_node**	ref;
};

struct rq_heap {
	struct rq_heap_node* 	head;
	/* We cache minimum and next of the heap.
	 * This speeds up repeated peek operations.
	 */
	struct rq_heap_node*	min;
	struct rq_heap_node*	next;
};

/* 
 * item comparison function:
 * return 1 if a has higher prio than b, 0 otherwise
 */
typedef int (*rq_heap_prio_t)(struct rq_heap_node* a, struct rq_heap_node* b);

static inline void rq_heap_init(struct rq_heap* heap)
{
	heap->head = NULL;
	heap->min  = NULL;
	heap->next  = NULL;
}

static inline void rq_heap_node_init_ref(struct rq_heap_node** _h, void* value)
{
	struct rq_heap_node* h = *_h;
	h->parent = NULL;
	h->next   = NULL;
	h->child  = NULL;
	h->degree = NOT_IN_HEAP;
	h->value  = value;
	h->ref    = _h;
}

static inline void rq_heap_node_init(struct rq_heap_node* h, void* value)
{
	h->parent = NULL;
	h->next   = NULL;
	h->child  = NULL;
	h->degree = NOT_IN_HEAP;
	h->value  = value;
	h->ref    = NULL;
}

static inline void* rq_heap_node_value(struct rq_heap_node* h)
{
	if(h)
		return h->value;
	else
		return NULL;
}

static inline int rq_heap_node_in_heap(struct rq_heap_node* h)
{
	return h->degree != NOT_IN_HEAP;
}

/*
 * we need to check min pointer, 'cause it's used as cache
 * for earliest deadline task.
 * Note that: if heap->min == NULL then heap->next == NULL
 */
static inline int rq_heap_empty(struct rq_heap* heap)
{
	return heap->head == NULL && heap->min == NULL && heap->next == NULL;
}

/* make child a subtree of root */
static inline void __rq_heap_link(struct rq_heap_node* root,
			       struct rq_heap_node* child)
{
	child->parent = root;
	child->next   = root->child;
	root->child   = child;
	root->degree++;
}

/* merge root lists */
static inline struct rq_heap_node* __rq_heap_merge(struct rq_heap_node* a,
					     struct rq_heap_node* b)
{
	struct rq_heap_node* head = NULL;
	struct rq_heap_node** pos = &head;

	while (a && b) {
		if (a->degree < b->degree) {
			*pos = a;
			a = a->next;
		} else {
			*pos = b;
			b = b->next;
		}
		pos = &(*pos)->next;
	}
	if (a)
		*pos = a;
	else
		*pos = b;
	return head;
}

/* reverse a linked list of nodes. also clears parent pointer */
static inline struct rq_heap_node* __rq_heap_reverse(struct rq_heap_node* h)
{
	struct rq_heap_node* tail = NULL;
	struct rq_heap_node* next;

	if (!h)
		return h;

	h->parent = NULL;
	while (h->next) {
		next    = h->next;
		h->next = tail;
		tail    = h;
		h       = next;
		h->parent = NULL;
	}
	h->next = tail;
	return h;
}

static inline void __rq_heap_min(rq_heap_prio_t higher_prio, struct rq_heap* heap,
			      struct rq_heap_node** prev, struct rq_heap_node** node)
{
	struct rq_heap_node *_prev, *cur;
	*prev = NULL;

	if (!heap->head) {
		*node = NULL;
		return;
	}

	*node = heap->head;
	_prev = heap->head;
	cur   = heap->head->next;
	while (cur) {
		if (higher_prio(cur, *node)) {
			*node = cur;
			*prev = _prev;
		}
		_prev = cur;
		cur   = cur->next;
	}
}

static inline void __rq_heap_union(rq_heap_prio_t higher_prio, struct rq_heap* heap,
				struct rq_heap_node* h2)
{
	struct rq_heap_node* h1;
	struct rq_heap_node *prev, *x, *next;
	if (!h2)
		return;
	h1 = heap->head;
	if (!h1) {
		heap->head = h2;
		return;
	}
	h1 = __rq_heap_merge(h1, h2);
	prev = NULL;
	x    = h1;
	next = x->next;
	while (next) {
		if (x->degree != next->degree ||
		    (next->next && next->next->degree == x->degree)) {
			/* nothing to do, advance */
			prev = x;
			x    = next;
		} else if (higher_prio(x, next)) {
			/* x becomes the root of next */
			x->next = next->next;
			__rq_heap_link(x, next);
		} else {
			/* next becomes the root of x */
			if (prev)
				prev->next = next;
			else
				h1 = next;
			__rq_heap_link(next, x);
			x = next;
		}
		next = x->next;
	}
	heap->head = h1;
}

static inline struct rq_heap_node* __rq_heap_extract_min(rq_heap_prio_t higher_prio,
						   struct rq_heap* heap)
{
	struct rq_heap_node *prev, *node;
	__rq_heap_min(higher_prio, heap, &prev, &node);
	if (!node)
		return NULL;
	if (prev)
		prev->next = node->next;
	else
		heap->head = node->next;
	__rq_heap_union(higher_prio, heap, __rq_heap_reverse(node->child));
	return node;
}

/* insert (and reinitialize) a node into the rq_heap */
static inline void rq_heap_insert(rq_heap_prio_t higher_prio, struct rq_heap* heap,
			       struct rq_heap_node* node)
{
	struct rq_heap_node *next;
	node->child  = NULL;
	node->parent = NULL;
	node->next   = NULL;
	node->degree = 0;

	if (!heap->min || (heap->min && higher_prio(node, heap->min))) {
		/* swap next cache if present */
		if(heap->next){
			next = heap->next;
			next->child  = NULL;
			next->parent = NULL;
			next->next   = NULL;
			next->degree = 0;
			__rq_heap_union(higher_prio, heap, next);
		}
		heap->next	= heap->min;
		heap->min		= node;
	} else if (!heap->next || (heap->next && higher_prio(node, heap->next))) {
		/* swap next cache if present */
		if(heap->next){
			next = heap->next;
			next->child  = NULL;
			next->parent = NULL;
			next->next   = NULL;
			next->degree = 0;
			__rq_heap_union(higher_prio, heap, next);
		}
		heap->next   = node;
	} else
		__rq_heap_union(higher_prio, heap, node);
}

static inline void __uncache_next(rq_heap_prio_t higher_prio, struct rq_heap* heap)
{
	struct rq_heap_node* next;
	if (heap->next) {
		next = heap->next;
		heap->next = NULL;
		rq_heap_insert(higher_prio, heap, next);
	}
}

static inline void __uncache_min(rq_heap_prio_t higher_prio, struct rq_heap* heap)
{
	struct rq_heap_node* min;

	__uncache_next(higher_prio, heap);
	if (heap->min) {
		min = heap->min;
		heap->min = NULL;
		rq_heap_insert(higher_prio, heap, min);
	}
}

/* merge addition into target */
static inline void rq_heap_union(rq_heap_prio_t higher_prio,
			      struct rq_heap* target, struct rq_heap* addition)
{
	/* first insert any cached minima, if necessary */
	__uncache_min(higher_prio, target);
	__uncache_next(higher_prio, target);
	__uncache_min(higher_prio, addition);
	__uncache_next(higher_prio, addition);
	__rq_heap_union(higher_prio, target, addition->head);
	/* this is a destructive merge */
	addition->head = NULL;
}

static inline struct rq_heap_node* rq_heap_peek(rq_heap_prio_t higher_prio,
					  struct rq_heap* heap)
{
	if (!heap->min)
		heap->min = __rq_heap_extract_min(higher_prio, heap);
	return heap->min;
}

static inline struct rq_heap_node* rq_heap_peek_next(rq_heap_prio_t higher_prio,
					  struct rq_heap* heap)
{
	if (!heap->min)
		heap->min = __rq_heap_extract_min(higher_prio, heap);
	if (!heap->next)
		heap->next = __rq_heap_extract_min(higher_prio, heap);
	return heap->next;
}

static inline struct rq_heap_node* rq_heap_take(rq_heap_prio_t higher_prio,
					  struct rq_heap* heap)
{
	struct rq_heap_node *node;
	if (!heap->min)
		heap->min = __rq_heap_extract_min(higher_prio, heap);
	if (heap->min && !heap->next)
		heap->next = __rq_heap_extract_min(higher_prio, heap);
	node = heap->min;
	heap->min = heap->next;
	if(heap->min)
		heap->next = __rq_heap_extract_min(higher_prio, heap);
	if (node)
		node->degree = NOT_IN_HEAP;
	return node;
}

static inline struct rq_heap_node* rq_heap_take_next(rq_heap_prio_t higher_prio,
					  struct rq_heap* heap)
{
	struct rq_heap_node *node;
	if (!heap->next)
		heap->next = __rq_heap_extract_min(higher_prio, heap);
	node = heap->next;
	if(node){
		heap->next = __rq_heap_extract_min(higher_prio, heap);
		node->degree = NOT_IN_HEAP;
	}
	return node;
}

/*
 * this function use ref field of node!!!
 */
static inline void rq_heap_decrease(rq_heap_prio_t higher_prio, struct rq_heap* heap,
				 struct rq_heap_node* node)
{
	struct rq_heap_node *parent;
	struct rq_heap_node** tmp_ref;
	void* tmp;

	/* node's priority was decreased, we need to update its position */
	if (!node->ref)
		return;
	if (heap->min != node && heap->next != node) {
		/* bubble up */
		parent = node->parent;
		while (parent && higher_prio(node, parent)) {
			/* swap parent and node */
			tmp           = parent->value;
			parent->value = node->value;
			node->value   = tmp;
			/* swap references */
			if (parent->ref)
				*(parent->ref) = node;
			*(node->ref)   = parent;
			tmp_ref        = parent->ref;
			parent->ref    = node->ref;
			node->ref      = tmp_ref;
			/* step up */
			node   = parent;
			parent = node->parent;
		}
		/* if node's priority is now less than min and/or next we must uncache them */
		if (heap->min && higher_prio(node, heap->min)){
			__uncache_min(higher_prio, heap);
			__uncache_next(higher_prio, heap);
		}
		else if (heap->next && higher_prio(node, heap->next))
			__uncache_next(higher_prio, heap);
	} else if (heap->next == node && higher_prio(heap->next, heap->min)) {	/* if node is "next" cached node we must compare it with min */
			/* swap min and next */
			tmp           = heap->min->value;
			heap->min->value = heap->next->value;
			heap->next->value   = tmp;
			/* swap references */
			if (heap->min->ref)
				*(heap->min->ref) = heap->next;
			*(heap->next->ref)   = heap->min;
			tmp_ref        = heap->min->ref;
			heap->min->ref    = heap->next->ref;
			heap->next->ref      = tmp_ref;
	}
}

/*
 * this function use ref field of node!!!
 */
static inline void rq_heap_delete(rq_heap_prio_t higher_prio, struct rq_heap* heap,
			       struct rq_heap_node* node)
{
	struct rq_heap_node *parent, *prev, *pos;
	struct rq_heap_node** tmp_ref;
	void* tmp;

	if (!node->ref) /* can only delete if we have a reference */
		return;
	if (heap->min != node && heap->next != node) {
		/* bubble up */
		parent = node->parent;
		while (parent) {
			/* swap parent and node */
			tmp           = parent->value;
			parent->value = node->value;
			node->value   = tmp;
			/* swap references */
			if (parent->ref)
				*(parent->ref) = node;
			*(node->ref)   = parent;
			tmp_ref        = parent->ref;
			parent->ref    = node->ref;
			node->ref      = tmp_ref;
			/* step up */
			node   = parent;
			parent = node->parent;
		}
		/* now delete:
		 * first find prev */
		prev = NULL;
		pos  = heap->head;
		while (pos != node) {
			prev = pos;
			pos  = pos->next;
		}
		/* we have prev, now remove node */
		if (prev)
			prev->next = node->next;
		else
			heap->head = node->next;
		__rq_heap_union(higher_prio, heap, __rq_heap_reverse(node->child));
	} else if (heap->min == node) {
		heap->min = heap->next;
		heap->next = __rq_heap_extract_min(higher_prio, heap);
	} else 
		heap->next = __rq_heap_extract_min(higher_prio, heap);
	node->degree = NOT_IN_HEAP;
}

#endif /* HEAP_H */
