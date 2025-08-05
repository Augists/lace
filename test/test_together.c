#include <stdio.h>
#include <stdlib.h>

#include <lace.h>

int counter;

VOID_TASK_0(test_count)
void test_count_CALL(lace_worker* worker)
{
    counter++;
    (void)worker;
}


int *worker_counter;

VOID_TASK_0(test_count_perworker)
void test_count_perworker_CALL(lace_worker* worker)
{
    worker_counter[lace_worker_id()]++;
}

VOID_TASK_1(test_only_newframe, int, depth)
void test_only_newframe_CALL(lace_worker* worker, int depth)
{
    if (depth > 0) {
        test_only_newframe_SPAWN(worker, depth-1);
        test_only_newframe_SPAWN(worker, depth-1);
        test_only_newframe_SPAWN(worker, depth-1);
        test_only_newframe_SPAWN(worker, depth-1);
        test_only_newframe_SYNC(worker);
        test_only_newframe_SYNC(worker);
        test_only_newframe_SYNC(worker);
        test_only_newframe_SYNC(worker);
    } else {
        test_count_NEWFRAME();
    }
}

VOID_TASK_1(test_only_together, int, depth)
void test_only_together_CALL(lace_worker* worker, int depth)
{
    if (depth > 0) {
        test_only_together_SPAWN(worker, depth-1);
        test_only_together_SPAWN(worker, depth-1);
        test_only_together_SPAWN(worker, depth-1);
        test_only_together_SPAWN(worker, depth-1);
        test_only_together_SYNC(worker);
        test_only_together_SYNC(worker);
        test_only_together_SYNC(worker);
        test_only_together_SYNC(worker);
    } else {
        test_count_perworker_TOGETHER();
    }
}

VOID_TASK_1(test_together, int, depth)
VOID_TASK_1(test_newframe, int, depth)

void test_together_CALL(lace_worker* worker, int depth)
{
    if (depth > 0) {
        test_together_SPAWN(worker, depth-1);
        test_together_SPAWN(worker, depth-1);
        test_together_SPAWN(worker, depth-1);
        test_together_SPAWN(worker, depth-1);
        test_together_SYNC(worker);
        test_together_SYNC(worker);
        test_together_SYNC(worker);
        test_together_SYNC(worker);
    } else {
        test_only_newframe_CALL(worker, 3);
    }
}

void test_newframe_CALL(lace_worker* worker, int depth)
{
    if (depth > 0) {
        test_newframe_SPAWN(worker, depth-1);
        test_newframe_SPAWN(worker, depth-1);
        test_newframe_SPAWN(worker, depth-1);
        test_newframe_SPAWN(worker, depth-1);
        test_newframe_SYNC(worker);
        test_newframe_SYNC(worker);
        test_newframe_SYNC(worker);
        test_newframe_SYNC(worker);
    } else {
        test_only_together_CALL(worker, 3);
    }
}

VOID_TASK_0(test_report_id)
void test_report_id_CALL(lace_worker* worker)
{
    printf("running from worker %d\n", lace_worker_id());
}

void
runtests(int n_workers)
{
    // Initialize the Lace framework for <n_workers> workers.
    lace_start(n_workers, 0, 0);
    lace_suspend();  // actually start suspended, Lace autoresumes.

    worker_counter = (int*)malloc(lace_worker_count() * sizeof(int));

    /*
    printf("Reporting worker id with newframe:\n");
    test_report_id_NEWFRAME();

    printf("Reporting worked id with together:\n");
    test_report_id_TOGETHER();
    */

    printf("Testing only newframe...\n");
    counter = 0;
    test_only_newframe(6);
    printf("Counter reads %d (expecting 4096)\n", counter);
    if (counter != 4096) exit(1);

    printf("Testing only together...\n");
    for (int i=0; i<lace_worker_count(); i++) worker_counter[i] = 0;
    test_only_together(6);
    for (int i=0; i<lace_worker_count(); i++) {
        printf("Counter %d reads %d (expecting 4096)\n", i, worker_counter[i]);
        if (worker_counter[i] != 4096) exit(1);
    }

    // Spawn and start all worker pthreads; suspends current thread until done.
    printf("Testing mixed newframe/together...\n");
    worker_counter = (int*)calloc(lace_worker_count(), sizeof(int));
    test_newframe(5);
    test_together(5);
    for (int i=0; i<lace_worker_count(); i++) {
        printf("Counter %d reads %d (expecting 65536)\n", i, worker_counter[i]);
        if (worker_counter[i] != 65536) exit(1);
    }

    free(worker_counter);

    // Finally, stop the workers again.
    lace_stop();
}

int
main (int argc, char *argv[])
{
    int n_workers = 0; // automatically detect number of workers

    if (argc > 1) {
        n_workers = atoi(argv[1]);
    }

    lace_set_verbosity(0);

#define EXECUTIONS 3

    for (int i=0; i<EXECUTIONS; i++) {
        printf("### RUNNING TEST %d OF %d ###\n", i+1, EXECUTIONS);
        runtests(n_workers);
        printf("\n");
    }

    return 0;
}
