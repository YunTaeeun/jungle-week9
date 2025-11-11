/* The main thread acquires locks A and B, then it creates two
   higher-priority threads.  Each of these threads blocks
   acquiring one of the locks and thus donate their priority to
   the main thread.  The main thread releases the locks in turn
   and relinquishes its donated priorities.
   
   Based on a test originally submitted for Stanford's CS 140 in
   winter 1999 by Matt Franklin <startled@leland.stanford.edu>,
   Greg Hutchins <gmh@leland.stanford.edu>, Yu Ping Hu
   <yph@cs.stanford.edu>.  Modified by arens. */
/* 메인 스레드가 A, B 두 락을 잡고 더 높은 우선순위의 두 스레드를 생성합니다.
   각 스레드는 자신이 필요한 락을 기다리며 메인 스레드에게 우선순위를 기부하고,
   메인 스레드는 락을 순서대로 해제하면서 기부받은 우선순위를 회수해야 합니다.
   해당 테스트는 Stanford CS140(1999년 겨울) 제출 코드를 기반으로 arens가 수정했습니다. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"

static thread_func a_thread_func;
static thread_func b_thread_func;

void
test_priority_donate_multiple (void) 
{
  struct lock a, b;

  /* This test does not work with the MLFQS. */
  ASSERT (!thread_mlfqs);
  /* MLFQS 모드에서는 donation 테스트가 동작하지 않습니다. */

  /* Make sure our priority is the default. */
  ASSERT (thread_get_priority () == PRI_DEFAULT);
  /* 메인 스레드 우선순위가 기본값인지 확인합니다. */

  lock_init (&a);
  lock_init (&b);

  lock_acquire (&a);
  lock_acquire (&b);
  /* 메인 스레드가 두 락을 모두 미리 획득합니다. */

  thread_create ("a", PRI_DEFAULT + 1, a_thread_func, &a);
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 1, thread_get_priority ());
  /* 스레드 a가 락을 기다리면 메인 스레드 우선순위가 PRI_DEFAULT+1이 되어야 합니다. */

  thread_create ("b", PRI_DEFAULT + 2, b_thread_func, &b);
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 2, thread_get_priority ());
  /* 스레드 b의 기부로 우선순위는 PRI_DEFAULT+2가 되어야 합니다. */

  lock_release (&b);
  msg ("Thread b should have just finished.");
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 1, thread_get_priority ());
  /* lock b 해제 후 스레드 b가 실행되어야 하고, 남은 기부는 스레드 a로부터의 것만 유지됩니다. */

  lock_release (&a);
  msg ("Thread a should have just finished.");
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT, thread_get_priority ());
  /* 마지막으로 lock a를 해제하면 스레드 a가 실행되고, 기부가 모두 회수되어 기본 우선순위로 복귀해야 합니다. */
}

static void
a_thread_func (void *lock_) 
{
  struct lock *lock = lock_;

  lock_acquire (lock);
  msg ("Thread a acquired lock a.");
  lock_release (lock);
  msg ("Thread a finished.");
  /* 스레드 a는 lock a를 획득/해제하며 종료합니다. */
}

static void
b_thread_func (void *lock_) 
{
  struct lock *lock = lock_;

  lock_acquire (lock);
  msg ("Thread b acquired lock b.");
  lock_release (lock);
  msg ("Thread b finished.");
  /* 스레드 b 역시 동일하게 처리합니다. */
}
