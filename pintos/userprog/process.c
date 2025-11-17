#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "threads/synch.h"  // 세마포어 사용을 위해 추가
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

// 임시 세마포어 (프로세스 대기용)
static struct semaphore temporary;

// 정적 함수 선언들
static void process_cleanup(void);                                // 프로세스 정리 함수
static bool load(const char* file_name, struct intr_frame* if_);  // ELF 파일 로드 함수
static void initd(void* f_name);  // 첫 번째 사용자 프로세스 실행 함수
static void __do_fork(void*);     // fork 실행 함수

/* General process initializer for initd and other process. */
/* initd 및 다른 프로세스를 위한 일반 프로세스 초기화 함수 */
static void process_init(void)
{
    // 현재는 빈 함수로, 추후 프로세스 초기화 로직이 추가될 예정
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
/* FILE_NAME에서 로드된 "initd"라는 첫 번째 사용자 프로그램을 시작합니다.
 * 새 스레드는 process_create_initd()가 반환되기 전에 스케줄링될 수 있으며(심지어 종료될 수도 있음)
 * initd의 스레드 ID를 반환하거나, 스레드를 생성할 수 없으면 TID_ERROR를 반환합니다.
 * 주의: 이 함수는 한 번만 호출되어야 합니다. */
tid_t process_create_initd(const char* file_name)
{
    /* 임시 세마포어 초기화 (process_wait보다 먼저 호출됨) */
    static bool sema_initialized = false;
    if (!sema_initialized)
    {
        sema_init(&temporary, 0);
        sema_initialized = true;
    }

    char* fn_copy;  // 파일 이름 복사본을 저장할 포인터
    tid_t tid;      // 생성된 스레드의 ID

    /* Make a copy of FILE_NAME.
     * Otherwise there's a race between the caller and load(). */
    /* FILE_NAME의 복사본을 만듭니다.
     * 그렇지 않으면 호출자와 load() 사이에 경쟁 조건이 발생합니다. */
    fn_copy = palloc_get_page(0);         // 페이지 할당자로부터 페이지 하나 할당
    if (fn_copy == NULL)                  // 할당 실패 시
        return TID_ERROR;                 // 에러 반환
    strlcpy(fn_copy, file_name, PGSIZE);  // 파일 이름을 할당된 페이지에 복사

    /* Create a new thread to execute FILE_NAME. */
    /* FILE_NAME을 실행할 새 스레드를 생성합니다. */
    char thread_name[16];
    strlcpy(thread_name, fn_copy, sizeof thread_name);
    char* space = strchr(thread_name, ' ');
    if (space != NULL) *space = '\0';

    tid = thread_create(thread_name, PRI_DEFAULT, initd,
                        fn_copy);   // 새 스레드 생성
    if (tid == TID_ERROR)           // 스레드 생성 실패 시
        palloc_free_page(fn_copy);  // 할당했던 페이지 해제
    return tid;                     // 생성된 스레드 ID 반환
}

/* A thread function that launches first user process. */
/* 첫 번째 사용자 프로세스를 실행하는 스레드 함수 */
static void initd(void* f_name)
{
#ifdef VM
    supplemental_page_table_init(&thread_current()->spt);  // VM 모드일 때 보조 페이지 테이블 초기화
#endif

    process_init();                       // 프로세스 초기화
    if (process_exec(f_name) < 0)         // 프로세스 실행 시도, 실패 시
        PANIC("Fail to launch initd\n");  // 패닉 발생
    NOT_REACHED();                        // 이 지점에 도달하면 안 됨
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
/* 현재 프로세스를 `name`으로 복제합니다. 새 프로세스의 스레드 ID를 반환하거나,
 * 스레드를 생성할 수 없으면 TID_ERROR를 반환합니다. */
tid_t process_fork(const char* name, struct intr_frame* if_ UNUSED)
{
    /* Clone current thread to new thread.*/
    /* 현재 스레드를 새 스레드로 복제합니다. */
    return thread_create(
        name,  // 새 스레드 이름
        PRI_DEFAULT, __do_fork,
        thread_current());  // 기본 우선순위로 __do_fork 함수 실행, 현재 스레드를 인자로 전달
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
/* 이 함수를 pml4_for_each에 전달하여 부모의 주소 공간을 복제합니다. 이것은 프로젝트 2 전용입니다.
 */
static bool duplicate_pte(uint64_t* pte, void* va, void* aux)
{
    struct thread* current = thread_current();    // 현재 스레드(자식) 가져오기
    struct thread* parent = (struct thread*)aux;  // aux에서 부모 스레드 가져오기
    void* parent_page;                            // 부모의 페이지 주소
    void* newpage;                                // 새로 할당할 페이지 주소
    bool writable;                                // 페이지 쓰기 가능 여부

    /* 1. TODO: If the parent_page is kernel page, then return immediately. */
    /* 1. TODO: parent_page가 커널 페이지이면 즉시 반환합니다. */

    /* 2. Resolve VA from the parent's page map level 4. */
    /* 2. 부모의 페이지 맵 레벨 4에서 VA를 해석합니다. */
    parent_page = pml4_get_page(
        parent->pml4, va);  // 부모의 페이지 테이블에서 가상 주소에 해당하는 물리 페이지 가져오기

    /* 3. TODO: Allocate new PAL_USER page for the child and set result to
     *    TODO: NEWPAGE. */
    /* 3. TODO: 자식을 위한 새로운 PAL_USER 페이지를 할당하고 결과를 NEWPAGE에 설정합니다. */

    /* 4. TODO: Duplicate parent's page to the new page and
     *    TODO: check whether parent's page is writable or not (set WRITABLE
     *    TODO: according to the result). */
    /* 4. TODO: 부모의 페이지를 새 페이지로 복제하고,
     *    TODO: 부모의 페이지가 쓰기 가능한지 확인합니다 (결과에 따라 WRITABLE 설정). */

    /* 5. Add new page to child's page table at address VA with WRITABLE
     *    permission. */
    /* 5. WRITABLE 권한으로 주소 VA에 자식의 페이지 테이블에 새 페이지를 추가합니다. */
    if (!pml4_set_page(current->pml4, va, newpage, writable))
    {  // 자식의 페이지 테이블에 페이지 매핑 시도
        /* 6. TODO: if fail to insert page, do error handling. */
        /* 6. TODO: 페이지 삽입에 실패하면 에러 처리를 수행합니다. */
    }
    return true;  // 성공 반환
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
/* 부모의 실행 컨텍스트를 복사하는 스레드 함수입니다.
 * 힌트) parent->tf는 프로세스의 사용자 영역 컨텍스트를 보유하지 않습니다.
 *       즉, process_fork의 두 번째 인자를 이 함수에 전달해야 합니다. */
static void __do_fork(void* aux)
{
    struct intr_frame if_;                        // 인터럽트 프레임 구조체 (자식용)
    struct thread* parent = (struct thread*)aux;  // aux에서 부모 스레드 가져오기
    struct thread* current = thread_current();    // 현재 스레드(자식) 가져오기
    /* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
    /* TODO: 어떤 식으로든 parent_if를 전달합니다. (즉, process_fork()의 if_) */
    struct intr_frame* parent_if;  // 부모의 인터럽트 프레임 포인터 (초기화 필요)
    bool succ = true;              // 성공 여부 플래그

    /* 1. Read the cpu context to local stack. */
    /* 1. CPU 컨텍스트를 로컬 스택으로 읽어옵니다. */
    memcpy(&if_, parent_if,
           sizeof(struct intr_frame));  // 부모의 인터럽트 프레임을 자식의 인터럽트 프레임으로 복사

    /* 2. Duplicate PT */
    /* 2. 페이지 테이블 복제 */
    current->pml4 = pml4_create();  // 자식을 위한 새로운 페이지 테이블 생성
    if (current->pml4 == NULL)      // 생성 실패 시
        goto error;                 // 에러 처리로 이동
    process_activate(current);      // 자식 프로세스의 페이지 테이블 활성화
#ifdef VM
    supplemental_page_table_init(&current->spt);  // VM 모드일 때 자식의 보조 페이지 테이블 초기화
    if (!supplemental_page_table_copy(&current->spt,
                                      &parent->spt))  // 부모의 보조 페이지 테이블을 자식으로 복사
        goto error;                                   // 복사 실패 시 에러 처리로 이동
#else
    if (!pml4_for_each(parent->pml4, duplicate_pte,
                       parent))  // 부모의 모든 페이지 테이블 엔트리를 순회하며 복제
        goto error;              // 복제 실패 시 에러 처리로 이동
#endif

    /* TODO: Your code goes here.
     * TODO: Hint) To duplicate the file object, use `file_duplicate`
     * TODO:       in include/filesys/file.h. Note that parent should not return
     * TODO:       from the fork() until this function successfully duplicates
     * TODO:       the resources of parent.*/
    /* TODO: 여기에 코드를 작성하세요.
     * TODO: 힌트) 파일 객체를 복제하려면 include/filesys/file.h의 `file_duplicate`를 사용하세요.
     * TODO:       부모는 이 함수가 부모의 리소스를 성공적으로 복제할 때까지 fork()에서 반환하지
     * 않아야 합니다. */

    process_init();  // 프로세스 초기화

    /* Finally, switch to the newly created process. */
    /* 마지막으로, 새로 생성된 프로세스로 전환합니다. */
    if (succ)           // 성공한 경우
        do_iret(&if_);  // 인터럽트 복귀를 통해 새 프로세스 실행 시작
error:
    thread_exit();  // 스레드 종료
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
/* 현재 실행 컨텍스트를 f_name으로 전환합니다.
 * 실패 시 -1을 반환합니다. */
int process_exec(void* f_name)
{
    char* file_name = f_name;  // 파일 이름 포인터
    bool success;              // 성공 여부

    /* We cannot use the intr_frame in the thread structure.
     * This is because when current thread rescheduled,
     * it stores the execution information to the member. */
    /* 스레드 구조체의 intr_frame을 사용할 수 없습니다.
     * 이는 현재 스레드가 재스케줄링될 때 실행 정보를 멤버에 저장하기 때문입니다. */
    struct intr_frame _if;  // 로컬 인터럽트 프레임 생성
    _if.ds = _if.es = _if.ss =
        SEL_UDSEG;  // 데이터, 확장, 스택 세그먼트를 사용자 데이터 세그먼트로 설정
    _if.cs = SEL_UCSEG;  // 코드 세그먼트를 사용자 코드 세그먼트로 설정
    _if.eflags = FLAG_IF | FLAG_MBS;  // 인터럽트 플래그와 멀티부트 플래그 설정

    /* We first kill the current context */
    /* 먼저 현재 컨텍스트를 종료합니다 */
    process_cleanup();  // 현재 프로세스의 리소스 정리

    /* And then load the binary */
    /* 그 다음 바이너리를 로드합니다 */
    success = load(file_name, &_if);  // ELF 파일 로드 시도

    /* If load failed, quit. */
    /* 로드에 실패하면 종료합니다. */
    palloc_free_page(file_name);  // 파일 이름이 저장된 페이지 해제
    if (!success)                 // 로드 실패 시
        return -1;                // -1 반환

    /* Start switched process. */
    /* 전환된 프로세스를 시작합니다. */
    do_iret(&_if);  // 인터럽트 복귀를 통해 새 프로세스 실행 시작
    NOT_REACHED();  // 이 지점에 도달하면 안 됨
}

/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
/* 스레드 TID가 종료될 때까지 대기하고 종료 상태를 반환합니다.
 * 커널에 의해 종료된 경우(즉, 예외로 인해 종료된 경우) -1을 반환합니다.
 * TID가 유효하지 않거나 호출 프로세스의 자식이 아니거나,
 * 주어진 TID에 대해 process_wait()가 이미 성공적으로 호출된 경우,
 * 대기하지 않고 즉시 -1을 반환합니다.
 *
 * 이 함수는 문제 2-2에서 구현될 예정입니다. 현재는 아무것도 하지 않습니다. */
int process_wait(tid_t child_tid UNUSED)
{
    /* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
     * XXX:       to add infinite loop here before
     * XXX:       implementing the process_wait. */
    /* XXX: 힌트) process_wait(initd)를 호출하면 pintos가 종료되므로,
     * XXX:       process_wait를 구현하기 전에 여기에 무한 루프를 추가하는 것을 권장합니다. */
    struct thread* current = thread_current();
    struct thread* child = NULL;
    struct list_elem* e;

    // children 리스트에서 해당 자식 찾기
    for (e = list_begin(&current->children); e != list_end(&current->children); e = list_next(e))
    {
        struct thread* t = list_entry(e, struct thread, child_elem);
        if (t->tid == child_tid)
        {
            child = t;
            break;
        }
    }

    // 자식을 찾지 못했거나 자신의 자식이 아니면 -1 반환
    if (child == NULL) return -1;

    // 이미 wait된 자식이면 -1 반환
    if (child->status == THREAD_BLOCKED) return -1;

    // 자식이 종료될 때까지 대기
    sema_down(&child->wait_sema);

    // 자식의 종료 상태 반환
    int exit_status = child->exit_status;
    list_remove(&child->child_elem);  // children 리스트에서 제거
    return exit_status;
}

/* Exit the process. This function is called by thread_exit (). */
/* 프로세스를 종료합니다. 이 함수는 thread_exit()에 의해 호출됩니다. */
void process_exit(void)
{
    /* TODO: Your code goes here.
     * TODO: Implement process termination message (see
     * TODO: project2/process_termination.html).
     * TODO: We recommend you to implement process resource cleanup here. */
    /* TODO: 여기에 코드를 작성하세요.
     * TODO: 프로세스 종료 메시지를 구현하세요 (project2/process_termination.html 참조).
     * TODO: 여기에 프로세스 리소스 정리를 구현하는 것을 권장합니다. */

    /* 대기 중인 부모 프로세스 깨우기 */

    struct thread* t = thread_current();

    // 파일 디스크립터 정리
    // 모든 열린 파일 닫기
    for (int i = 2; i < MAX_FD; i++)
    {
        if (t->fds[i] != NULL)
        {
            file_close(t->fds[i]);
            t->fds[i] = NULL;
        }
    }

    // 실행 파일에 대한 쓰기 허용
    if (t->exec_file != NULL)
    {
        file_allow_write(t->exec_file);
        file_close(t->exec_file);
        t->exec_file = NULL;
    }

    // 종료 상태 출력
    // sys_exit()을 통해 종료된 경우 이미 출력되었지만,
    // 예외로 종료된 경우(잘못된 메모리 접근 등)를 위해 여기서도 출력
    // exit_status가 0이면 sys_exit(0)을 통해 정상 종료된 것이므로 이미 출력됨
    // 하지만 예외로 종료된 경우 exit_status가 -1이거나 다른 값일 수 있음

    // 대기 중인 부모 프로세스 깨우기
    sema_up(&t->wait_sema);

    process_cleanup();  // 프로세스 리소스 정리
}

/* Free the current process's resources. */
/* 현재 프로세스의 리소스를 해제합니다. */
static void process_cleanup(void)
{
    struct thread* curr = thread_current();  // 현재 스레드 가져오기

#ifdef VM
    supplemental_page_table_kill(&curr->spt);  // VM 모드일 때 보조 페이지 테이블 정리
#endif

    uint64_t* pml4;  // 페이지 테이블 포인터
    /* Destroy the current process's page directory and switch back
     * to the kernel-only page directory. */
    /* 현재 프로세스의 페이지 디렉토리를 파괴하고 커널 전용 페이지 디렉토리로 다시 전환합니다. */
    pml4 = curr->pml4;  // 현재 프로세스의 페이지 테이블 저장
    if (pml4 != NULL)
    {  // 페이지 테이블이 존재하는 경우
        /* Correct ordering here is crucial.  We must set
         * cur->pagedir to NULL before switching page directories,
         * so that a timer interrupt can't switch back to the
         * process page directory.  We must activate the base page
         * directory before destroying the process's page
         * directory, or our active page directory will be one
         * that's been freed (and cleared). */
        /* 여기서 올바른 순서가 중요합니다. 페이지 디렉토리를 전환하기 전에
         * cur->pagedir를 NULL로 설정해야 하므로, 타이머 인터럽트가
         * 프로세스 페이지 디렉토리로 다시 전환할 수 없습니다. 프로세스의 페이지
         * 디렉토리를 파괴하기 전에 기본 페이지 디렉토리를 활성화해야 하며,
         * 그렇지 않으면 활성 페이지 디렉토리가 해제된(그리고 지워진) 것이 됩니다. */
        curr->pml4 = NULL;    // 현재 스레드의 페이지 테이블 포인터를 NULL로 설정
        pml4_activate(NULL);  // 커널 페이지 테이블 활성화
        pml4_destroy(pml4);   // 프로세스의 페이지 테이블 파괴 및 메모리 해제
    }
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
/* 다음 스레드에서 사용자 코드를 실행하기 위해 CPU를 설정합니다.
 * 이 함수는 모든 컨텍스트 스위치에서 호출됩니다. */
void process_activate(struct thread* next)
{
    /* Activate thread's page tables. */
    /* 스레드의 페이지 테이블을 활성화합니다. */
    pml4_activate(next->pml4);  // 다음 스레드의 페이지 테이블 활성화

    /* Set thread's kernel stack for use in processing interrupts. */
    /* 인터럽트 처리에 사용할 스레드의 커널 스택을 설정합니다. */
    tss_update(next);  // TSS(태스크 상태 세그먼트) 업데이트
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */
/* ELF 바이너리를 로드합니다. 다음 정의는 ELF 사양 [ELF1]에서 거의 그대로 가져온 것입니다. */

/* ELF types.  See [ELF1] 1-2. */
/* ELF 타입들. [ELF1] 1-2를 참조하세요. */
#define EI_NIDENT 16  // ELF 식별자 크기

#define PT_NULL 0 /* Ignore. */                   /* 무시. */
#define PT_LOAD 1 /* Loadable segment. */         /* 로드 가능한 세그먼트. */
#define PT_DYNAMIC 2 /* Dynamic linking info. */  /* 동적 링킹 정보. */
#define PT_INTERP 3 /* Name of dynamic loader. */ /* 동적 로더 이름. */
#define PT_NOTE 4 /* Auxiliary info. */           /* 보조 정보. */
#define PT_SHLIB 5 /* Reserved. */                /* 예약됨. */
#define PT_PHDR 6 /* Program header table. */     /* 프로그램 헤더 테이블. */
#define PT_STACK 0x6474e551 /* Stack segment. */  /* 스택 세그먼트. */

#define PF_X 1 /* Executable. */ /* 실행 가능. */
#define PF_W 2 /* Writable. */   /* 쓰기 가능. */
#define PF_R 4 /* Readable. */   /* 읽기 가능. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
/* 실행 파일 헤더. [ELF1] 1-4부터 1-8을 참조하세요.
 * 이것은 ELF 바이너리의 맨 처음에 나타납니다. */
struct ELF64_hdr
{
    unsigned char e_ident[EI_NIDENT];  // ELF 식별자 (매직 넘버 등)
    uint16_t e_type;                   // 파일 타입
    uint16_t e_machine;                // 머신 아키텍처
    uint32_t e_version;                // ELF 버전
    uint64_t e_entry;                  // 진입점 주소
    uint64_t e_phoff;                  // 프로그램 헤더 테이블 오프셋
    uint64_t e_shoff;                  // 섹션 헤더 테이블 오프셋
    uint32_t e_flags;                  // 프로세서별 플래그
    uint16_t e_ehsize;                 // ELF 헤더 크기
    uint16_t e_phentsize;              // 프로그램 헤더 엔트리 크기
    uint16_t e_phnum;                  // 프로그램 헤더 엔트리 개수
    uint16_t e_shentsize;              // 섹션 헤더 엔트리 크기
    uint16_t e_shnum;                  // 섹션 헤더 엔트리 개수
    uint16_t e_shstrndx;               // 섹션 이름 문자열 테이블 인덱스
};

struct ELF64_PHDR
{
    uint32_t p_type;    // 세그먼트 타입
    uint32_t p_flags;   // 세그먼트 플래그 (읽기/쓰기/실행)
    uint64_t p_offset;  // 파일 내 오프셋
    uint64_t p_vaddr;   // 가상 주소
    uint64_t p_paddr;   // 물리 주소 (일반적으로 무시됨)
    uint64_t p_filesz;  // 파일에서의 크기
    uint64_t p_memsz;   // 메모리에서의 크기
    uint64_t p_align;   // 정렬 요구사항
};

/* Abbreviations */
/* 약어 */
#define ELF ELF64_hdr    // ELF 헤더 타입 약어
#define Phdr ELF64_PHDR  // 프로그램 헤더 타입 약어

// 정적 함수 선언들
static bool setup_stack(struct intr_frame* if_);                 // 스택 설정 함수
static bool validate_segment(const struct Phdr*, struct file*);  // 세그먼트 유효성 검사 함수
static bool load_segment(struct file* file, off_t ofs, uint8_t* upage,  // 세그먼트 로드 함수
                         uint32_t read_bytes, uint32_t zero_bytes, bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
/* FILE_NAME에서 ELF 실행 파일을 현재 스레드로 로드합니다.
 * 실행 파일의 진입점을 *RIP에 저장하고
 * 초기 스택 포인터를 *RSP에 저장합니다.
 * 성공하면 true를 반환하고, 그렇지 않으면 false를 반환합니다. */
static bool load(const char* file_name, struct intr_frame* if_)
{
    struct thread* t = thread_current();  // 현재 스레드 가져오기
    struct ELF ehdr;                      // ELF 헤더 구조체
    struct file* file = NULL;             // 파일 포인터
    off_t file_ofs;                       // 파일 오프셋
    bool success = false;                 // 성공 여부 플래그
    int i;                                // 반복문 인덱스

    /* 인자 파싱을 위한 변수들 - 스택 오버플로우 방지를 위해 동적 할당 사용 */
    char* fn_copy = NULL;
    char** argv = NULL;
    uintptr_t* argv_addrs = NULL;

    fn_copy = palloc_get_page(0);
    if (fn_copy == NULL) return false;

    argv = palloc_get_page(0);  // 인자 포인터 배열 (동적 할당)
    if (argv == NULL)
    {
        palloc_free_page(fn_copy);
        return false;
    }

    argv_addrs = palloc_get_page(0);  // 각 문자열의 실제 주소를 저장할 배열
    if (argv_addrs == NULL)
    {
        palloc_free_page(argv);
        palloc_free_page(fn_copy);
        return false;
    }

    int argc = 0;  // 인자 개수
    char *token, *save_ptr;

    /* file_name을 복사 (strtok_r이 원본을 수정하므로) */
    strlcpy(fn_copy, file_name, LOADER_ARGS_LEN);

    /* 공백으로 인자 파싱 */
    for (token = strtok_r(fn_copy, " ", &save_ptr); token != NULL;
         token = strtok_r(NULL, " ", &save_ptr))
    {
        argv[argc++] = token;
    }

    /* 최소한 실행 파일 이름은 있어야 함 */
    if (argc == 0) goto done;

    /* Allocate and activate page directory. */
    /* 페이지 디렉토리를 할당하고 활성화합니다. */
    t->pml4 = pml4_create();             // 새 페이지 테이블 생성
    if (t->pml4 == NULL)                 // 생성 실패 시
        goto done;                       // 종료 처리로 이동
    process_activate(thread_current());  // 현재 스레드의 페이지 테이블 활성화

    /* Open executable file. */
    /* 실행 파일을 엽니다. */
    file = filesys_open(argv[0]);  // 파일 시스템에서 파일 열기
    if (file == NULL)
    {                                                // 파일 열기 실패 시
        printf("load: %s: open failed\n", argv[0]);  // 에러 메시지 출력
        goto done;                                   // 종료 처리로 이동
    }

    file_deny_write(file);  // 현재 연 파일에 대헤 수정 금지
    t->exec_file = file;

    /* Read and verify executable header. */
    /* 실행 파일 헤더를 읽고 검증합니다. */
    if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr  // ELF 헤더 읽기 실패
        || memcmp(ehdr.e_ident, "\177ELF\2\1\1", 7)         // ELF 매직 넘버 확인 실패
        || ehdr.e_type != 2                                 // 실행 파일 타입이 아님
        || ehdr.e_machine != 0x3E                   // amd64  // amd64 아키텍처가 아님
        || ehdr.e_version != 1                      // ELF 버전이 1이 아님
        || ehdr.e_phentsize != sizeof(struct Phdr)  // 프로그램 헤더 크기 불일치
        || ehdr.e_phnum > 1024)
    {  // 프로그램 헤더 개수가 너무 많음
        printf("load: %s: error loading executable\n", file_name);  // 에러 메시지 출력
        goto done;                                                  // 종료 처리로 이동
    }

    /* Read program headers. */
    /* 프로그램 헤더를 읽습니다. */
    file_ofs = ehdr.e_phoff;  // 프로그램 헤더 테이블의 파일 오프셋 가져오기
    for (i = 0; i < ehdr.e_phnum; i++)
    {                      // 모든 프로그램 헤더 엔트리 순회
        struct Phdr phdr;  // 프로그램 헤더 구조체

        if (file_ofs < 0 || file_ofs > file_length(file))  // 오프셋이 파일 범위를 벗어남
            goto done;                                     // 종료 처리로 이동
        file_seek(file, file_ofs);  // 파일 포인터를 해당 오프셋으로 이동

        if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)  // 프로그램 헤더 읽기 실패
            goto done;                                           // 종료 처리로 이동
        file_ofs += sizeof phdr;  // 다음 프로그램 헤더로 오프셋 이동
        switch (phdr.p_type)
        {                   // 세그먼트 타입에 따라 분기
            case PT_NULL:   // NULL 타입
            case PT_NOTE:   // NOTE 타입
            case PT_PHDR:   // PHDR 타입
            case PT_STACK:  // STACK 타입
            default:        // 기타 타입
                /* Ignore this segment. */
                /* 이 세그먼트를 무시합니다. */
                break;        // 다음 반복으로
            case PT_DYNAMIC:  // 동적 링킹 정보
            case PT_INTERP:   // 인터프리터 정보
            case PT_SHLIB:    // 공유 라이브러리
                goto done;    // 지원하지 않는 타입이므로 종료
            case PT_LOAD:     // 로드 가능한 세그먼트
                if (validate_segment(&phdr, file))
                {                                                // 세그먼트 유효성 검사
                    bool writable = (phdr.p_flags & PF_W) != 0;  // 쓰기 가능 여부 확인
                    uint64_t file_page =
                        phdr.p_offset & ~PGMASK;  // 파일에서의 페이지 정렬된 오프셋
                    uint64_t mem_page =
                        phdr.p_vaddr & ~PGMASK;  // 메모리에서의 페이지 정렬된 가상 주소
                    uint64_t page_offset = phdr.p_vaddr & PGMASK;  // 페이지 내 오프셋
                    uint32_t read_bytes, zero_bytes;  // 읽을 바이트 수, 0으로 채울 바이트 수
                    if (phdr.p_filesz > 0)
                    {  // 파일 크기가 0보다 큰 경우
                        /* Normal segment.
                         * Read initial part from disk and zero the rest. */
                        /* 일반 세그먼트.
                         * 디스크에서 초기 부분을 읽고 나머지는 0으로 채웁니다. */
                        read_bytes = page_offset + phdr.p_filesz;  // 읽을 바이트 수 계산
                        zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz,
                                               PGSIZE)  // 0으로 채울 바이트 수 계산
                                      - read_bytes);
                    }
                    else
                    {  // 파일 크기가 0인 경우
                        /* Entirely zero.
                         * Don't read anything from disk. */
                        /* 완전히 0으로 채움.
                         * 디스크에서 아무것도 읽지 않습니다. */
                        read_bytes = 0;  // 읽을 바이트 수는 0
                        zero_bytes = ROUND_UP(page_offset + phdr.p_memsz,
                                              PGSIZE);  // 전체를 0으로 채울 바이트 수
                    }
                    if (!load_segment(file, file_page, (void*)mem_page,  // 세그먼트 로드 시도
                                      read_bytes, zero_bytes, writable))
                        goto done;  // 로드 실패 시 종료 처리로 이동
                }
                else            // 세그먼트 유효성 검사 실패
                    goto done;  // 종료 처리로 이동
                break;          // switch 문 종료
        }
    }

    /* Set up stack. */
    /* 스택을 설정합니다. */
    if (!setup_stack(if_))  // 스택 설정 실패 시
        goto done;          // 종료 처리로 이동

    /* Start address. */
    /* 시작 주소. */
    if_->rip = ehdr.e_entry;  // 인터럽트 프레임의 RIP에 ELF 진입점 주소 설정

    /* ========== 인자를 스택에 배치 ========== */
    /*
     * 최종 스택 레이아웃 (주소가 낮아지는 방향):
     *
     * USER_STACK (0x47480000)
     *    |
     *    v (스택 아래로 자람)
     * +----------------------+
     * | "args-single\0"      | ← argv[0]이 가리키는 문자열
     * +----------------------+
     * | "onearg\0"           | ← argv[1]이 가리키는 문자열
     * +----------------------+
     * | padding (8바이트 정렬)| ← 0~7바이트 패딩
     * +----------------------+
     * | 0 (NULL)             | ← argv[argc] = NULL (8바이트)
     * +----------------------+
     * | &"onearg"            | ← argv[1] 포인터 (8바이트)
     * +----------------------+
     * | &"args-single"       | ← argv[0] 포인터 (8바이트) ← RSI가 가리킴
     * +----------------------+
     * | 0 (fake ret addr)    | ← 가짜 리턴 주소 (8바이트) ← RSP가 가리킴
     * +----------------------+
     *
     * 레지스터:
     * RDI = 2 (argc)
     * RSI = argv 배열의 주소 (argv[0]의 주소)
     */

    // 스택 포인터 (현재 USER_STACK을 가리킴)
    uintptr_t rsp = if_->rsp;

    // 1. 각 인자 문자열을 스택에 배치 (역순으로)
    // 왜 역순? argv[0]이 낮은 주소에 있어야 하므로, 나중에 push한 것이 먼저 나옴
    // argv_addrs는 이미 동적 할당됨

    for (i = argc - 1; i >= 0; i--)  // argc-1부터 0까지 역순
    {
        size_t len = strlen(argv[i]) + 1;  // 문자열 길이 + null terminator
        rsp -= len;                        // 스택 포인터를 문자열 크기만큼 내림

        // 사용자 가상 주소에 쓰기 위해 커널 가상 주소를 가져옴
        void* kpage = pml4_get_page(t->pml4, (void*)rsp);
        if (kpage == NULL) goto done;  // 페이지가 없으면 오류
        memcpy(kpage, argv[i], len);  // 커널 가상 주소를 통해 문자열을 스택에 복사
        argv_addrs[i] = rsp;          // 이 문자열의 주소를 저장
    }
    // 예: argc=2, argv[0]="args-single", argv[1]="onearg"
    //     먼저 argv[1] "onearg\0"를 스택에 push
    //     그 다음 argv[0] "args-single\0"를 스택에 push

    // 2. 8바이트 정렬 (word-align)
    // x86-64 ABI 요구사항: 스택은 8바이트 경계에 정렬되어야 함
    // 예: rsp가 0x47479ff3이면 → 0x47479ff0으로 내림 (하위 3비트를 0으로)
    rsp = rsp & ~0x7;  // 비트 AND 연산으로 8의 배수로 내림
    // ~0x7 = 0xFFFFFFFFFFFFFFF8 (마지막 3비트가 0)

    // 3. argv[argc] = NULL 배치
    // C 표준: argv 배열은 NULL 포인터로 끝나야 함
    rsp -= sizeof(char*);  // 포인터 크기(8바이트)만큼 스택 내림
    void* kpage = pml4_get_page(t->pml4, (void*)rsp);
    if (kpage == NULL) goto done;
    *(char**)kpage = NULL;  // NULL 포인터 저장

    // 4. argv 배열 (포인터들) 배치 (역순으로)
    // argv[argc-1], argv[argc-2], ..., argv[0] 순서로 push
    for (i = argc - 1; i >= 0; i--)
    {
        rsp -= sizeof(char*);  // 포인터 크기만큼 스택 내림
        kpage = pml4_get_page(t->pml4, (void*)rsp);
        if (kpage == NULL) goto done;
        *(uintptr_t*)kpage = argv_addrs[i];  // 문자열 주소를 스택에 저장
    }
    // 예: 먼저 argv[1]의 주소를 push, 그 다음 argv[0]의 주소를 push
    // 결과: 스택에서 위에서 아래로 argv[0], argv[1], ... 순서로 배치됨

    // 5. argv 배열의 시작 주소 저장
    // RSI 레지스터에 전달할 값 (argv[0]의 주소를 가리키는 포인터)
    uintptr_t argv_addr = rsp;

    // 6. fake return address 배치
    // 함수 호출 규약: 스택에는 리턴 주소가 있어야 함
    // main()은 실제로 return하지 않지만, 구조상 필요
    rsp -= sizeof(void*);  // 포인터 크기만큼 스택 내림
    kpage = pml4_get_page(t->pml4, (void*)rsp);
    if (kpage == NULL) goto done;
    *(void**)kpage = NULL;  // NULL을 리턴 주소로 (실제로는 사용 안 됨)

    // 7. 레지스터 설정
    // x86-64 호출 규약: 첫 6개 인자는 레지스터로 전달
    // 1번째 인자 → RDI, 2번째 인자 → RSI, 3번째 → RDX, ...
    if_->R.rdi = argc;       // 첫 번째 인자: argc (인자 개수)
    if_->R.rsi = argv_addr;  // 두 번째 인자: argv (인자 배열의 주소)
    if_->rsp = rsp;          // 스택 포인터를 최종 위치로 업데이트

    /*
     * 최종 결과:
     * - 사용자 프로그램의 main(argc, argv)가 호출될 때
     * - RDI = 2 (argc)
     * - RSI = argv 배열 주소 (argv[0], argv[1], NULL을 가리키는 포인터 배열)
     * - RSP = 스택의 최하단 (fake return address)
     *
     * 사용자 프로그램은 다음과 같이 인자를 받음:
     * int main(int argc, char *argv[])
     * {
     *     // argc = 2
     *     // argv[0] = "args-single"
     *     // argv[1] = "onearg"
     *     // argv[2] = NULL
     * }
     */

    success = true;  // 성공 플래그 설정
    // 성공한 경우 할당한 메모리 해제
    palloc_free_page(argv_addrs);
    palloc_free_page(argv);
    palloc_free_page(fn_copy);
done:
    /* We arrive here whether the load is successful or not. */
    if (!success)
    {
        // 실패 시 할당한 메모리 해제
        if (argv_addrs != NULL) palloc_free_page(argv_addrs);
        if (argv != NULL) palloc_free_page(argv);
        if (fn_copy != NULL) palloc_free_page(fn_copy);
        if (file != NULL)
        {
            file_close(file);  // ← 실패한 경우에만 닫음!
        }
    }
    // 성공한 경우 파일은 열린 채로 유지!
    return success;
}

/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
/* PHDR가 FILE에서 유효하고 로드 가능한 세그먼트를 설명하는지 확인하고,
 * 그렇다면 true를 반환하고, 그렇지 않으면 false를 반환합니다. */
static bool validate_segment(const struct Phdr* phdr, struct file* file)
{
    /* p_offset and p_vaddr must have the same page offset. */
    /* p_offset과 p_vaddr는 같은 페이지 오프셋을 가져야 합니다. */
    if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))  // 페이지 오프셋이 다름
        return false;                                           // 유효하지 않음

    /* p_offset must point within FILE. */
    /* p_offset은 FILE 내를 가리켜야 합니다. */
    if (phdr->p_offset > (uint64_t)file_length(file))  // 오프셋이 파일 크기를 초과
        return false;                                  // 유효하지 않음

    /* p_memsz must be at least as big as p_filesz. */
    /* p_memsz는 최소한 p_filesz만큼 커야 합니다. */
    if (phdr->p_memsz < phdr->p_filesz)  // 메모리 크기가 파일 크기보다 작음
        return false;                    // 유효하지 않음

    /* The segment must not be empty. */
    /* 세그먼트는 비어있지 않아야 합니다. */
    if (phdr->p_memsz == 0)  // 메모리 크기가 0
        return false;        // 유효하지 않음

    /* The virtual memory region must both start and end within the
       user address space range. */
    /* 가상 메모리 영역은 시작과 끝이 모두 사용자 주소 공간 범위 내에 있어야 합니다. */
    if (!is_user_vaddr((void*)phdr->p_vaddr))  // 시작 주소가 사용자 주소 공간이 아님
        return false;                          // 유효하지 않음
    if (!is_user_vaddr(
            (void*)(phdr->p_vaddr + phdr->p_memsz)))  // 끝 주소가 사용자 주소 공간이 아님
        return false;                                 // 유효하지 않음

    /* The region cannot "wrap around" across the kernel virtual
       address space. */
    /* 영역은 커널 가상 주소 공간을 가로질러 "래핑"될 수 없습니다. */
    if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)  // 오버플로우 발생 (래핑)
        return false;                                   // 유효하지 않음

    /* Disallow mapping page 0.
       Not only is it a bad idea to map page 0, but if we allowed
       it then user code that passed a null pointer to system calls
       could quite likely panic the kernel by way of null pointer
       assertions in memcpy(), etc. */
    /* 페이지 0 매핑을 금지합니다.
       페이지 0을 매핑하는 것은 좋은 생각이 아닐 뿐만 아니라, 이를 허용하면
       시스템 호출에 null 포인터를 전달하는 사용자 코드가 memcpy() 등의
       null 포인터 어설션으로 인해 커널을 패닉시킬 가능성이 높습니다. */
    if (phdr->p_vaddr < PGSIZE)  // 가상 주소가 페이지 크기보다 작음 (페이지 0 포함)
        return false;            // 유효하지 않음

    /* It's okay. */
    /* 괜찮습니다. */
    return true;  // 모든 검증 통과
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */
/* 이 블록의 코드는 프로젝트 2 중에만 사용됩니다.
 * 전체 프로젝트 2에 대한 함수를 구현하려면 #ifndef 매크로 외부에 구현하세요. */

/* load() helpers. */
/* load() 헬퍼 함수들. */
static bool install_page(void* upage, void* kpage, bool writable);  // 페이지 설치 함수 선언

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
/* FILE의 오프셋 OFS에서 시작하는 세그먼트를 주소 UPAGE에 로드합니다.
 * 총 READ_BYTES + ZERO_BYTES 바이트의 가상 메모리가 다음과 같이 초기화됩니다:
 *
 * - UPAGE의 READ_BYTES 바이트는 오프셋 OFS에서 시작하여 FILE에서 읽어야 합니다.
 *
 * - UPAGE + READ_BYTES의 ZERO_BYTES 바이트는 0으로 채워져야 합니다.
 *
 * 이 함수에 의해 초기화된 페이지는 WRITABLE이 true이면 사용자 프로세스가
 * 쓰기 가능해야 하고, 그렇지 않으면 읽기 전용이어야 합니다.
 *
 * 성공하면 true를 반환하고, 메모리 할당 오류나 디스크 읽기 오류가 발생하면 false를 반환합니다. */
static bool load_segment(struct file* file, off_t ofs, uint8_t* upage, uint32_t read_bytes,
                         uint32_t zero_bytes, bool writable)
{
    ASSERT((read_bytes + zero_bytes) % PGSIZE ==
           0);  // 읽기 바이트와 0 바이트의 합이 페이지 크기의 배수인지 확인
    ASSERT(pg_ofs(upage) == 0);  // 가상 주소가 페이지 정렬되어 있는지 확인
    ASSERT(ofs % PGSIZE == 0);   // 파일 오프셋이 페이지 크기의 배수인지 확인

    file_seek(file, ofs);  // 파일 포인터를 오프셋으로 이동
    while (read_bytes > 0 || zero_bytes > 0)
    {  // 읽을 바이트나 0으로 채울 바이트가 남아있는 동안 반복
        /* Do calculate how to fill this page.
         * We will read PAGE_READ_BYTES bytes from FILE
         * and zero the final PAGE_ZERO_BYTES bytes. */
        /* 이 페이지를 채우는 방법을 계산합니다.
         * FILE에서 PAGE_READ_BYTES 바이트를 읽고
         * 마지막 PAGE_ZERO_BYTES 바이트를 0으로 채웁니다. */
        size_t page_read_bytes = read_bytes < PGSIZE
                                     ? read_bytes
                                     : PGSIZE;  // 이 페이지에서 읽을 바이트 수 (최대 PGSIZE)
        size_t page_zero_bytes = PGSIZE - page_read_bytes;  // 이 페이지에서 0으로 채울 바이트 수

        /* Get a page of memory. */
        /* 메모리 페이지를 가져옵니다. */
        uint8_t* kpage = palloc_get_page(PAL_USER);  // 사용자 풀에서 페이지 할당
        if (kpage == NULL)                           // 할당 실패 시
            return false;                            // 실패 반환

        /* Load this page. */
        /* 이 페이지를 로드합니다. */
        if (file_read(file, kpage, page_read_bytes) != (int)page_read_bytes)
        {                             // 파일에서 페이지 읽기 실패
            palloc_free_page(kpage);  // 할당했던 페이지 해제
            return false;             // 실패 반환
        }
        memset(kpage + page_read_bytes, 0, page_zero_bytes);  // 읽은 부분 이후를 0으로 채움

        /* Add the page to the process's address space. */
        /* 프로세스의 주소 공간에 페이지를 추가합니다. */
        if (!install_page(upage, kpage, writable))
        {                             // 페이지를 페이지 테이블에 설치 실패
            printf("fail\n");         // 에러 메시지 출력
            palloc_free_page(kpage);  // 할당했던 페이지 해제
            return false;             // 실패 반환
        }

        /* Advance. */
        /* 진행합니다. */
        read_bytes -= page_read_bytes;  // 남은 읽을 바이트 수 감소
        zero_bytes -= page_zero_bytes;  // 남은 0으로 채울 바이트 수 감소
        upage += PGSIZE;                // 다음 가상 페이지 주소로 이동
    }
    return true;  // 성공 반환
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
/* USER_STACK에 0으로 채워진 페이지를 매핑하여 최소 스택을 생성합니다 */
static bool setup_stack(struct intr_frame* if_)
{
    uint8_t* kpage;        // 커널 페이지 포인터
    bool success = false;  // 성공 여부 플래그

    kpage = palloc_get_page(PAL_USER | PAL_ZERO);  // 0으로 초기화된 사용자 페이지 할당
    if (kpage != NULL)
    {  // 페이지 할당 성공 시
        success = install_page(((uint8_t*)USER_STACK) - PGSIZE, kpage,
                               true);  // 스택 하단에 페이지 설치 (쓰기 가능)
        if (success)                   // 설치 성공 시
            if_->rsp = USER_STACK;  // 인터럽트 프레임의 스택 포인터를 USER_STACK으로 설정
        else                        // 설치 실패 시
            palloc_free_page(kpage);  // 할당했던 페이지 해제
    }
    return success;  // 성공 여부 반환
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
/* 사용자 가상 주소 UPAGE에서 커널 가상 주소 KPAGE로의 매핑을 페이지 테이블에 추가합니다.
 * WRITABLE이 true이면 사용자 프로세스가 페이지를 수정할 수 있습니다;
 * 그렇지 않으면 읽기 전용입니다.
 * UPAGE는 이미 매핑되어 있지 않아야 합니다.
 * KPAGE는 아마도 palloc_get_page()로 사용자 풀에서 얻은 페이지여야 합니다.
 * 성공하면 true를 반환하고, UPAGE가 이미 매핑되어 있거나 메모리 할당이 실패하면 false를 반환합니다.
 */
static bool install_page(void* upage, void* kpage, bool writable)
{
    struct thread* t = thread_current();  // 현재 스레드 가져오기

    /* Verify that there's not already a page at that virtual
     * address, then map our page there. */
    /* 해당 가상 주소에 이미 페이지가 없는지 확인한 다음,
     * 그곳에 페이지를 매핑합니다. */
    return (pml4_get_page(t->pml4, upage) == NULL  // 해당 가상 주소에 페이지가 없고
            && pml4_set_page(t->pml4, upage, kpage, writable));  // 페이지 테이블에 매핑 성공
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */
/* 여기서부터는 프로젝트 3 이후에 사용될 코드입니다.
 * 프로젝트 2만을 위한 함수를 구현하려면 위 블록에 구현하세요. */

static bool lazy_load_segment(struct page* page, void* aux)
{
    /* TODO: Load the segment from the file */
    /* TODO: 파일에서 세그먼트를 로드합니다 */
    /* TODO: This called when the first page fault occurs on address VA. */
    /* TODO: 이것은 주소 VA에서 첫 번째 페이지 폴트가 발생할 때 호출됩니다. */
    /* TODO: VA is available when calling this function. */
    /* TODO: 이 함수를 호출할 때 VA를 사용할 수 있습니다. */
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
/* FILE의 오프셋 OFS에서 시작하는 세그먼트를 주소 UPAGE에 로드합니다.
 * 총 READ_BYTES + ZERO_BYTES 바이트의 가상 메모리가 다음과 같이 초기화됩니다:
 *
 * - UPAGE의 READ_BYTES 바이트는 오프셋 OFS에서 시작하여 FILE에서 읽어야 합니다.
 *
 * - UPAGE + READ_BYTES의 ZERO_BYTES 바이트는 0으로 채워져야 합니다.
 *
 * 이 함수에 의해 초기화된 페이지는 WRITABLE이 true이면 사용자 프로세스가
 * 쓰기 가능해야 하고, 그렇지 않으면 읽기 전용이어야 합니다.
 *
 * 성공하면 true를 반환하고, 메모리 할당 오류나 디스크 읽기 오류가 발생하면 false를 반환합니다. */
static bool load_segment(struct file* file, off_t ofs, uint8_t* upage, uint32_t read_bytes,
                         uint32_t zero_bytes, bool writable)
{
    ASSERT((read_bytes + zero_bytes) % PGSIZE ==
           0);  // 읽기 바이트와 0 바이트의 합이 페이지 크기의 배수인지 확인
    ASSERT(pg_ofs(upage) == 0);  // 가상 주소가 페이지 정렬되어 있는지 확인
    ASSERT(ofs % PGSIZE == 0);   // 파일 오프셋이 페이지 크기의 배수인지 확인

    while (read_bytes > 0 || zero_bytes > 0)
    {  // 읽을 바이트나 0으로 채울 바이트가 남아있는 동안 반복
        /* Do calculate how to fill this page.
         * We will read PAGE_READ_BYTES bytes from FILE
         * and zero the final PAGE_ZERO_BYTES bytes. */
        /* 이 페이지를 채우는 방법을 계산합니다.
         * FILE에서 PAGE_READ_BYTES 바이트를 읽고
         * 마지막 PAGE_ZERO_BYTES 바이트를 0으로 채웁니다. */
        size_t page_read_bytes = read_bytes < PGSIZE
                                     ? read_bytes
                                     : PGSIZE;  // 이 페이지에서 읽을 바이트 수 (최대 PGSIZE)
        size_t page_zero_bytes = PGSIZE - page_read_bytes;  // 이 페이지에서 0으로 채울 바이트 수

        /* TODO: Set up aux to pass information to the lazy_load_segment. */
        /* TODO: lazy_load_segment에 정보를 전달하기 위해 aux를 설정합니다. */
        void* aux = NULL;  // 보조 데이터 포인터 (초기화 필요)
        if (!vm_alloc_page_with_initializer(
                VM_ANON, upage,  // 익명 페이지를 lazy_load_segment로 초기화하여 할당
                writable, lazy_load_segment, aux))
            return false;  // 할당 실패 시 false 반환

        /* Advance. */
        /* 진행합니다. */
        read_bytes -= page_read_bytes;  // 남은 읽을 바이트 수 감소
        zero_bytes -= page_zero_bytes;  // 남은 0으로 채울 바이트 수 감소
        upage += PGSIZE;                // 다음 가상 페이지 주소로 이동
    }
    return true;  // 성공 반환
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
/* USER_STACK에 스택의 페이지를 생성합니다. 성공하면 true를 반환합니다. */
static bool setup_stack(struct intr_frame* if_)
{
    bool success = false;                                           // 성공 여부 플래그
    void* stack_bottom = (void*)(((uint8_t*)USER_STACK) - PGSIZE);  // 스택 하단 주소 계산

    /* TODO: Map the stack on stack_bottom and claim the page immediately.
     * TODO: If success, set the rsp accordingly.
     * TODO: You should mark the page is stack. */
    /* TODO: stack_bottom에 스택을 매핑하고 페이지를 즉시 클레임합니다.
     * TODO: 성공하면 rsp를 그에 따라 설정합니다.
     * TODO: 페이지가 스택임을 표시해야 합니다. */
    /* TODO: Your code goes here */
    /* TODO: 여기에 코드를 작성하세요 */

    return success;  // 성공 여부 반환
}
#endif /* VM */
