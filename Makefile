build: libscheduler.so

libscheduler.so: so_scheduler.o
	gcc -shared -o libscheduler.so so_scheduler.o -pthread
so_scheduler.o: so_scheduler.c priority_queue.h info_vec.h io_threads_list.h
	gcc -fPIC -Wall -g -c so_scheduler.c

clean:
	rm so_scheduler.o
	rm libscheduler.so
