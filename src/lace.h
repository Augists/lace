/* 
 * Copyright 2013-2016 Formal Methods and Tools, University of Twente
 * Copyright 2016-2018 Tom van Dijk, Johannes Kepler University Linz
 * Copyright 2019-2025 Tom van Dijk, Formal Methods and Tools, University of Twente
 *
 * Licensed under the Apache License, Version 2.0 (the License);
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once
#ifndef __LACE_H__
#define __LACE_H__

// Standard includes
#include <assert.h> // for static_assert
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#ifndef __cplusplus
  #include <stdatomic.h>
#else
  // Even though we are not really intending to support C++...
  // Compatibility with C11
  #include <atomic>
  #define _Atomic(T) std::atomic<T>
  using std::memory_order_relaxed;
  using std::memory_order_acquire;
  using std::memory_order_release;
  using std::memory_order_seq_cst;
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// Lace version
#define LACE_VERSION_MAJOR 2
#define LACE_VERSION_MINOR 0
#define LACE_VERSION_PATCH 1

// Platform configuration
#include <lace_config.h>

// Architecture configuration
#ifndef LACE_CACHE_LINE_SIZE
// Override LACE_CACHE_LINE_SIZE (e.g., with -DLACE_CACHE_LINE_SIZE=128) if targeting certain architectures.
#define LACE_CACHE_LINE_SIZE 64
#endif

// Forward declarations
typedef struct _LaceWorker LaceWorker;
typedef struct _Task Task;

/**************************************
 * Lifecycle functions
 * - lace_set_verbosity
 * - lace_start
 * - lace_suspend
 * - lace_resume
 * - lace_stop
 **************************************/

/**
 * Set verbosity level (0 = no startup messages, 1 = startup messages)
 * Default level: 0
 */
void lace_set_verbosity(int level);

/**
 * Start Lace with <n_workers> workers and a task deque size of <dqsize> per worker.
 * If <n_workers> is set to 0, automatically detects available cores.
 * If <dqsize> is set to 0, uses a reasonable default value.
 * If <stacksize> is set to 0, uses the minimum of 16M and the stack size of the calling thread.
 */
void lace_start(unsigned int n_workers, size_t dqsize, size_t stacksize);

/**
 * Suspend all workers. Do not call this from inside Lace threads.
 */
void lace_suspend(void);

/**
 * Resume all workers. Do not call this from inside Lace threads.
 */
void lace_resume(void);

/**
 * Stop Lace. Do not call this from inside Lace threads.
 */
void lace_stop(void);

/**************************************
 * Worker context
 * - lace_worker_count
 * - lace_is_worker
 * - lace_get_worker
 * - lace_worker_id
 * - lace_rng
 **************************************/

/**
 * Retrieve the number of Lace workers.
 */
unsigned int lace_worker_count(void);

/**
 * Retrieve whether we are running in a Lace worker. Returns 1 if this is the case, 0 otherwise.
 */
static inline int lace_is_worker(void) __attribute__((unused));

/**
 * Retrieve the current worker data.
 */
static inline LaceWorker* lace_get_worker(void) __attribute__((unused));

/**
 * Get the current worker id. Returns -1 if not inside a Lace thread.
 */
static inline int lace_worker_id(void) __attribute__((unused));

/**
 * Thread-local pseudo-random number generator for Lace workers.
 */
static inline uint64_t lace_rng(LaceWorker *lace_worker) __attribute__((unused));

/**************************************
 * Task operations
 * - lace_barrier
 * - lace_drop
 * - lace_is_stolen_task
 * - lace_is_completed_task
 * - lace_steal_random
 * - lace_check_yield
 * - lace_make_all_shared
 * - lace_get_head
 **************************************/

/**
 * Enter the Lace barrier. This is a collective operation.
 * All workers must enter it before the method returns for all workers.
 * Only run this from inside a Lace task.
 */
void lace_barrier(void);

/**
 * Instead of SYNCing on the next task, drop the task (unless stolen already)
 */
void lace_drop(LaceWorker *lace_worker);

/**
 * Returns 1 if the given task is stolen, 0 otherwise.
 */
static inline int lace_is_stolen_task(Task* t) __attribute__((unused));

/**
 * Returns 1 if the given task is completed, 0 otherwise.
 */
static inline int lace_is_completed_task(Task* t) __attribute__((unused));

/**
 * Try to steal and execute a random task from a random worker.
 * Only use this from inside a Lace task.
 */
void lace_steal_random(LaceWorker*);

/**
 * Check if current tasks must be interrupted, and if so, interrupt.
 */
static inline void lace_check_yield(LaceWorker*) __attribute__((unused));

/**
 * Make all tasks of the current worker shared.
 */
static inline void lace_make_all_shared(void) __attribute__((unused));

/**
 * Retrieve the current head of the deque of the worker.
 */
static inline Task *lace_get_head(void) __attribute__((unused));

/**************************************
 * Statistics
 * - lace_count_report_file
 * - lace_count_reset
 * - lace_count_report
 **************************************/

