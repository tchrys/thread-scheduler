#include <stdlib.h>
#include "so_scheduler.h"

/* fiecare node_t contine indexul, prioritatea si pointerii next si prev.
 * Atunci cand inseram un nod, inserarea se face pastrand ordinea
 * prioritatiilor. Astfel head-ul va reprezenta nodul cu cea mai mare
 * prioritate. Operatia de pop consta il mutarea headului.
 */

typedef struct node {
	int index;
	unsigned int priority;
	struct node *next;
	struct node *prev;
} node_t;

typedef struct priority_queue {
	node_t *head;
	node_t *tail;
} pq_t;

pq_t *init_priority_queue()
{
	pq_t *pq;

	pq = malloc(sizeof(pq_t));
	pq->head = NULL;
	pq->tail = NULL;
	return pq;
}

void insert_node(int index, unsigned int priority, pq_t *pq)
{
	node_t *crt_node;
	node_t *prev_node;
	node_t *new_node;

	if (pq->head == NULL) {
		pq->head = malloc(sizeof(node_t));
		pq->head->index = index;
		pq->head->priority = priority;
		pq->head->next = NULL;
		pq->head->prev = NULL;
		pq->tail = pq->head;
		return;
	}
	crt_node = pq->head;
	prev_node = NULL;
	while (crt_node != NULL && crt_node->priority >= priority) {
		prev_node = crt_node;
		crt_node = crt_node->next;
	}
	new_node = malloc(sizeof(node_t));
	new_node->index = index;
	new_node->priority = priority;
	new_node->next = crt_node;
	new_node->prev = prev_node;
	if (prev_node)
		prev_node->next = new_node;
	if (crt_node)
		crt_node->prev = new_node;
	if (prev_node == NULL)
		pq->head = new_node;
	if (crt_node == NULL)
		pq->tail = new_node;
}

int get_top_index(pq_t *pq)
{
	if (pq->head != NULL)
		return pq->head->index;
	return -1;
}

int get_top_priority(pq_t *pq)
{
	if (pq->head != NULL)
		return pq->head->priority;
	return -1;
}

void pop(pq_t *pq)
{
	node_t *after_head;

	if (pq->head == NULL)
		return;
	if (pq->head->next == NULL) {
		free(pq->head);
		pq->head = NULL;
		pq->tail = NULL;
		return;
	}
	after_head = pq->head->next;
	after_head->prev = NULL;
	free(pq->head);
	pq->head = after_head;
}

void free_queue(pq_t *pq)
{
	node_t *tmp;

	while (pq->head != NULL) {
		tmp = pq->head;
		pq->head = pq->head->next;
		free(tmp);
	}
	free(pq);
}
