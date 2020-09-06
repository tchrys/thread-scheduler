
#include <stdio.h>
#include<pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "so_scheduler.h"
#include "priority_queue.h"
#include "info_vec.h"
#include "io_threads_list.h"

#define MAX_IOS 256

/* sched_t contine cuanta de timp, nr de dispozitive, o coada de prioritati,
 * inca o coada pt nodurile preemptate, informatiile pt vectori si lista
 * de threaduri blocate
 */

typedef struct scheduler {
	unsigned int time_quantum;
	unsigned int io;
	pq_t *ready_queue;
	pq_t *quantum_queue;
	info_vec_t *threads_info;
	io_thr_t *io_blocked;

} sched_t;

/* init_called este folosit la init si end
 * step mutex este mutexul principal (cel care impreuna cu variabila de
 * conditie permite executarea unei functii)
 * check mutex este folosit in principal la operatiile pe coada, care
 * nu sunt thread_safe (pop, insert)
 * proc_tid este tid-ul procesului care lanseaza primul thread. Am avut
 * impresia ca poate executa si el orice functie, dar am vazut dupa
 * ca se ocupa doar cu init, end si primul fork
 */

sched_t my_sched;
int init_called;
pthread_mutex_t step_mutex;
pthread_mutex_t check_mutex;
pthread_mutex_t index_mutex;
pthread_cond_t cv;
tid_t proc_tid;

/* functia returneaza 1 daca putem intra in sectiunea critica
 * altfel intoarce 0
 */

int check_scheduler(int idx)
{
	int top_idx;
	int time;
	unsigned int my_prior;
	int nx_idx;
	unsigned int nx_p;

	if (idx == -1)
		return 1;
	pthread_mutex_lock(&check_mutex);
	top_idx = get_top_index(my_sched.ready_queue);
	time = my_sched.threads_info->vec[idx].time_on_proc;
	/* daca coada e goala inseamna ca putem intra in sectiunea critica */
	if (top_idx == -1) {
		my_prior = my_sched.threads_info->vec[idx].priority;
		insert_node(idx, my_prior, my_sched.ready_queue);
		pthread_mutex_unlock(&check_mutex);
		return 1;
	}
	/* nu suntem in varful cozii */
	if (top_idx != idx) {
		pthread_cond_signal(&cv);
		pthread_mutex_unlock(&check_mutex);
		return 0;
	}
	/* daca ne-a expirat timpul ne scoatem de pe coada, daca coada devine
	 * goala sau varful are prioritate mai mica ne punem la loc,
	 * altfel ne punem in quantum_queue daca acolo exista un thread cu
	 * prioritate mai mare
	 */
	if (my_sched.time_quantum <= time && my_sched.time_quantum != 0) {
		my_sched.threads_info->vec[idx].time_on_proc = 0;
		my_prior = get_top_priority(my_sched.ready_queue);
		pop(my_sched.ready_queue);
		if (get_top_index(my_sched.ready_queue) == -1) {
			if (get_top_index(my_sched.quantum_queue) == -1) {
				insert_node(idx, my_prior,
						my_sched.ready_queue);
				pthread_mutex_unlock(&check_mutex);
				return 1;
			}
			nx_idx = get_top_index(my_sched.quantum_queue);
			nx_p = get_top_priority(my_sched.quantum_queue);
			if (nx_p > my_prior) {
				pop(my_sched.quantum_queue);
				insert_node(nx_idx, nx_p,
						my_sched.ready_queue);
				insert_node(idx, my_prior,
						my_sched.quantum_queue);
			} else
				insert_node(idx, my_prior,
						my_sched.ready_queue);
		} else {
			if (get_top_priority(my_sched.ready_queue) > my_prior)
				insert_node(idx, my_prior,
						my_sched.quantum_queue);
			else
				insert_node(idx, my_prior,
						my_sched.ready_queue);
		}
		pthread_cond_signal(&cv);
		pthread_mutex_unlock(&check_mutex);
		return 0;
	}
	/* nodurile in starea waiting sau terminated ies de pe coada */
	if (my_sched.threads_info->vec[top_idx].state == TERMINATED) {
		pop(my_sched.ready_queue);
		pthread_mutex_unlock(&check_mutex);
		return 0;
	}
	if (my_sched.threads_info->vec[idx].state == WAITING) {
		pop(my_sched.ready_queue);
		pthread_mutex_unlock(&check_mutex);
		return 0;
	}
	pthread_mutex_unlock(&check_mutex);
	return 1;
}

