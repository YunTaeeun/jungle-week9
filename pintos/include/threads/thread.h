#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/synch.h"
#ifdef VM
#include "vm/vm.h"
#endif

/* States in a thread's life cycle. */
/* 스레드 생애 주기의 상태를 나타냅니다. */
enum thread_status
{
    THREAD_RUNNING, /* Running thread. */
                    /* 실행 중인 스레드. */
    THREAD_READY,   /* Not running but ready to run. */
                    /* 실행 중은 아니지만 실행 준비가 된 스레드. */
    THREAD_BLOCKED, /* Waiting for an event to trigger. */
                    /* 특정 사건이 발생하기를 기다리며 블록된 스레드. */
    THREAD_DYING    /* About to be destroyed. */
                    /* 곧 파괴될 예정인 스레드. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
/* 스레드 식별자 타입입니다.
   필요하다면 원하는 타입으로 재정의할 수 있습니다. */
typedef int tid_t;
#define TID_ERROR ((tid_t) - 1) /* Error value for tid_t. */
                                /* tid_t를 위한 에러 값. */

/* Thread priorities. */
/* 스레드 우선순위 값들. */
#define PRI_MIN 0      /* Lowest priority. */
                       /* 가장 낮은 우선순위. */
#define PRI_DEFAULT 31 /* Default priority. */
                       /* 기본 우선순위. */
#define PRI_MAX 63     /* Highest priority. */
                       /* 가장 높은 우선순위. */

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* 커널 스레드 또는 사용자 프로세스입니다.
 *
 * 각 스레드 구조체는 고유한 4 kB 페이지에 저장됩니다.
 * 스레드 구조체 자체는 페이지의 맨 아래(오프셋 0)에 위치하고,
 * 페이지의 나머지 부분은 위쪽에서 아래쪽으로 성장하는
 * 스레드의 커널 스택을 위해 예약됩니다(오프셋 4 kB).
 *
 * 결과적으로 다음 두 가지를 주의해야 합니다.
 *
 *    1. `struct thread`의 크기를 너무 크게 만들면 안 됩니다.
 *       크기가 커지면 커널 스택을 위한 공간이 부족해집니다.
 *       기본 `struct thread`는 매우 작으며, 1 kB 이하로 유지하는 것이 좋습니다.
 *
 *    2. 커널 스택도 너무 커지지 않도록 주의해야 합니다.
 *       스택이 넘치면 스레드 상태가 손상됩니다.
 *       따라서 커널 함수에서 큰 구조체나 배열을 비정적 지역 변수로 선언하지 말고,
 *       대신 malloc() 또는 palloc_get_page() 같은 동적 할당을 사용해야 합니다.
 *
 * 이러한 문제의 첫 번째 증상은 thread_current()의 assertion 실패일 수 있습니다.
 * thread_current()는 실행 중인 스레드의 `magic` 멤버가 THREAD_MAGIC인지 확인하는데,
 * 스택 오버플로우는 이 값을 변경하여 assertion을 유발하기 때문입니다. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
/* `elem` 멤버는 두 가지 목적으로 사용됩니다.
 * 실행 큐(thread.c)의 원소로 쓰일 수도 있고,
 * 세마포어 대기 리스트(synch.c)의 원소로 쓰일 수도 있습니다.
 * 두 상태가 상호 배타적이기 때문에 이런 이중 용도가 가능합니다.
 * 준비(ready) 상태의 스레드만 실행 큐에 있고,
 * 블록(blocked) 상태의 스레드만 세마포어 대기 리스트에 있기 때문입니다. */
struct thread
{
    /* Owned by thread.c. */
    tid_t tid;                  /* Thread identifier. */
                                /* 스레드 식별자. */
    enum thread_status status;  /* Thread state. */
                                /* 스레드 상태. */
    char name[16];              /* Name (for debugging purposes). */
                                /* 디버깅을 위한 스레드 이름. */
    int priority;               /* Priority.  현재 우선순위 */
    int original_priority;      // 처음 부여 받는 우선순위
    struct list holding_locks;  // 내가 보유한 락 리스트
    struct lock* waiting_lock;  // 내가 기다리는 락

    /* system call에서 사용 */
    int exit_status;  // 종료 상태

    int nice;        // Nice 값 (-20 ~ 20)
    int recent_cpu;  // 최근 CPU 사용량 (고정소수점) (17.14)

    /* Shared between thread.c and synch.c. */
    struct list_elem elem; /* List element. */
                           /* 리스트 원소(실행 큐 혹은 대기 큐에서 사용). */
    int64_t wakeup_tick;   /* Wakeup tick. */
                           /* 깨어날 시각(틱). */

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint64_t* pml4; /* Page map level 4 */
                    /* 사용자 프로그램용 PML4 포인터. */
#endif
#ifdef VM
    /* Table for whole virtual memory owned by thread. */
    struct supplemental_page_table spt;
    /* 보조 페이지 테이블. */
#endif

    /* Owned by thread.c. */
    struct intr_frame tf; /* Information for switching */
                          /* 문맥 전환을 위한 인터럽트 프레임. */
    unsigned magic;       /* Detects stack overflow. */
                          /* 스택 오버플로우 감지를 위한 매직 값. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
/* false(기본값)이면 라운드 로빈 스케줄러를 사용하고,
   true이면 다단계 피드백 큐 스케줄러(mlfqs)를 사용합니다.
   커널 명령줄 옵션 "-o mlfqs"로 제어됩니다. */
extern bool thread_mlfqs;

void thread_init(void);
void thread_start(void);

void thread_tick(void);
void thread_print_stats(void);

typedef void thread_func(void* aux);
tid_t thread_create(const char* name, int priority, thread_func*, void*);

void thread_block(void);
void thread_unblock(struct thread*);
void thread_reorder_ready_list(struct thread*);

struct thread* thread_current(void);
tid_t thread_tid(void);
const char* thread_name(void);
void thread_exit(void) NO_RETURN;
void thread_yield(void);

int thread_get_priority(void);
void thread_set_priority(int);

int thread_get_nice(void);
void thread_set_nice(int);
int thread_get_recent_cpu(void);
int thread_get_load_avg(void);

void do_iret(struct intr_frame* tf);

#endif /* threads/thread.h */
