#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
	 nonnegative integer along with two atomic operators for
	 manipulating it:

	 - down or "P": wait for the value to become positive, then
	 decrement it.

	 - up or "V": increment the value (and wake up one waiting
	 thread, if any). */
/* 세마포어를 초기화하는 함수
	주어진 value로 초기화하며
	빈 waiters 리스트를 생성한 다음 곧장 해당 세마포어를 리스트의 첫째 자리에 넣는다.
*/
void sema_init(struct semaphore *sema, unsigned value)
{
	ASSERT(sema != NULL);

	sema->value = value;
	list_init(&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
	 to become positive and then atomically decrements it.

	 This function may sleep, so it must not be called within an
	 interrupt handler.  This function may be called with
	 interrupts disabled, but if it sleeps then the next scheduled
	 thread will probably turn interrupts back on. This is
	 sema_down function. */
/*
- 세마포어 값이 0이면 대기 - waiters에 추가, thread_block으로 잠듦
- 값이 1 이상이 되면 깨어나서 값을 1 줄임
- 인터럽트 핸들러에서는 사용 금지
- 인터럽트가 꺼진 상태에서도 호출 가능(단, 잠들면 다음 스레드가 인터럽트 켬)
- 내가 자원 없어서 잠들면, 다음에 실행되는 스레드가 인터럽트 상태를 복구(켜줌)한다는 의미
*/
void sema_down(struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT(sema != NULL);
	ASSERT(!intr_context());

	old_level = intr_disable();
	while (sema->value == 0)
	{
		/* 	추가한 부분. week08. 11.10. project1 - priority-change TC */
		// list_push_back(&sema->waiters, &thread_current()->elem);
		list_insert_ordered(&sema->waiters, &thread_current()->elem, compare_ready_priority, NULL);
		thread_block();
	}
	sema->value--;
	intr_set_level(old_level);
}

/* Down or "P" operation on a semaphore, but only if the
	 semaphore is not already 0.  Returns true if the semaphore is
	 decremented, false otherwise.

	 This function may be called from an interrupt handler. */
bool sema_try_down(struct semaphore *sema)
{
	enum intr_level old_level;
	bool success;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level(old_level);

	return success;
}

/*
- 세마포어(자원 관리 변수)의 값을 1 올리고,
- 기다리고 있던 스레드가 있으면 그 중 한 명을 깨워서 실행
- 인터럽트 핸들러에서도 호출 가능 */
void sema_up(struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (!list_empty(&sema->waiters))
	{
		// 가장 높은 순위의 스레드를 깨우기 위해 정렬
		list_sort(&sema->waiters, compare_ready_priority, NULL);
		// 자고 있던 스레드 깨워서 ready_list에 넣는다.
		thread_unblock(list_entry(list_pop_front(&sema->waiters), struct thread, elem));
	}

	sema->value++;

	/* 	추가한 부분. week08. 11.10. project1 - priority-change TC */
	preemption_by_priority();

	intr_set_level(old_level);
}

/* 	추가한 부분. week08. 11.10. project1 - priority-change TC */
bool compare_sema_priority(const struct list_elem *a, const struct list_elem *b, void *aux)
{
	struct semaphore_elem *sa = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sb = list_entry(b, struct semaphore_elem, elem);

	struct list_elem *ta = list_begin(&sa->semaphore.waiters);
	struct list_elem *tb = list_begin(&sb->semaphore.waiters);
	return compare_ready_priority(ta, tb, NULL);
}

static void sema_test_helper(void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
	 between a pair of threads.  Insert calls to printf() to see
	 what's going on. */
void sema_self_test(void)
{
	struct semaphore sema[2];
	int i;

	printf("Testing semaphores...");
	sema_init(&sema[0], 0);
	sema_init(&sema[1], 0);
	thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up(&sema[0]);
		sema_down(&sema[1]);
	}
	printf("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper(void *sema_)
{
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down(&sema[0]);
		sema_up(&sema[1]);
	}
}

/*
- LOCK은 한 번에 하나의 스레드만 소유 가능(중복 소유 불가, 재귀적 사용 불가)
- LOCK은 세마포어(초기값 1)를 기반으로 하며, 세마포어와 달리 오직 한 스레드만 소유
- 세마포어는 여러 값(>1) 가능, LOCK은 1만 가능
- 세마포어는 소유자 개념 없음, LOCK은 반드시 소유자가 acquire/release 해야 함 */
void lock_init(struct lock *lock)
{
	ASSERT(lock != NULL);

	lock->holder = NULL;
	sema_init(&lock->semaphore, 1);
}

/*
- LOCK을 획득한다.
- lock이 사용 중이라면 LOCK이 사용 가능해질 때까지 잠듭니다.
- 현재 스레드가 이미 LOCK을 가지고 있으면 안 됩니다.

- 이 함수는 잠들 수 있으므로, 인터럽트 핸들러에서는 호출하면 안 됩니다.
- 인터럽트가 꺼진 상태에서도 호출할 수 있지만, 잠들게 되면 다음에 실행되는 스레드가 인터럽트를 다시 켭니다. */
// 1. lock이 사용중이면
// 	- 내가 어떤 lock을 기다리는지 waiting lock에 기록
//	- 우선순위 기부 : donate_priority()
// 2. 내 우선순위가 holder 우선순위보다 높은가?
//	- 높으면 donate
//	- 낮으면 기다림
// 3. holder가 기다리는 다른 락이 있는가?
//	- 있으면 nested donation
//	- 없으면 끝
void lock_acquire(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(!lock_held_by_current_thread(lock));

	if (lock->holder != NULL) // 1. lock이 사용중인가?
	{
		// 내가 어떤 lock을 기다리는지 waiting lock에 기록
		thread_current()->waiting_lock = lock;

		// 기부자 명단에 현재 스레드를 추가한다.
		list_insert_ordered(&lock->holder->donators, &thread_current()->donation_elem, compare_donation_priority, NULL);

		// 재귀적 우선순위 기부
		donate_priority(lock->holder);
	}

	sema_down(&lock->semaphore);
	lock->holder = thread_current();
	thread_current()->waiting_lock = NULL;
}

void donate_priority(struct thread *holder)
{
	// printf("🟥 donate_priority()\n");
	struct thread *curr_thread = thread_current();
	int depth = 0;
	const int MAX_DEPTH = 8;

	// holder가 NULL이 아니고, depth가 MAX값보다 작을 때 까지
	while (holder != NULL && depth < MAX_DEPTH)
	{
		// 내 우선순위가 holder의 우선순위보다 높으면 기부
		if (curr_thread->priority > holder->priority)
		{
			holder->priority = curr_thread->priority;
		}
		// 중첩 기부: holder가 기다리는 락이 있으면 재귀적 기부
		if (holder->waiting_lock != NULL)
		{
			holder = holder->waiting_lock->holder;
			depth++;
		}
		else
		{
			break;
		}
	}
}

/* Tries to acquires LOCK and returns true if successful or false
	 on failure.  The lock must not already be held by the current
	 thread.

	 This function will not sleep, so it may be called within an
	 interrupt handler. */
bool lock_try_acquire(struct lock *lock)
{
	bool success;

	ASSERT(lock != NULL);
	ASSERT(!lock_held_by_current_thread(lock));

	success = sema_try_down(&lock->semaphore);
	if (success)
		lock->holder = thread_current();
	return success;
}

/*
- 현재 스레드가 소유한 LOCK을 해제합니다.
- LOCK은 반드시 현재 스레드가 가지고 있어야만 해제할 수 있습니다.
- 인터럽트 핸들러에서는 LOCK을 획득할 수 없으므로, 인터럽트 핸들러에서 LOCK을 해제하는 것도 의미가 없습니다. */
void lock_release(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(lock_held_by_current_thread(lock));

	// 1. 이 lock을 기다리던 스레드의 기부 원복
	remove_donations(lock);

	// 2. 남은 기부 중에 최고 우선순위로 갱신
	recaculate_priority();

	// 3. lock 해제
	lock->holder = NULL;
	sema_up(&lock->semaphore);
}

/* 이 lock 관련된 donation 제거*/
void remove_donations(struct lock *lock)
{
	struct thread *curr_thread = thread_current();
	struct list_elem *e;

	// donator 순회
	e = list_begin(&curr_thread->donators);
	while (e != list_end(&curr_thread->donators))
	{
		struct thread *donor = list_entry(e, struct thread, donation_elem);

		// 이 donor가 현재 해제하는 lock을 기다리고 있었다면 제거
		// donators에 있다고 해서 꼭 같은 공유자원 lock을 기다리는 것이 아니니까.
		if (donor->waiting_lock == lock)
		{
			e = list_remove(e); // 제거하고 다음 elem 반환
		}
		else
		{
			e = list_next(e);
		}
	}
}

/* 우선순위 재계산 */
void recaculate_priority(void)
{
	struct thread *curr_thread = thread_current();

	// 기본 우선순위로 초기화
	curr_thread->priority = curr_thread->original_priority;

	// 남은 기부 중 최댓과 비교
	if (!list_empty(&curr_thread->donators))
	{
		struct thread *top_donor = list_entry(list_front(&curr_thread->donators), struct thread, donation_elem);

		// 기부받은 우선순위가 더 높으면 그것을 사용
		if (top_donor->priority > curr_thread->priority)
		{
			curr_thread->priority = top_donor->priority;
		}
	}
}

/* Returns true if the current thread holds LOCK, false
	 otherwise.  (Note that testing whether some other thread holds
	 a lock would be racy.) */
bool lock_held_by_current_thread(const struct lock *lock)
{
	ASSERT(lock != NULL);

	return lock->holder == thread_current();
}

/*
 * [5] struct semaphore_elem - condition variable의 대기자
 * ┌─────────────────────────────┐
 * │ struct semaphore_elem       │
 * ├─────────────────────────────┤
 * │ elem: list_elem             │ ← condition.waiters에 연결
 * │ semaphore: struct semaphore │ ← 각 대기 스레드용 개별 semaphore
 * └─────────────────────────────┘
 *         │
 *         └─ semaphore.waiters → [특정 thread->elem]
 */
/* One semaphore in a list. */

/* 	삭제하고 .h로 이동한 부분. week08. 11.10. project1 - priority-change TC */
// struct semaphore_elem
// {
// 	struct list_elem elem;			/* List element. */
// 	struct semaphore semaphore; /* This semaphore. */
// };

/* Initializes condition variable COND.  A condition variable
	 allows one piece of code to signal a condition and cooperating
	 code to receive the signal and act upon it. */
void cond_init(struct condition *cond)
{
	ASSERT(cond != NULL);

	list_init(&cond->waiters);
}

/*
- LOCK을 해제하고 COND 신호를 기다렸다가, 신호가 오면 LOCK을 다시 획득합니다.
- (Mesa 스타일: 신호와 대기가 원자적이지 않으므로, 깨어난 뒤 조건을 다시 확인해야 함)
- 조건 변수는 하나의 LOCK과만 연결, LOCK은 여러 조건 변수와 연결 가능
- 잠들 수 있으므로 인터럽트 핸들러에서는 호출 금지.. */
void cond_wait(struct condition *cond, struct lock *lock)
{
	struct semaphore_elem waiter;

	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	sema_init(&waiter.semaphore, 0);

	/* 	추가한 부분. week08. 11.10. project1 - priority-change TC */
	// list_push_back(&cond->waiters, &waiter.elem);
	list_insert_ordered(&cond->waiters, &waiter.elem, compare_sema_priority, NULL);

	lock_release(lock);
	sema_down(&waiter.semaphore);
	lock_acquire(lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
	 this function signals one of them to wake up from its wait.
	 LOCK must be held before calling this function.

	 An interrupt handler cannot acquire a lock, so it does not
	 make sense to try to signal a condition variable within an
	 interrupt handler. */
void cond_signal(struct condition *cond, struct lock *lock UNUSED)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	if (!list_empty(&cond->waiters))
	{
		/* 	추가한 부분. week08. 11.10. project1 - priority-change TC */
		list_sort(&cond->waiters, compare_sema_priority, NULL);

		sema_up(&list_entry(list_pop_front(&cond->waiters),
												struct semaphore_elem, elem)
								 ->semaphore);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
	 LOCK).  LOCK must be held before calling this function.

	 An interrupt handler cannot acquire a lock, so it does not
	 make sense to try to signal a condition variable within an
	 interrupt handler. */
void cond_broadcast(struct condition *cond, struct lock *lock)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);

	while (!list_empty(&cond->waiters))
		cond_signal(cond, lock);
}
