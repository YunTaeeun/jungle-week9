#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
/* struct thread의 `magic` 멤버에 사용되는 임의의 값입니다.
   스택 오버플로우를 감지하는 데 쓰이며, 자세한 내용은 thread.h 상단의
   큰 주석을 참고하세요. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
/* 기본 스레드를 위한 임의의 값입니다.
   이 값은 수정하면 안 됩니다. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
/* THREAD_READY 상태에 있는 프로세스들의 리스트로,
   실행 준비가 되어 있지만 실제로 실행 중은 아닌 프로세스들을 담습니다. */
static struct list ready_list;

static int load_avg;  // 시스템 전체 load average (고정소수점)

/* List of processes in THREAD_BLOCKED state, that is, processes
   that are blocked and waiting for an event to trigger. */
/* THREAD_BLOCKED 상태, 즉 이벤트가 발생하길 기다리며 블록된 프로세스들의 리스트입니다. */

/* Idle thread. */
/* Idle 스레드입니다. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
/* init.c:main()을 실행하는 초기 스레드입니다. */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
/* allocate_tid()에서 사용하는 락입니다. */
static struct lock tid_lock;

/* Thread destruction requests */
/* 스레드 파괴 요청 목록입니다. */
static struct list destruction_req;

/* Statistics. */
/* 통계 정보입니다. */
static long long idle_ticks;   /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks;   /* # of timer ticks in user programs. */

/* Scheduling. */
/* 스케줄링 관련 상수입니다. */
#define TIME_SLICE 4          /* # of timer ticks to give each thread. */
static unsigned thread_ticks; /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
/* false(기본값)이면 라운드 로빈 스케줄러를 사용하고,
   true이면 다단계 피드백 큐 스케줄러를 사용합니다.
   커널 명령줄 옵션 "-o mlfqs"로 제어됩니다. */
bool thread_mlfqs;

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule(void);
static tid_t allocate_tid(void);
static bool compare_priority(const struct list_elem *a, const struct list_elem *b,
                             void *aux UNUSED);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *)(pg_round_down(rrsp())))

// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = {0, 0x00af9a000000ffff, 0x00cf92000000ffff};

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
/* 현재 실행 중인 코드를 스레드로 전환하여 스레딩 시스템을 초기화합니다.
   일반적으로는 불가능하지만, loader.S가 스택의 바닥을 페이지 경계에 맞춰 놓았기 때문에
   이 경우에만 가능합니다.

   또한 실행 큐와 tid 락을 초기화합니다.

   이 함수를 호출한 뒤에는 페이지 할당자를 초기화한 다음에 thread_create()로 스레드를
   생성해야 합니다.

   이 함수가 끝나기 전까지는 thread_current()를 호출하면 안전하지 않습니다. */
