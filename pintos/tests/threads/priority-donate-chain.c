/* The main thread set its priority to PRI_MIN and creates 7 threads 
   (thread 1..7) with priorities PRI_MIN + 3, 6, 9, 12, ...
   The main thread initializes 8 locks: lock 0..7 and acquires lock 0.

   When thread[i] starts, it first acquires lock[i] (unless i == 7.)
   Subsequently, thread[i] attempts to acquire lock[i-1], which is held by
   thread[i-1], except for lock[0], which is held by the main thread.
   Because the lock is held, thread[i] donates its priority to thread[i-1],
   which donates to thread[i-2], and so on until the main thread
   receives the donation.

   After threads[1..7] have been created and are blocked on locks[0..7],
   the main thread releases lock[0], unblocking thread[1], and being
   preempted by it.
   Thread[1] then completes acquiring lock[0], then releases lock[0],
   then releases lock[1], unblocking thread[2], etc.
   Thread[7] finally acquires & releases lock[7] and exits, allowing 
   thread[6], then thread[5] etc. to run and exit until finally the 
   main thread exits.

   In addition, interloper threads are created at priority levels
   p = PRI_MIN + 2, 5, 8, 11, ... which should not be run until the 
   corresponding thread with priority p + 1 has finished.
  
   Written by Godmar Back <gback@cs.vt.edu> */
/* 메인 스레드는 자신의 우선순위를 PRI_MIN으로 낮춘 뒤 7개의 스레드(thread 1..7)를
   생성하고, 각각의 우선순위를 PRI_MIN + 3, 6, 9, 12 ... 로 설정합니다.
   메인 스레드는 8개의 락(lock 0..7)을 초기화한 뒤, lock 0을 먼저 획득합니다.

   thread[i]가 시작되면 (i == 7이 아닌 경우) lock[i]를 먼저 획득합니다.
   이후 lock[i-1]을 획득하려 시도하는데, 이 락은 thread[i-1]이 (i>1) 혹은
   메인 스레드가 (i==1) 이미 잡고 있는 상태입니다.
   따라서 thread[i]는 자신의 우선순위를 thread[i-1]에게 기부하고,
   thread[i-1]도 thread[i-2]에게 기부하는 식으로 우선순위가 체인 형태로 전파되어
   결국 메인 스레드까지 도달합니다.

   thread[1..7]이 생성되어 각각 lock[0..7]에서 블록된 뒤,
   메인 스레드는 lock[0]을 해제하여 thread[1]을 깨우고, 그에 의해 선점됩니다.
   thread[1]은 lock[0] 획득을 완료하고 lock[0], lock[1]을 차례로 해제하여 thread[2]를 깨웁니다.
   이런 식으로 thread[7]이 lock[7]을 획득/해제하여 종료하면,
   다시 thread[6], thread[5], ... 순으로 실행/종료되며 마지막에 메인 스레드가 종료합니다.

   추가로, 중간 간섭(interloper) 스레드들도 PRI_MIN + 2, 5, 8, 11 ... 우선순위로 생성되는데,
   반드시 그보다 우선순위가 1 높은 스레드가 종료된 후에야 실행되어야 합니다.

   작성자: Godmar Back <gback@cs.vt.edu> */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"

#define NESTING_DEPTH 8

struct lock_pair
  {
    struct lock *second;
    struct lock *first;
  };
/* 두 개의 락 포인터를 묶어서 전달하기 위한 구조체.
   second: 반드시 획득해야 하는 락 (chain에서 앞쪽 락)
   first : 마지막 스레드를 제외하면 먼저 획득해야 할 락 */

/* thread_func이라는 함수를 donor_thread_func이 이름으로도 쓰고 interloper_thread_func이 이름으로도 쓰겠다는 선언
   이렇게 쓰는 이유는 그냥 가독성을 위한 거임 같은 함수를 여러 곳에서쓸 때 쓰는 이유를 표현하기 위함*/
static thread_func donor_thread_func;
static thread_func interloper_thread_func;

