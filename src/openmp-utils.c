#include "data.table.h"
#ifdef _OPENMP
#include <pthread.h>
#endif

/* GOALS:
* 1) By default use all CPU for end-user convenience in most usage scenarios.
* 2) But not on CRAN - two threads max is policy
* 3) And not if user doesn't want to:
*    i) Respect env variable OMP_NUM_THREADS (which just calls (ii) on startup)
*    ii) Respect omp_set_num_threads()
*    iii) Provide way to restrict data.table only independently of base R and
*         other packages using openMP
* 4) Avoid user needing to remember to unset this control after their use of data.table
* 5) Automatically drop down to 1 thread when called from parallel package (e.g. mclapply) to
*    avoid the deadlock/hang (#1745 and #1727) and return to prior state afterwards.
*/

static int DTthreads = 0;
// Never read directly, hence static. Always go via getDTthreads() and check that in
// future using grep's in CRAN_Release.cmd

int getDTthreads() {
#ifdef _OPENMP
  int ans = DTthreads == 0 ? omp_get_max_threads() : MIN(DTthreads, omp_get_max_threads());
  return MAX(1, ans);
#else
  return 1;
#endif
}

SEXP getDTthreads_R() {
  return ScalarInteger(getDTthreads());
}

SEXP setDTthreads(SEXP threads) {
  if (!isInteger(threads) || length(threads) != 1 || INTEGER(threads)[0] < 0) {
    // catches NA too since NA is -ve
    error("Argument to setDTthreads must be a single integer >= 0. \
           Default 0 is recommended to use all CPU.");
  }
  int old = DTthreads;
  DTthreads = INTEGER(threads)[0];
#ifdef _OPENMP
  if (omp_get_max_threads() < omp_get_thread_limit()) {
    if (DTthreads==0) {
      // for example after test 1705 has auto switched to single-threaded for parallel's fork,
      // we want to return to multi-threaded.
      // omp_set_num_threads() sets the value returned by omp_get_max_threads()
      omp_set_num_threads(omp_get_thread_limit());
    } else if (DTthreads > omp_get_max_threads()) {
      omp_set_num_threads( MIN(DTthreads, omp_get_thread_limit()) );
    }
  }
#endif
  return ScalarInteger(old);
}

/*
  Auto avoid deadlock when data.table is used from within parallel::mclapply
  GNU OpenMP seems ok with just setting DTthreads to 1 which limits the next parallel region
  if data.table is used within the fork'd proceess. This is tested by test 1705.

  We used to have an after_fork() callback too, to return to multi-threaded mode after parallel's
  fork completes. But now in an attempt to alleviate problems propogating (likely Intel's OpenMP only)
  we now leave data.table in single-threaded mode after parallel's fork. User can call setDTthreads(0)
  to return to multi-threaded as we do in tests on Linux.

  DO NOT call omp_set_num_threads(1) inside when_fork()!! That causes a different crash/hang on MacOS
  upon mclapply's fork even if data.table is merely loaded and neither used yet in the session nor by
  what mclapply is calling. Even when passing on CRAN's MacOS all-OK. As discovered by several MacOS
  and RStudio users here when 1.10.4-2 went to CRAN :
     https://github.com/Rdatatable/data.table/issues/2418
  v1.10.4-3 removed the call to omp_set_num_threads().
  We do not know why calling (mere) omp_set_num_threads(1) in the when_fork() would cause a problem
  on MacOS.
*/

void when_fork() {
  DTthreads = 1;
}

void avoid_openmp_hang_within_fork() {
  // Called once on loading data.table from init.c
#ifdef _OPENMP
  pthread_atfork(&when_fork, NULL, NULL);
#endif
}

