/*
 * Copyright 2013-2016 Formal Methods and Tools, University of Twente
 * Copyright 2016-2018 Tom van Dijk, Johannes Kepler University Linz
 * Copyright 2019-2025 Tom van Dijk, Formal Methods and Tools, University of Twente
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define _GNU_SOURCE
#include <errno.h> // for errno
#include <sched.h> // for sched_getaffinity
#include <stddef.h> // for size_t
#include <stdio.h>  // for fprintf
#include <stdlib.h> // for memalign, malloc
#include <string.h> // for memset
#include <time.h> // for clock_gettime
#include <pthread.h> // for POSIX threading
#include <stdatomic.h>

#ifndef _WIN32
#include <sys/resource.h> // for getrlimit
#endif

#ifdef _WIN32
#include <windows.h> // to use GetSystemInfo
#endif

#if defined(__APPLE__)
/* Mac OS X defines sem_init but actually does not implement them */
#include <mach/mach.h>

typedef semaphore_t sem_t;
#define sem_init(sem, x, value)	semaphore_create(mach_task_self(), sem, SYNC_POLICY_FIFO, value)
#define sem_wait(sem)           semaphore_wait(*sem)
#define sem_post(sem)           semaphore_signal(*sem)
#define sem_destroy(sem)        semaphore_destroy(mach_task_self(), *sem)
#else
#include <semaphore.h> // for sem_*
#endif

#include <lace128.h>

#if LACE_USE_MMAP
#include <sys/mman.h> // for mmap, etc
#endif

#if LACE_USE_HWLOC
#include <hwloc.h>

/**
 * HWLOC information
 */
static hwloc_topology_t topo;
static hwloc_cpuset_t *cpusets;
static unsigned int n_nodes, n_cores, n_pus;
#else
static unsigned int n_pus;
#endif

/**
 * Little helper to get cache line size
 */
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>

size_t get_cache_line_size(void)
{
    DWORD buffer_size = 0;
    GetLogicalProcessorInformation(NULL, &buffer_size);
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION *buffer = malloc(buffer_size);
    if (!buffer) return 64;

    if (!GetLogicalProcessorInformation(buffer, &buffer_size)) {
        free(buffer);
        return 64;
    }

    size_t line_size = 0;
    for (DWORD i = 0; i < buffer_size / sizeof(*buffer); i++) {
        if (buffer[i].Relationship == RelationCache &&
            buffer[i].Cache.Level == 1) {
            line_size = buffer[i].Cache.LineSize;
            break;
        }
    }

    free(buffer);
    return line_size ? line_size : 64;
}
#elif defined(__APPLE__)
size_t get_cache_line_size(void)
{
    #include <sys/sysctl.h>

    size_t line_size = 0;
    size_t size = sizeof(line_size);
    if (sysctlbyname("hw.cachelinesize", &line_size, &size, NULL, 0) == 0 && line_size != 0) {
        return line_size;
    }
    return 64;
}
#elif defined(__linux__)
size_t get_cache_line_size(void)
{
    long line_size = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    return (line_size > 0) ? (size_t)line_size : 64;
}
#else
size_t get_cache_line_size(void)
{
    // Unknown platform, fall back to a safe default
    return 64;
}
#endif

static size_t cache_line_size;

/**
 * (public) Worker data
 */
static lace_worker_public **workers = NULL;

/**
 * Size of the task deque
 */
static size_t dqsize = 100000;

/**
 * Verbosity flag, set with lace_set_verbosity
 */
static int verbosity = 0;

/**
 * Number of workers
 */
static unsigned int n_workers = 0;

/**
 * Datastructure of the task deque etc for each worker.
 * - first public cachelines (accessible via global "workers" variable)
 * - then private cachelines
 * - then the deque array
 */
typedef struct {
    lace_worker_public worker_public;
    alignas(LACE_PADDING_TARGET) lace_worker worker_private;
    alignas(LACE_PADDING_TARGET) lace_task deque[];
} worker_data;

/**
 * (Secret) holds pointers to the memory block allocated for each worker
 */
static worker_data **workers_memory = NULL;

/**
 * Number of bytes allocated for each worker's worker data.
 */
static size_t workers_memory_size = 0;

/**
 * (Secret) holds pointer to private worker data, just for stats collection at end
 */
static lace_worker **workers_p;

/**
 * Flag to signal all workers to quit.
 */
static atomic_int lace_quits = 0;
static atomic_uint workers_running = 0;

/**
 * Thread-specific mechanism to access current worker data
 */
#ifdef __linux__
__thread lace_worker *lace_thread_worker;
#else
pthread_key_t lace_thread_worker_key;
#endif

#ifndef LACE_LEAP_RANDOM /* Use random leaping when leapfrogging fails */
#define LACE_LEAP_RANDOM 1
#endif

lace_worker_public* lace_steal(lace_worker *self, lace_worker_public *victim);
int lace_shrink_shared(lace_worker *w);
void lace_leapfrog(lace_worker *__lace_worker);
void lace_drop_slow(lace_worker *w, lace_task *head);

/**
 * Global newframe variable used for the implementation of NEWFRAME and TOGETHER
 */
lace_newframe_t lace_newframe;

/**
 * Get the number of workers
 */
unsigned int
lace_worker_count()
{
    return n_workers;
}

/**
 * If we are collecting PIE times, then we need some helper functions.
 */
#if LACE_PIE_TIMES
static uint64_t count_at_start, count_at_end;
static uint64_t us_elapsed_timer;

static void
us_elapsed_start(void)
{
    struct timespec ts_now;
    clock_gettime(CLOCK_MONOTONIC, &ts_now);
    us_elapsed_timer = ts_now.tv_sec * 1000000LL + ts_now.tv_nsec/1000;
}

static long long unsigned
us_elapsed(void)
{
    struct timespec ts_now;
    clock_gettime(CLOCK_MONOTONIC, &ts_now);
    uint64_t t = ts_now.tv_sec * 1000000LL + ts_now.tv_nsec/1000;
    return t - us_elapsed_timer;
}
#endif

/**
 * Lace barrier implementation, that synchronizes on all workers.
 */
typedef struct {
    atomic_int __attribute__((aligned(LACE_PADDING_TARGET))) count;
    atomic_int __attribute__((aligned(LACE_PADDING_TARGET))) leaving;
    atomic_int __attribute__((aligned(LACE_PADDING_TARGET))) wait;
} barrier_t;

barrier_t lace_bar;

/**
 * Enter the Lace barrier and wait until all workers have entered the Lace barrier.
 */