void
test_priority_donate_chain (void) 
{
  int i;  
  struct lock locks[NESTING_DEPTH - 1];
  struct lock_pair lock_pairs[NESTING_DEPTH];
  /* 7개의 thread[i]가 사용할 락 배열과
     각 스레드에게 전달할 락 쌍 정보를 위한 배열을 선언합니다. */

  /* This test does not work with the MLFQS. */
  ASSERT (!thread_mlfqs);
  /* MLFQS 모드에서는 이 테스트가 동작하지 않으므로 비활성화되어야 합니다. */

  thread_set_priority (PRI_MIN);
  /* 메인 스레드 우선순위를 최소로 낮춰 놓습니다. */

  for (i = 0; i < NESTING_DEPTH - 1; i++)
    lock_init (&locks[i]);
  /* lock[0]부터 lock[6]까지 초기화합니다. */

  lock_acquire (&locks[0]);
  msg ("%s got lock.", thread_name ());
  /* 메인 스레드가 lock[0]을 미리 잡아 chain의 시작점이 됩니다. */

  for (i = 1; i < NESTING_DEPTH; i++)
    {
      char name[16];
      int thread_priority;

      snprintf (name, sizeof name, "thread %d", i);
      thread_priority = PRI_MIN + i * 3;
      lock_pairs[i].first = i < NESTING_DEPTH - 1 ? locks + i: NULL;
      lock_pairs[i].second = locks + i - 1;
      /* thread i에게는 lock[i] (첫 번째), lock[i-1] (두 번째)를 순서대로 시도하도록 설정합니다.
         마지막 스레드(thread 7)는 lock[i]가 필요 없으므로 first를 NULL로 둡니다. */

      thread_create (name, thread_priority, donor_thread_func, lock_pairs + i);
      msg ("%s should have priority %d.  Actual priority: %d.",
          thread_name (), thread_priority, thread_get_priority ());
      /* donor 스레드를 생성한 후, 메인 스레드가 기대 우선순위로 기부 받았는지 확인 메시지를 출력합니다. */

      snprintf (name, sizeof name, "interloper %d", i);
      thread_create (name, thread_priority - 1, interloper_thread_func, NULL);
      /* interloper 스레드(우선순위가 1 낮은 스레드)를 생성하여
         donation이 제대로 적용되면 순서가 뒤바뀌지 않는지 확인합니다. */
    }

  lock_release (&locks[0]);
  msg ("%s finishing with priority %d.", thread_name (),
                                         thread_get_priority ());
  /* 모든 준비가 끝난 후 lock[0]을 해제하여 체인을 시작시키고,
     마지막에 자신의 우선순위를 출력합니다. */
}

static void
donor_thread_func (void *locks_) 
{
  struct lock_pair *locks = locks_;

  if (locks->first)
    lock_acquire (locks->first);
  /* i < 7인 경우 먼저 lock[i]를 획득합니다. (thread 7은 건너뜀) */

  lock_acquire (locks->second);
  msg ("%s got lock", thread_name ());
  /* lock[i-1]을 획득하면서 앞선 스레드에게 우선순위를 기부합니다. */

  lock_release (locks->second);
  msg ("%s should have priority %d. Actual priority: %d", 
        thread_name (), (NESTING_DEPTH - 1) * 3,
        thread_get_priority ());

  if (locks->first)
    lock_release (locks->first);
  /* (필요하다면) lock[i]까지 모두 해제. */

  msg ("%s finishing with priority %d.", thread_name (),
                                         thread_get_priority ());
  /* 스레드 종료 시점에서 우선순위가 적절히 회복되었는지 출력합니다. */
}

static void
interloper_thread_func (void *arg_ UNUSED)
{
  msg ("%s finished.", thread_name ());
}
/* interloper 스레드는 특별한 일을 하지 않고 끝나며,
   donation이 제대로 이루어질 경우 선점되지 않고 뒤늦게 실행됩니다. */

// vim: sw=2
