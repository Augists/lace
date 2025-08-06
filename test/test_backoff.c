#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <lace.h>

VOID_TASK_1(sleeptask, long, us)
void sleeptask_CALL(lace_worker *lace, long us)
{
    lace_sleep_us(us);
}

TASK_1(long, pfib, int, n)
long pfib_CALL(lace_worker* worker, int n)
{
    if (n<2) return n;
    long m,k;
    pfib_SPAWN(worker, n-1);
    k = pfib_CALL(worker, n-2);
    m = pfib_SYNC(worker);
    return m+k;
}

double wctime() 
{
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return (tv.tv_sec + 1E-9 * tv.tv_nsec);
}

int
main (int argc, char *argv[])
{
    int n_workers = 0; // automatically detect number of workers

    if (argc > 1) {
        n_workers = atoi(argv[1]);
    }

    lace_set_verbosity(1);
    lace_start(n_workers, 0, 0);
    double time0 = wctime();
    long res = pfib(35);
    double time1 = wctime();
    res = pfib(35);
    double time2 = wctime();
    res = pfib(35);
    double time3 = wctime();
    sleeptask(1000000);
    double time4 = wctime();
    res = pfib(35);
    double time5 = wctime();
    res = pfib(35);
    double time6 = wctime();
    res = pfib(35);
    double time7 = wctime();
    sleeptask(1000000);
    double time8 = wctime();
    res = pfib(35);
    double time9 = wctime();
    res = pfib(35);
    double time10 = wctime();
    res = pfib(35);
    double time11 = wctime();
    lace_stop();

    printf("Calculating pfib(35) -: %f\n", (time2-time1));
    printf("Calculating pfib(35) -: %f\n", (time3-time2));
    printf("Calculating pfib(35) +: %f\n", (time5-time4));
    printf("Calculating pfib(35) -: %f\n", (time6-time5));
    printf("Calculating pfib(35) -: %f\n", (time7-time6));
    printf("Calculating pfib(35) +: %f\n", (time9-time8));
    printf("Calculating pfib(35) -: %f\n", (time10-time9));
    printf("Calculating pfib(35) -: %f\n", (time11-time10));

    return 0;
}