void
lace_barrier()
{
    int wait = atomic_load_explicit(&lace_bar.wait, memory_order_relaxed);
    if ((int)n_workers == 1 + atomic_fetch_add_explicit(&lace_bar.count, 1, memory_order_acq_rel)) {
        // This thread is the last to arrive (the leader)
        atomic_store_explicit(&lace_bar.count, 0, memory_order_relaxed);
        atomic_store_explicit(&lace_bar.leaving, n_workers, memory_order_relaxed);
        atomic_store_explicit(&lace_bar.wait, 1 - wait, memory_order_release);
    } else {
        // Wait until leader flips the wait value
        while (atomic_load_explicit(&lace_bar.wait, memory_order_acquire) == wait) {
            // possibly pause/yield
        }
    }
    // Needed for lace_barrier_destroy to observe all threads have exited
    atomic_fetch_sub_explicit(&lace_bar.leaving, 1, memory_order_release);
}

/**
 * Initialize the Lace barrier
 */
static void
lace_barrier_init()
{
    memset(&lace_bar, 0, sizeof(barrier_t));
}

/**
 * Destroy the Lace barrier (just wait until all are exited)
 */
static void
lace_barrier_destroy()
{
    while (atomic_load_explicit(&lace_bar.leaving, memory_order_acquire) > 0) {
        // possibly pause/yield
    }
}

/**
 * For debugging purposes, check if memory is allocated on the correct memory nodes.
 */
static void __attribute__((unused))
lace_check_memory(void)
{
#if LACE_USE_HWLOC
    // get our current worker
    lace_worker *w = lace_get_worker();
    void* mem = workers_memory[w->worker];

    // get pinned PUs
    hwloc_cpuset_t cpuset = hwloc_bitmap_alloc();
    hwloc_get_cpubind(topo, cpuset, HWLOC_CPUBIND_THREAD);

    // get nodes of pinned PUs
    hwloc_nodeset_t cpunodes = hwloc_bitmap_alloc();
    hwloc_cpuset_to_nodeset(topo, cpuset, cpunodes);

    // get location of memory
    hwloc_nodeset_t memlocation = hwloc_bitmap_alloc();
    hwloc_get_area_memlocation(topo, mem, sizeof(worker_data), memlocation, HWLOC_MEMBIND_BYNODESET);

    // check if CPU and node are on the same place
    if (!hwloc_bitmap_isincluded(memlocation, cpunodes)) {
        fprintf(stdout, "Lace warning: Lace thread not on same memory domain as data!\n");

        char *strp, *strp2, *strp3;
        hwloc_bitmap_list_asprintf(&strp, cpuset);
        hwloc_bitmap_list_asprintf(&strp2, cpunodes);
        hwloc_bitmap_list_asprintf(&strp3, memlocation);
        fprintf(stdout, "Worker %d is pinned on PUs %s, node %s; memory is pinned on node %s\n", w->worker, strp, strp2, strp3);
        free(strp);
        free(strp2);
        free(strp3);
    }

    // free allocated memory
    hwloc_bitmap_free(cpuset);
    hwloc_bitmap_free(cpunodes);
    hwloc_bitmap_free(memlocation);
#endif
}

void
lace_pin_worker(void)
{
#if LACE_USE_HWLOC
    // Get the worker id
    unsigned int worker = lace_get_worker()->worker;

    // Pin the thread
    if (hwloc_set_cpubind(topo, cpusets[worker], HWLOC_CPUBIND_THREAD) != 0) {
        fprintf(stderr, "Lace warning: hwloc_set_cpubind failed!\n");
    }

    // Grab nodeset for this cpuset
    hwloc_bitmap_t nodeset = hwloc_bitmap_alloc();
    if (hwloc_cpuset_to_nodeset(topo, cpusets[worker], nodeset) != 0) {
        fprintf(stderr, "Lace error: Unable to convert cpuset to nodeset!\n");
    }

    // Pin the memory area
    if (hwloc_set_area_membind(topo, workers_memory[worker], workers_memory_size, nodeset, HWLOC_MEMBIND_BIND, HWLOC_MEMBIND_STRICT | HWLOC_MEMBIND_MIGRATE | HWLOC_MEMBIND_BYNODESET) != 0) {
        fprintf(stderr, "Lace error: Unable to bind worker memory to node!\n");
    }

    // Free allocated memory
    hwloc_bitmap_free(nodeset);

    // Check if everything is on the correct node
    lace_check_memory();
#endif
}

