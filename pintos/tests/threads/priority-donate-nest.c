/* Low-priority main thread L acquires lock A.  Medium-priority
   thread M then acquires lock B then blocks on acquiring lock A.
   High-priority thread H then blocks on acquiring lock B.  Thus,
   thread H donates its priority to M, which in turn donates it
   to thread L.
   
   Based on a test originally submitted for Stanford's CS 140 in
   winter 1999 by Matt Franklin <startled@leland.stanford.edu>,
   Greg Hutchins <gmh@leland.stanford.edu>, Yu Ping Hu
   <yph@cs.stanford.edu>.  Modified by arens. */
/* 낮은 우선순위의 메인 스레드 L이 lock A를 잡는다.
   중간 우선순위 스레드 M이 lock B를 잡은 후 lock A를 기다리고,
   높은 우선순위 스레드 H는 lock B를 기다리면서 우선순위를 차례로 기부한다.
   이 연쇄 donation이 제대로 전파되는지 확인하는 테스트로,
   Stanford CS140 테스트 기반이며 arens가 수정했다. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"

struct locks 
  {
    struct lock *a;
    struct lock *b;
  };

static thread_func medium_thread_func;
static thread_func high_thread_func;

void
test_priority_donate_nest (void) 
{
  struct lock a, b;
  struct locks locks;

  /* This test does not work with the MLFQS. */
  ASSERT (!thread_mlfqs);
  /* MLFQS 모드에서는 동작하지 않음 */

  /* Make sure our priority is the default. */
  ASSERT (thread_get_priority () == PRI_DEFAULT);
  /* 메인 스레드 우선순위가 기본값인지 확인 */

  lock_init (&a);
  lock_init (&b);

  lock_acquire (&a);
  /* 메인 스레드가 lock A를 먼저 획득 */

  locks.a = &a;
  locks.b = &b;
  thread_create ("medium", PRI_DEFAULT + 1, medium_thread_func, &locks);
  thread_yield ();
  msg ("Low thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 1, thread_get_priority ());
  /* 스레드 M이 lock B를 잡고 lock A를 기다리면서, 메인 스레드로 우선순위 기부가 전파됨 */

  thread_create ("high", PRI_DEFAULT + 2, high_thread_func, &b);
  thread_yield ();
  msg ("Low thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 2, thread_get_priority ());
  /* 스레드 H가 lock B를 기다리면서 중간 -> 낮은 우선순위까지 기부가 체인으로 전파됨 */

  lock_release (&a);
  thread_yield ();
  msg ("Medium thread should just have finished.");
  msg ("Low thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT, thread_get_priority ());
  /* lock A를 해제하면 중간 스레드가 실행되고, 기부가 회수되어 원래 우선순위로 복귀 */
}

static void
medium_thread_func (void *locks_) 
{
  struct locks *locks = locks_;

  lock_acquire (locks->b);
  lock_acquire (locks->a);
  /* M 스레드는 B를 잡고 A를 기다리며 H로부터 기부 받은 우선순위를 유지 */

  msg ("Medium thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 2, thread_get_priority ());
  msg ("Medium thread got the lock.");

  lock_release (locks->a);
  thread_yield ();

  lock_release (locks->b);
  thread_yield ();

  msg ("High thread should have just finished.");
  msg ("Middle thread finished.");
  /* 높은 우선순위 스레드가 마무리된 뒤 M도 종료 */
}

static void
high_thread_func (void *lock_) 
{
  struct lock *lock = lock_;

  lock_acquire (lock);
  msg ("High thread got the lock.");
  lock_release (lock);
  msg ("High thread finished.");
  /* 높은 우선순위 스레드가 lock B를 획득/해제하면서 donation 과정을 마무리 */
}
