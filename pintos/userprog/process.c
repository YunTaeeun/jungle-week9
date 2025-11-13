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
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);  // 프로세스 리소스 정리 함수
static bool load (const char *file_name, struct intr_frame *if_);  // ELF 바이너리 로드 함수
static void initd (void *f_name);  // 첫 번째 사용자 프로세스 실행 함수
static void __do_fork (void *);  // fork 실행 함수

/* General process initializer for initd and other process. */
/* initd 및 다른 프로세스를 위한 일반 프로세스 초기화 함수 */
static void
process_init (void) {
	struct thread *current = thread_current ();  // 현재 스레드 가져오기
	// TODO: 여기에 프로세스 초기화 코드 추가 (fd 테이블 등)
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
/* FILE_NAME에서 로드된 "initd"라는 첫 번째 사용자 영역 프로그램을 시작합니다.
 * 새 스레드는 process_create_initd()가 반환되기 전에 스케줄될 수 있으며
 * (심지어 종료될 수도 있습니다). initd의 스레드 ID를 반환하거나,
 * 스레드를 생성할 수 없는 경우 TID_ERROR를 반환합니다.
 * 주의: 이 함수는 한 번만 호출되어야 합니다. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;  // 파일명 복사본
	tid_t tid;  // 스레드 ID

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	/* FILE_NAME의 복사본을 만듭니다.
	 * 그렇지 않으면 호출자와 load() 사이에 경쟁 조건이 발생합니다. */
	fn_copy = palloc_get_page (0);  // 페이지 할당 (플래그 없음)
	if (fn_copy == NULL)  // 할당 실패 시
		return TID_ERROR;  // 에러 반환
	strlcpy (fn_copy, file_name, PGSIZE);  // 파일명 복사 (최대 PGSIZE 바이트)

	/* Create a new thread to execute FILE_NAME. */
	/* FILE_NAME을 실행할 새 스레드를 생성합니다. */
	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy);  // 스레드 생성
	if (tid == TID_ERROR)  // 생성 실패 시
		palloc_free_page (fn_copy);  // 할당한 페이지 해제
	return tid;  // 스레드 ID 반환
}

