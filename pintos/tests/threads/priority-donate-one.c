/* The main thread acquires a lock.  Then it creates two
   higher-priority threads that block acquiring the lock, causing
   them to donate their priorities to the main thread.  When the
   main thread releases the lock, the other threads should
   acquire it in priority order.

   Based on a test originally submitted for Stanford's CS 140 in
   winter 1999 by Matt Franklin <startled@leland.stanford.edu>,
   Greg Hutchins <gmh@leland.stanford.edu>, Yu Ping Hu
   <yph@cs.stanford.edu>.  Modified by arens. */
/* 메인 스레드가 락을 잡은 뒤 두 개의 더 높은 우선순위 스레드를 생성하여
   락을 기다리면서 메인 스레드에게 우선순위를 기부하게 합니다.
   메인 스레드가 락을 해제하면 높은 우선순위 스레드부터 순서대로 실행되어야 합니다.
   원본 테스트는 Stanford CS 140(1999년 겨울)에서 제출된 코드이며 arens가 수정했습니다. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"

static thread_func acquire1_thread_func;
static thread_func acquire2_thread_func;

void
test_priority_donate_one (void) 
{
  struct lock lock;

  /* This test does not work with the MLFQS. */
  ASSERT (!thread_mlfqs);
  /* MLFQS 모드에서는 donation 테스트가 동작하지 않습니다. */

  /* Make sure our priority is the default. */
  ASSERT (thread_get_priority () == PRI_DEFAULT);
  /* 메인 스레드 우선순위가 기본값인지 확인합니다. */

  lock_init (&lock);
  lock_acquire (&lock);
  thread_create ("acquire1", PRI_DEFAULT + 1, acquire1_thread_func, &lock);
  msg ("This thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 1, thread_get_priority ());
  /* 첫 번째 스레드가 락을 기다리면, 메인 스레드가 그 우선순위를 기부받아
     PRI_DEFAULT+1 우선순위를 회득해야 합니다. */
  thread_create ("acquire2", PRI_DEFAULT + 2, acquire2_thread_func, &lock);
  msg ("This thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 2, thread_get_priority ());
  /* 두 번째 스레드가 더 높은 우선순위를 기부하므로 메인 스레드는 PRI_DEFAULT+2가 되어야 합니다. */
  lock_release (&lock);
  msg ("acquire2, acquire1 must already have finished, in that order.");
  msg ("This should be the last line before finishing this test.");
  /* 락을 해제하면 높은 우선순위 스레드부터 실행되었는지 메시지로 확인합니다. */
}

static void
acquire1_thread_func (void *lock_) 
{
  struct lock *lock = lock_;

  lock_acquire (lock);
  msg ("acquire1: got the lock");
  lock_release (lock);
  msg ("acquire1: done");
  /* 첫 번째 스레드가 락을 획득/해제하여 종료합니다. */
}

static void
acquire2_thread_func (void *lock_) 
{
  struct lock *lock = lock_;

  lock_acquire (lock);
  msg ("acquire2: got the lock");
  lock_release (lock);
  msg ("acquire2: done");
  /* 두 번째 스레드도 락을 획득/해제하여 마무리합니다. */
}
