/* The main thread acquires a lock.  Then it creates a
   higher-priority thread that blocks acquiring the lock, causing
   it to donate their priorities to the main thread.  The main
   thread attempts to lower its priority, which should not take
   effect until the donation is released. */
/* 메인 스레드가 락을 잡은 뒤, 더 높은 우선순위의 스레드를 생성하여
   해당 스레드가 락을 기다리는 동안 메인 스레드에게 우선순위를 기부하도록 합니다.
   그 상태에서 메인 스레드가 자신의 우선순위를 낮추려고 해도,
   기부가 유지되는 동안에는 실제 우선순위가 낮아지지 않아야 합니다. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"

static thread_func acquire_thread_func;

void
test_priority_donate_lower (void) 
{
  struct lock lock;

  /* This test does not work with the MLFQS. */
  ASSERT (!thread_mlfqs);
  /* MLFQS 모드에서는 donation 테스트가 동작하지 않습니다. */

  /* Make sure our priority is the default. */
  ASSERT (thread_get_priority () == PRI_DEFAULT);
  /* 우선순위가 기본값인지 확인합니다. */

  lock_init (&lock);
  lock_acquire (&lock);
  /* 메인 스레드가 먼저 락을 획득합니다. */
  thread_create ("acquire", PRI_DEFAULT + 10, acquire_thread_func, &lock);
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 10, thread_get_priority ());
  /* 더 높은 우선순위 스레드를 생성하여 기부가 발생하면
     메인 스레드의 실제 우선순위가 PRI_DEFAULT+10이 되어야 합니다. */

  msg ("Lowering base priority...");
  thread_set_priority (PRI_DEFAULT - 10);
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 10, thread_get_priority ());
  /* 기부 중에는 우선순위를 낮추더라도 실제 우선순위는 내려가지 않아야 합니다. */
  lock_release (&lock);
  msg ("acquire must already have finished.");
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT - 10, thread_get_priority ());
  /* 락을 해제한 뒤에는 기부가 회수되어 원래 설정한 낮은 우선순위로 돌아와야 합니다. */
}

static void
acquire_thread_func (void *lock_) 
{
  struct lock *lock = lock_;

  lock_acquire (lock);
  msg ("acquire: got the lock");
  /* 기부를 통해 우선순위가 올라간 스레드가 락을 획득/해제합니다. */
  lock_release (lock);
  msg ("acquire: done");
}
