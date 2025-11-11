/* The main thread acquires locks A and B, then it creates three
   higher-priority threads.  The first two of these threads block
   acquiring one of the locks and thus donate their priority to
   the main thread.  The main thread releases the locks in turn
   and relinquishes its donated priorities, allowing the third thread
   to run.

   In this test, the main thread releases the locks in a different
   order compared to priority-donate-multiple.c.
   
   Written by Godmar Back <gback@cs.vt.edu>. 
   Based on a test originally submitted for Stanford's CS 140 in
   winter 1999 by Matt Franklin <startled@leland.stanford.edu>,
   Greg Hutchins <gmh@leland.stanford.edu>, Yu Ping Hu
   <yph@cs.stanford.edu>.  Modified by arens. */
/* 메인 스레드가 락 A와 B를 잡고 우선순위가 높은 세 스레드를 생성합니다.
   그중 두 스레드는 락을 기다리며 메인 스레드에게 우선순위를 기부하고,
   메인 스레드가 락을 해제하는 순서를 바꿔 가며 기부가 올바르게 회수되는지 확인합니다.
   나머지 스레드는 기부 대상이 아니지만, 최종적으로 기부가 끝난 뒤 실행되어야 합니다.
   Godmar Back이 작성했으며 Stanford CS 140 테스트를 기반으로 arens가 수정했습니다. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"

static thread_func a_thread_func;
static thread_func b_thread_func;
static thread_func c_thread_func;

void
test_priority_donate_multiple2 (void) 
{
  struct lock a, b;

  /* This test does not work with the MLFQS. */
  ASSERT (!thread_mlfqs);
  /* MLFQS에서는 donation 테스트가 동작하지 않습니다. */

  /* Make sure our priority is the default. */
  ASSERT (thread_get_priority () == PRI_DEFAULT);
  /* 메인 스레드 우선순위가 기본값인지 확인합니다. */

  lock_init (&a);
  lock_init (&b);

  lock_acquire (&a);
  lock_acquire (&b);
  /* 메인 스레드가 두 락을 모두 먼저 획득합니다. */

  thread_create ("a", PRI_DEFAULT + 3, a_thread_func, &a);
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 3, thread_get_priority ());
  /* 스레드 a가 락을 기다리면 메인 스레드는 PRI_DEFAULT+3 우선순위를 기부받아야 합니다. */

  thread_create ("c", PRI_DEFAULT + 1, c_thread_func, NULL);
  /* 스레드 c는 락을 기다리지 않는 비교용 스레드입니다. */

  thread_create ("b", PRI_DEFAULT + 5, b_thread_func, &b);
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 5, thread_get_priority ());
  /* 스레드 b가 더 높은 우선순위를 기부하여 메인 스레드는 PRI_DEFAULT+5가 되어야 합니다. */

  lock_release (&a);
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 5, thread_get_priority ());
  /* lock a를 먼저 해제해도 아직 lock b의 기부가 남아 있으므로 우선순위는 유지됩니다. */

  lock_release (&b);
  msg ("Threads b, a, c should have just finished, in that order.");
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT, thread_get_priority ());
  /* lock b를 해제하면 스레드 b, a, c가 순서대로 실행되어야 하고,
     기부가 모두 회수되어 기본 우선순위로 돌아와야 합니다. */
}

static void
a_thread_func (void *lock_) 
{
  struct lock *lock = lock_;

  lock_acquire (lock);
  msg ("Thread a acquired lock a.");
  lock_release (lock);
  msg ("Thread a finished.");
}

static void
b_thread_func (void *lock_) 
{
  struct lock *lock = lock_;

  lock_acquire (lock);
  msg ("Thread b acquired lock b.");
  lock_release (lock);
  msg ("Thread b finished.");
}

static void
c_thread_func (void *a_ UNUSED) 
{
  msg ("Thread c finished.");
}