void
lace_init_worker(unsigned int worker)
{
    // Allocate our memory
#if LACE_USE_MMAP
    workers_memory[worker] = mmap(NULL, workers_memory_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (workers_memory[worker] == MAP_FAILED) {
        fprintf(stderr, "Lace error: Unable to allocate mmapped memory for the Lace worker!\n");
        exit(1);
    }
#else
#if defined(_MSC_VER) || defined(__MINGW64_VERSION_MAJOR)
    workers_memory[worker] = _aligned_malloc(workers_memory_size, cache_line_size);
#elif defined(__MINGW32__)
    workers_memory[worker] = __mingw_aligned_malloc(workers_memory_size, cache_line_size);
#else
    workers_memory[worker] = aligned_alloc(cache_line_size, workers_memory_size);
#endif
    if (workers_memory[worker] == 0) {
        fprintf(stderr, "Lace error: Unable to allocate memory for the Lace worker!\n");
        exit(1);
    }
    memset(workers_memory[worker], 0, workers_memory_size);
#endif

    // Set pointers
    lace_worker_public *wt = workers[worker] = &workers_memory[worker]->worker_public;
    lace_worker *w = workers_p[worker] = &workers_memory[worker]->worker_private;
    w->dq = workers_memory[worker]->deque;
    w->head = w->dq;
#ifdef __linux__
    lace_thread_worker = w;
#else
    pthread_setspecific(lace_thread_worker_key, w);
#endif

    // Initialize public worker data
    wt->dq = w->dq;
    wt->ts.v = 0;
    wt->allstolen = 0;
    wt->movesplit = 0;

    // Initialize private worker data
    w->_public = wt;
    w->end = w->dq + dqsize;
    w->split = w->dq;
    w->allstolen = 0;
    w->worker = worker;
    w->rng = ((uint64_t)rand() << 32 | rand()) | 1ULL;

#if LACE_COUNT_EVENTS
    // Initialize counters
    { int k; for (k=0; k<CTR_MAX; k++) w->ctr[k] = 0; }
#endif

#if LACE_PIE_TIMES
    w->time = lace_gethrtime();
    w->level = 0;
#endif
}

static atomic_int must_suspend = 0;
static sem_t suspend_semaphore;
static atomic_int lace_awaken_count = 0;

void
lace_suspend()
{
    while (1) {
        int state = atomic_load_explicit(&lace_awaken_count, memory_order_consume);
        // state "should" be >= 1 !!
        if (state <= 0) {
            continue; // ???
        } else if (state == 1) {
            int next = -1; // intermediate state
            if (atomic_compare_exchange_weak(&lace_awaken_count, &state, next) == 1) {
                while (workers_running != n_workers) {} // they must first run, to avoid rare condition
                atomic_thread_fence(memory_order_seq_cst);
                atomic_store_explicit(&must_suspend, 1, memory_order_relaxed);
                while (workers_running != 0) {}
                atomic_thread_fence(memory_order_seq_cst);
                atomic_store_explicit(&must_suspend, 0, memory_order_relaxed);
                atomic_store_explicit(&lace_awaken_count, 0, memory_order_release);
                break;
            }
        } else {
            int next = state - 1;
            if (atomic_compare_exchange_weak(&lace_awaken_count, &state, next) == 1) break;
        }
    }
}

void
lace_resume()
{
    while (1) {
        int state = atomic_load_explicit(&lace_awaken_count, memory_order_consume);
        if (state < 0) {
            continue; // wait until suspending is done
        } else if (state == 0) {
            int next = -1; // intermediate state
            if (atomic_compare_exchange_weak(&lace_awaken_count, &state, next) == 1) {
                for (unsigned int i=0; i<n_workers; i++) sem_post(&suspend_semaphore);
                atomic_store_explicit(&lace_awaken_count, 1, memory_order_release);
                break;
            }
        } else {
            int next = state + 1;
            if (atomic_compare_exchange_weak(&lace_awaken_count, &state, next) == 1) break;
        }
    }
}

/**
 * "External" task management
 */

/**
 * Global "external" task
 */
typedef struct _Extlace_task {
    lace_task *task;
    sem_t sem;
} Extlace_task;

static _Atomic(Extlace_task*) external_task = NULL;

static pthread_mutex_t external_task_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t external_task_cond = PTHREAD_COND_INITIALIZER;

static int external_task_counter = 0;
static int external_task_exclusive = 0;

void
lace_run_task(lace_task *task)
{
    // check if we are really not in a Lace thread
    lace_worker* self = lace_get_worker();
    if (self != 0) {
        task->f(self, task);
    } else {
        // if needed, wake up the workers
        lace_resume();

        Extlace_task et;
        et.task = task;
        atomic_store_explicit(&et.task->thief, 0, memory_order_relaxed);
        sem_init(&et.sem, 0, 0);

        pthread_mutex_lock(&external_task_lock);
        while (external_task_exclusive) {
            // if "exclusive" is set, then we wait until we can continue
            pthread_cond_wait(&external_task_cond, &external_task_lock);
        }
        external_task_counter++;
        pthread_mutex_unlock(&external_task_lock);

        Extlace_task *exp = 0;
        while (atomic_compare_exchange_weak(&external_task, &exp, &et) != 1) {}

        sem_wait(&et.sem);
        sem_destroy(&et.sem);

        pthread_mutex_lock(&external_task_lock);
        external_task_counter--;
        if (external_task_exclusive && external_task_counter == 0) {
            // if counter is 0 and exclusive is set, then we should wake up the threads
            pthread_cond_broadcast(&external_task_cond);
        }
        pthread_mutex_unlock(&external_task_lock);

        // allow Lace workers to sleep again
        lace_suspend();
    }
}

void
lace_run_task_exclusive(lace_task *task)
{
    // check if we are really not in a Lace thread
    lace_worker* self = lace_get_worker();
    if (self != 0) {
        task->f(self, task);
    } else {
        // if needed, wake up the workers
        lace_resume();

        Extlace_task et;
        et.task = task;
        atomic_store_explicit(&et.task->thief, 0, memory_order_relaxed);
        sem_init(&et.sem, 0, 0);

        pthread_mutex_lock(&external_task_lock);
        while (external_task_exclusive) {
            // if "exclusive" is set, then we wait until we can continue
            pthread_cond_wait(&external_task_cond, &external_task_lock);
        }
        external_task_exclusive = 1;
        while (external_task_counter > 0) {
            // wait until all other tasks are done
            pthread_cond_wait(&external_task_cond, &external_task_lock);
        }
        pthread_mutex_unlock(&external_task_lock);

        Extlace_task *exp = 0;
        while (atomic_compare_exchange_weak(&external_task, &exp, &et) != 1) {}

        sem_wait(&et.sem);
        sem_destroy(&et.sem);

        pthread_mutex_lock(&external_task_lock);
        external_task_exclusive = 0;
        // wake up any waiters
        pthread_cond_broadcast(&external_task_cond);
        pthread_mutex_unlock(&external_task_lock);

        // allow Lace workers to sleep again
        lace_suspend();
    }
}

static inline void
lace_steal_external(lace_worker *self) 
{
    Extlace_task *stolen_task = atomic_exchange(&external_task, NULL);
    if (stolen_task != 0) {
        // execute task
        stolen_task->task->thief = self->_public;
        atomic_store_explicit(&stolen_task->task->thief, self->_public, memory_order_relaxed);
        lace_time_event(self, 1);
        // atomic_thread_fence(memory_order_relaxed);
        stolen_task->task->f(self, stolen_task->task);
        // atomic_thread_fence(memory_order_relaxed);
        lace_time_event(self, 2);
        // atomic_thread_fence(memory_order_relaxed);
        atomic_store_explicit(&stolen_task->task->thief, THIEF_COMPLETED, memory_order_relaxed);
        // atomic_thread_fence(memory_order_relaxed);
        sem_post(&stolen_task->sem);
        lace_time_event(self, 8);
    }
}

/**
 * (Try to) steal and execute a task from a random worker.
 */
void lace_steal_random(lace_worker *__lace_worker)
{
    lace_check_yield(__lace_worker);

    if (__builtin_expect(atomic_load_explicit(&external_task, memory_order_acquire) != 0, 0)) {
        lace_steal_external(__lace_worker);
    } else if (n_workers > 1) {
        lace_worker_public *victim = workers[(__lace_worker->worker + 1 + (lace_rng(__lace_worker) % (n_workers-1))) % n_workers];

        PR_COUNTSTEALS(__lace_worker, CTR_steal_tries);
        lace_worker_public *res = lace_steal(__lace_worker, victim);
        if (res == LACE_STOLEN) {
            PR_COUNTSTEALS(__lace_worker, CTR_steals);
        } else if (res == LACE_BUSY) {
            PR_COUNTSTEALS(__lace_worker, CTR_steal_busy);
        }
    }
}

/**
 * Main Lace worker implementation.
 * Steal from random victims until "quit" is set.
 */
VOID_TASK_1(lace_steal_loop, atomic_int*, quit)

void lace_steal_loop_CALL(lace_worker* lace_worker, atomic_int* quit)
{
    // Determine who I am
    const int worker_id = lace_worker->worker;

    // Prepare self, victim
    lace_worker_public ** const self = &workers[worker_id];
    lace_worker_public ** victim = self;

#if LACE_PIE_TIMES
    lace_worker->time = lace_gethrtime();
#endif

    unsigned int n = n_workers;
    int i=0;
#if LACE_BACKOFF
    unsigned int backoff=0;
#endif

    while(*quit == 0) {
#if LACE_BACKOFF
        backoff++;
#endif
        if (n > 1) {
            // Select victim
            if( i>0 ) {
                i--;
                victim++;
                if (victim == self) victim++;
                if (victim >= workers + n) victim = workers;
                if (victim == self) victim++;
            } else {
                i = lace_rng(lace_worker) % 40;
                victim = workers + ((lace_rng(lace_worker) % (n-1)) + worker_id + 1) % n;
            }

            PR_COUNTSTEALS(lace_worker, CTR_steal_tries);
            lace_worker_public *res = lace_steal(lace_worker, *victim);
            if (res == LACE_STOLEN) {
                PR_COUNTSTEALS(lace_worker, CTR_steals);
#if LACE_BACKOFF
                backoff = 0;
#endif
            } else if (res == LACE_BUSY) {
                PR_COUNTSTEALS(lace_worker, CTR_steal_busy);
#if LACE_BACKOFF
                backoff = 0;
#endif
            } else { // LACE_NOWORK
            }
        }

        lace_check_yield(lace_worker);

        if (__builtin_expect(atomic_load_explicit(&external_task, memory_order_acquire) != 0, 0)) {
            lace_steal_external(lace_get_worker());
#if LACE_BACKOFF
            backoff = 0;
#endif
        }

        if (__builtin_expect(atomic_load_explicit(&must_suspend, memory_order_acquire), 0)) {
            workers_running -= 1;
            sem_wait(&suspend_semaphore);
            lace_barrier(); // ensure we're all back before continuing
            workers_running += 1;
#if LACE_BACKOFF
            backoff = 0;
#endif
        }

#if LACE_BACKOFF
        if (backoff > 1000) { // only back off after 1000 attempts
            int delay_us = (1 << ((backoff-1000)/5)); // exponential backoff
            if (delay_us > 5000) delay_us = 5000; // cap at 5ms
#if LACE_PIE_TIMES
            uint64_t prev = lace_gethrtime();
#endif
            lace_sleep_us(delay_us);
#if LACE_PIE_TIMES
            PR_ADD(lace_worker,CTR_backoff, lace_gethrtime()-prev);
#endif
        }
#endif
    }
}

/**
 * Initialize the current thread as a Lace thread, and perform work-stealing
 * as worker <worker> until lace_exit() is called.
 */
static void*
lace_worker_thread(void* arg)
{
    int worker = (int)(size_t)arg;

    // Initialize data structures
    lace_init_worker(worker);

    // Pin CPU
    lace_pin_worker();

    // Wait for the first time we are resumed
    sem_wait(&suspend_semaphore);

    // Signal that we are running
    workers_running += 1;

    // Run the steal loop
    lace_steal_loop_CALL(lace_get_worker(), &lace_quits);

    // Time worker exit event
    lace_time_event(lace_get_worker(), 9);

    // Signal that we stopped
    workers_running -= 1;

    return NULL;
}

/**
 * Set the verbosity of Lace.
 */
void
lace_set_verbosity(int level)
{
    verbosity = level;
}

/**
 * Initialize Lace for work-stealing with <n> workers, where
 * each worker gets a task deque with <dqsize> elements.
 */
void
lace_start(unsigned int _n_workers, size_t dequesize, size_t stacksize)
{
#if LACE_USE_HWLOC
    // Initialize topology and information about cpus
    hwloc_topology_init(&topo);
    hwloc_topology_load(topo);

    n_nodes = hwloc_get_nbobjs_by_type(topo, HWLOC_OBJ_NODE);
    n_cores = hwloc_get_nbobjs_by_type(topo, HWLOC_OBJ_CORE);
    n_pus = hwloc_get_nbobjs_by_type(topo, HWLOC_OBJ_PU);
#else
#ifdef WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    n_pus = sysinfo.dwNumberOfProcessors;
#elif defined(sched_getaffinity)
    cpu_set_t cs;
    CPU_ZERO(&cs);
    sched_getaffinity(0, sizeof(cs), &cs);
    n_pus = CPU_COUNT(&cs);
#else
    n_pus = sysconf(_SC_NPROCESSORS_ONLN);
#endif
#endif

    cache_line_size = get_cache_line_size();
    size_t task_size = sizeof(lace_task);
    if (cache_line_size % task_size != 0 && task_size % cache_line_size != 0) {
        // typical values of task_size: 32, 64, 128...
        // lace14.h on 32-bit: 32 bytes; lace14.h on 64-bit: 64 bytes
        // lace14.h on 32-bit: 64 bytes; lace14.h on 64-bit: 128 bytes
        // typical values of cache_line_size: 32, 64, 128...
        // for example some ARM chips have a 32-byte cache line size
        // and some modern ARM chips have a 128-byte cache line size like Apple's CPUs
        // if not aligned, this will result in excessive false sharing and therefore
        // significant performance loss

        fprintf(stderr, "Lace warning: task size %zu and cache line size %zu may be unaligned!\n",
            task_size, cache_line_size);
    }

    // Initialize globals
    n_workers = _n_workers == 0 ? n_pus : _n_workers;
    dqsize = dequesize > 0 ? dequesize : 100000;
    lace_quits = 0;
    atomic_store_explicit(&workers_running, 0, memory_order_relaxed);

#if LACE_USE_HWLOC
    // Distribute workers over cores.
    // It tries to first use all cores, before using multiple PUs per core, avoiding hyperthreading.
    cpusets = malloc(n_workers * sizeof(*cpusets));
    // one way of doing this is just with hwloc_distrib, but this is suboptimal
    // for (int i=0; i<n_workers; i++) cpusets[i] = hwloc_bitmap_alloc();
    // hwloc_obj_t root = hwloc_get_root_obj(topo);
    // hwloc_distrib(topo, &root, 1, cpusets, n_workers, INT_MAX, 0);

    unsigned int i=0;
    hwloc_obj_t core = NULL;
    hwloc_obj_t cores[n_cores];
    while ((core = hwloc_get_next_obj_by_type(topo, HWLOC_OBJ_CORE, core)) != NULL) cores[i++] = core;

    i = 0;
    unsigned int j=0, k=0;
    // i is index of worker, j is index of cpu, k is how many PUs per core we have used
    while (i < n_workers) {
        if (j < n_cores && k < (unsigned)hwloc_bitmap_weight(cores[j]->cpuset)) {
            cpusets[i] = cores[j]->cpuset;
            // grab the kth in cpuset
            // turns out this is slightly slower than just pinning to all threads
            // int idx = hwloc_bitmap_first(cores[j]->cpuset);
            // for (int kk=1; kk<k; kk++) idx = hwloc_bitmap_next(cores[j]->cpuset, idx);
            // hwloc_obj_t pu = hwloc_get_pu_obj_by_os_index(topo, idx);
            // cpusets[i] = pu->cpuset;
            i++;
            j++;
        } else {
            k++;
            for (j=0; j<n_cores; j++) {
                if (k < (unsigned)hwloc_bitmap_weight(cores[j]->cpuset)) break;
            }
            if (j == n_cores) {
                j = 0;
                k = 0;
            }
        }
    }
#endif

    // Initialize Lace barrier
    lace_barrier_init();

    // Create suspend semaphore
    memset(&suspend_semaphore, 0, sizeof(sem_t));
    sem_init(&suspend_semaphore, 0, 0);

    must_suspend = 0;
    lace_awaken_count = 0;

    // Allocate array with all workers
    // first make sure that the amount to allocate (n_workers times pointer) is a multiple of cache_line_size
    size_t to_allocate = n_workers * sizeof(void*);
    to_allocate = (to_allocate+cache_line_size-1) & (~(cache_line_size-1));
#if defined(_MSC_VER) || defined(__MINGW64_VERSION_MAJOR)
    workers = _aligned_malloc(to_allocate, cache_line_size);
    workers_p = _aligned_malloc(to_allocate, cache_line_size);
    workers_memory = _aligned_malloc(to_allocate, cache_line_size);
#elif defined(__MINGW32__)
    workers = __mingw_aligned_malloc(to_allocate, cache_line_size);
    workers_p = __mingw_aligned_malloc(to_allocate, cache_line_size);
    workers_memory = __mingw_aligned_malloc(to_allocate, cache_line_size);
#else
    workers = aligned_alloc(cache_line_size, to_allocate);
    workers_p = aligned_alloc(cache_line_size, to_allocate);
    workers_memory = aligned_alloc(cache_line_size, to_allocate);
#endif
    if (workers == 0 || workers_p == 0 || workers_memory == 0) {
        fprintf(stderr, "Lace error: unable to allocate memory for the workers!\n");
        exit(1);
    }

    // Ensure worker array is set to 0 initially
    memset(workers, 0, n_workers*sizeof(lace_worker_public*));

    // Compute memory size for each worker
    workers_memory_size = sizeof(worker_data) + sizeof(lace_task) * dqsize;

#ifndef __linux__
    // Create pthread key
    pthread_key_create(&lace_thread_worker_key, NULL);
#endif

    // Prepare structures for thread creation
    pthread_attr_t worker_attr;
    pthread_attr_init(&worker_attr);

    // Set the stack size
    if (stacksize != 0) {
        if (stacksize < 16*1024*1024) stacksize = 16*1024*1024;
        pthread_attr_setstacksize(&worker_attr, stacksize);
    } else {
        // on certain systems, the default stack size is too small (e.g. OSX)
        // so by default, we just pick the current RLIMIT_STACK or 16M whichever is greatest
#ifndef _WIN32
        struct rlimit lim;
        getrlimit(RLIMIT_STACK, &lim);
        size_t size = lim.rlim_cur;
        if (size < 16*1024*1024) size = 16*1024*1024;
#else
        size_t size = 16*1024*1024;
#endif
        pthread_attr_setstacksize(&worker_attr, size);
        stacksize = size;
    }

    if (verbosity) {
#if LACE_USE_HWLOC
        fprintf(stdout, "Lace startup: %u nodes, %u cores, %u logical processors, %d workers.\n", n_nodes, n_cores, n_pus, n_workers);
        // Print resulting CPU sets
        if (verbosity != 0) {
            for (unsigned int i = 0; i < n_workers; ++i) {
                unsigned int id;
                hwloc_bitmap_foreach_begin(id, cpusets[i]);
                hwloc_obj_t pu = hwloc_get_pu_obj_by_os_index(topo, id);
                // find the core
                hwloc_obj_t core = hwloc_get_ancestor_obj_by_type(topo, HWLOC_OBJ_CORE, pu);
                printf("Lace startup: will pin worker thread %d to pu %u (on core %u)\n", i, pu->logical_index, core->logical_index);
                hwloc_bitmap_foreach_end();
            }
        }
#else
        fprintf(stdout, "Lace startup: %u available cores, %d workers.\n", n_pus, n_workers);
#endif
    }

    // Prepare lace_init structure
    atomic_store_explicit(&lace_newframe.t, NULL, memory_order_relaxed);

#if LACE_PIE_TIMES
    // Initialize counters for pie times
    us_elapsed_start();
    count_at_start = lace_gethrtime();
#endif

    /* Report startup if verbose */
    if (verbosity) {
        fprintf(stdout, "Lace startup: creating %d worker threads with program stack %zu bytes.\n", n_workers, stacksize);
    }

    /* Spawn all workers */
    for (unsigned int i=0; i<n_workers; i++) {
        pthread_t res;
        pthread_create(&res, &worker_attr, lace_worker_thread, (void*)(size_t)i);
    }

    /* Make sure we start resumed */
    lace_resume();

    pthread_attr_destroy(&worker_attr);
}


#if LACE_COUNT_EVENTS
static uint64_t ctr_all[CTR_MAX];
#endif

/**
 * Reset the counters of Lace.
 */
void
lace_count_reset()
{
#if LACE_COUNT_EVENTS
    unsigned int i;
    size_t j;

    for (i=0;i<n_workers;i++) {
        for (j=0;j<CTR_MAX;j++) {
            workers_p[i]->ctr[j] = 0;
        }
    }

#if LACE_PIE_TIMES
    for (i=0;i<n_workers;i++) {
        workers_p[i]->time = lace_gethrtime();
        if (i != 0) workers_p[i]->level = 0;
    }

    us_elapsed_start();
    count_at_start = lace_gethrtime();
#endif
#endif
}

/**
 * Report counters to the given file.
 */
void
lace_count_report_file(FILE *file)
{
#if LACE_COUNT_EVENTS
    unsigned int i;
    size_t j;

    for (j=0;j<CTR_MAX;j++) ctr_all[j] = 0;
    for (i=0;i<n_workers;i++) {
        uint64_t *wctr = workers_p[i]->ctr;
        for (j=0;j<CTR_MAX;j++) {
            ctr_all[j] += wctr[j];
        }
    }

#if LACE_COUNT_TASKS
    for (i=0;i<n_workers;i++) {
        fprintf(file, "lace_tasks (%d): %zu\n", i, workers_p[i]->ctr[CTR_tasks]);
    }
    fprintf(file, "lace_tasks (sum): %zu\n", ctr_all[CTR_tasks]);
    fprintf(file, "\n");
#endif

#if LACE_COUNT_STEALS
    for (i=0;i<n_workers;i++) {
        fprintf(file, "Steals (%d): %zu good/%zu busy of %zu tries; leaps: %zu good/%zu busy of %zu tries\n", i,
            workers_p[i]->ctr[CTR_steals], workers_p[i]->ctr[CTR_steal_busy],
            workers_p[i]->ctr[CTR_steal_tries], workers_p[i]->ctr[CTR_leaps],
            workers_p[i]->ctr[CTR_leap_busy], workers_p[i]->ctr[CTR_leap_tries]);
    }
    fprintf(file, "Steals (sum): %zu good/%zu busy of %zu tries; leaps: %zu good/%zu busy of %zu tries\n", 
        ctr_all[CTR_steals], ctr_all[CTR_steal_busy],
        ctr_all[CTR_steal_tries], ctr_all[CTR_leaps],
        ctr_all[CTR_leap_busy], ctr_all[CTR_leap_tries]);
    fprintf(file, "\n");
#endif

#if LACE_COUNT_STEALS && LACE_COUNT_TASKS
    for (i=0;i<n_workers;i++) {
        if ((workers_p[i]->ctr[CTR_steals]+workers_p[i]->ctr[CTR_leaps]) > 0) {
            fprintf(file, "lace_tasks per steal (%d): %zu\n", i,
                workers_p[i]->ctr[CTR_tasks]/(workers_p[i]->ctr[CTR_steals]+workers_p[i]->ctr[CTR_leaps]));
        }
    }
    if ((ctr_all[CTR_steals]+ctr_all[CTR_leaps]) > 0) {
        fprintf(file, "lace_tasks per steal (sum): %zu\n", ctr_all[CTR_tasks]/(ctr_all[CTR_steals]+ctr_all[CTR_leaps]));
    }
    fprintf(file, "\n");
#endif

#if LACE_COUNT_SPLITS
    for (i=0;i<n_workers;i++) {
        fprintf(file, "Splits (%d): %zu shrinks, %zu grows, %zu outgoing requests\n", i,
            workers_p[i]->ctr[CTR_split_shrink], workers_p[i]->ctr[CTR_split_grow], workers_p[i]->ctr[CTR_split_req]);
    }
    fprintf(file, "Splits (sum): %zu shrinks, %zu grows, %zu outgoing requests\n",
        ctr_all[CTR_split_shrink], ctr_all[CTR_split_grow], ctr_all[CTR_split_req]);
    fprintf(file, "\n");
#endif

#if LACE_PIE_TIMES
    count_at_end = lace_gethrtime();

    uint64_t count_per_ms = (count_at_end - count_at_start) / (us_elapsed() / 1000);
    double dcpm = (double)count_per_ms;

    uint64_t sum_count;
    sum_count = ctr_all[CTR_init] + ctr_all[CTR_wapp] + ctr_all[CTR_lapp] + ctr_all[CTR_wsteal] + ctr_all[CTR_lsteal]
              + ctr_all[CTR_close] + ctr_all[CTR_wstealsucc] + ctr_all[CTR_lstealsucc] + ctr_all[CTR_wsignal]
              + ctr_all[CTR_lsignal];

    fprintf(file, "Measured clock (tick) frequency: %.2f GHz\n", count_per_ms / 1000000.0);
    fprintf(file, "Aggregated time per pie slice, total time: %.2f CPU seconds\n\n", sum_count / (1000*dcpm));

    for (i=0;i<n_workers;i++) {
        fprintf(file, "Startup time (%d):    %10.2f ms\n", i, workers_p[i]->ctr[CTR_init] / dcpm);
        fprintf(file, "Steal work (%d):      %10.2f ms\n", i, workers_p[i]->ctr[CTR_wapp] / dcpm);
        fprintf(file, "Leap work (%d):       %10.2f ms\n", i, workers_p[i]->ctr[CTR_lapp] / dcpm);
        fprintf(file, "Steal overhead (%d):  %10.2f ms\n", i, (workers_p[i]->ctr[CTR_wstealsucc]+workers_p[i]->ctr[CTR_wsignal]) / dcpm);
        fprintf(file, "Leap overhead (%d):   %10.2f ms\n", i, (workers_p[i]->ctr[CTR_lstealsucc]+workers_p[i]->ctr[CTR_lsignal]) / dcpm);
        fprintf(file, "Steal search (%d):    %10.2f ms\n", i, (workers_p[i]->ctr[CTR_wsteal]-workers_p[i]->ctr[CTR_wstealsucc]-workers_p[i]->ctr[CTR_wsignal]) / dcpm);
        fprintf(file, "Leap search (%d):     %10.2f ms\n", i, (workers_p[i]->ctr[CTR_lsteal]-workers_p[i]->ctr[CTR_lstealsucc]-workers_p[i]->ctr[CTR_lsignal]) / dcpm);
        fprintf(file, "Backoff time (%d):    %10.2f ms\n", i, workers_p[i]->ctr[CTR_backoff] / dcpm);
        fprintf(file, "Exit time (%d):       %10.2f ms\n", i, workers_p[i]->ctr[CTR_close] / dcpm);
        fprintf(file, "\n");
    }

    fprintf(file, "Startup time (sum):    %10.2f ms\n", ctr_all[CTR_init] / dcpm);
    fprintf(file, "Steal work (sum):      %10.2f ms\n", ctr_all[CTR_wapp] / dcpm);
    fprintf(file, "Leap work (sum):       %10.2f ms\n", ctr_all[CTR_lapp] / dcpm);
    fprintf(file, "Steal overhead (sum):  %10.2f ms\n", (ctr_all[CTR_wstealsucc]+ctr_all[CTR_wsignal]) / dcpm);
    fprintf(file, "Leap overhead (sum):   %10.2f ms\n", (ctr_all[CTR_lstealsucc]+ctr_all[CTR_lsignal]) / dcpm);
    fprintf(file, "Steal search (sum):    %10.2f ms\n", (ctr_all[CTR_wsteal]-ctr_all[CTR_wstealsucc]-ctr_all[CTR_wsignal]) / dcpm);
    fprintf(file, "Leap search (sum):     %10.2f ms\n", (ctr_all[CTR_lsteal]-ctr_all[CTR_lstealsucc]-ctr_all[CTR_lsignal]) / dcpm);
    fprintf(file, "Backoff time (sum):    %10.2f ms\n", ctr_all[CTR_backoff] / dcpm);
    fprintf(file, "Exit time (sum):       %10.2f ms\n", ctr_all[CTR_close] / dcpm);
    fprintf(file, "\n" );
#endif
#endif
    return;
    (void)file;
}

/**
 * End Lace. All Workers are signaled to quit.
 * This function waits until all threads are done, then returns.
 */
void lace_stop()
{
    // Workers need to be awake for this
    lace_resume();

    // Do not stop if not all workers are running yet
    while (workers_running != n_workers) {}

    lace_quits = 1;

    while (workers_running != 0) {}

#if LACE_COUNT_EVENTS
    lace_count_report_file(stdout);
#endif

    // finally, destroy the barriers
    lace_barrier_destroy();
    sem_destroy(&suspend_semaphore);

    for (unsigned int i=0; i<n_workers; i++) {
#if LACE_USE_MMAP
        munmap(workers_memory[i], workers_memory_size);
#elif defined(_MSC_VER) || defined(__MINGW64_VERSION_MAJOR)
	_aligned_free(workers_memory[i]);
#elif defined(__MINGW32__)
	__mingw_aligned_free(workers_memory[i]);
#else
        free(workers_memory[i]);
#endif
    }

#if defined(_MSC_VER) || defined(__MINGW64_VERSION_MAJOR)
    _aligned_free(workers);
    _aligned_free(workers_p);
    _aligned_free(workers_memory);
#elif defined(__MINGW32__)
    __mingw_aligned_free(workers);
    __mingw_aligned_free(workers_p);
    __mingw_aligned_free(workers_memory);
#else
    free(workers);
    free(workers_p);
    free(workers_memory);
#endif

    workers = 0;
    workers_p = 0;
    workers_memory = 0;
}

/**
 * Execute the given <root> task in a new frame (synchronizing with all Lace threads)
 * 1) Creates a new frame
 * 2) LACE BARRIER
 * 3) Execute the <root> task
 * 4) LACE BARRIER
 * 5) Restore the old frame
 */
void
lace_exec_in_new_frame(lace_worker* __lace_worker, lace_task *root)
{
    lace_task *__lace_dq_head = __lace_worker->head;

    TailSplitNA old;
    uint8_t old_as;

    // save old tail, split, allstolen and initiate new frame
    {
        lace_worker_public *wt = __lace_worker->_public;

        old_as = wt->allstolen;
        wt->allstolen = 1;
        old.ts.split = wt->ts.ts.split;
        wt->ts.ts.split = 0;
        atomic_thread_fence(memory_order_seq_cst);
        old.ts.tail = wt->ts.ts.tail;

        TailSplitNA ts_new;
        ts_new.ts.tail = __lace_dq_head - __lace_worker->dq;
        ts_new.ts.split = __lace_dq_head - __lace_worker->dq;
        wt->ts.v = ts_new.v;

        __lace_worker->split = __lace_dq_head;
        __lace_worker->allstolen = 1;
    }

    // wait until all workers are ready
    lace_barrier();

    // execute task
    root->f(__lace_worker, root);

    // wait until all workers are back (else they may steal from previous frame)
    lace_barrier();

    // restore tail, split, allstolen
    {
        lace_worker_public *wt = __lace_worker->_public;
        wt->allstolen = old_as;
        wt->ts.v = old.v;
        __lace_worker->split = __lace_worker->dq + old.ts.split;
        __lace_worker->allstolen = old_as;
    }
}

/**
 * This method is called when there is a new frame (NEWFRAME or TOGETHER)
 * Each Lace worker executes lace_yield to execute the task in a new frame.
 */
void
lace_yield(lace_worker *worker)
{
    // make a local copy of the task
    lace_task _t;
    memcpy(&_t, lace_newframe.t, sizeof(lace_task));

    // wait until all workers have made a local copy
    lace_barrier();

    lace_exec_in_new_frame(worker, &_t);
}

/**
 * Root task for the TOGETHER method.
 * Ensures after executing, to steal random tasks until done.
 */
VOID_TASK_2(lace_together_root, lace_task*, t, atomic_int*, finished)

void
lace_together_root_CALL(lace_worker* lace_worker, lace_task* t, atomic_int* finished)
{
    // run the root task
    t->f(lace_worker, t);

    // signal out completion
    *finished -= 1;

    // while threads aren't done, steal randomly
    while (*finished != 0) lace_steal_random(lace_worker);
}

VOID_TASK_1(lace_wrap_together, lace_task*, task)

void
lace_wrap_together_CALL(lace_worker* worker, lace_task* task)
{
    /* synchronization integer (decrease by 1 when done...) */
    atomic_int done = n_workers;

    /* wrap task in lace_together_root */
    lace_task _t2;
    TD_lace_together_root *t2 = (TD_lace_together_root *)&_t2;
    t2->f = lace_together_root_WRAP;
    atomic_store_explicit(&t2->thief, THIEF_TASK, memory_order_relaxed);
    t2->d.args.arg_1 = task;
    t2->d.args.arg_2 = &done;

    /* now try to be the one who sets it! */
    while (1) {
        lace_task *expected = 0;
        if (atomic_compare_exchange_weak(&lace_newframe.t, &expected, &_t2)) break;
        lace_yield(worker);
    }

    // wait until other workers have made a local copy
    lace_barrier();

    // reset the newframe struct
    atomic_store_explicit(&lace_newframe.t, NULL, memory_order_relaxed);

    lace_exec_in_new_frame(worker, &_t2);
}

VOID_TASK_2(lace_newframe_root, lace_task*, t, atomic_int*, done)

void
lace_newframe_root_CALL(lace_worker *lace_worker, lace_task* t, atomic_int *done)
{
    t->f(lace_worker, t);
    *done = 1;
}

VOID_TASK_1(lace_wrap_newframe, lace_task*, task)

void
lace_wrap_newframe_CALL(lace_worker* worker, lace_task* task)
{
    /* synchronization integer (set to 1 when done...) */
    atomic_int done = 0;

    /* create the lace_steal_loop task for the other workers */
    lace_task _s;
    TD_lace_steal_loop *s = (TD_lace_steal_loop *)&_s;
    s->f = &lace_steal_loop_WRAP;
    atomic_store_explicit(&s->thief, THIEF_TASK, memory_order_relaxed);
    s->d.args.arg_1 = &done;

    /* now try to be the one who sets it! */
    while (1) {
        lace_task *expected = 0;
        if (atomic_compare_exchange_weak(&lace_newframe.t, &expected, &_s)) break;
        lace_yield(worker);
    }

    // wait until other workers have made a local copy
    lace_barrier();

    // reset the newframe struct, then wrap and run ours
    atomic_store_explicit(&lace_newframe.t, NULL, memory_order_relaxed);

    /* wrap task in lace_newframe_root */
    lace_task _t2;
    TD_lace_newframe_root *t2 = (TD_lace_newframe_root *)&_t2;
    t2->f = lace_newframe_root_WRAP;
    atomic_store_explicit(&t2->thief, THIEF_TASK, memory_order_relaxed);
    t2->d.args.arg_1 = task;
    t2->d.args.arg_2 = &done;

    lace_exec_in_new_frame(worker, &_t2);
}

void
lace_run_together(lace_task *t)
{
    lace_worker* self = lace_get_worker();
    if (self != 0) {
        lace_wrap_together_CALL(self, t);
    } else {
        lace_wrap_together(t);
    }
}

void
lace_run_newframe(lace_task *t)
{
    lace_worker* self = lace_get_worker();
    if (self != 0) {
        lace_wrap_newframe_CALL(self, t);
    } else {
        lace_wrap_newframe(t);
    }
}

/**
 * Called by _SPAWN functions when the lace_task stack is full.
 */
void
lace_abort_stack_overflow(void)
{
    fprintf(stderr, "Lace fatal error: lace_task stack overflow! Aborting.\n");
    exit(-1);
}

lace_worker_public*
lace_steal(lace_worker *self, lace_worker_public *victim)
{
    if (victim != NULL && !victim->allstolen) {
        TailSplitNA ts;
        ts.v = victim->ts.v;
        if (ts.ts.tail < ts.ts.split) {
            TailSplitNA ts_new;
            ts_new.v = ts.v;
            ts_new.ts.tail++;
            if (atomic_compare_exchange_weak(&victim->ts.v, &ts.v, ts_new.v)) {
                // Stolen
                lace_task *t = &victim->dq[ts.ts.tail];
                atomic_store_explicit(&t->thief, self->_public, memory_order_relaxed);
                lace_time_event(self, 1);
                t->f(self, t);
                lace_time_event(self, 2);
                atomic_store_explicit(&t->thief, THIEF_COMPLETED, memory_order_release);
                lace_time_event(self, 8);
                return LACE_STOLEN;
            }

            lace_time_event(self, 7);
            return LACE_BUSY;
        }

        if (victim->movesplit == 0) {
            victim->movesplit = 1;
            PR_COUNTSPLITS(self, CTR_split_req);
        }
    }

    lace_time_event(self, 7);
    return LACE_NOWORK;
}

int
lace_shrink_shared(lace_worker *w)
{
    lace_worker_public *wt = w->_public;
    TailSplitNA ts; /* Use non-atomic version to emit better code */
    ts.v = wt->ts.v; /* Force in 1 memory read */
    uint32_t tail = ts.ts.tail;
    uint32_t split = ts.ts.split;

    if (tail != split) {
        uint32_t newsplit = (tail + split)/2;
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed); /* emit normal write */
        atomic_thread_fence(memory_order_seq_cst);
        tail = wt->ts.ts.tail;
        if (tail != split) {
            if (__builtin_expect(tail > newsplit, 0)) {
                newsplit = (tail + split) / 2;
                atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed); /* emit normal write */
            }
            w->split = w->dq + newsplit;
            PR_COUNTSPLITS(w, CTR_split_shrink);
            return 0;
        }
    }

    wt->allstolen = 1;
    w->allstolen = 1;
    return 1;
}

