#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "so_scheduler.h"

#define NEW 0
#define READY 1
#define WAITING 2
#define RUNNING 3
#define TERMINATED 4

#define INIT_SIZE 2500

/* structura contine toate informatiile unu thread iar info_vector contine
 * lista tuturor threadurilor create. Fiind maxim 1024 de threaduri create
 * la un test am preferat sa aloc de la inceput suficient spatiu.
 * Primul thread adaugat va fi la indexul 0, iar indexul se incrementeaza
 * la fiecare adaugare
 */

typedef struct thread_info {
	tid_t thread_id;
	so_handler *func;
	unsigned int priority;
	int time_on_proc;
	int waiting_io;
	int state;
} tinf_t;

typedef struct info_vector {
	int idx;
	int size;
	tinf_t *vec;
} info_vec_t;

info_vec_t *init_info_vec(int cap)
{
	info_vec_t *v;

	v = malloc(sizeof(info_vec_t));
	v->size = cap;
	v->idx = 0;
	v->vec = malloc(v->size * sizeof(tinf_t));

	//fprintf(stderr, "%d\n", v->idx);
	return v;
}

void add_info(info_vec_t *v, so_handler *func, tid_t thread_id,
		unsigned int prior, int waiting_io, int state) {
	v->vec[v->idx].thread_id = thread_id;
	v->vec[v->idx].func = func;
	v->vec[v->idx].priority = prior;
	v->vec[v->idx].waiting_io = waiting_io;
	v->vec[v->idx].state = state;
	v->vec[v->idx].time_on_proc = 0;
	v->idx = v->idx + 1;
}

void free_struct(info_vec_t *v) {
	free(v->vec);
	free(v);
}




