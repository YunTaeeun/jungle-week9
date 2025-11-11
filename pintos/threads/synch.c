/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"


/* One semaphore in a list. */
/* 리스트 안의 세마포어 단위 요소입니다. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
   struct thread *waiter_thread;        /* Waiting thread. */
};

static void donate_priority(struct thread *t);
static bool compare_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
static bool compare_priority_cond(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
/* 세마포어 SEMA를 VALUE로 초기화합니다. 세마포어는 0 이상의 정수와,
   이를 조작하는 두 가지 원자적 연산으로 구성됩니다.

   - down 또는 "P": 값이 양수가 될 때까지 기다린 뒤 값을 감소시킵니다.

   - up 또는 "V": 값을 증가시키고, 대기 중인 스레드가 있다면 하나를 깨웁니다. */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
/* 세마포어에 대한 down 또는 "P" 연산입니다. SEMA의 값이 양수가 될 때까지
   기다린 뒤, 원자적으로 값을 감소시킵니다.

   이 함수는 슬립할 수 있으므로 인터럽트 핸들러 내부에서 호출하면 안 됩니다.
   인터럽트를 비활성화한 상태에서 호출할 수 있지만, 잠들게 되면 다음에 스케줄된
   스레드가 인터럽트를 다시 켤 가능성이 큽니다. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
      list_insert_ordered(&sema->waiters, &thread_current ()->elem, compare_priority, NULL); 
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
/* 세마포어 값이 0이 아닐 때만 수행하는 down 또는 "P" 연산입니다.
   값을 감소시켰다면 true, 그렇지 않으면 false를 반환합니다.

   이 함수는 인터럽트 핸들러에서 호출될 수 있습니다. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
/* 세마포어에 대한 up 또는 "V" 연산입니다. SEMA의 값을 증가시키고,
   대기 중인 스레드가 있다면 한 스레드를 깨웁니다.

   이 함수는 인터럽트 핸들러에서 호출될 수 있습니다. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;
	ASSERT (sema != NULL);

	old_level = intr_disable ();
   struct thread *cur_thread = thread_current ();
   struct thread *t = NULL;
	
   if (!list_empty(&sema->waiters)) {
      // 최고 우선순위 스레드를 수동으로 찾기
      struct list_elem *e;
      struct list_elem *max_elem = list_begin(&sema->waiters);
      struct thread *max_thread = list_entry(max_elem, struct thread, elem);
      
      for (e = list_next(max_elem); e != list_end(&sema->waiters); e = list_next(e)) {
         struct thread *cur = list_entry(e, struct thread, elem);
         if (cur->priority > max_thread->priority) {
            max_elem = e;
            max_thread = cur;
         }
      }
      
      list_remove(max_elem);
      t = max_thread;
      thread_unblock(t);
   }

	sema->value++;
	intr_set_level (old_level);

   if (t != NULL && cur_thread->priority < t->priority) {
		if (intr_context()) {
			intr_yield_on_return(); // 인터럽트 컨텍스트면 리턴 시 양보
		} else {
			thread_yield(); // 일반 컨텍스트면 바로 양보
		}
   }
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
/* 두 스레드 사이에서 제어를 '핑퐁'처럼 주고받으며 세마포어를 테스트하는
   자가 테스트 함수입니다. 동작을 확인하려면 printf()를 삽입해 볼 수 있습니다. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
/* sema_self_test()에서 사용하는 스레드 함수입니다. */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* 스레드에서서 우선순위 비교 함수입니다. */
static bool compare_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
	struct thread *ta = list_entry(a, struct thread, elem);
	struct thread *tb = list_entry(b, struct thread, elem);
	return ta->priority > tb -> priority;
}


/* 조건 변수의 semaphore_elem 특수 구조체에서의 대기 스레드의 우선순위 비교를 위한한 함수입니다. */
static bool compare_priority_cond(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
	struct semaphore_elem *sa = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sb = list_entry(b, struct semaphore_elem, elem);
	return sa->waiter_thread->priority > sb->waiter_thread->priority;
}