/* cu ajutorul acestei functii fiecare thread isi obtine indexul din
 * vector prin cautarea tid-ului
 */

int get_idx_from_tid(void)
{
	int i;
	int size;
	int tid;

	tid = syscall(SYS_gettid);
	if (proc_tid == tid)
		return -1;
	pthread_mutex_lock(&index_mutex);
	size = my_sched.threads_info->idx;
	pthread_mutex_unlock(&index_mutex);
	for (i = 0; i < size; ++i) {
		if (pthread_equal(my_sched.threads_info->vec[i].thread_id,
					pthread_self()))
			return i;
	}
	return -2;
}


void *thread_function(void *params)
{
	int idx;
	so_handler *func;
	unsigned int prior;
	/* asteptam ca thread-ul sa isi obtina tid-ul */
	while (get_idx_from_tid() == -2)
		continue;
	idx = get_idx_from_tid();
	prior = my_sched.threads_info->vec[idx].priority;
	func = my_sched.threads_info->vec[idx].func;
	pthread_mutex_lock(&step_mutex);
	/* verificam daca putem intra in functie
	 * acest procedeu va aparea la sfarsitul fiecarei instructiuni
	 * (mai exact functie din cod) pentru a putea intra in urmatoarea
	 */
	while (!check_scheduler(idx))
		pthread_cond_wait(&cv, &step_mutex);
	pthread_cond_signal(&cv);
	pthread_mutex_unlock(&step_mutex);
	func(prior);
	/* am terminat, ma pot scoate de pe coada */
	my_sched.threads_info->vec[idx].state = TERMINATED;
	pthread_mutex_lock(&check_mutex);
	if (get_top_index(my_sched.ready_queue) == idx)
		pop(my_sched.ready_queue);
	pthread_cond_signal(&cv);
	pthread_mutex_unlock(&check_mutex);
	return NULL;
}

int so_init(unsigned int time_quantum, unsigned int io)
{
	int i;

	if (time_quantum == 0)
		return -1;
	if (io > 256)
		return -1;
	if (init_called == 1)
		return -1;
	init_called = 1;
	proc_tid = syscall(SYS_gettid);
	my_sched.time_quantum = time_quantum;
	my_sched.io = io;
	my_sched.ready_queue = init_priority_queue();
	my_sched.threads_info = init_info_vec(INIT_SIZE);
	my_sched.io_blocked = init_io_list((int)io);
	my_sched.quantum_queue = init_priority_queue();
	for (i = 0; i < INIT_SIZE; ++i)
		my_sched.threads_info->vec[i].thread_id = INVALID_TID;
	pthread_cond_init(&cv, NULL);
	pthread_mutex_init(&step_mutex, NULL);
	pthread_mutex_init(&check_mutex, NULL);
	pthread_mutex_init(&index_mutex, NULL);
	return 0;
}


tid_t so_fork(so_handler *func, unsigned int priority)
{
	tid_t new_thread;
	int caller_id;

	caller_id = get_idx_from_tid();
	if (priority > SO_MAX_PRIO) {
		pthread_cond_signal(&cv);
		pthread_mutex_unlock(&step_mutex);
		return INVALID_TID;
	}
	if (func == NULL) {
		pthread_cond_signal(&cv);
		pthread_mutex_unlock(&step_mutex);
		return INVALID_TID;
	}
	/* bag noul thread in coada, il creez si adaug info in vector */
	pthread_mutex_lock(&check_mutex);
	insert_node(my_sched.threads_info->idx, priority,
			my_sched.ready_queue);
	pthread_mutex_unlock(&check_mutex);
	if (pthread_create(&new_thread, NULL, &thread_function, NULL)) {
		perror("pthread_create");
		exit(1);
	}
	pthread_mutex_lock(&index_mutex);
	add_info(my_sched.threads_info, func, new_thread, priority, -1, READY);
	pthread_mutex_unlock(&index_mutex);
	/* procesul parinte e responsabil de primul fork, dupa aceea forkurile
	 * provin doar din thread-uri
	 */
	if (caller_id != -1)
		my_sched.threads_info->vec[caller_id].time_on_proc++;
	pthread_mutex_lock(&step_mutex);
	while (!check_scheduler(caller_id))
		pthread_cond_wait(&cv, &step_mutex);
	pthread_cond_signal(&cv);
	pthread_mutex_unlock(&step_mutex);
	return new_thread;
}

