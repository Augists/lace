#include <stdio.h>
#include <stdlib.h>

#include <lace.h>

VOID_TASK_1(test_together, int, depth);
VOID_TASK_1(test_newframe, int, depth);

void test_together(LaceWorker* worker, int depth)
{
    if (depth != 0) {
        test_together_SPAWN(worker, depth-1);
        test_together_SPAWN(worker, depth-1);
        test_together_SPAWN(worker, depth-1);
        test_together_SPAWN(worker, depth-1);
        test_newframe_NEWFRAME(depth-1);
        test_together_SYNC(worker);
        test_together_SYNC(worker);
        test_together_SYNC(worker);
        test_together_SYNC(worker);
    }
}

void test_newframe(LaceWorker* worker, int depth)
{
    if (depth != 0) {
        test_newframe_SPAWN(worker, depth-1);
        test_newframe_SPAWN(worker, depth-1);
        test_newframe_SPAWN(worker, depth-1);
        test_newframe_SPAWN(worker, depth-1);
        test_together_TOGETHER(depth-1);
        test_newframe_SYNC(worker);
        test_newframe_SYNC(worker);
        test_newframe_SYNC(worker);
        test_newframe_SYNC(worker);
    }
}

VOID_TASK_0(test_something)
void test_something(LaceWorker* worker)
{
    printf("running from worker %d\n", lace_worker_id());
}

VOID_TASK_1(_main, void*, arg)
void _main(LaceWorker* worker, void* arg)
{
    fprintf(stdout, "Testing TOGETHER and NEWFRAME with %u workers...\n", lace_worker_count());

    for (int i=0; i<5; i++) {
        test_newframe_NEWFRAME(5);
        test_together_TOGETHER(5);
    }

    test_something_RUN();

    // We didn't use arg
    (void)arg;
}

void
runtests(int n_workers)
{
    // Initialize the Lace framework for <n_workers> workers.
    lace_start(n_workers, 0);

    printf("Newframe:\n");
    test_something_NEWFRAME();

    printf("Together:\n");
    test_something_TOGETHER();

    lace_suspend();
    lace_resume();

    // Spawn and start all worker pthreads; suspends current thread until done.
    printf("Running (10x):\n");
    for (int i=0; i<5; i++) {
        printf("%d: ", i);
        test_something_RUN();
    }

    // Spawn and start all worker pthreads; suspends current thread until done.
    printf("Recursive test\n");
    _main_RUN(NULL);

    // The lace_startup command also exits Lace after _main is completed.
    lace_stop();
}

int
main (int argc, char *argv[])
{
    int n_workers = 0; // automatically detect number of workers

    if (argc > 1) {
        n_workers = atoi(argv[1]);
    }

    lace_set_verbosity(1);

    for (int i=0; i<5; i++) {
        runtests(n_workers);
    }

    return 0;
}