void
lace_leapfrog(lace_worker *lace_worker)
{
    lace_time_event(lace_worker, 3);
    lace_task *t = lace_worker->head;
    lace_worker_public *thief = t->thief;
    if (thief != THIEF_COMPLETED) {
        while ((size_t)thief <= 1) thief = t->thief;

        /* PRE-LEAP: increase head again */
        lace_worker->head += 1;

        /* Now leapfrog */
        int attempts = 32;
        while (thief != THIEF_COMPLETED) {
            PR_COUNTSTEALS(lace_worker, CTR_leap_tries);
            lace_worker_public *res = lace_steal(lace_worker, thief);
            if (res == LACE_NOWORK) {
                lace_check_yield(lace_worker);
                if ((LACE_LEAP_RANDOM) && (--attempts == 0)) {
                    lace_steal_random(lace_worker);
                    attempts = 32;
                }
            } else if (res == LACE_STOLEN) {
                PR_COUNTSTEALS(lace_worker, CTR_leaps);
            } else if (res == LACE_BUSY) {
                PR_COUNTSTEALS(lace_worker, CTR_leap_busy);
            }
            atomic_thread_fence(memory_order_acquire);
            thief = t->thief;
        }

        /* POST-LEAP: really pop the finished task */
        atomic_thread_fence(memory_order_acquire);
        if (lace_worker->allstolen == 0) {
            /* Assume: tail = split = head (pre-pop) */
            /* Now we do a real pop ergo either decrease tail,split,head or declare allstolen */
            lace_worker_public *wt = lace_worker->_public;
            wt->allstolen = 1;
            lace_worker->allstolen = 1;
        }
        lace_worker->head -= 1;
    }

    /*compiler_barrier();*/
    atomic_thread_fence(memory_order_acquire);
    atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);
    lace_time_event(lace_worker, 4);
}

