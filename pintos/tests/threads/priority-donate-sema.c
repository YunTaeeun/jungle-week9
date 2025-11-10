/* Low priority thread L acquires a lock, then blocks downing a
   semaphore.  Medium priority thread M then blocks waiting on
   the same semaphore.  Next, high priority thread H attempts to
   acquire the lock, donating its priority to L.

   Next, the main thread ups the semaphore, waking up L.  L
   releases the lock, which wakes up H.  H "up"s the semaphore,
   waking up M.  H terminates, then M, then L, and finally the
   main thread.

   Written by Godmar Back <gback@cs.vt.edu>. */
/* 낮은 우선순위 스레드 L이 락을 잡고 세마포어에서 대기합니다.
   이어서 중간 우선순위 스레드 M도 같은 세마포어에서 기다리고,
   높은 우선순위 스레드 H는 락을 기다리면서 우선순위를 L에게 기부합니다.
   세마포어가 순차적으로 깨워지면서 L → H → M 순으로 실행되어야 하는지를 확인합니다.
   Godmar Back이 작성했습니다. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"

struct lock_and_sema 
  {
    struct lock lock;
    struct semaphore sema;
  };

static thread_func l_thread_func;
static thread_func m_thread_func;
static thread_func h_thread_func;

void
test_priority_donate_sema (void) 
{
  struct lock_and_sema ls;

  /* This test does not work with the MLFQS. */
  ASSERT (!thread_mlfqs);
  /* MLFQS 모드에서는 donation 테스트가 동작하지 않습니다. */

  /* Make sure our priority is the default. */
  ASSERT (thread_get_priority () == PRI_DEFAULT);
  /* 메인 스레드 우선순위가 기본값인지 확인합니다. */

  lock_init (&ls.lock);
  sema_init (&ls.sema, 0);
  /* 세마포어는 0으로 초기화하여 down 시 대기하도록 합니다. */
  thread_create ("low", PRI_DEFAULT + 1, l_thread_func, &ls);
  thread_create ("med", PRI_DEFAULT + 3, m_thread_func, &ls);
  thread_create ("high", PRI_DEFAULT + 5, h_thread_func, &ls);
  /* 각 스레드를 순차적으로 생성합니다. (low → medium → high) */
  sema_up (&ls.sema);
  msg ("Main thread finished.");
  /* 세마포어를 먼저 up하여 low 스레드가 깨어나도록 하면서 시나리오를 시작합니다. */
}

static void
l_thread_func (void *ls_) 
{
  struct lock_and_sema *ls = ls_;

  lock_acquire (&ls->lock);
  msg ("Thread L acquired lock.");
  sema_down (&ls->sema);
  msg ("Thread L downed semaphore.");
  lock_release (&ls->lock);
  msg ("Thread L finished.");
  /* L이 락을 잡은 뒤 세마포어에서 대기하고, 깨어난 뒤 락을 해제합니다. */
}

static void
m_thread_func (void *ls_) 
{
  struct lock_and_sema *ls = ls_;

  sema_down (&ls->sema);
  msg ("Thread M finished.");
  /* M은 세마포어에서 깨어나면 바로 종료합니다. */
}

static void
h_thread_func (void *ls_) 
{
  struct lock_and_sema *ls = ls_;

  lock_acquire (&ls->lock);
  msg ("Thread H acquired lock.");

  sema_up (&ls->sema);
  lock_release (&ls->lock);
  msg ("Thread H finished.");
  /* H가 락을 획득하면서 L에게 우선순위를 기부했고,
     세마포어를 up하여 M을 깨운 뒤 락을 해제하고 종료합니다. */
}
