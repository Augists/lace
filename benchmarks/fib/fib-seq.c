#include <stdio.h>
#include <stdlib.h>
#include <time.h>

double wctime() 
{
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return (tv.tv_sec + 1E-9 * tv.tv_nsec);
}

int pfib(int n)
{
    if (n < 2) {
        return n;
    } else {
        return pfib(n-1) + pfib(n-2);
    }
}

int main( int argc, char **argv )
{
    int n,m;

    if (argc < 2) {
        fprintf(stderr, "Usage: fib-seq <arg>\n");
        exit(2);
    }

    n = atoi(argv[1]);

    printf("Running fibonacci n=%d sequentially...\n", n);

    double t1 = wctime();
    m = pfib(n);
    double t2 = wctime();

    printf("fib(%d) = %d\n", n, m);
    printf("Time: %f\n", t2-t1);
    return 0;
}