/**
 * Reset internal stats counters.
 */
void lace_count_reset(void);

/**
 * Report Lace stats to the given file.
 */
void lace_count_report_file(FILE *file);

/**
 * Report Lace stats to stdout.
 */
static inline __attribute__((unused)) void lace_count_report(void)
{
    lace_count_report_file(stdout);
}

/**************************************
 * Internals
 **************************************/

/* The size is in bytes. Note that this is without the extra overhead from Lace.
   The value must be greater than or equal to the maximum size of your tasks.
   The task size is the maximum of the size of the result or of the sum of the parameter sizes. */
#ifndef LACE_TASKSIZE
#define LACE_TASKSIZE (6)*sizeof(void*)
#endif

#ifndef LACE_COUNT_EVENTS
#define LACE_COUNT_EVENTS (LACE_PIE_TIMES || LACE_COUNT_TASKS || LACE_COUNT_STEALS || LACE_COUNT_SPLITS)
#endif

typedef enum {
#ifdef LACE_COUNT_TASKS
    CTR_tasks,       /* Number of tasks spawned */
#endif
#ifdef LACE_COUNT_STEALS
    CTR_steal_tries, /* Number of steal attempts */
    CTR_leap_tries,  /* Number of leap attempts */
    CTR_steals,      /* Number of succesful steals */
    CTR_leaps,       /* Number of succesful leaps */
    CTR_steal_busy,  /* Number of steal busies */
    CTR_leap_busy,   /* Number of leap busies */
#endif
#ifdef LACE_COUNT_SPLITS
    CTR_split_grow,  /* Number of split right */
    CTR_split_shrink,/* Number of split left */
    CTR_split_req,   /* Number of split requests */
#endif
    CTR_fast_sync,   /* Number of fast syncs */
    CTR_slow_sync,   /* Number of slow syncs */
#ifdef LACE_PIE_TIMES
    CTR_init,        /* Timer for initialization */
    CTR_close,       /* Timer for shutdown */
    CTR_wapp,        /* Timer for application code (steal) */
    CTR_lapp,        /* Timer for application code (leap) */
    CTR_wsteal,      /* Timer for steal code (steal) */
    CTR_lsteal,      /* Timer for steal code (leap) */
    CTR_wstealsucc,  /* Timer for succesful steal code (steal) */
    CTR_lstealsucc,  /* Timer for succesful steal code (leap) */
    CTR_wsignal,     /* Timer for signal after work (steal) */
    CTR_lsignal,     /* Timer for signal after work (leap) */
#endif
    CTR_MAX
} CTR_index;

#define TASK_COMMON_FIELDS(type)                   \
    void (*f)(LaceWorker *, struct type *);        \
    _Atomic(struct _Worker*) thief;

typedef struct _Task {
    TASK_COMMON_FIELDS(_Task)
    char d[LACE_TASKSIZE];
} Task;

static_assert(LACE_CACHE_LINE_SIZE % 64 == 0, "LACE_CACHE_LINE_SIZE must be a multiple of 64");
static_assert((sizeof(Task) % LACE_CACHE_LINE_SIZE) == 0, "Task size should be a multiple of LACE_CACHE_LINE_SIZE");

typedef union {
    struct {
        _Atomic(uint32_t) tail;
        _Atomic(uint32_t) split;
    } ts;
    _Atomic(uint64_t) v;
} TailSplit;

typedef union {
    struct {
        uint32_t tail;
        uint32_t split;
    } ts;
    uint64_t v;
} TailSplitNA;

static_assert(sizeof(TailSplit) == 8, "TailSplit size should be 8 bytes");
static_assert(sizeof(TailSplitNA) == 8, "TailSplit size should be 8 bytes");

typedef struct _Worker {
    Task *dq;
    TailSplit ts;
    uint8_t allstolen;

    alignas(LACE_CACHE_LINE_SIZE) uint8_t movesplit;
} Worker;

typedef struct _LaceWorker {
    Task *head;                 // my head
    Task *split;                // same as dq+ts.ts.split
    Task *end;                  // dq+dq_size
    Task *dq;                   // my queue
    Worker *_public;            // pointer to public Worker struct
    __uint128_t rng;            // my random seed (for lace_rng)
    uint16_t worker;            // what is my worker id?
    uint8_t allstolen;          // my allstolen

#if LACE_COUNT_EVENTS
    uint64_t ctr[CTR_MAX];      // counters
    uint64_t time;
    int level;
#endif
} LaceWorker;

#ifdef __linux__
extern __thread LaceWorker *lace_thread_worker;

static inline LaceWorker* lace_get_worker(void)
{
    return lace_thread_worker;
}
#else
extern pthread_key_t lace_thread_worker_key;

static inline LaceWorker* lace_get_worker(void)
{
    return (LaceWorker*)pthread_getspecific(lace_thread_worker_key);
}
#endif

/**
 * Retrieve whether we are running in a Lace worker. Returns 1 if this is the case, 0 otherwise.
 */