/* A thread function that launches first user process. */
/* 첫 번째 사용자 프로세스를 실행하는 스레드 함수 */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);  // VM 모드: 보조 페이지 테이블 초기화
#endif

	process_init ();  // 프로세스 초기화

	if (process_exec (f_name) < 0)  // 프로그램 실행 (실패 시)
		PANIC("Fail to launch initd\n");  // 커널 패닉
	NOT_REACHED ();  // 여기에 도달하면 안 됨 (process_exec이 성공하면 리턴하지 않음)
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
/* 현재 프로세스를 `name`으로 복제합니다. 새 프로세스의 스레드 ID를 반환하거나,
 * 스레드를 생성할 수 없는 경우 TID_ERROR를 반환합니다. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* Clone current thread to new thread.*/
	/* 현재 스레드를 새 스레드로 복제합니다. */
	// TODO: if_를 __do_fork에 전달해야 함 (현재는 UNUSED로 표시됨)
	return thread_create (name,  // 스레드 이름
			PRI_DEFAULT, __do_fork, thread_current ());  // 기본 우선순위, __do_fork 함수, 현재 스레드를 인자로
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
/* pml4_for_each에 이 함수를 전달하여 부모의 주소 공간을 복제합니다.
 * 이것은 프로젝트 2 전용입니다. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();  // 현재 스레드 (자식)
	struct thread *parent = (struct thread *) aux;  // 부모 스레드
	void *parent_page;  // 부모의 물리 페이지 주소
	void *newpage;  // 자식을 위한 새 페이지 주소
	bool writable;  // 쓰기 가능 여부

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	/* 1. TODO: parent_page가 커널 페이지인 경우 즉시 반환합니다. */
	// TODO: is_kernel_vaddr(va)로 확인하고 커널 페이지면 true 반환

	/* 2. Resolve VA from the parent's page map level 4. */
	/* 2. 부모의 페이지 맵 레벨 4에서 VA를 해석합니다. */
	parent_page = pml4_get_page (parent->pml4, va);  // 부모의 페이지 테이블에서 물리 주소 가져오기
	if (parent_page == NULL)  // 페이지가 없으면
		return true;  // 건너뛰기 (정상)

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	/* 3. TODO: 자식을 위한 새로운 PAL_USER 페이지를 할당하고 결과를
	 *    TODO: NEWPAGE에 설정합니다. */
	// TODO: newpage = palloc_get_page(PAL_USER);

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	/* 4. TODO: 부모의 페이지를 새 페이지로 복제하고
	 *    TODO: 부모의 페이지가 쓰기 가능한지 확인합니다 (결과에 따라
	 *    TODO: WRITABLE을 설정). */
	// TODO: memcpy(newpage, parent_page, PGSIZE);
	// TODO: writable = pml4_is_writable(parent->pml4, va);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	/* 5. WRITABLE 권한으로 주소 VA에 자식의 페이지 테이블에 새 페이지 추가 */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {  // 페이지 테이블에 추가 실패 시
		/* 6. TODO: if fail to insert page, do error handling. */
		/* 6. TODO: 페이지 삽입 실패 시 에러 처리를 수행합니다. */
		// TODO: palloc_free_page(newpage);
		// TODO: return false;
	}
	return true;  // 성공
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
/* 부모의 실행 컨텍스트를 복사하는 스레드 함수입니다.
 * 힌트) parent->tf는 프로세스의 사용자 영역 컨텍스트를 가지고 있지 않습니다.
 *       즉, process_fork의 두 번째 인자를 이 함수에 전달해야 합니다. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;  // 자식의 인터럽트 프레임
	struct thread *parent = (struct thread *) aux;  // 부모 스레드
	struct thread *current = thread_current ();  // 현재 스레드 (자식)
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	/* TODO: 어떻게든 parent_if를 전달합니다. (즉, process_fork()의 if_) */
	struct intr_frame *parent_if;  // 부모의 인터럽트 프레임 (TODO: 초기화 필요)
	bool succ = true;  // 성공 여부

	/* 1. Read the cpu context to local stack. */
	/* 1. CPU 컨텍스트를 로컬 스택으로 읽습니다. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));  // 부모의 컨텍스트 복사
	if_.R.rax = 0;  // 자식은 fork()에서 0 반환

	/* 2. Duplicate PT */
	/* 2. 페이지 테이블 복제 */
	current->pml4 = pml4_create();  // 자식의 페이지 테이블 생성
	if (current->pml4 == NULL)  // 생성 실패 시
		goto error;  // 에러 처리로 이동

	process_activate (current);  // 자식의 페이지 테이블 활성화
#ifdef VM
	supplemental_page_table_init (&current->spt);  // VM 모드: 보조 페이지 테이블 초기화
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))  // 부모의 보조 페이지 테이블 복사
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))  // 부모의 모든 페이지를 복제
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/
	/* TODO: 여기에 코드를 작성하세요.
	 * TODO: 힌트) 파일 객체를 복제하려면 include/filesys/file.h의
	 * TODO:       `file_duplicate`를 사용하세요. 부모는 이 함수가
	 * TODO:       부모의 리소스를 성공적으로 복제할 때까지 fork()에서
	 * TODO:       반환되어서는 안 됩니다.*/
	// TODO: 부모의 fd 테이블을 자식에게 복제
	// TODO: 부모가 자식의 복제 완료를 기다리도록 세마포어 사용

	process_init ();  // 프로세스 초기화

	/* Finally, switch to the newly created process. */
	/* 마지막으로 새로 생성된 프로세스로 전환합니다. */
	if (succ)  // 성공 시
		do_iret (&if_);  // 인터럽트 복귀 (자식 프로세스 시작)
error:
	thread_exit ();  // 에러 시 스레드 종료
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
/* 현재 실행 컨텍스트를 f_name으로 전환합니다.
 * 실패 시 -1을 반환합니다. */