void thread_init(void)
{
    ASSERT(intr_get_level() == INTR_OFF);

    /* Reload the temporal gdt for the kernel
     * This gdt does not include the user context.
     * The kernel will rebuild the gdt with user context, in gdt_init (). */
    struct desc_ptr gdt_ds = {.size = sizeof(gdt) - 1, .address = (uint64_t)gdt};
    lgdt(&gdt_ds);

    /* Init the globla thread context */
    lock_init(&tid_lock);
    list_init(&ready_list);
    list_init(&destruction_req);

    /* Set up a thread structure for the running thread. */
    initial_thread = running_thread();
    init_thread(initial_thread, "main", PRI_DEFAULT);
    initial_thread->status = THREAD_RUNNING;
    initial_thread->tid = allocate_tid();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
/* 인터럽트를 활성화하여 선점형 스레드 스케줄링을 시작하고,
   idle 스레드를 생성합니다. */
void thread_start(void)
{
    /* Create the idle thread. */
    struct semaphore idle_started;
    sema_init(&idle_started, 0);
    thread_create("idle", PRI_MIN, idle, &idle_started);

    /* Start preemptive thread scheduling. */
    intr_enable();

    /* Wait for the idle thread to initialize idle_thread. */
    sema_down(&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
/* 타이머 인터럽트 핸들러가 매 타이머 틱마다 호출하는 함수로,
   외부 인터럽트 컨텍스트에서 실행됩니다. */
void thread_tick(void)
{
    struct thread *t = thread_current();

    /* Update statistics. */
    if (t == idle_thread) idle_ticks++;
#ifdef USERPROG
    else if (t->pml4 != NULL)
        user_ticks++;
#endif
    else
        kernel_ticks++;

    /* Enforce preemption. */
    if (++thread_ticks >= TIME_SLICE) intr_yield_on_return();
}

/* Prints thread statistics. */
/* 스레드 통계 정보를 출력합니다. */
void thread_print_stats(void)
{
    printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n", idle_ticks,
           kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
/* NAME이라는 이름과 초기 PRIORITY를 가진 커널 스레드를 생성하여,
   AUX를 인자로 FUNCTION을 실행하게 하고 준비 큐에 추가합니다.
   새 스레드의 식별자를 반환하며, 실패하면 TID_ERROR를 반환합니다.

   thread_start()가 호출된 이후라면 새 스레드가 thread_create()가 반환되기 전에
   스케줄될 수도 있고, 심지어 반환되기 전에 종료될 수도 있습니다.
   반대로 기존 스레드는 새 스레드가 스케줄되기 전까지 얼마나 오래든 실행될 수 있습니다.
   실행 순서를 보장하려면 세마포어나 다른 동기화 수단을 사용하세요.

   제공된 코드는 새 스레드의 `priority` 멤버를 PRIORITY로 설정하지만,
   실제 우선순위 스케줄링은 구현되어 있지 않습니다.
   우선순위 스케줄링은 과제 1-3의 목표입니다. */
tid_t thread_create(const char *name, int priority, thread_func *function, void *aux)
{
    struct thread *cur_thread = thread_current();
    struct thread *t;
    tid_t tid;

    ASSERT(function != NULL);

    /* Allocate thread. */
    t = palloc_get_page(PAL_ZERO);
    if (t == NULL) return TID_ERROR;

    /* Initialize thread. */
    init_thread(t, name, priority);
    tid = t->tid = allocate_tid();

    /* Call the kernel_thread if it scheduled.
     * Note) rdi is 1st argument, and rsi is 2nd argument. */
    t->tf.rip = (uintptr_t)kernel_thread;
    t->tf.R.rdi = (uint64_t)function;
    t->tf.R.rsi = (uint64_t)aux;
    t->tf.ds = SEL_KDSEG;
    t->tf.es = SEL_KDSEG;
    t->tf.ss = SEL_KDSEG;
    t->tf.cs = SEL_KCSEG;
    t->tf.eflags = FLAG_IF;

    /* Add to run queue. */
    thread_unblock(t);
    /* 현재 스레드와 새로 생성된 스레드의 우선순위를 비교하여 양보가 필요하면 실행되도록  */
    list_push_back(&cur_thread->children, &t->child_elem);

    if (t->priority > cur_thread->priority)
    {
        thread_yield();  // 인터럽트 컨텍스트에서 스레드 양보가 필요하면 실행되도록
    }
    return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
/* 현재 스레드를 잠재웁니다. thread_unblock()으로 깨우기 전까지는 스케줄되지 않습니다.

   이 함수는 반드시 인터럽트를 끈 상태에서 호출해야 하며,
   보통은 synch.h에 있는 동기화 기본 요소를 사용하는 것이 더 좋습니다. */
void thread_block(void)
{
    ASSERT(!intr_context());
    ASSERT(intr_get_level() == INTR_OFF);
    thread_current()->status = THREAD_BLOCKED;
    schedule();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
/* 블록된 스레드 T를 실행 준비 상태로 전환합니다.
   T가 블록된 상태가 아니라면 오류입니다. (실행 중인 스레드를 준비 상태로 만들려면
   thread_yield()를 사용하세요.)

   이 함수는 현재 실행 중인 스레드를 선점하지 않습니다.
   호출자가 인터럽트를 스스로 끈 상태였다면, 스레드를 원자적으로 깨우고 다른 데이터도
   업데이트할 수 있다고 기대할 수 있기 때문에 중요합니다. */
void thread_unblock(struct thread *t)
{
    enum intr_level old_level;

    ASSERT(is_thread(t));

    old_level = intr_disable();
    ASSERT(t->status == THREAD_BLOCKED);
    t->status = THREAD_READY;
    list_insert_ordered(&ready_list, &t->elem, compare_priority, NULL);
    intr_set_level(old_level);
}

/* Reorders a thread in the ready list when its priority changes.
   Must be called with interrupts disabled. */
void thread_reorder_ready_list(struct thread *t)
{
    ASSERT(is_thread(t));
    ASSERT(t->status == THREAD_READY);
    ASSERT(intr_get_level() == INTR_OFF);

    list_remove(&t->elem);
    list_insert_ordered(&ready_list, &t->elem, compare_priority, NULL);
}

/* Returns the name of the running thread. */
const char *thread_name(void)
{
    return thread_current()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
/* 실행 중인 스레드를 반환합니다.
   running_thread()에 몇 가지 안정성 검사를 추가한 형태입니다.
   자세한 내용은 thread.h 상단의 큰 주석을 참고하세요. */
struct thread *thread_current(void)
{
    struct thread *t = running_thread();

    /* Make sure T is really a thread.
       If either of these assertions fire, then your thread may
       have overflowed its stack.  Each thread has less than 4 kB
       of stack, so a few big automatic arrays or moderate
       recursion can cause stack overflow. */
    /* T가 실제 스레드인지 확인합니다.
       어느 하나라도 assertion이 실패한다면 스레드의 스택이 넘쳤을 가능성이 있습니다.
       각 스레드의 스택은 4KB보다 작으므로 큰 자동 변수 배열이나 적당한 깊이의 재귀 호출만으로도
       스택 오버플로우가 발생할 수 있습니다. */
    ASSERT(is_thread(t));
    ASSERT(t->status == THREAD_RUNNING);

    return t;
}

/* Returns the running thread's tid. */
tid_t thread_tid(void)
{
    return thread_current()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
/* 현재 스레드를 스케줄에서 제거하고 파괴합니다.
   호출자에게는 절대 돌아오지 않습니다. */
void thread_exit(void)
{
    ASSERT(!intr_context());

#ifdef USERPROG
    process_exit();
#endif

    /* Just set our status to dying and schedule another process.
       We will be destroyed during the call to schedule_tail(). */
    /* 단순히 상태를 THREAD_DYING으로 설정하고 다른 프로세스를 스케줄합니다.
       실제 파괴는 schedule_tail() 호출 중에 이루어집니다. */
    intr_disable();
    do_schedule(THREAD_DYING);
    NOT_REACHED();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
/* CPU를 양보합니다. 현재 스레드는 잠들지 않으며, 스케줄러의 판단에 따라
   즉시 다시 스케줄될 수도 있습니다. */
void thread_yield(void)
{
    struct thread *curr = thread_current();
    enum intr_level old_level;

    ASSERT(!intr_context());

    old_level = intr_disable();
    if (curr != idle_thread) list_insert_ordered(&ready_list, &curr->elem, compare_priority, NULL);
    do_schedule(THREAD_READY);
    intr_set_level(old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void thread_set_priority(int new_priority)
{
    struct thread *cur_thread = thread_current();

    if (cur_thread->original_priority == cur_thread->priority)
    {  // 도네이션이 없는 상황
        if (new_priority < cur_thread->priority)
        {  // 낮아지면 thread_yield 실행
            cur_thread->original_priority = new_priority;
            cur_thread->priority = new_priority;
            thread_yield();  // 인터럽트 컨텍스트에서 스레드 양보가 필요하면 실행되도록
        }
        else
        {
            cur_thread->original_priority = new_priority;
            cur_thread->priority = new_priority;
        }
    }
    else
    {  // 도네이션이 실행된 상황
        if (cur_thread->priority < new_priority)
        {  // 변경 값이 도네이션 받은 값보다 큼
            /*두 값다 새로운 값으로 변경*/
            cur_thread->priority = new_priority;
            cur_thread->original_priority = new_priority;
        }
        /*도네이션 값보다는 작으면 오리지날만 변경*/
        else
            cur_thread->original_priority = new_priority;
    }
}

/* Returns the current thread's priority. */
int thread_get_priority(void)
{
    return thread_current()->priority;
}

/* Sets the current thread's nice value to NICE. */
void thread_set_nice(int nice UNUSED)
{
    /* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int thread_get_nice(void)
{
    /* TODO: Your implementation goes here */
    return 0;
}

/* Returns 100 times the system load average. */
int thread_get_load_avg(void)
{
    /* TODO: Your implementation goes here */
    return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int thread_get_recent_cpu(void)
{
    /* TODO: Your implementation goes here */
    return 0;
}

/* 스레드의 priority를 계산하는 헬퍼 함수 */
void mlfqs_calculate_priority(struct thread *t) {}

/* 스레드의 recent_cpu를 계산하는 헬퍼 함수 */
void mlfqs_calculate_recent_cpu(struct thread *t) {}

/* 시스템 load_avg를 계산하는 헬퍼 함수 */
void mlfqs_calculate_load_avg(void) {}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
/* Idle 스레드입니다. 실행할 준비가 된 스레드가 없을 때 실행됩니다.

   idle 스레드는 처음에 thread_start()가 준비 리스트에 넣습니다.
   최초의 한 번 스케줄되며, 그 시점에 idle_thread를 초기화하고 전달받은 세마포어에 up을 호출한 뒤
   곧바로 블록됩니다. 이후로는 idle 스레드가 준비 리스트에 나타나는 일은 없습니다.
   준비 리스트가 비어 있는 특별한 경우에만 next_thread_to_run()이 idle 스레드를 반환합니다. */
static void idle(void *idle_started_ UNUSED)
{
    struct semaphore *idle_started = idle_started_;

    idle_thread = thread_current();
    sema_up(idle_started);

    for (;;)
    {
        /* Let someone else run. */
        intr_disable();
        thread_block();

        /* Re-enable interrupts and wait for the next one.

           The `sti' instruction disables interrupts until the
           completion of the next instruction, so these two
           instructions are executed atomically.  This atomicity is
           important; otherwise, an interrupt could be handled
           between re-enabling interrupts and waiting for the next
           one to occur, wasting as much as one clock tick worth of
           time.

           See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
           7.11.1 "HLT Instruction". */
        __asm__ volatile("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
/* 커널 스레드의 기반이 되는 함수입니다. */
static void kernel_thread(thread_func *function, void *aux)
{
    ASSERT(function != NULL);

    intr_enable(); /* The scheduler runs with interrupts off. */
    function(aux); /* Execute the thread function. */
    thread_exit(); /* If function() returns, kill the thread. */
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
/* T를 NAME이라는 이름의 블록된 스레드로 기본 초기화합니다. */
static void init_thread(struct thread *t, const char *name, int priority)
{
    ASSERT(t != NULL);
    ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
    ASSERT(name != NULL);

    memset(t, 0, sizeof *t);
    t->status = THREAD_BLOCKED;
    strlcpy(t->name, name, sizeof t->name);
    t->tf.rsp = (uint64_t)t + PGSIZE - sizeof(void *);
    t->priority = priority;
    t->original_priority = priority;
    t->magic = THREAD_MAGIC;
    t->waiting_lock = NULL;
    list_init(&t->holding_locks);
#ifdef USERPROG
    for (int i = 0; i < MAX_FD; i++) t->fds[i] = NULL;
    t->exec_file = NULL;
    t->exit_status = 0;
    sema_init(&t->wait_sema, 0);  // wait_sema 초기화 (0으로 시작)
    list_init(&t->children);      // children 리스트 초기화
#endif
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
/* 다음에 스케줄할 스레드를 선택하여 반환합니다.
   실행 큐가 비어 있지 않다면 그 안에서 스레드를 반환해야 합니다.
   (실행 중인 스레드가 계속 실행 가능하다면 실행 큐에 존재합니다.)
   실행 큐가 비어 있다면 idle_thread를 반환합니다. */
static struct thread *next_thread_to_run(void)
{
    if (list_empty(&ready_list))
        return idle_thread;
    else
        return list_entry(list_pop_front(&ready_list), struct thread, elem);
}

static bool compare_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
    struct thread *ta = list_entry(a, struct thread, elem);
    struct thread *tb = list_entry(b, struct thread, elem);
    return ta->priority > tb->priority;
}

/* Use iretq to launch the thread */
/* iretq 명령을 사용해 스레드를 실행합니다. */
void do_iret(struct intr_frame *tf)
{
    __asm __volatile(
        "movq %0, %%rsp\n"
        "movq 0(%%rsp),%%r15\n"
        "movq 8(%%rsp),%%r14\n"
        "movq 16(%%rsp),%%r13\n"
        "movq 24(%%rsp),%%r12\n"
        "movq 32(%%rsp),%%r11\n"
        "movq 40(%%rsp),%%r10\n"
        "movq 48(%%rsp),%%r9\n"
        "movq 56(%%rsp),%%r8\n"
        "movq 64(%%rsp),%%rsi\n"
        "movq 72(%%rsp),%%rdi\n"
        "movq 80(%%rsp),%%rbp\n"
        "movq 88(%%rsp),%%rdx\n"
        "movq 96(%%rsp),%%rcx\n"
        "movq 104(%%rsp),%%rbx\n"
        "movq 112(%%rsp),%%rax\n"
        "addq $120,%%rsp\n"
        "movw 8(%%rsp),%%ds\n"
        "movw (%%rsp),%%es\n"
        "addq $32, %%rsp\n"
        "iretq"
        :
        : "g"((uint64_t)tf)
        : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
/* 새 스레드의 페이지 테이블을 활성화하여 스레드를 전환하고,
   이전 스레드가 종료 상태라면 파괴합니다.

   이 함수가 호출될 때는 방금 PREV 스레드에서 전환된 상태이며,
   새 스레드가 이미 실행 중이고 인터럽트는 아직 비활성화되어 있습니다.

   스레드 전환이 완료되기 전까지 printf()를 호출하는 것은 안전하지 않습니다.
   실제로는 함수 끝부분에서만 printf()를 호출해야 한다는 의미입니다. */
static void thread_launch(struct thread *th)
{
    uint64_t tf_cur = (uint64_t)&running_thread()->tf;
    uint64_t tf = (uint64_t)&th->tf;
    ASSERT(intr_get_level() == INTR_OFF);

    /* The main switching logic.
     * We first restore the whole execution context into the intr_frame
     * and then switching to the next thread by calling do_iret.
     * Note that, we SHOULD NOT use any stack from here
     * until switching is done. */
    __asm __volatile(
        /* Store registers that will be used. */
        "push %%rax\n"
        "push %%rbx\n"
        "push %%rcx\n"
        /* Fetch input once */
        "movq %0, %%rax\n"
        "movq %1, %%rcx\n"
        "movq %%r15, 0(%%rax)\n"
        "movq %%r14, 8(%%rax)\n"
        "movq %%r13, 16(%%rax)\n"
        "movq %%r12, 24(%%rax)\n"
        "movq %%r11, 32(%%rax)\n"
        "movq %%r10, 40(%%rax)\n"
        "movq %%r9, 48(%%rax)\n"
        "movq %%r8, 56(%%rax)\n"
        "movq %%rsi, 64(%%rax)\n"
        "movq %%rdi, 72(%%rax)\n"
        "movq %%rbp, 80(%%rax)\n"
        "movq %%rdx, 88(%%rax)\n"
        "pop %%rbx\n"  // Saved rcx
        "movq %%rbx, 96(%%rax)\n"
        "pop %%rbx\n"  // Saved rbx
        "movq %%rbx, 104(%%rax)\n"
        "pop %%rbx\n"  // Saved rax
        "movq %%rbx, 112(%%rax)\n"
        "addq $120, %%rax\n"
        "movw %%es, (%%rax)\n"
        "movw %%ds, 8(%%rax)\n"
        "addq $32, %%rax\n"
        "call __next\n"  // read the current rip.
        "__next:\n"
        "pop %%rbx\n"
        "addq $(out_iret -  __next), %%rbx\n"
        "movq %%rbx, 0(%%rax)\n"  // rip
        "movw %%cs, 8(%%rax)\n"   // cs
        "pushfq\n"
        "popq %%rbx\n"
        "mov %%rbx, 16(%%rax)\n"  // eflags
        "mov %%rsp, 24(%%rax)\n"  // rsp
        "movw %%ss, 32(%%rax)\n"
        "mov %%rcx, %%rdi\n"
        "call do_iret\n"
        "out_iret:\n"
        :
        : "g"(tf_cur), "g"(tf)
        : "memory");
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
/* 새로운 프로세스를 스케줄합니다. 진입 시 반드시 인터럽트가 꺼져 있어야 합니다.
 * 현재 스레드의 상태를 status로 바꾼 뒤 실행할 다른 스레드를 찾아 전환합니다.
 * schedule() 안에서는 printf()를 호출하면 안전하지 않습니다. */
static void do_schedule(int status)
{
    ASSERT(intr_get_level() == INTR_OFF);
    ASSERT(thread_current()->status == THREAD_RUNNING);
    while (!list_empty(&destruction_req))
    {
        struct thread *victim = list_entry(list_pop_front(&destruction_req), struct thread, elem);
        palloc_free_page(victim);
    }
    thread_current()->status = status;
    schedule();
}

static void schedule(void)
{
    struct thread *curr = running_thread();
    struct thread *next = next_thread_to_run();

    ASSERT(intr_get_level() == INTR_OFF);
    ASSERT(curr->status != THREAD_RUNNING);
    ASSERT(is_thread(next));
    /* Mark us as running. */
    next->status = THREAD_RUNNING;

    /* Start new time slice. */
    thread_ticks = 0;

#ifdef USERPROG
    /* Activate the new address space. */
    process_activate(next);
#endif

    if (curr != next)
    {
        /* If the thread we switched from is dying, destroy its struct
           thread. This must happen late so that thread_exit() doesn't
           pull out the rug under itself.
           We just queuing the page free reqeust here because the page is
           currently used by the stack.
           The real destruction logic will be called at the beginning of the
           schedule(). */
        /* 전환된 이전 스레드가 죽어가는 중이라면 해당 struct thread를 파괴합니다.
           thread_exit()가 자기 발등을 찍지 않도록 늦게 수행해야 합니다.
           현재 스택이 해당 페이지를 사용 중이므로 여기서는 페이지 해제 요청만 큐에 넣습니다.
           실제 파괴 로직은 다음 schedule() 시작 부분에서 호출됩니다. */
        if (curr && curr->status == THREAD_DYING && curr != initial_thread)
        {
            ASSERT(curr != next);
            list_push_back(&destruction_req, &curr->elem);
        }

        /* Before switching the thread, we first save the information
         * of current running. */
        thread_launch(next);
    }
}

/* Returns a tid to use for a new thread. */
/* 새 스레드에 사용할 tid를 반환합니다. */
static tid_t allocate_tid(void)
{
    static tid_t next_tid = 1;
    tid_t tid;

    lock_acquire(&tid_lock);
    tid = next_tid++;
    lock_release(&tid_lock);

    return tid;
}