static inline int lace_is_worker(void)
{
    return lace_get_worker() != NULL ? 1 : 0;
}

/**
 * Retrieve the current head of the deque of the worker.
 */
static inline Task *lace_get_head(void)
{
    return lace_get_worker()->head;
}

/**
 * Helper function to call from outside Lace threads.
 */
void lace_run_task(Task *task);

/**
 * Helper function to call from outside Lace threads.
 */
void lace_run_task_exclusive(Task *task);

/**
 * Helper function to start a new task execution (task frame) on a given task.
 * This helper function is used by the _NEWFRAME methods for the NEWFRAME() macro
 * Only when the task is done, do workers continue with the previous task frame.
 */
void lace_run_newframe(Task *task);

/**
 * Helper function to make all run a given task together.
 * This helper function is used by the _TOGETHER methods for the TOGETHER() macro
 * They all start the task in a lace_barrier and complete it with a lace barrier.
 * Meaning they all start together, and all end together.
 */
void lace_run_together(Task *task);

/**
 * Get the current worker id, or -1 if not inside a Lace thread.
 */
static inline int lace_worker_id(void)
{
    return lace_get_worker() == NULL ? -1 : lace_get_worker()->worker;
}

/**
 * 1 if the given task is stolen, 0 otherwise.
 */
static inline int lace_is_stolen_task(Task* t)
{
    return ((size_t)(Worker*)t->thief > 1) ? 1 : 0;
}

/**
 * 1 if the given task is completed, 0 otherwise.
 */
static inline int lace_is_completed_task(Task* t)
{
    return ((size_t)(Worker*)t->thief == 2) ? 1 : 0;
}

/**
 * Retrieves a pointer to the result of the given task.
 */
#define lace_task_result(t) (&t->d[0])

/**
 * Compute a random number, thread-local (so scalable)
 */
static inline uint64_t lace_rng(LaceWorker *worker)
{
    worker->rng *= 0xda942042e4dd58b5;
    return worker->rng >> 64;
}

/* Some flags that influence Lace behavior */

#if LACE_PIE_TIMES
/* High resolution timer */
static inline uint64_t lace_gethrtime(void)
{
    uint32_t hi, lo;
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
    return (uint64_t)hi<<32 | lo;
}
#endif

#if LACE_COUNT_TASKS
#define PR_COUNTTASK(s) PR_INC(s,CTR_tasks)
#else
#define PR_COUNTTASK(s) /* Empty */
#endif

#if LACE_COUNT_STEALS
#define PR_COUNTSTEALS(s,i) PR_INC(s,i)
#else
#define PR_COUNTSTEALS(s,i) /* Empty */
#endif

#if LACE_COUNT_SPLITS
#define PR_COUNTSPLITS(s,i) PR_INC(s,i)
#else
#define PR_COUNTSPLITS(s,i) /* Empty */
#endif

#if LACE_COUNT_EVENTS
#define PR_ADD(s,i,k) ( ((s)->ctr[i])+=k )
#else
#define PR_ADD(s,i,k) /* Empty */
#endif
#define PR_INC(s,i) PR_ADD(s,i,1)

#define THIEF_EMPTY     ((struct _Worker*)0x0)
#define THIEF_TASK      ((struct _Worker*)0x1)
#define THIEF_COMPLETED ((struct _Worker*)0x2)

#define LACE_STOLEN   ((Worker*)0)
#define LACE_BUSY     ((Worker*)1)
#define LACE_NOWORK   ((Worker*)2)

#if LACE_PIE_TIMES
static __attribute__((unused)) void lace_time_event( LaceWorker *w, int event )
{
    uint64_t now = lace_gethrtime(),
             prev = w->time;

    switch( event ) {

        // Enter application code
        case 1 :
            if(  w->level /* level */ == 0 ) {
                PR_ADD( w, CTR_init, now - prev );
                w->level = 1;
            } else if( w->level /* level */ == 1 ) {
                PR_ADD( w, CTR_wsteal, now - prev );
                PR_ADD( w, CTR_wstealsucc, now - prev );
            } else {
                PR_ADD( w, CTR_lsteal, now - prev );
                PR_ADD( w, CTR_lstealsucc, now - prev );
            }
            break;

            // Exit application code
        case 2 :
            if( w->level /* level */ == 1 ) {
                PR_ADD( w, CTR_wapp, now - prev );
            } else {
                PR_ADD( w, CTR_lapp, now - prev );
            }
            break;

            // Enter sync on stolen
        case 3 :
            if( w->level /* level */ == 1 ) {
                PR_ADD( w, CTR_wapp, now - prev );
            } else {
                PR_ADD( w, CTR_lapp, now - prev );
            }
            w->level++;
            break;

            // Exit sync on stolen
        case 4 :
            if( w->level /* level */ == 1 ) {
                fprintf( stderr, "This should not happen, level = %d\n", w->level );
            } else {
                PR_ADD( w, CTR_lsteal, now - prev );
            }
            w->level--;
            break;

            // Return from failed steal
        case 7 :
            if( w->level /* level */ == 0 ) {
                PR_ADD( w, CTR_init, now - prev );
            } else if( w->level /* level */ == 1 ) {
                PR_ADD( w, CTR_wsteal, now - prev );
            } else {
                PR_ADD( w, CTR_lsteal, now - prev );
            }
            break;

            // Signalling time
        case 8 :
            if( w->level /* level */ == 1 ) {
                PR_ADD( w, CTR_wsignal, now - prev );
                PR_ADD( w, CTR_wsteal, now - prev );
            } else {
                PR_ADD( w, CTR_lsignal, now - prev );
                PR_ADD( w, CTR_lsteal, now - prev );
            }
            break;

            // Done
        case 9 :
            if( w->level /* level */ == 0 ) {
                PR_ADD( w, CTR_init, now - prev );
            } else {
                PR_ADD( w, CTR_close, now - prev );
            }
            break;

        default: return;
    }

    w->time = now;
}
#else
#define lace_time_event( w, e ) /* Empty */
#endif

