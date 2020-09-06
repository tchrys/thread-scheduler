#include <stdlib.h>
#include "so_scheduler.h"

#define INIT_LIST_CAP 1000

/* structura contine o lista de liste pentru fiecare io
 * cu threadurile in starea waiting si un vector cu
 * numarul de threaduri blocate de fiecare io
 */

typedef struct io_list {
	int total_ios;
	int **lists_per_io;
	int cap_per_list;
	int *size_per_list;
} io_thr_t;

io_thr_t *init_io_list(int cap)
{
	int i;
	io_thr_t *v;

	v = malloc(sizeof(io_thr_t));
	v->total_ios = cap;
	v->cap_per_list = INIT_LIST_CAP;
	v->size_per_list = calloc(cap, sizeof(int));
	v->lists_per_io = malloc(cap * sizeof(int *));
	for (i = 0; i < cap; ++i)
		v->lists_per_io[i] = malloc(v->cap_per_list * sizeof(int));
	return v;
}

void add_index(io_thr_t *v, int thread_nr, int io)
{
	v->lists_per_io[io][v->size_per_list[io]] = thread_nr;
	v->size_per_list[io] = v->size_per_list[io] + 1;
}

int *get_all_thr_blocked_by(io_thr_t *v, int io)
{
	int i;
	int *ans;

	ans = malloc(v->size_per_list[io] * sizeof(int));
	for (i = 0; i < v->size_per_list[io]; ++i)
		ans[i] = v->lists_per_io[io][i];
	return ans;
}

void reset_list_size(io_thr_t *v, int io)
{
	v->size_per_list[io] = 0;
}

int get_list_size(io_thr_t *v, int io)
{
	return v->size_per_list[io];
}

void free_all(io_thr_t *v)
{
	int i;

	for (i = 0; i < v->total_ios; ++i)
		free(v->lists_per_io[i]);
	free(v->lists_per_io);
	free(v->size_per_list);
	free(v);
}