int
process_exec (void *f_name) {
	char *file_name = f_name;  // 파일명
	bool success;  // 성공 여부

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	/* 스레드 구조체의 intr_frame을 사용할 수 없습니다.
	 * 이는 현재 스레드가 재스케줄될 때 실행 정보를 멤버에 저장하기 때문입니다. */
	struct intr_frame _if;  // 로컬 인터럽트 프레임
	_if.ds = _if.es = _if.ss = SEL_UDSEG;  // 데이터/추가/스택 세그먼트: 사용자 데이터 세그먼트
	_if.cs = SEL_UCSEG;  // 코드 세그먼트: 사용자 코드 세그먼트
	_if.eflags = FLAG_IF | FLAG_MBS;  // 인터럽트 플래그 활성화

	/* We first kill the current context */
	/* 먼저 현재 컨텍스트를 종료합니다 */
	process_cleanup ();  // 현재 프로세스의 리소스 정리 (페이지 테이블 등)

	/* And then load the binary */
	/* 그 다음 바이너리를 로드합니다 */
	success = load (file_name, &_if);  // ELF 바이너리 로드

	/* If load failed, quit. */
	/* 로드 실패 시 종료합니다. */
	palloc_free_page (file_name);  // 파일명 메모리 해제
	if (!success)  // 로드 실패 시
		return -1;  // -1 반환

	/* Start switched process. */
	/* 전환된 프로세스를 시작합니다. */
	do_iret (&_if);  // 인터럽트 복귀 (새 프로그램 시작)
	NOT_REACHED ();  // 여기에 도달하면 안 됨
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
/* 스레드 TID가 종료될 때까지 기다리고 종료 상태를 반환합니다.
 * 커널에 의해 종료된 경우(즉, 예외로 인해 kill된 경우) -1을 반환합니다.
 * TID가 유효하지 않거나 호출 프로세스의 자식이 아니거나,
 * 주어진 TID에 대해 process_wait()가 이미 성공적으로 호출된 경우,
 * 대기하지 않고 즉시 -1을 반환합니다.
 *
 * 이 함수는 problem 2-2에서 구현될 것입니다. 현재는 아무것도 하지 않습니다. */
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */
	/* XXX: 힌트) process_wait(initd)일 경우 pintos가 종료됩니다.
	 * XXX:       process_wait를 구현하기 전에 여기에 무한 루프를
	 * XXX:       추가하는 것을 권장합니다. */
	// TODO: 자식 프로세스 리스트에서 child_tid 찾기
	// TODO: 이미 wait한 자식인지 확인
	// TODO: 자식이 종료될 때까지 대기 (세마포어)
	// TODO: 자식의 exit_status 반환
	return -1;  // 임시: 항상 -1 반환
}

/* Exit the process. This function is called by thread_exit (). */
/* 프로세스를 종료합니다. 이 함수는 thread_exit()에 의해 호출됩니다. */
void
process_exit (void) {
	struct thread *curr = thread_current ();  // 현재 스레드
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */
	/* TODO: 여기에 코드를 작성하세요.
	 * TODO: 프로세스 종료 메시지를 구현하세요 (project2/process_termination.html 참조).
	 * TODO: 여기에 프로세스 리소스 정리를 구현하는 것을 권장합니다. */
	// TODO: printf("%s: exit(%d)\n", thread_name(), curr->exit_status);
	// TODO: 열려있는 모든 파일 닫기
	// TODO: 실행 파일에 대한 write deny 해제
	// TODO: 부모에게 종료 알림 (세마포어 up)

	process_cleanup ();  // 페이지 테이블 등 리소스 정리
}