/**
 * Helper function when a Task stack overflow is detected.
 */
void lace_abort_stack_overflow(void) __attribute__((noreturn));

/**
 * Support for interrupting Lace workers
 */

typedef struct
{
    _Atomic(Task*) t;
    char pad[LACE_CACHE_LINE_SIZE-sizeof(Task *)];
} lace_newframe_t;

extern lace_newframe_t lace_newframe;

/**
 * Interrupt the current worker and run a task in a new frame
 */
void lace_yield(LaceWorker*);

/**
 * Check if current tasks must be interrupted, and if so, interrupt.
 */
static inline void lace_check_yield(LaceWorker *w)
{
    if (__builtin_expect(atomic_load_explicit(&lace_newframe.t, memory_order_relaxed) != NULL, 0)) {
        lace_yield(w);
    }
}

/**
 * Make all tasks of the current worker shared.
 */
static inline void lace_make_all_shared(void)
{
    LaceWorker* w = lace_get_worker();
    if (w->split != w->head) {
        w->split = w->head;
        w->_public->ts.ts.split = w->head - w->dq;
    }
}

/**
 * Helper function for _SYNC implementations
 */
int lace_sync(LaceWorker *w, Task *head);


// Task macros for tasks of arity 0

#define TASK_0(RTYPE, NAME)                                                           \
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union {  RTYPE res; } d;                                                            \
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(Task), "TD_" #NAME " is too large, set LACE_TASKSIZE to a higher value!");\
                                                                                      \
RTYPE NAME##_CALL(LaceWorker*);                                                       \
                                                                                      \
