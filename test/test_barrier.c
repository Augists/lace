#include <stdio.h>
#include <stdlib.h>

#include <lace.h>

int *worker_counter;

VOID_TASK_0(test_barrier)
void test_barrier_CALL(lace_worker* worker)
{
    int id = lace_worker_id();
    int count = lace_worker_count();

    for (int i=0; i<1000; i++) {
        if (i % count == 0) {
            sched_yield();
            nanosleep(&(struct timespec){.tv_nsec = 2000}, NULL);
        }

        worker_counter[id]++;

        lace_barrier();

        for (int j=0; j<count; j++) {
            if (worker_counter[j] != i+1) {
                printf("Mismatch at iteration %d: worker_counter[%d] = %d (expected %d)\n", i, j, worker_counter[j], i+1);
                exit(1);
            }
        }

        lace_barrier();
    }
}

int
main (int argc, char *argv[])
{
    int n_workers = 0; // automatically detect number of workers

    if (argc > 1) {
        n_workers = atoi(argv[1]);
    }

    lace_set_verbosity(0);

#define EXECUTIONS 5

    for (int i=0; i<EXECUTIONS; i++) {
        printf("### RUNNING TEST %d OF %d ###\n", i+1, EXECUTIONS);

        lace_start(n_workers, 0, 0);

        worker_counter = (int*)calloc(lace_worker_count(), sizeof(int));
        test_barrier_TOGETHER();
        free(worker_counter);

        lace_stop();

        printf("\n");
    }

    return 0;
}