/* Free the current process's resources. */
/* 현재 프로세스의 리소스를 해제합니다. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();  // 현재 스레드

#ifdef VM
	supplemental_page_table_kill (&curr->spt);  // VM 모드: 보조 페이지 테이블 정리
#endif

	uint64_t *pml4;  // 페이지 테이블 포인터
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	/* 현재 프로세스의 페이지 디렉토리를 파괴하고 커널 전용 페이지 디렉토리로 전환합니다. */
	pml4 = curr->pml4;  // 현재 프로세스의 페이지 테이블 저장
	if (pml4 != NULL) {  // 페이지 테이블이 존재하면
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
		curr->pml4 = NULL;  // 먼저 NULL로 설정 (타이머 인터럽트 방지)
		pml4_activate (NULL);  // 커널 페이지 테이블 활성화
		pml4_destroy (pml4);  // 프로세스 페이지 테이블 파괴
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
/* 다음 스레드에서 사용자 코드를 실행하기 위해 CPU를 설정합니다.
 * 이 함수는 모든 컨텍스트 스위치에서 호출됩니다. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	/* 스레드의 페이지 테이블을 활성화합니다. */
	pml4_activate (next->pml4);  // 다음 스레드의 페이지 테이블 활성화

	/* Set thread's kernel stack for use in processing interrupts. */
	/* 인터럽트 처리에 사용할 스레드의 커널 스택을 설정합니다. */
	tss_update (next);  // TSS에 다음 스레드의 커널 스택 포인터 업데이트
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */
/* ELF 바이너리를 로드합니다. 다음 정의는 ELF 사양 [ELF1]에서 거의 그대로 가져온 것입니다. */

/* ELF types.  See [ELF1] 1-2. */
/* ELF 타입. [ELF1] 1-2 참조. */
#define EI_NIDENT 16  // ELF 식별자 크기

#define PT_NULL    0            /* Ignore. */  /* 무시 */
#define PT_LOAD    1            /* Loadable segment. */  /* 로드 가능한 세그먼트 */
#define PT_DYNAMIC 2            /* Dynamic linking info. */  /* 동적 링킹 정보 */
#define PT_INTERP  3            /* Name of dynamic loader. */  /* 동적 로더 이름 */
#define PT_NOTE    4            /* Auxiliary info. */  /* 보조 정보 */
#define PT_SHLIB   5            /* Reserved. */  /* 예약됨 */
#define PT_PHDR    6            /* Program header table. */  /* 프로그램 헤더 테이블 */
#define PT_STACK   0x6474e551   /* Stack segment. */  /* 스택 세그먼트 */

#define PF_X 1          /* Executable. */  /* 실행 가능 */
#define PF_W 2          /* Writable. */  /* 쓰기 가능 */
#define PF_R 4          /* Readable. */  /* 읽기 가능 */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
/* 실행 파일 헤더. [ELF1] 1-4에서 1-8 참조.
 * 이것은 ELF 바이너리의 맨 처음에 나타납니다. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];  // ELF 식별자 (매직 넘버 등)
	uint16_t e_type;  // 파일 타입
	uint16_t e_machine;  // 머신 타입 (0x3E = x86-64)
	uint32_t e_version;  // 버전
	uint64_t e_entry;  // 진입점 주소
	uint64_t e_phoff;  // 프로그램 헤더 테이블 오프셋
	uint64_t e_shoff;  // 섹션 헤더 테이블 오프셋
	uint32_t e_flags;  // 플래그
	uint16_t e_ehsize;  // ELF 헤더 크기
	uint16_t e_phentsize;  // 프로그램 헤더 엔트리 크기
	uint16_t e_phnum;  // 프로그램 헤더 엔트리 개수
	uint16_t e_shentsize;  // 섹션 헤더 엔트리 크기
	uint16_t e_shnum;  // 섹션 헤더 엔트리 개수
	uint16_t e_shstrndx;  // 섹션 이름 문자열 테이블 인덱스
};

struct ELF64_PHDR {  // 프로그램 헤더 (세그먼트 정보)
	uint32_t p_type;  // 세그먼트 타입
	uint32_t p_flags;  // 세그먼트 플래그 (읽기/쓰기/실행)
	uint64_t p_offset;  // 파일 내 오프셋
	uint64_t p_vaddr;  // 가상 주소
	uint64_t p_paddr;  // 물리 주소 (일반적으로 p_vaddr와 동일)
	uint64_t p_filesz;  // 파일에서의 크기
	uint64_t p_memsz;  // 메모리에서의 크기
	uint64_t p_align;  // 정렬
};

/* Abbreviations */
/* 약어 */
#define ELF ELF64_hdr  // ELF 헤더 타입 약어
#define Phdr ELF64_PHDR  // 프로그램 헤더 타입 약어

static bool setup_stack (struct intr_frame *if_);  // 스택 설정 함수 선언
static bool validate_segment (const struct Phdr *, struct file *);  // 세그먼트 유효성 검사 함수 선언
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,  // 세그먼트 로드 함수 선언
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
/* FILE_NAME에서 ELF 실행 파일을 현재 스레드로 로드합니다.
 * 실행 파일의 진입점을 *RIP에 저장하고
 * 초기 스택 포인터를 *RSP에 저장합니다.
 * 성공 시 true, 실패 시 false를 반환합니다. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();  // 현재 스레드
	struct ELF ehdr;  // ELF 헤더
	struct file *file = NULL;  // 실행 파일
	off_t file_ofs;  // 파일 오프셋
	bool success = false;  // 성공 여부
	int i;  // 반복문 인덱스

	/* Allocate and activate page directory. */
	/* 페이지 디렉토리를 할당하고 활성화합니다. */
	t->pml4 = pml4_create ();  // 페이지 테이블 생성
	if (t->pml4 == NULL)  // 생성 실패 시
		goto done;  // 종료
	process_activate (thread_current ());  // 페이지 테이블 활성화

	/* Open executable file. */
	/* 실행 파일을 엽니다. */
	file = filesys_open (file_name);  // 파일 시스템에서 파일 열기
	if (file == NULL) {  // 열기 실패 시
		printf ("load: %s: open failed\n", file_name);  // 에러 메시지 출력
		goto done;  // 종료
	}

	/* Read and verify executable header. */
	/* 실행 파일 헤더를 읽고 검증합니다. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr  // 헤더 읽기 실패
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)  // ELF 매직 넘버 확인 (7F 45 4C 46 02 01 01)
			|| ehdr.e_type != 2  // 타입이 실행 파일(ET_EXEC)이 아님
			|| ehdr.e_machine != 0x3E // amd64  // 머신 타입이 x86-64가 아님
			|| ehdr.e_version != 1  // 버전이 1이 아님
			|| ehdr.e_phentsize != sizeof (struct Phdr)  // 프로그램 헤더 크기 불일치
			|| ehdr.e_phnum > 1024) {  // 프로그램 헤더 개수가 너무 많음
		printf ("load: %s: error loading executable\n", file_name);  // 에러 메시지
		goto done;  // 종료
	}

	/* Read program headers. */
	/* 프로그램 헤더를 읽습니다. */
	file_ofs = ehdr.e_phoff;  // 프로그램 헤더 테이블 오프셋
	for (i = 0; i < ehdr.e_phnum; i++) {  // 각 프로그램 헤더에 대해
		struct Phdr phdr;  // 프로그램 헤더

		if (file_ofs < 0 || file_ofs > file_length (file))  // 오프셋 유효성 검사
			goto done;  // 범위를 벗어나면 종료
		file_seek (file, file_ofs);  // 파일 포인터 이동

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)  // 헤더 읽기
			goto done;  // 읽기 실패 시 종료
		file_ofs += sizeof phdr;  // 다음 헤더로 이동
		switch (phdr.p_type) {  // 세그먼트 타입에 따라
			case PT_NULL:  // NULL 세그먼트
			case PT_NOTE:  // NOTE 세그먼트
			case PT_PHDR:  // 프로그램 헤더 테이블
			case PT_STACK:  // 스택 세그먼트
			default:
				/* Ignore this segment. */
				/* 이 세그먼트를 무시합니다. */
				break;  // 건너뛰기
			case PT_DYNAMIC:  // 동적 링킹
			case PT_INTERP:  // 인터프리터
			case PT_SHLIB:  // 공유 라이브러리
				goto done;  // 지원하지 않음, 종료
			case PT_LOAD:  // 로드 가능한 세그먼트
				if (validate_segment (&phdr, file)) {  // 세그먼트 유효성 검사
					bool writable = (phdr.p_flags & PF_W) != 0;  // 쓰기 가능 여부
					uint64_t file_page = phdr.p_offset & ~PGMASK;  // 파일 내 페이지 경계 정렬
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;  // 메모리 내 페이지 경계 정렬
					uint64_t page_offset = phdr.p_vaddr & PGMASK;  // 페이지 내 오프셋
					uint32_t read_bytes, zero_bytes;  // 읽을 바이트, 0으로 채울 바이트
					if (phdr.p_filesz > 0) {  // 파일 크기가 0보다 크면
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						/* 일반 세그먼트.
						 * 디스크에서 초기 부분을 읽고 나머지는 0으로 채웁니다. */
						read_bytes = page_offset + phdr.p_filesz;  // 읽을 바이트 수
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)  // 메모리 크기를 페이지 단위로 올림
								- read_bytes);  // 읽은 바이트를 뺀 나머지 (0으로 채울 부분)
					} else {  // 파일 크기가 0이면
						/* Entirely zero.
						 * Don't read anything from disk. */
						/* 완전히 0으로 채움.
						 * 디스크에서 아무것도 읽지 않습니다. */
						read_bytes = 0;  // 읽을 바이트 없음
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);  // 전체를 0으로 채움
					}
					if (!load_segment (file, file_page, (void *) mem_page,  // 세그먼트 로드
								read_bytes, zero_bytes, writable))
						goto done;  // 로드 실패 시 종료
				}
				else  // 세그먼트 유효성 검사 실패
					goto done;  // 종료
				break;
		}
	}

	/* Set up stack. */
	/* 스택을 설정합니다. */
	if (!setup_stack (if_))  // 스택 설정 실패 시
		goto done;  // 종료

	/* Start address. */
	/* 시작 주소. */
	if_->rip = ehdr.e_entry;  // 진입점 주소 설정

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */
	/* TODO: 여기에 코드를 작성하세요.
	 * TODO: 인자 전달을 구현하세요 (project2/argument_passing.html 참조). */
	// TODO: 커맨드 라인 인자 파싱
	// TODO: 스택에 인자 배치 (역순)
	// TODO: argv 배열 포인터 배치
	// TODO: argv, argc 배치
	// TODO: fake return address 배치
	// TODO: 스택 포인터 설정

	success = true;  // 성공