static void NAME##_WRAP(LaceWorker* lace_worker, TD_##NAME *t __attribute__((unused)))\
{                                                                                     \
    t->d.res = NAME##_CALL(lace_worker);                                              \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
Task* NAME##_SPAWN(LaceWorker* _lace_worker)                                          \
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    Task *lace_head = _lace_worker->head;                                             \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    Worker *wt = _lace_worker->_public;                                               \
    if (__builtin_expect(_lace_worker->allstolen, 0)) {                               \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - _lace_worker->dq;                                          \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - _lace_worker->dq;                                          \
        split = _lace_worker->split - _lace_worker->dq;                               \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_NEWFRAME()                                                               \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER()                                                                \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME()                                                                          \
{                                                                                     \
    LaceWorker *worker = lace_get_worker();                                           \
    if (worker != NULL) {                                                             \
        return NAME##_CALL(worker);                                                   \
    } else {                                                                          \
        Task _t;                                                                      \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
                                                                                      \
        lace_run_task(&_t);                                                           \
        return ((TD_##NAME *)t)->d.res;                                               \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_RUNEX()                                                                  \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    lace_run_task_exclusive(&_t);                                                     \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_SYNC(LaceWorker* _lace_worker)                                           \
{                                                                                     \
    Task* head = _lace_worker->head - 1;                                              \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == _lace_worker->_public->movesplit, 1)) {                 \
        if (__builtin_expect(_lace_worker->split <= head, 1)) {                       \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker);                                         \
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker);                                             \
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_0(NAME)                                                             \
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
                                                                                      \
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(Task), "TD_" #NAME " is too large, set LACE_TASKSIZE to a higher value!");\
                                                                                      \
void NAME##_CALL(LaceWorker*);                                                        \
                                                                                      \
static void NAME##_WRAP(LaceWorker* lace_worker, TD_##NAME *t __attribute__((unused)))\
{                                                                                     \
     NAME##_CALL(lace_worker);                                                        \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
Task* NAME##_SPAWN(LaceWorker* _lace_worker)                                          \
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    Task *lace_head = _lace_worker->head;                                             \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    Worker *wt = _lace_worker->_public;                                               \
    if (__builtin_expect(_lace_worker->allstolen, 0)) {                               \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - _lace_worker->dq;                                          \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - _lace_worker->dq;                                          \
        split = _lace_worker->split - _lace_worker->dq;                               \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_NEWFRAME()                                                                \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER()                                                                \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME()                                                                           \
{                                                                                     \
    LaceWorker *worker = lace_get_worker();                                           \
    if (worker != NULL) {                                                             \
        NAME##_CALL(worker);                                                          \
    } else {                                                                          \
        Task _t;                                                                      \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
                                                                                      \
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_RUNEX()                                                                   \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    lace_run_task_exclusive(&_t);                                                     \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SYNC(LaceWorker* _lace_worker)                                            \
{                                                                                     \
    Task* head = _lace_worker->head - 1;                                              \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == _lace_worker->_public->movesplit, 1)) {                 \
        if (__builtin_expect(_lace_worker->split <= head, 1)) {                       \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker);                                                \
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker);                                                    \
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for tasks of arity 1

#define TASK_1(RTYPE, NAME, ATYPE_1, ARG_1)                                           \
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; } args; RTYPE res; } d;                            \
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(Task), "TD_" #NAME " is too large, set LACE_TASKSIZE to a higher value!");\
                                                                                      \
RTYPE NAME##_CALL(LaceWorker*, ATYPE_1);                                              \
                                                                                      \
static void NAME##_WRAP(LaceWorker* lace_worker, TD_##NAME *t __attribute__((unused)))\
{                                                                                     \
    t->d.res = NAME##_CALL(lace_worker, t->d.args.arg_1);                             \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
Task* NAME##_SPAWN(LaceWorker* _lace_worker, ATYPE_1 arg_1)                           \
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    Task *lace_head = _lace_worker->head;                                             \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    Worker *wt = _lace_worker->_public;                                               \
    if (__builtin_expect(_lace_worker->allstolen, 0)) {                               \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - _lace_worker->dq;                                          \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - _lace_worker->dq;                                          \
        split = _lace_worker->split - _lace_worker->dq;                               \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1)                                                  \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1)                                                   \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME(ATYPE_1 arg_1)                                                             \
{                                                                                     \
    LaceWorker *worker = lace_get_worker();                                           \
    if (worker != NULL) {                                                             \
        return NAME##_CALL(worker, arg_1);                                            \
    } else {                                                                          \
        Task _t;                                                                      \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1;                                                     \
        lace_run_task(&_t);                                                           \
        return ((TD_##NAME *)t)->d.res;                                               \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_RUNEX(ATYPE_1 arg_1)                                                     \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    lace_run_task_exclusive(&_t);                                                     \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_SYNC(LaceWorker* _lace_worker)                                           \
{                                                                                     \
    Task* head = _lace_worker->head - 1;                                              \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == _lace_worker->_public->movesplit, 1)) {                 \
        if (__builtin_expect(_lace_worker->split <= head, 1)) {                       \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, t->d.args.arg_1);                        \
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, t->d.args.arg_1);                            \
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_1(NAME, ATYPE_1, ARG_1)                                             \
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; } args; } d;                                       \
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(Task), "TD_" #NAME " is too large, set LACE_TASKSIZE to a higher value!");\
                                                                                      \
void NAME##_CALL(LaceWorker*, ATYPE_1);                                               \
                                                                                      \
static void NAME##_WRAP(LaceWorker* lace_worker, TD_##NAME *t __attribute__((unused)))\
{                                                                                     \
     NAME##_CALL(lace_worker, t->d.args.arg_1);                                       \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
Task* NAME##_SPAWN(LaceWorker* _lace_worker, ATYPE_1 arg_1)                           \
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    Task *lace_head = _lace_worker->head;                                             \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    Worker *wt = _lace_worker->_public;                                               \
    if (__builtin_expect(_lace_worker->allstolen, 0)) {                               \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - _lace_worker->dq;                                          \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - _lace_worker->dq;                                          \
        split = _lace_worker->split - _lace_worker->dq;                               \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_NEWFRAME(ATYPE_1 arg_1)                                                   \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1)                                                   \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME(ATYPE_1 arg_1)                                                              \
{                                                                                     \
    LaceWorker *worker = lace_get_worker();                                           \
    if (worker != NULL) {                                                             \
        NAME##_CALL(worker, arg_1);                                                   \
    } else {                                                                          \
        Task _t;                                                                      \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1;                                                     \
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_RUNEX(ATYPE_1 arg_1)                                                      \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    lace_run_task_exclusive(&_t);                                                     \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SYNC(LaceWorker* _lace_worker)                                            \
{                                                                                     \
    Task* head = _lace_worker->head - 1;                                              \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == _lace_worker->_public->movesplit, 1)) {                 \
        if (__builtin_expect(_lace_worker->split <= head, 1)) {                       \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, t->d.args.arg_1);                               \
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, t->d.args.arg_1);                                   \
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for tasks of arity 2

#define TASK_2(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2)                           \
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; } args; RTYPE res; } d;             \
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(Task), "TD_" #NAME " is too large, set LACE_TASKSIZE to a higher value!");\
                                                                                      \
RTYPE NAME##_CALL(LaceWorker*, ATYPE_1, ATYPE_2);                                     \
                                                                                      \
static void NAME##_WRAP(LaceWorker* lace_worker, TD_##NAME *t __attribute__((unused)))\
{                                                                                     \
    t->d.res = NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2);            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
Task* NAME##_SPAWN(LaceWorker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2)            \
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    Task *lace_head = _lace_worker->head;                                             \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    Worker *wt = _lace_worker->_public;                                               \
    if (__builtin_expect(_lace_worker->allstolen, 0)) {                               \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - _lace_worker->dq;                                          \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - _lace_worker->dq;                                          \
        split = _lace_worker->split - _lace_worker->dq;                               \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2)                                   \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2)                                    \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME(ATYPE_1 arg_1, ATYPE_2 arg_2)                                              \
{                                                                                     \
    LaceWorker *worker = lace_get_worker();                                           \
    if (worker != NULL) {                                                             \
        return NAME##_CALL(worker, arg_1, arg_2);                                     \
    } else {                                                                          \
        Task _t;                                                                      \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                            \
        lace_run_task(&_t);                                                           \
        return ((TD_##NAME *)t)->d.res;                                               \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2)                                      \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    lace_run_task_exclusive(&_t);                                                     \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_SYNC(LaceWorker* _lace_worker)                                           \
{                                                                                     \
    Task* head = _lace_worker->head - 1;                                              \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == _lace_worker->_public->movesplit, 1)) {                 \
        if (__builtin_expect(_lace_worker->split <= head, 1)) {                       \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2);       \
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2);           \
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_2(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2)                             \
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; } args; } d;                        \
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(Task), "TD_" #NAME " is too large, set LACE_TASKSIZE to a higher value!");\
                                                                                      \
void NAME##_CALL(LaceWorker*, ATYPE_1, ATYPE_2);                                      \
                                                                                      \
static void NAME##_WRAP(LaceWorker* lace_worker, TD_##NAME *t __attribute__((unused)))\
{                                                                                     \
     NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2);                      \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
Task* NAME##_SPAWN(LaceWorker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2)            \
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    Task *lace_head = _lace_worker->head;                                             \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    Worker *wt = _lace_worker->_public;                                               \
    if (__builtin_expect(_lace_worker->allstolen, 0)) {                               \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - _lace_worker->dq;                                          \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - _lace_worker->dq;                                          \
        split = _lace_worker->split - _lace_worker->dq;                               \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2)                                    \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2)                                    \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME(ATYPE_1 arg_1, ATYPE_2 arg_2)                                               \
{                                                                                     \
    LaceWorker *worker = lace_get_worker();                                           \
    if (worker != NULL) {                                                             \
        NAME##_CALL(worker, arg_1, arg_2);                                            \
    } else {                                                                          \
        Task _t;                                                                      \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                            \
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2)                                       \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    lace_run_task_exclusive(&_t);                                                     \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SYNC(LaceWorker* _lace_worker)                                            \
{                                                                                     \
    Task* head = _lace_worker->head - 1;                                              \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == _lace_worker->_public->movesplit, 1)) {                 \
        if (__builtin_expect(_lace_worker->split <= head, 1)) {                       \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2);              \
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2);                  \
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for tasks of arity 3

#define TASK_3(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3)           \
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(Task), "TD_" #NAME " is too large, set LACE_TASKSIZE to a higher value!");\
                                                                                      \
RTYPE NAME##_CALL(LaceWorker*, ATYPE_1, ATYPE_2, ATYPE_3);                            \
                                                                                      \
static void NAME##_WRAP(LaceWorker* lace_worker, TD_##NAME *t __attribute__((unused)))\
{                                                                                     \
    t->d.res = NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
Task* NAME##_SPAWN(LaceWorker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    Task *lace_head = _lace_worker->head;                                             \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    Worker *wt = _lace_worker->_public;                                               \
    if (__builtin_expect(_lace_worker->allstolen, 0)) {                               \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - _lace_worker->dq;                                          \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - _lace_worker->dq;                                          \
        split = _lace_worker->split - _lace_worker->dq;                               \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                    \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                     \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                               \
{                                                                                     \
    LaceWorker *worker = lace_get_worker();                                           \
    if (worker != NULL) {                                                             \
        return NAME##_CALL(worker, arg_1, arg_2, arg_3);                              \
    } else {                                                                          \
        Task _t;                                                                      \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;   \
        lace_run_task(&_t);                                                           \
        return ((TD_##NAME *)t)->d.res;                                               \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                       \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    lace_run_task_exclusive(&_t);                                                     \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_SYNC(LaceWorker* _lace_worker)                                           \
{                                                                                     \
    Task* head = _lace_worker->head - 1;                                              \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == _lace_worker->_public->movesplit, 1)) {                 \
        if (__builtin_expect(_lace_worker->split <= head, 1)) {                       \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_3(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3)             \
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; } args; } d;         \
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(Task), "TD_" #NAME " is too large, set LACE_TASKSIZE to a higher value!");\
                                                                                      \
void NAME##_CALL(LaceWorker*, ATYPE_1, ATYPE_2, ATYPE_3);                             \
                                                                                      \
static void NAME##_WRAP(LaceWorker* lace_worker, TD_##NAME *t __attribute__((unused)))\
{                                                                                     \
     NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3);     \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
Task* NAME##_SPAWN(LaceWorker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    Task *lace_head = _lace_worker->head;                                             \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    Worker *wt = _lace_worker->_public;                                               \
    if (__builtin_expect(_lace_worker->allstolen, 0)) {                               \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - _lace_worker->dq;                                          \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - _lace_worker->dq;                                          \
        split = _lace_worker->split - _lace_worker->dq;                               \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                     \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                     \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                                \
{                                                                                     \
    LaceWorker *worker = lace_get_worker();                                           \
    if (worker != NULL) {                                                             \
        NAME##_CALL(worker, arg_1, arg_2, arg_3);                                     \
    } else {                                                                          \
        Task _t;                                                                      \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;   \
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                        \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    lace_run_task_exclusive(&_t);                                                     \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SYNC(LaceWorker* _lace_worker)                                            \
{                                                                                     \
    Task* head = _lace_worker->head - 1;                                              \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == _lace_worker->_public->movesplit, 1)) {                 \
        if (__builtin_expect(_lace_worker->split <= head, 1)) {                       \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3); \
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for tasks of arity 4

#define TASK_4(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(Task), "TD_" #NAME " is too large, set LACE_TASKSIZE to a higher value!");\
                                                                                      \
RTYPE NAME##_CALL(LaceWorker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4);                   \
                                                                                      \
static void NAME##_WRAP(LaceWorker* lace_worker, TD_##NAME *t __attribute__((unused)))\
{                                                                                     \
    t->d.res = NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
Task* NAME##_SPAWN(LaceWorker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    Task *lace_head = _lace_worker->head;                                             \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    Worker *wt = _lace_worker->_public;                                               \
    if (__builtin_expect(_lace_worker->allstolen, 0)) {                               \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - _lace_worker->dq;                                          \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - _lace_worker->dq;                                          \
        split = _lace_worker->split - _lace_worker->dq;                               \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)     \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)      \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)                \
{                                                                                     \
    LaceWorker *worker = lace_get_worker();                                           \
    if (worker != NULL) {                                                             \
        return NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4);                       \
    } else {                                                                          \
        Task _t;                                                                      \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
        lace_run_task(&_t);                                                           \
        return ((TD_##NAME *)t)->d.res;                                               \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)        \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    lace_run_task_exclusive(&_t);                                                     \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_SYNC(LaceWorker* _lace_worker)                                           \
{                                                                                     \
    Task* head = _lace_worker->head - 1;                                              \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == _lace_worker->_public->movesplit, 1)) {                 \
        if (__builtin_expect(_lace_worker->split <= head, 1)) {                       \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_4(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; } args; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(Task), "TD_" #NAME " is too large, set LACE_TASKSIZE to a higher value!");\
                                                                                      \
void NAME##_CALL(LaceWorker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4);                    \
                                                                                      \
static void NAME##_WRAP(LaceWorker* lace_worker, TD_##NAME *t __attribute__((unused)))\
{                                                                                     \
     NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
Task* NAME##_SPAWN(LaceWorker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    Task *lace_head = _lace_worker->head;                                             \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    Worker *wt = _lace_worker->_public;                                               \
    if (__builtin_expect(_lace_worker->allstolen, 0)) {                               \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - _lace_worker->dq;                                          \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - _lace_worker->dq;                                          \
        split = _lace_worker->split - _lace_worker->dq;                               \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)      \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)      \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)                 \
{                                                                                     \
    LaceWorker *worker = lace_get_worker();                                           \
    if (worker != NULL) {                                                             \
        NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4);                              \
    } else {                                                                          \
        Task _t;                                                                      \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)         \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    lace_run_task_exclusive(&_t);                                                     \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SYNC(LaceWorker* _lace_worker)                                            \
{                                                                                     \
    Task* head = _lace_worker->head - 1;                                              \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == _lace_worker->_public->movesplit, 1)) {                 \
        if (__builtin_expect(_lace_worker->split <= head, 1)) {                       \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for tasks of arity 5

#define TASK_5(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(Task), "TD_" #NAME " is too large, set LACE_TASKSIZE to a higher value!");\
                                                                                      \
RTYPE NAME##_CALL(LaceWorker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5);          \
                                                                                      \
static void NAME##_WRAP(LaceWorker* lace_worker, TD_##NAME *t __attribute__((unused)))\
{                                                                                     \
    t->d.res = NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
Task* NAME##_SPAWN(LaceWorker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    Task *lace_head = _lace_worker->head;                                             \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    Worker *wt = _lace_worker->_public;                                               \
    if (__builtin_expect(_lace_worker->allstolen, 0)) {                               \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - _lace_worker->dq;                                          \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - _lace_worker->dq;                                          \
        split = _lace_worker->split - _lace_worker->dq;                               \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5) \
{                                                                                     \
    LaceWorker *worker = lace_get_worker();                                           \
    if (worker != NULL) {                                                             \
        return NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5);                \
    } else {                                                                          \
        Task _t;                                                                      \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
        lace_run_task(&_t);                                                           \
        return ((TD_##NAME *)t)->d.res;                                               \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    lace_run_task_exclusive(&_t);                                                     \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_SYNC(LaceWorker* _lace_worker)                                           \
{                                                                                     \
    Task* head = _lace_worker->head - 1;                                              \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == _lace_worker->_public->movesplit, 1)) {                 \
        if (__builtin_expect(_lace_worker->split <= head, 1)) {                       \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_5(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; } args; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(Task), "TD_" #NAME " is too large, set LACE_TASKSIZE to a higher value!");\
                                                                                      \
void NAME##_CALL(LaceWorker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5);           \
                                                                                      \
static void NAME##_WRAP(LaceWorker* lace_worker, TD_##NAME *t __attribute__((unused)))\
{                                                                                     \
     NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
Task* NAME##_SPAWN(LaceWorker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    Task *lace_head = _lace_worker->head;                                             \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    Worker *wt = _lace_worker->_public;                                               \
    if (__builtin_expect(_lace_worker->allstolen, 0)) {                               \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - _lace_worker->dq;                                          \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - _lace_worker->dq;                                          \
        split = _lace_worker->split - _lace_worker->dq;                               \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)  \
{                                                                                     \
    LaceWorker *worker = lace_get_worker();                                           \
    if (worker != NULL) {                                                             \
        NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5);                       \
    } else {                                                                          \
        Task _t;                                                                      \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    lace_run_task_exclusive(&_t);                                                     \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SYNC(LaceWorker* _lace_worker)                                            \
{                                                                                     \
    Task* head = _lace_worker->head - 1;                                              \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == _lace_worker->_public->movesplit, 1)) {                 \
        if (__builtin_expect(_lace_worker->split <= head, 1)) {                       \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for tasks of arity 6

#define TASK_6(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(Task), "TD_" #NAME " is too large, set LACE_TASKSIZE to a higher value!");\
                                                                                      \
RTYPE NAME##_CALL(LaceWorker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6); \
                                                                                      \
static void NAME##_WRAP(LaceWorker* lace_worker, TD_##NAME *t __attribute__((unused)))\
{                                                                                     \
    t->d.res = NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
Task* NAME##_SPAWN(LaceWorker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    Task *lace_head = _lace_worker->head;                                             \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    Worker *wt = _lace_worker->_public;                                               \
    if (__builtin_expect(_lace_worker->allstolen, 0)) {                               \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - _lace_worker->dq;                                          \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - _lace_worker->dq;                                          \
        split = _lace_worker->split - _lace_worker->dq;                               \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    LaceWorker *worker = lace_get_worker();                                           \
    if (worker != NULL) {                                                             \
        return NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);         \
    } else {                                                                          \
        Task _t;                                                                      \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
        lace_run_task(&_t);                                                           \
        return ((TD_##NAME *)t)->d.res;                                               \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    lace_run_task_exclusive(&_t);                                                     \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_SYNC(LaceWorker* _lace_worker)                                           \
{                                                                                     \
    Task* head = _lace_worker->head - 1;                                              \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == _lace_worker->_public->movesplit, 1)) {                 \
        if (__builtin_expect(_lace_worker->split <= head, 1)) {                       \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_6(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; } args; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(Task), "TD_" #NAME " is too large, set LACE_TASKSIZE to a higher value!");\
                                                                                      \
void NAME##_CALL(LaceWorker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6);  \
                                                                                      \
static void NAME##_WRAP(LaceWorker* lace_worker, TD_##NAME *t __attribute__((unused)))\
{                                                                                     \
     NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
Task* NAME##_SPAWN(LaceWorker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    Task *lace_head = _lace_worker->head;                                             \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    Worker *wt = _lace_worker->_public;                                               \
    if (__builtin_expect(_lace_worker->allstolen, 0)) {                               \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - _lace_worker->dq;                                          \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - _lace_worker->dq;                                          \
        split = _lace_worker->split - _lace_worker->dq;                               \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    LaceWorker *worker = lace_get_worker();                                           \
    if (worker != NULL) {                                                             \
        NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);                \
    } else {                                                                          \
        Task _t;                                                                      \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    lace_run_task_exclusive(&_t);                                                     \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SYNC(LaceWorker* _lace_worker)                                            \
{                                                                                     \
    Task* head = _lace_worker->head - 1;                                              \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == _lace_worker->_public->movesplit, 1)) {                 \
        if (__builtin_expect(_lace_worker->split <= head, 1)) {                       \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