/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
/* LOCK을 초기화합니다. 한 번에 하나의 스레드만 락을 보유할 수 있습니다.
   이 락은 "재귀적"이지 않으므로, 현재 락을 보유한 스레드가 다시 획득하려고 하면
   오류가 발생합니다.

   락은 초기값이 1인 세마포어의 특수한 형태입니다. 차이점은 두 가지입니다.
   첫째, 세마포어는 1보다 큰 값을 가질 수 있지만 락은 언제나 하나의 스레드만
   소유할 수 있습니다. 둘째, 세마포어는 소유자가 없어서 한 스레드가 down하고
   다른 스레드가 up할 수 있지만, 락은 같은 스레드가 획득과 해제를 모두 수행해야
   합니다. 이러한 제약이 불편하다면 세마포어를 사용하는 것이 좋습니다. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
/* LOCK을 획득합니다. 필요하다면 사용 가능해질 때까지 잠듭니다.
   현재 스레드가 이미 해당 락을 보유해서는 안 됩니다.

   이 함수는 잠들 수 있으므로 인터럽트 핸들러 내부에서 호출하면 안 됩니다.
   인터럽트를 비활성화한 상태에서 호출할 수 있지만, 잠들게 되면 깨우는 과정에서
   인터럽트가 다시 활성화될 수 있습니다. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));
   struct thread *curr_thread = thread_current ();

   
   if (lock->holder != NULL) { // 다른 놈이 가지고 있어서 기다려야 되는 상황이면면
      curr_thread->waiting_lock = lock; // 기다리는 락에 넣고
      donate_priority(lock->holder); // 필요하면 도네이션이 일어나도록
   }
	sema_down (&lock->semaphore); 
	lock->holder = curr_thread; // 내가 락 홀드드
   curr_thread->waiting_lock = NULL; // 기다리는 락 제거
   list_push_back(&curr_thread->holding_locks, &lock->elem); // 내가 가진 락들 리스트에 넣어줌
}

void donate_priority(struct thread *t) {
   if (thread_current()->priority > t->priority) { 
      // 만약 지금 우선순위를 가진게 나보다 우선순위가 낮으면 도네이트
      t->priority = thread_current()->priority;

      //현재 스레드가 있는 리스트에서 재정렬 필요
      if (t->status == THREAD_READY) {
         enum intr_level old_level = intr_disable(); // 레디 리스트 건드리는 거니 인터럽트 차단단
         thread_reorder_ready_list(t);
         intr_set_level(old_level);
      }
      
      if (t->waiting_lock != NULL) {
         // 만약 내 앞도 기다리는게 있으면 그거에 대해서도 도네이션 필요한지 체크
         donate_priority(t->waiting_lock->holder);
      }
   }
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
/* LOCK 획득을 시도합니다. 성공하면 true, 실패하면 false를 반환합니다.
   현재 스레드가 이미 락을 보유하고 있어서는 안 됩니다.

   이 함수는 잠들지 않으므로 인터럽트 핸들러에서도 호출할 수 있습니다. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
/* 현재 스레드가 보유한 LOCK을 해제합니다. lock_release 함수입니다.

   인터럽트 핸들러는 락을 획득할 수 없으므로, 락을 해제하려고 시도하는 것도
   의미가 없습니다. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));
   struct thread *curr_thread = thread_current ();
   struct list_elem *e;
   int old_priority = curr_thread->priority;
   
   list_remove(&lock->elem); // 내가 보유한 락 리스트에서 제거
   curr_thread->priority = curr_thread->original_priority; // 우선순위 복원

   // 내가 가지고 있는 락들을 기다리는 놈들중 가장 높은 우선순위로 필요하면 가져옮
   for (e = list_begin(&curr_thread->holding_locks); e != list_end(&curr_thread->holding_locks); e = list_next(e)) {
      struct lock *l = list_entry(e, struct lock, elem);
      
      // l->semaphore.waiters에서 최고 우선순위 찾기 
      if (!list_empty(&l->semaphore.waiters)) {
         struct thread *waiter = list_entry(list_front(&l->semaphore.waiters), struct thread, elem);
         if (waiter->priority > curr_thread->priority) {
            curr_thread->priority = waiter->priority;
         }
      }
   }   

   lock->holder = NULL;
	sema_up (&lock->semaphore);

   // priority가 낮아졌으면 yield
   if (curr_thread->priority < old_priority) {
      thread_yield();
   }
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
/* 현재 스레드가 LOCK을 보유하고 있으면 true, 아니면 false를 반환합니다.
   (다른 스레드가 락을 보유하고 있는지를 검사하면 경쟁 상태가 발생할 수 있습니다.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}




































/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
/* 조건 변수 COND를 초기화합니다. 조건 변수는 하나의 코드가 조건을 알리고,
   협력하는 다른 코드가 해당 신호를 받아 처리하도록 합니다. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
/* LOCK을 원자적으로 해제한 뒤, 다른 코드가 COND를 신호할 때까지 기다립니다.
   COND가 신호를 받으면, 반환하기 전에 LOCK을 다시 획득합니다.
   이 함수를 호출하기 전에 LOCK을 반드시 보유하고 있어야 합니다.

   이 함수가 구현하는 모니터는 "Mesa" 방식이며, 신호를 보내고 받는 동작은 원자적이지 않습니다.
   따라서 보통 기다림이 끝난 뒤 조건을 다시 확인하고 필요하면 다시 기다려야 합니다.

   하나의 조건 변수는 하나의 락과만 연결되지만, 하나의 락은 여러 조건 변수와 연결될 수 있습니다.
   즉, 락과 조건 변수 간에는 일대다 관계가 존재합니다.

   이 함수는 잠들 수 있으므로 인터럽트 핸들러 내부에서 호출하면 안 됩니다.
   인터럽트를 비활성화한 상태에서도 호출할 수 있지만, 잠들게 되면 인터럽트가 다시 켜질 수 있습니다. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;
   waiter.waiter_thread = thread_current ();

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
   list_insert_ordered(&cond->waiters, &waiter.elem, compare_priority_cond, NULL); 
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
/* LOCK으로 보호되는 COND에서 대기 중인 스레드가 있다면 하나를 깨우는 신호를 보냅니다.
   이 함수를 호출하기 전에 LOCK을 반드시 보유하고 있어야 합니다.

   인터럽트 핸들러는 락을 획득할 수 없으므로, 조건 변수에 신호를 보내려 해도 의미가 없습니다. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters))
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
/* LOCK으로 보호되는 COND에서 대기 중인 모든 스레드를 깨웁니다.
   이 함수를 호출하기 전에 LOCK을 반드시 보유하고 있어야 합니다.

   인터럽트 핸들러는 락을 획득할 수 없으므로, 조건 변수에 신호를 보내도 소용이 없습니다. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