done:
	/* We arrive here whether the load is successful or not. */
	/* 로드가 성공했든 실패했든 여기에 도달합니다. */
	file_close (file);  // 파일 닫기
	return success;  // 성공 여부 반환
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
/* PHDR가 FILE에서 유효한 로드 가능한 세그먼트를 설명하는지 확인하고,
 * 그렇다면 true를, 그렇지 않으면 false를 반환합니다. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	/* p_offset과 p_vaddr는 같은 페이지 오프셋을 가져야 합니다. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))  // 페이지 오프셋 불일치
		return false;  // 유효하지 않음

	/* p_offset must point within FILE. */
	/* p_offset은 FILE 내를 가리켜야 합니다. */
	if (phdr->p_offset > (uint64_t) file_length (file))  // 파일 크기를 초과
		return false;  // 유효하지 않음

	/* p_memsz must be at least as big as p_filesz. */
	/* p_memsz는 최소한 p_filesz만큼 커야 합니다. */
	if (phdr->p_memsz < phdr->p_filesz)  // 메모리 크기가 파일 크기보다 작음
		return false;  // 유효하지 않음

	/* The segment must not be empty. */
	/* 세그먼트는 비어있지 않아야 합니다. */
	if (phdr->p_memsz == 0)  // 메모리 크기가 0
		return false;  // 유효하지 않음

	/* The virtual memory region must both start and end within the
	   user address space range. */
	/* 가상 메모리 영역은 시작과 끝이 모두 사용자 주소 공간 범위 내에 있어야 합니다. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))  // 시작 주소가 사용자 영역이 아님
		return false;  // 유효하지 않음
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))  // 끝 주소가 사용자 영역이 아님
		return false;  // 유효하지 않음

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	/* 영역은 커널 가상 주소 공간을 가로질러 "감싸기"할 수 없습니다. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)  // 오버플로우 (감싸기)
		return false;  // 유효하지 않음

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	/* 페이지 0 매핑을 금지합니다.
	   페이지 0을 매핑하는 것은 좋은 생각이 아닐 뿐만 아니라, 이를 허용하면
	   시스템 콜에 null 포인터를 전달한 사용자 코드가 memcpy() 등의
	   null 포인터 어설션으로 인해 커널을 패닉시킬 수 있습니다. */
	if (phdr->p_vaddr < PGSIZE)  // 페이지 0 (0 ~ PGSIZE-1)에 매핑 시도
		return false;  // 유효하지 않음

	/* It's okay. */
	/* 괜찮습니다. */
	return true;  // 모든 검증 통과
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */
/* 이 블록의 코드는 프로젝트 2 중에만 사용됩니다.
 * 프로젝트 2 전체를 위해 함수를 구현하려면 #ifndef 매크로 밖에 구현하세요. */

