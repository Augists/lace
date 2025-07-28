# Changelog

All notable changes to Lace will be documented in this file.

## [2.0.0] - 2025-07-28

This is a major rewrite of the Lace API. The goal is to make the framework more
intuitive to work with by reducing the amount of macro-style code. For example, 
instead of `SPAWN(fib, 40)` we write `fib_SPAWN(lace, 40)`. In most cases, the 
performance is not changed by this, except the `knapsack` benchmark, as in the 
new style the head of the deque is now stored in the worker struct in the heap, 
rather than as a register or on the program stack.
See also [#10](https://github.com/trolando/lace/pull/10).

### Added

- Documentation is much improved now.
- Added a more 'proper' Changelog (this file).
- Added a large number of benchmarks from the Nowa repository.

### Changed

- Significant API-breaking rewrite to be a bit more user-friendly.
- Changed algorithm for `lace_rng` from lcg to lehmer64.
- `lace_start` now takes three parameters: the number of workers, the size of
  the task deque, the size of the program stack. There are reasonable defaults
  for the size of the task deque (100K) and program stack (16M).
- Improved algorithm to determine which cores to pin threads to with `hwloc`

### Fixed

- The flags `LACE_USE_HWLOC` and `LACE_USE_MMAP` were ignored because they were
  not propagated in the config header. This is now fixed.
- Correctly set the program stack size to at least 16M

## [1.4.2] - 2023-11-18

### Added

- Once again install pkg-config files.

## [1.4.1] - 2023-10-25

### Added

- Allow installing Lace again.

### Fixed

- Several "pedantic" warnings are now solved.

## [1.4.0] - 2023-03-19

## Added

- Improved support for Windows and added windows tests to the GitHub CI script.
- When suspended and a task is queued (via `RUN`), Lace now automatically
  resumes until the task is finished, then suspends again.

## Changed

- Don't use `thread_local` from `threads.h` since it's reportedly unreliable.
- Now allocate memory with `aligned_alloc` instead of `posix_memalign`.
- The `pi` benchmark now uses a local rng rather than `rand_r`.

## Fixed

- If `mmap` is not found, automatically disable `LACE_USE_MMAP`.
- Fixed Windows implementation of getting the number of processors.

## [1.3.1] - 2022-09-04

Lace no longer automatically generates the header files when building; instead,
the generated header files are part of the repository.

## [1.3.0] - 2022-09-04

## [1.2] - 2021-06-19

## [1.1] - 2021-04-17

In this new version of Lace, it is no longer possible to use the current thread
as a Lace thread.

Instead, lace_start starts the worker threads, lace_stop stops the worker
threads.
Then use RUN(...) to run a Lace task from outside Lace.

Only use LACE_ME and CALL macros from inside a Lace thread, i.e., if you are
running deep inside some Lace task.init

To use Lace:
- make new tasks like in the benchmark examples: VOID_TASK_n for tasks that
  don't return values and have n parameters, and TASK_n otherwise
- you can separate the TASK_DECL_n for header files and TASK_IMPL_n for
  implementation files if you want that
- to have Lace run a Lace task, use RUN(taskname, param1, param2, ...)
- if RUN is used from a Lace thread, this is detected and the Lace thread runs
  the task
- when using RUN, the current thread halts until the task is completed
- use macros SPAWN, SYNC, CALL from inside a Lace task to spawn/sync/call other
  Lace tasks
- if you are in a Lace worker thread but not a Lace task, use LACE_ME before
  using SPAWN/SYNC/CALL
- use lace_suspend and lace_resume to temporarily halt Lace workers
- use lace_stop (not while threads are suspended) to end Lace workers and
  reclaim memory

## [1.0] - 2017-02-02
