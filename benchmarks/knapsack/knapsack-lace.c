/*
 * Cilk program to solve the 0-1 knapsack problem using a branch-and-bound
 * technique.
 *
 * Author: Matteo Frigo
 */
/*
 * Copyright (c) 2000 Massachusetts Institute of Technology
 * Copyright (c) 2000 Matteo Frigo
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <time.h>
#include <lace.h>

#define __SYNC
//#define __PRUNE

struct item {
    int value;
    int weight;
};

int n = 32;
static int capacity = 900;
static int sol;

static struct item items[] = {
    { 15, 23 },
    { 22, 12 },
    { 17, 42 },
    { 1, 13 },
    { 32, 21 },
    { 65, 43 },
    { 23, 56 },
    { 4, 7 },
    { 4, 8 },
    { 32, 42 },
    { 51, 32 },
    { 22, 12 },
    { 17, 24 },
    { 12, 13 },
    { 23, 21 },
    { 56, 47 },
    { 23, 65 },
    { 6, 7 },
    { 4, 7 },
    { 32, 42 },
    { 22, 42 },
    { 59, 32 },
    { 23, 12 },
    { 12, 24 },
    { 12, 13 },
    { 23, 21 },
    { 39, 48 },
    { 22, 65 },
    { 6, 7 },
    { 4, 7 },
    { 33, 42 },
    { 18, 53 }
};

static int best_so_far = INT_MIN;

static int compare(struct item *a, struct item *b)
{
    double c = ((double) a->value / a->weight) -
        ((double) b->value / b->weight);

    if (c > 0)
        return -1;
    if (c < 0)
        return 1;
    return 0;
}

/*
 * return the optimal solution for n items (first is e) and
 * capacity c. Value so far is v.
 */
TASK_4(int, knapsack, struct item *, e, int, c, int, n, int, v)

int knapsack_CALL(LaceWorker* worker, struct item *e, int c, int n, int v)
{
    int with, without, best;

    /* base case: full knapsack or no items */
    if (c < 0)
        return INT_MIN;

    if (n == 0 || c == 0)
        return v;		/* feasible solution, with value v */

#ifdef __PRUNE
#ifdef __SYNC
    double ub = (double) v + c * e->value / e->weight;
    if (ub < __atomic_load_n(&best_so_far, __ATOMIC_ACQUIRE)) {
        /* prune ! */
        return INT_MIN;
    }
#else
    if (ub < best_so_far) {
        /* prune ! */
        return INT_MIN;
    }
#endif
#endif

#ifdef __REVERSE_EXEC_ORDER
    /* compute the best solution with the current item in the knapsack */
    knapsack_SPAWN(worker, e + 1, c - e->weight, n - 1, v + e->value);
    /* compute the best solution without the current item in the knapsack */
    without = knapsack_CALL(worker, e + 1, c, n - 1, v);
    with = knapsack_SYNC(worker);
#else
    /* compute the best solution without the current item in the knapsack */
    knapsack_SPAWN(worker, e + 1, c, n - 1, v);
    /* compute the best solution with the current item in the knapsack */
    with = knapsack_CALL(worker, e + 1, c - e->weight, n - 1, v + e->value);
    without = knapsack_SYNC(worker);
#endif

    best = with > without ? with : without;

    /*
     * notice the race condition here. The program is still
     * correct, in the sense that the best solution so far
     * is at least best_so_far. Moreover best_so_far gets updated
     * when returning, so eventually it should get the right
     * value. The program is highly non-deterministic.
     */
#ifdef __SYNC
    int bsf = __atomic_load_n(&best_so_far, __ATOMIC_ACQUIRE);
    do {
        if (bsf >= best)
            break;
    } while (!__atomic_compare_exchange_n(&best_so_far, &bsf, best, 0, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE));
#else
    if (best > best_so_far)
        best_so_far = best;
#endif

    return best;
}

void init()
{
    /* sort the items on decreasing order of value/weight */
    qsort(items, n, sizeof(struct item),
            (int (*)(const void *, const void *)) compare);
}

void prep()
{
#ifdef __SYNC
    __atomic_store_n(&best_so_far, INT_MIN, __ATOMIC_RELEASE);
#else
    best_so_far = INT_MIN;
#endif
}

static double wctime() 
{
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return (tv.tv_sec + 1E-9 * tv.tv_nsec);
}

void usage(char *s)
{
    fprintf(stderr, "Usage: %s [-w <workers>] [-q dqsize]\n", s);
}

int main(int argc, char *argv[])
{
    int workers = 1;
    int dqsize = 100000;

    char c;
    while ((c=getopt(argc, argv, "w:q:h")) != -1) {
        switch (c) {
            case 'w':
                workers = atoi(optarg);
                break;
            case 'q':
                dqsize = atoi(optarg);
                break;
            case 'h':
                usage(argv[0]);
                break;
            default:
                abort();
        }
    }

    lace_start(workers, dqsize, 0);

    init();
    prep();

    double t1 = wctime();
    sol = knapsack(items, capacity, n, 0);
    double t2 = wctime();

    printf("Best value is %d\n", sol);
    printf("Time: %f\n", t2-t1);

    lace_stop();

    return 0;
}