/* load() helpers. */
/* load() 헬퍼 함수들 */
static bool install_page (void *upage, void *kpage, bool writable);  // 페이지 매핑 함수 선언

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
 * 이 함수가 초기화한 페이지는 WRITABLE이 true이면 사용자 프로세스가 쓰기 가능하고,
 * 그렇지 않으면 읽기 전용입니다.
 *
 * 성공 시 true를 반환하고, 메모리 할당 오류나 디스크 읽기 오류가 발생하면 false를 반환합니다. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);  // 전체 크기가 페이지 크기의 배수
	ASSERT (pg_ofs (upage) == 0);  // 가상 주소가 페이지 경계에 정렬됨
	ASSERT (ofs % PGSIZE == 0);  // 파일 오프셋이 페이지 크기의 배수

	file_seek (file, ofs);  // 파일 포인터를 오프셋으로 이동
	while (read_bytes > 0 || zero_bytes > 0) {  // 읽을 바이트나 0으로 채울 바이트가 남아있는 동안
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		/* 이 페이지를 채우는 방법을 계산합니다.
		 * FILE에서 PAGE_READ_BYTES 바이트를 읽고
		 * 마지막 PAGE_ZERO_BYTES 바이트를 0으로 채웁니다. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;  // 이 페이지에서 읽을 바이트 수
		size_t page_zero_bytes = PGSIZE - page_read_bytes;  // 이 페이지에서 0으로 채울 바이트 수

		/* Get a page of memory. */
		/* 메모리 페이지를 가져옵니다. */
		uint8_t *kpage = palloc_get_page (PAL_USER);  // 사용자 풀에서 페이지 할당
		if (kpage == NULL)  // 할당 실패 시
			return false;  // 실패 반환

		/* Load this page. */
		/* 이 페이지를 로드합니다. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {  // 파일에서 읽기
			palloc_free_page (kpage);  // 읽기 실패 시 페이지 해제
			return false;  // 실패 반환
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);  // 나머지 부분을 0으로 채움

		/* Add the page to the process's address space. */
		/* 페이지를 프로세스의 주소 공간에 추가합니다. */
		if (!install_page (upage, kpage, writable)) {  // 페이지 테이블에 매핑
			printf("fail\n");  // 디버그 메시지
			palloc_free_page (kpage);  // 매핑 실패 시 페이지 해제
			return false;  // 실패 반환
		}

		/* Advance. */
		/* 진행합니다. */
		read_bytes -= page_read_bytes;  // 읽은 바이트 수 감소
		zero_bytes -= page_zero_bytes;  // 0으로 채운 바이트 수 감소
		upage += PGSIZE;  // 다음 페이지로 이동
	}
	return true;  // 성공
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
/* USER_STACK에 0으로 채워진 페이지를 매핑하여 최소 스택을 생성합니다 */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;  // 커널 페이지 포인터
	bool success = false;  // 성공 여부

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);  // 사용자 풀에서 0으로 초기화된 페이지 할당
	if (kpage != NULL) {  // 할당 성공 시
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);  // USER_STACK 바로 아래에 페이지 매핑 (쓰기 가능)
		if (success)  // 매핑 성공 시
			if_->rsp = USER_STACK;  // 스택 포인터를 USER_STACK으로 설정
		else  // 매핑 실패 시
			palloc_free_page (kpage);  // 페이지 해제
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
 * 성공 시 true를 반환하고, UPAGE가 이미 매핑되어 있거나 메모리 할당이 실패하면 false를 반환합니다. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();  // 현재 스레드

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	/* 해당 가상 주소에 이미 페이지가 없는지 확인한 다음,
	 * 그곳에 페이지를 매핑합니다. */
	return (pml4_get_page (t->pml4, upage) == NULL  // 페이지가 이미 매핑되어 있지 않음
			&& pml4_set_page (t->pml4, upage, kpage, writable));  // 페이지 테이블에 매핑 추가
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */
/* 여기서부터는 프로젝트 3 이후에 사용될 코드입니다.
 * 프로젝트 2만을 위해 함수를 구현하려면 위 블록에 구현하세요. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	/* TODO: 파일에서 세그먼트를 로드합니다 */
	/* TODO: 주소 VA에서 첫 번째 페이지 폴트가 발생할 때 호출됩니다. */
	/* TODO: 이 함수를 호출할 때 VA를 사용할 수 있습니다. */
	// 프로젝트 3에서 구현
	return false;
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
 * 이 함수가 초기화한 페이지는 WRITABLE이 true이면 사용자 프로세스가 쓰기 가능하고,
 * 그렇지 않으면 읽기 전용입니다.
 *
 * 성공 시 true를 반환하고, 메모리 할당 오류나 디스크 읽기 오류가 발생하면 false를 반환합니다. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);  // 전체 크기가 페이지 크기의 배수
	ASSERT (pg_ofs (upage) == 0);  // 가상 주소가 페이지 경계에 정렬됨
	ASSERT (ofs % PGSIZE == 0);  // 파일 오프셋이 페이지 크기의 배수

	while (read_bytes > 0 || zero_bytes > 0) {  // 읽을 바이트나 0으로 채울 바이트가 남아있는 동안
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		/* 이 페이지를 채우는 방법을 계산합니다.
		 * FILE에서 PAGE_READ_BYTES 바이트를 읽고
		 * 마지막 PAGE_ZERO_BYTES 바이트를 0으로 채웁니다. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;  // 이 페이지에서 읽을 바이트 수
		size_t page_zero_bytes = PGSIZE - page_read_bytes;  // 이 페이지에서 0으로 채울 바이트 수

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		/* TODO: lazy_load_segment에 정보를 전달하기 위해 aux를 설정합니다. */
		void *aux = NULL;  // aux 구조체 (파일 정보 등) - TODO: 초기화 필요
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,  // 익명 페이지 할당 (lazy loading)
					writable, lazy_load_segment, aux))  // lazy_load_segment를 초기화 함수로 사용
			return false;  // 할당 실패 시 종료

		/* Advance. */
		/* 진행합니다. */
		read_bytes -= page_read_bytes;  // 읽은 바이트 수 감소
		zero_bytes -= page_zero_bytes;  // 0으로 채운 바이트 수 감소
		upage += PGSIZE;  // 다음 페이지로 이동
	}
	return true;  // 성공
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
/* USER_STACK에 스택 페이지를 생성합니다. 성공 시 true를 반환합니다. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;  // 성공 여부
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);  // 스택 하단 주소

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */
	/* TODO: stack_bottom에 스택을 매핑하고 페이지를 즉시 클레임합니다.
	 * TODO: 성공 시 rsp를 그에 따라 설정합니다.
	 * TODO: 페이지가 스택임을 표시해야 합니다. */
	/* TODO: 여기에 코드를 작성하세요 */
	// 프로젝트 3에서 구현

	return success;  // 성공 여부 반환
}
#endif /* VM */