int so_wait(unsigned int io)
{
	int caller_id;
	int top_index;

	caller_id = get_idx_from_tid();
	if (io < 0 || io >= my_sched.io) {
		pthread_cond_signal(&cv);
		pthread_mutex_unlock(&step_mutex);
		return -1;
	}
	/* mutex pt stergerea din coada */
	pthread_mutex_lock(&check_mutex);
	top_index = get_top_index(my_sched.ready_queue);
	my_sched.threads_info->vec[caller_id].waiting_io = io;
	my_sched.threads_info->vec[caller_id].state = WAITING;
	my_sched.threads_info->vec[caller_id].time_on_proc = 0;
	pop(my_sched.ready_queue);
	pthread_mutex_unlock(&check_mutex);
	add_index(my_sched.io_blocked, top_index, (int)io);
	pthread_mutex_lock(&step_mutex);
	while (!check_scheduler(caller_id))
		pthread_cond_wait(&cv, &step_mutex);
	pthread_cond_signal(&cv);
	pthread_mutex_unlock(&step_mutex);
	return 0;
}

int so_signal(unsigned int io)
{
	int i;
	int index;
	int prior;
	int no_threads;
	int caller_id;
	int *thr_blocked;

	caller_id = get_idx_from_tid();
	if (io < 0 || io >= my_sched.io) {
		pthread_cond_signal(&cv);
		pthread_mutex_unlock(&step_mutex);
		return -1;
	}
	/* luam thread-urile blocate de acel io, le punem pe coada si le
	 * modificam starea
	 */
	no_threads = get_list_size(my_sched.io_blocked, (int)io);
	thr_blocked = get_all_thr_blocked_by(my_sched.io_blocked, (int)io);
	for (i = 0; i < no_threads; ++i) {
		index = thr_blocked[i];
		prior = my_sched.threads_info->vec[index].priority;
		my_sched.threads_info->vec[index].waiting_io = -1;
		my_sched.threads_info->vec[index].state = READY;
		pthread_mutex_lock(&check_mutex);
		insert_node(index, prior, my_sched.ready_queue);
		pthread_mutex_unlock(&check_mutex);
	}
	free(thr_blocked);
	reset_list_size(my_sched.io_blocked, (int)io);
	if (caller_id != -1)
		my_sched.threads_info->vec[caller_id].time_on_proc++;
	pthread_mutex_lock(&step_mutex);
	while (!check_scheduler(caller_id))
		pthread_cond_wait(&cv, &step_mutex);
	pthread_cond_signal(&cv);
	pthread_mutex_unlock(&step_mutex);
	pthread_cond_signal(&cv);
	return no_threads;
}

void so_exec(void)
{
	int caller_id;

	caller_id = get_idx_from_tid();
	if (caller_id != -1)
		my_sched.threads_info->vec[caller_id].time_on_proc++;
	pthread_mutex_lock(&step_mutex);
	while (!check_scheduler(caller_id))
		pthread_cond_wait(&cv, &step_mutex);
	pthread_cond_signal(&cv);
	pthread_mutex_unlock(&step_mutex);
}

void so_end(void)
{
	int i;
	int caller_id;
	tid_t t;

	if (init_called == 0)
		return;
	init_called = 0;
	caller_id = get_idx_from_tid();
	i = 0;
	/* asteptam toate threadurile */
	while (i < my_sched.threads_info->idx) {
		if (i == caller_id)
			continue;
		t = my_sched.threads_info->vec[i].thread_id;
		if (pthread_join(t, NULL))
			perror("pthread join");
		i++;
	}
	free_queue(my_sched.ready_queue);
	free_queue(my_sched.quantum_queue);
	free_all(my_sched.io_blocked);
	free_struct(my_sched.threads_info);
	pthread_mutex_destroy(&step_mutex);
	pthread_mutex_destroy(&check_mutex);
	pthread_cond_destroy(&cv);
}