int
lace_sync(lace_worker *w, lace_task *head)
{
    if ((w->allstolen) || (w->split > head && lace_shrink_shared(w))) {
        lace_leapfrog(w);
        return 1;
    }

    lace_worker_public *wt = w->_public;
    if (wt->movesplit) {
        lace_task *t = w->split;
        size_t diff = head - t;
        diff = (diff + 1) / 2;
        w->split = t + diff;
        wt->ts.ts.split += diff;
        wt->movesplit = 0;
        PR_COUNTSPLITS(w, CTR_split_grow);
    }

    return 0;
}

void
lace_drop_slow(lace_worker *w, lace_task *head)
{
    if ((w->allstolen) || (w->split > head && lace_shrink_shared(w))) lace_leapfrog(w);
}

void
lace_drop(lace_worker *_lace_worker)
{
    lace_task* lace_head = _lace_worker->head - 1;
    _lace_worker->head = lace_head;
    if (__builtin_expect(0 == _lace_worker->_public->movesplit, 1)) {
        if (__builtin_expect(_lace_worker->split <= lace_head, 1)) {
            return;
        }
    }
    lace_drop_slow(_lace_worker, lace_head);
}

#if defined(_WIN32)
void lace_sleep_us(int microseconds) {
    Sleep((microseconds + 999) / 1000); // Sleep takes ms
}
#endif

