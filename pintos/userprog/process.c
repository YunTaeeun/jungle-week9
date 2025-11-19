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
#include "threads/synch.h"
#ifdef VM
#include "vm/vm.h"
#endif

// process.c : ELF 바이너리를 로드하고 프로세스를 시작합니다
static struct semaphore initial;
static void process_cleanup(void);
static bool load(const char *file_name, struct intr_frame *if_);
static void initd(void *f_name);
static bool duplicate_fdt(struct thread *parent, struct thread *child);
static void __do_fork(void *);
struct thread *find_child(tid_t);

/* initd와 다른 프로세스들을 위한 일반 프로세스 초기화 함수입니다. */
static void process_init(void)
{
	struct thread *current = thread_current();
	struct fd_table *fdt = current->fdt;

	// fd table 동적 할당
	fdt = palloc_get_page(0);
	if (fdt == NULL)
		PANIC("fd_table allocation failed");

	// fd table 초기화
	fdt->files = palloc_get_multiple(
	    PAL_ZERO, DIV_ROUND_UP(FD_INITIAL_CAPACITY * sizeof(struct file *), PGSIZE));
	fdt->capacity = FD_INITIAL_CAPACITY;
	fdt->next_fd = 3;
	fdt->magic = FILE_FD_MAGIC;

	current->fdt = fdt;
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
// filename 은 'programname args ~' 이런식
// 최초 유저 프로세스를 실행하는 함수
tid_t process_create_initd(const char *file_name)
{
	// printf("===> process_create_initd started.\n");

	static bool sema_initialized = false;
	if (!sema_initialized) {
		sema_init(&initial, 0);
		sema_initialized = true;
	}
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page(0);
	if (fn_copy == NULL)
		return TID_ERROR;
	// fn_copy = 'programname args ~'
	strlcpy(fn_copy, file_name, PGSIZE);

	// file_name 파싱 하는 부분 'programname args' -> 'programname' 됨
	char *ptr;
	strtok_r(file_name, " ", &ptr);

	/* Create a new thread to execute FILE_NAME. */
	// 프로그램을 실행할 쓰레드를 하나 만들고 , 그 쓰레드는 바로 initd
	// 실행 (fn_copy를 인자로 받아서 -> fn_copy에는 programname args 다
	// 들어있음)
	tid = thread_create(file_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page(fn_copy);
	// printf("===> process_create_initd ended.\n");

	return tid;
}

/* 첫 번째 유저 프로세스를 실행하는 스레드 함수입니다. */
static void initd(void *f_name)
{
	// printf("[INITD] Started, f_name='%s'\n", (char*)f_name);

#ifdef VM
	supplemental_page_table_init(&thread_current()->spt);
#endif
	// 최초 유저 프로세스 초기화
	process_init();

	// 프로그램명만 추출하여 "Executing" 메시지 출력
	// AS-IS: exec 시스템콜로 실행되는 자식 프로세스에서도 Executing 메시지
	// 출력 TO-BE:최초 프로세스(initd를 통해 실행)되는 경우에는 Executing
	// 출력
	char *prog_name = f_name;
	char *space = strchr(f_name, ' ');
	if (space) {
		char saved = *space;
		*space = '\0';
		printf("Executing '%s':\n", prog_name);
		*space = saved;
	} else {
		printf("Executing '%s':\n", prog_name);
	}

	// 여기서 process_exec 실행 -> 자기 자신(쓰레드) 를 사용자 프로그램으로
	// 변환
	// printf("===> initd.\n");

	if (process_exec(f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED();
}

/* fork할 때 필요한 인자들을 담는 구조체를 선언/정의한다. */
struct fork_args {
	struct thread *parent;
	struct intr_frame *parent_if;
	struct semaphore child_create;
	bool success;
};

/* 현재 프로세스를 `name`으로 복제합니다. 새 프로세스의 스레드 ID를 반환하거나,
 * 스레드를 생성할 수 없으면 TID_ERROR를 반환합니다. */
tid_t process_fork(const char *name, struct intr_frame *if_ UNUSED)
{
	/* fork인자용 구조체에 데이터를 담는다 */
	struct fork_args args = {.parent = thread_current(), .parent_if = if_, .success = false};
	sema_init(&args.child_create, 0);

	/* 현재 스레드를 새 스레드로 복제합니다. */
	tid_t child_tid = thread_create(name, PRI_DEFAULT, __do_fork, &args);
	/* 스레드 생성에 실패하면 무의미한 대기를 하지 않고, 바로 TID_ERROR를
	 * 반환한다 */
	if (child_tid == TID_ERROR) {
		return TID_ERROR;
	}

	/* 자식 프로세스 생성과 복제가 완료될 때까지 부모 프로세스는 기다린다 */
	sema_down(&args.child_create);

	/* 복제 성공 여부를 판단한다 */
	if (!args.success) {
		return TID_ERROR;
	}

	/* 자식 프로세스 생성 결과를 돌려준다. */
	return child_tid;
}

#ifndef VM
/* 이 함수를 pml4_for_each에 전달하여 부모의 주소 공간을 복제합니다.
 * 이것은 프로젝트 2 전용입니다.
 * pte: 페이지 테이블 엔트리
 * va: 가상 주소 (현재 처리 중인 페이지의 주소)
 * aux: 부모 스레드 포인터
 */
static bool duplicate_pte(uint64_t *pte, void *va, void *aux)
{
	struct thread *current = thread_current();
	struct thread *parent = (struct thread *)aux;
	void *parent_page;
	void *newpage;
	bool writable = false;

	/* 1. 커널 페이지는 건너뛰기 */
	if (!is_user_vaddr(va))
		return true;

	/* 2. 부모 페이지 가져오기 */
	parent_page = pml4_get_page(parent->pml4, va);

	/* 3. 새 페이지 할당 */
	newpage = palloc_get_page(PAL_USER);
	if (newpage == NULL)
		return false;

	/* 4. 복제&권한 확인 */
	memcpy(newpage, parent_page, PGSIZE);
	if (*pte & PTE_W)
		writable = true;

	/* 5. 자식의 페이지 테이블에 새 페이지를 추가 */
	if (!pml4_set_page(current->pml4, va, newpage, writable)) {
		/* 6. 페이지 삽입 실패 시, 에러 처리 */
		palloc_free_page(newpage); // 메모리 해제
		return false;		   // 전체 fork 실패처리
	}
	return true;
}
#endif

static bool duplicate_fdt(struct thread *parent, struct thread *child)
{
	// 1. null 체크
	if (parent->fdt == NULL)
		return true; // 커널 스레드

	struct fd_table *parent_fdt = parent->fdt;
	struct fd_table *child_fdt = child->fdt;

	// 2. 부모보다 capacity 작으면 동일하게 확장
	if (child_fdt->capacity < parent_fdt->capacity) {
		int new_pages = DIV_ROUND_UP(parent_fdt->capacity * sizeof(struct file *), PGSIZE);
		struct file **new = palloc_get_multiple(PAL_ZERO, new_pages);
		memcpy(new, parent_fdt->files, parent_fdt->capacity * sizeof(struct file *));

		// 기존 페이지 free
		palloc_free_multiple(
		    child_fdt->files,
		    DIV_ROUND_UP(child_fdt->capacity * sizeof(struct file *), PGSIZE));

		// 테이블 포인터 변경
		child_fdt->files = new;

		// capacity 변경
		child_fdt->capacity = parent_fdt->capacity;
	}

	// 3. fd 복제
	lock_acquire(&filesys_lock);

	for (int fd = 3; fd < parent_fdt->capacity; fd++) {
		if (parent_fdt->files[fd] != NULL) {
			child_fdt->files[fd] = file_duplicate(parent_fdt->files[fd]);
			if (child_fdt->files[fd] == NULL) {
				// 복제 실패
				lock_release(&filesys_lock);
				return false; // 롤백은 process_exit()에서 자동처리됨
			}
		}
	}

	lock_release(&filesys_lock);

	// 4. next_fd 동기화
	child_fdt->next_fd = parent_fdt->next_fd;

	return true;
}

/* 부모의 실행 컨텍스트를 복사하는 스레드 함수
 * - CPU 컨텍스트 복제
 * - 페이지 테이블 복제
 * - 부모-자식 관계 설정
 * - 에러 핸들링
 */

static void __do_fork(void *aux)
{
	struct fork_args *args = (struct fork_args *)aux;
	struct intr_frame if_;
	struct thread *parent = args->parent;
	struct thread *current = thread_current();
	struct intr_frame *parent_if = args->parent_if;
	bool succ = true;

	/* 1. CPU 컨텍스트를 로컬 스택으로 읽어옵니다. */
	memcpy(&if_, parent_if, sizeof(struct intr_frame));
	if_.R.rax = 0; // 자식 프로세스의 입장에서 fork()의 반환값을 0으로 설정

	/* 2. 페이지 테이블을 복제합니다 */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate(current);
#ifdef VM
	supplemental_page_table_init(&current->spt);
	if (!supplemental_page_table_copy(&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each(parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	process_init();
	if (!duplicate_fdt(parent, current))
		goto error; // 복제 실패시 에러처리 후 자동 롤백

	/* 수행 성공을 저장 */
	args->success = true;

	/* 부모 스레드, 자식 리스트 저장 */
	current->parent = parent;
	list_push_back(&parent->child_list, &current->child_elem);

	/* 부모 프로세스에게 종료를 알린다 */
	sema_up(&args->child_create);

	/* 마지막으로, 새로 생성된 프로세스로 전환합니다. */
	if (succ)
		do_iret(&if_);

error:
	/* 수행 실패를 저장하고 부모 프로세스에게 종료를 알린다. */
	args->success = false; // 명시적으로 보여주기 위해 써줌
	sema_up(&args->child_create);

	thread_exit();
}

/* 현재 실행 중인 프로세스(커널 스레드)를 'f_name'의
 * 새 유저 프로그램으로 교체합니다.
 * 이 함수는 실패 시 -1을 반환하며, 성공 시 리턴하지 않습니다. */
int process_exec(void *f_name)
{
	bool success;
	char *file_name = palloc_get_page(0);
	if (file_name == NULL)
		return -1;
	strlcpy(file_name, f_name, strlen(f_name) + 1);

	/* 1. 유저 모드 진입을 위한 '임시' CPU 레지스터(intr_frame)를 설정. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS; // 인터럽트 활성화

	/* 2. 현재 컨텍스트(메모리 공간, pml4)를 정리(파괴)하여
	 * 새 유저 프로세스로 '변신'할 준비를 함. */
	process_cleanup();

	/* 3. load() 함수를 호출하여 새 프로그램을 메모리에 적재. */
	// printf("===> load.\n");

	success = load(file_name, &_if);

	/* 4. f_name은 process_create_initd에서 할당(palloc)한 복사본이므로,
	 * 로드가 끝났으니 해당 메모리 페이지를 해제. */
	palloc_free_page(file_name);

	if (!success)
		return -1; // 로드 실패 (예: 파일 없음, 메모리 부족 등)

	/* 5. do_iret()을 호출하여 유저 모드로 전환.
	 * CPU 레지스터가 _if에 설정된 값(rip, rsp 등)으로 갱신되며,
	 * 유저 프로그램의 진입점(rip)에서 실행을 시작.
	 * 이 함수는 커널로 돌아오지 않음. */
	// printf("===> do_iret.\n");

	do_iret(&_if);
	NOT_REACHED();
}

struct thread *find_child(tid_t target_tid)
{
	struct list_elem *e;
	struct thread *child = NULL;
	for (e = list_begin(&thread_current()->child_list);
	     e != list_end(&thread_current()->child_list); e = list_next(e)) {
		child = list_entry(e, struct thread, child_elem);
		if (child->tid == target_tid) {
			return child;
		}
	}

	return NULL;
}

/* 최초 프로세스(initd로 생성된 프로세스)가 종료될 때까지 기다립니다.
 * 이 함수는 init.c의 run_task에서 호출됩니다.
 * process_wait와 달리 부모-자식 관계가 없는 최초 프로세스를 위해
 * initial 세마포어를 사용합니다. */
void process_wait_initd(void)
{
	sema_down(&initial);
}

/* 스레드 TID가 종료될 때까지 기다리고 종료 상태를 반환합니다. 만약
 * 커널에 의해 종료되었다면 (즉, 예외로 인해 죽임당함), -1을 반환합니다.
 * TID가 유효하지 않거나, 호출 프로세스의 자식이 아니거나,
 * 주어진 TID에 대해 process_wait()가 이미 성공적으로 호출되었다면,
 * 기다리지 않고 즉시 -1을 반환합니다.*/
int process_wait(tid_t child_tid UNUSED)
{
	// [1] 세팅: thread 구조체
	// 1. thread 구조체에 struct semaphore dead 추가
	// 2. thread 구조체에 list_elem 추가
	// 3. thread 구조체에 bool waited 추가 (wait은 1번만 가능)
	// 4. thread 구조체에 parent 추가
	// 5. 스레드 생길 때 child_list 선언 및 초기화 (부모-자식 확인용) :
	// init_thread()
	// 5. 스레드 생길 때 sema_init(&child->dead, 0) : init_thread()
	// 6. fork시 parent 값 세팅 : __do_fork()

	// [2] 자식 스레드의 작업
	// 1. 자식 스레드는 자기 작업이 다 끝나면 exit_status 값 세팅
	// : exit syscall에서 세팅
	// 2. (조건문) 부모 스레드가 NULL 이거나 죽은 스레드인지 확인 (고아
	// 확인 - TC: wait-killed) : process_exit()
	// 3. (조건문) 2번이 false이면 세마포어 업 (부모 깨우기)
	// 4. 자기 자신 thread_exit() (스케쥴러로 넘어가서 이 스레드 다시
	// 실행되지 않음)

	// [3] 부모 스레드의 작업
	// 1. find_child(): child_list에서 해당 tid가 자기 자식 맞는지 확인:
	// 없으면 -1 반환
	// 2. 유효한 TID인지: 아니면 -1 반환
	// 3. 인터럽트 끄기 & child->waited = false인지 확인: 아니면 -1 반환 &
	// 인터럽트 원래 상태로 복원
	// 4. 1~3 통과했으면 child->waited = true로 세팅 & 인터럽트 원래 상태로
	// 복원
	// 5. sema_down 하고 자식이 up할 까지 기다림
	// 6. up됐으면 exit_status 값을 지역 변수 status에 기록
	// 7. list_remove
	// 8. return status
	struct thread *child = find_child(child_tid);
	if (!child) // 못 찾았거나 유효하지 않은 TID
	{
		return -1;
	}

	enum intr_level old_level;
	ASSERT(!intr_context());
	old_level = intr_disable();
	if (child->waited) {
		intr_set_level(old_level);
		return -1;
	}
	child->waited = true;
	intr_set_level(old_level);

	sema_down(&child->dead);

	int status = child->exit_status;
	list_remove(&child->child_elem);

	return status;
}

/* Exit the process. This function is called by thread_exit (). */
void process_exit(void)
{
	struct thread *curr = thread_current();

	/* 1. 파일 정리 */
	/* fd table 매직넘버 확인 */
	if (curr->fdt->magic != FILE_FD_MAGIC)
		return -1;

	int cnt = 0; // 실제 사용 중인 슬롯수
	for (int i = 3; i < curr->fdt->capacity; i++) {
		struct file *file = curr->fdt->files[i];
		if (file != NULL) {
			file_close(file);
			cnt++;
		}
	}

	/* 동적할당 받았던 연속된 페이지 전체를 한꺼번에 해제한다 */
	int pages = DIV_ROUND_UP(curr->fdt->capacity * sizeof(struct file *), PGSIZE);
	palloc_free_multiple(curr->fdt->files, pages);

	/* 2. 부모에게 알리기 */
	/* 부모가 있는 경우 (fork로 생성된 프로세스) */
	if (curr->parent != NULL && curr->parent->status != THREAD_DYING) {
		sema_up(&curr->dead);
	}
	/* 부모가 없는 경우 (최초 프로세스 - initd로 생성) */
	else if (curr->parent == NULL) {
		sema_up(&initial);
	}

	process_cleanup();
}

/* 현재 프로세스의 리소스를 해제합니다. */
static void process_cleanup(void)
{
	struct thread *curr = thread_current();

#ifdef VM
	supplemental_page_table_kill(&curr->spt);
#endif

	uint64_t *pml4;
	/* 현재 프로세스의 페이지 디렉토리를 파괴하고
	 * 커널 전용 페이지 디렉토리로 다시 전환합니다. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* 여기서 올바른 순서가 중요합니다. 페이지 디렉토리를 전환하기
		 * 전에 cur->pagedir를 NULL로 설정해야 합니다. 그래야 타이머
		 * 인터럽트가 프로세스 페이지 디렉토리로 다시 전환할 수
		 * 없습니다. 프로세스의 페이지 디렉토리를 파괴하기 전에 베이스
		 * 페이지 디렉토리를 활성화해야 합니다. 그렇지 않으면 활성
		 * 페이지 디렉토리가 해제된(그리고 지워진) 것이 됩니다. */
		curr->pml4 = NULL;
		pml4_activate(NULL);
		pml4_destroy(pml4);
	}
}

/* 다음 스레드에서 사용자 코드를 실행하기 위해 CPU를 설정합니다.
 * 이 함수는 모든 컨텍스트 전환 시 호출됩니다. */
void process_activate(struct thread *next)
{
	/* 스레드의 페이지 테이블을 활성화합니다. */
	pml4_activate(next->pml4);

	/* 인터럽트 처리에 사용할 스레드의 커널 스택을 설정합니다. */
	tss_update(next);
}

/* ELF 바이너리를 로드합니다. 다음 정의들은
 * ELF 명세서 [ELF1]에서 거의 그대로 가져왔습니다. */

/* ELF 타입들. [ELF1] 1-2 참고. */
#define EI_NIDENT 16

#define PT_NULL 0	    /* 무시. */
#define PT_LOAD 1	    /* 로드 가능한 세그먼트. */
#define PT_DYNAMIC 2	    /* 동적 링킹 정보. */
#define PT_INTERP 3	    /* 동적 로더의 이름. */
#define PT_NOTE 4	    /* 보조 정보. */
#define PT_SHLIB 5	    /* 예약됨. */
#define PT_PHDR 6	    /* 프로그램 헤더 테이블. */
#define PT_STACK 0x6474e551 /* 스택 세그먼트. */

#define PF_X 1 /* 실행 가능. */
#define PF_W 2 /* 쓰기 가능. */
#define PF_R 4 /* 읽기 가능. */

/* 실행 파일 헤더. [ELF1] 1-4에서 1-8 참고.
 * ELF 바이너리의 맨 처음에 나타납니다. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* 약어들 */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack(struct intr_frame *if_);
static bool validate_segment(const struct Phdr *, struct file *);
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage, uint32_t read_bytes,
			 uint32_t zero_bytes, bool writable);

// 로드 함수에서 file_name (cmd_line) 넘기면 스택에 밀어넣는 부분
bool arg_load_stack(char *cmdline, struct intr_frame *if_)
{
	// 성공 실패 반환
	bool success = false;

	char *token, *save_ptr;
	int argc = 0;
	char *argv[64]; // 최대 64개의 인자를 처리한다고 가정

	// 1. 커맨드 라인 파싱
	for (token = strtok_r(cmdline, " ", &save_ptr); token != NULL;
	     token = strtok_r(NULL, " ", &save_ptr)) {
		argv[argc++] = token;
	}

	// 2. 인자 문자열들을 스택에 역순으로 Push
	for (int i = argc - 1; i >= 0; i--) {
		int len = strlen(argv[i]);
		// 인자들의 각 바이트 수만큼 스택 증가(증가니까 - , + NULL
		// 생각!)
		if_->rsp -= (len + 1); // 널 종결 문자 포함
		memcpy((void *)if_->rsp, argv[i],
		       len + 1);	    // arvg[i]에 있는 데이터 스택에 넣고
		argv[i] = (char *)if_->rsp; // 배열 해당 자리엔 그 데이터 주소 넣기
	}
	// 3. 스택 포인터를 8바이트로 정렬
	// 포인터 공간만 만들고 초기화 x -> 어차피 안씀 (아래에서 실제 데이터
	// 접근할땐 주소로 뛰니까)
	while (if_->rsp % 8 != 0)
		if_->rsp--;

	// 4. argv 포인터(문자열 주소)들을 스택에 Push
	// argv[argc]에 해당하는 널 포인터 sentinel 삽입
	if_->rsp -= 8;
	memset((void *)if_->rsp, 0, sizeof(char *));

	// 인자들의 주소를 역순으로 삽입
	for (int i = argc - 1; i >= 0; i--) {
		// 삽입되니 스택의 최상단 주소 8씩 감소 (8씩 증가)
		if_->rsp -= 8;
		memcpy((void *)if_->rsp, &argv[i], sizeof(char *));
	}

	// 5. main(argc, argv)를 위한 레지스터 설정
	if_->R.rdi = argc;
	if_->R.rsi = if_->rsp;

	// 6. 가짜 반환 주소 Push
	if_->rsp -= 8;
	memset((void *)if_->rsp, 0, sizeof(void *));

	success = true;
	return success;
}

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
// 유저가 실행을 요청한 프로그램을 하드 디스크에서 찾아서 메모리에 적재(load)
// 하는 단계
/* Loads an ELF executable from the file system into the current process's
 * memory.
 *
 * file_name: 실행할 유저 프로그램의 파일 이름
 * if_:       유저 프로그램이 시작될 때의 레지스터 상태를 저장하는 intr_frame
 *
 * 이 함수는 다음 단계를 수행한다:
 * 1. 새로운 페이지 테이블(pml4)을 만들고 활성화
 * 2. 파일 시스템에서 실행 파일을 열고 ELF 헤더 검증
 * 3. 프로그램 헤더를 읽으며 메모리에 코드/데이터 세그먼트를 적재
 * 4. 유저 스택을 설정 (setup_stack)
 * 5. 진입점(e_entry)을 설정하여 CPU가 해당 주소부터 실행하도록 함
 */
static bool load(const char *file_name, struct intr_frame *if_)
{
	struct thread *t = thread_current(); // 현재 스레드 (실행할 프로세스)
	struct ELF ehdr;		     // ELF 헤더 구조체
	struct file *file = NULL;	     // 실행 파일 포인터
	off_t file_ofs;			     // 파일 오프셋
	bool success = false;		     // 성공 여부
	int i;

	char *cmd = palloc_get_page(0);
	if (cmd == NULL) {
		return false;
	}
	strlcpy(cmd, file_name, PGSIZE);

	char *save_ptr;
	char *program_name = strtok_r(cmd, " ", &save_ptr);
	if (program_name == NULL) {
		palloc_free_page(cmd);
		return false;
	}

	/* 1️⃣ 페이지 테이블 생성 및 활성화 */
	t->pml4 = pml4_create(); // 새 pml4(페이지 테이블) 생성
	if (t->pml4 == NULL)
		goto done;
	process_activate(thread_current()); // 새 페이지 테이블 활성화

	/* 2️⃣ 실행 파일 열기 */
	file = filesys_open(program_name); // 파일 시스템에서 실행 파일 탐색 및 오픈
	if (file == NULL) {
		// printf ("load: %s: open failed\n", program_name);
		goto done;
	}

	/* 3️⃣ ELF 헤더 읽고 검증 */
	// 실행 파일이 올바른 ELF 포맷인지 확인
	if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr ||
	    memcmp(ehdr.e_ident, "\177ELF\2\1\1", 7) // ELF 매직 넘버 확인
	    || ehdr.e_type != 2			     // 실행 파일 타입
	    || ehdr.e_machine != 0x3E		     // x86-64 아키텍처
	    || ehdr.e_version != 1 ||
	    ehdr.e_phentsize != sizeof(struct Phdr) // 프로그램 헤더 크기 확인
	    || ehdr.e_phnum > 1024) {		    // 프로그램 헤더 개수 유효성
		// printf ("load: %s: error loading executable\n",
		// program_name);
		goto done;
	}

	/* 4️⃣ 프로그램 헤더를 순회하며 세그먼트 로드 */
	file_ofs = ehdr.e_phoff; // 프로그램 헤더 오프셋부터 읽기 시작
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		// 파일 범위 검증
		if (file_ofs < 0 || file_ofs > file_length(file))
			goto done;
		file_seek(file, file_ofs); // 해당 프로그램 헤더 위치로 이동

		// 프로그램 헤더 읽기
		if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;

		// 세그먼트 타입에 따라 처리
		switch (phdr.p_type) {
		case PT_NULL:
		case PT_NOTE:
		case PT_PHDR:
		case PT_STACK:
		default:
			/* 무시 가능한 세그먼트 */
			break;

		case PT_DYNAMIC:
		case PT_INTERP:
		case PT_SHLIB:
			/* 지원하지 않는 타입 → 실패 처리 */
			goto done;

		case PT_LOAD: {
			/* 로드 가능한 세그먼트 → 메모리에 적재 */
			if (validate_segment(&phdr, file)) {
				bool writable = (phdr.p_flags & PF_W) != 0; // 쓰기 가능 여부
				uint64_t file_page =
				    phdr.p_offset & ~PGMASK; // 파일 오프셋 페이지 단위
				uint64_t mem_page = phdr.p_vaddr & ~PGMASK; // 가상주소 페이지 단위
				uint64_t page_offset = phdr.p_vaddr & PGMASK; // 페이지 내 오프셋
				uint32_t read_bytes, zero_bytes;

				if (phdr.p_filesz > 0) {
					/* 일부는 파일에서 읽고, 나머지는 0으로
					 * 채움 (BSS 등) */
					read_bytes = page_offset + phdr.p_filesz;
					zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) -
						      read_bytes);
				} else {
					/* 완전히 0으로 채워지는 세그먼트 */
					read_bytes = 0;
					zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
				}

				/* 파일에서 메모리로 세그먼트 로드 */
				if (!load_segment(file, file_page, (void *)mem_page, read_bytes,
						  zero_bytes, writable))
					goto done;
			} else
				goto done;
			break;
		}
		}
	}

	/* 5️⃣ 유저 스택 설정 */
	if (!setup_stack(if_))

		goto done;

	/* 6️⃣ 실행 시작 주소 설정 */
	if_->rip = ehdr.e_entry; // ELF 진입점 (main 함수 시작 주소)

	/* TODO: 인자 전달 구현 (argument passing) */
	// - 프로젝트 2에서 argv, argc 스택에 적재하는 부분 구현 예정
	success = arg_load_stack(file_name, if_);

	// success = true;

done:
	/* 성공/실패 여부와 관계없이 파일 닫기 */
	file_close(file);
	palloc_free_page(cmd);
	return success;
}

static bool set_argument_stack(int argc, char **argv, struct intr_frame *if_)
{
	bool success = false;
	/* 커널 메모리에서 유저 메모리로 문자열을 복사한다 (1~7)*/
	// 1. 문자열을 스택에 복사
	uintptr_t rsp = USER_STACK; // 스택이 시작하는 주소
	char *arg_addr[128];	    // 주소를 담을 포인터 배열

	for (int i = argc - 1; i >= 0; i--) // 뒤>앞으로 넣어야 하기 때문에 숫자 줄이며 루프
	{
		int len = strlen(argv[i]) + 1; // 인자의 길이(널센티널 포함)
		rsp -= len; // 스택 포인터를 문자열길이만큼 줄인다
		memcpy((void *)rsp, argv[i], len); // 실제 문자열을 스택에 복사
		// NOTE: (void*)이유? > memcpy시그니처. 이 숫자를 메모리 주소로
		// 해석하라는 의미.
		arg_addr[i] = (char *)rsp; // 나중에 포인터 배열을 만들기 위해
					   // 각 문자열의 주소를 저장한다
	}

	// 2. 8바이트 정렬
	while (rsp % 8 != 0) // 8의 배수인지 아닌지 확인해서 8바이트 정렬 확인
	{
		rsp--;		     // 1바이트 공간 확보하고
		*(uint8_t *)rsp = 0; // 그 주소에 0 쓰기 (패딩)
	}

	// 3. argv[argc] = null
	rsp -= 8; // null이 몇바이트인지 모르겠지만 정렬 위해 8 줄인다
	*(char **)rsp = NULL; // TODO: 포인터 모르겠다.

	// 4. 포인터 배열에 역순으로 저장했던 주소를 스택에 저장
	for (int i = argc - 1; i >= 0; i--) {
		rsp -= 8;
		*(char **)rsp = arg_addr[i]; // 아까 저장했던 인자를 저장한 포인터 저장
	}

	// 5. argv 주소 저장
	char **argv_ptr = (char **)rsp; // rsp가 정수형이니까 포인터 타입으로 변환

	// 6. fake return addr
	/* 일반 함수 호출시, caller가 return address를 스택에 푸시하는데, main
	 * 함수는 첫번째 함수라서 호출자가 없다 그래도 x86-64 ABI는 return
	 * address가 있다고 가정하므로 가짜 리턴주소를 넣어준다. */
	rsp -= 8;
	*(void **)rsp = 0; // 만약 실수로 return하면 0 주소로 점프 → 즉시 page
			   // fault 발생 for 디버깅

	// 7. 레지스터 설정
	if_->R.rdi = argc;
	if_->R.rsi = (uint64_t)argv_ptr;
	if_->rsp = rsp;

	success = true;
	return success;

	/* 유저메모리
	높은 주소 (0x47480000)
	┌─────────────────────┐
	│ "args-single\0"     │ <- arg_addr[0]이 여기 가리킴
	│ "onearg\0"          │ <- arg_addr[1]이 여기 가리킴
	├─────────────────────┤
	│ 0 0 0 (패딩)        │ <- rsp % 8 == 0 되도록
	├─────────────────────┤
	│ NULL (8 bytes)      │ <- argv[2] = NULL
	├─────────────────────┤
	│ arg_addr[1]         │ <- argv[1] 포인터
	│ arg_addr[0]         │ <- argv[0] 포인터
	├─────────────────────┤ <- argv_ptr가 여기
	│ 0 (8 bytes)         │ <- fake return address
	└─────────────────────┘ <- rsp (최종 위치)
	낮은 주소*/
}

/* PHDR이 FILE에서 유효하고 로드 가능한 세그먼트를 설명하는지 확인하고
 * 그렇다면 true를, 그렇지 않으면 false를 반환합니다. */
static bool validate_segment(const struct Phdr *phdr, struct file *file)
{
	/* p_offset과 p_vaddr은 같은 페이지 오프셋을 가져야 합니다. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset은 FILE 내를 가리켜야 합니다. */
	if (phdr->p_offset > (uint64_t)file_length(file))
		return false;

	/* p_memsz는 적어도 p_filesz만큼 커야 합니다. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* 세그먼트는 비어있으면 안 됩니다. */
	if (phdr->p_memsz == 0)
		return false;

	/* 가상 메모리 영역은 사용자 주소 공간 범위 내에서
	   시작하고 끝나야 합니다. */
	if (!is_user_vaddr((void *)phdr->p_vaddr))
		return false;
	if (!is_user_vaddr((void *)(phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* 영역은 커널 가상 주소 공간을 "순환"할 수 없습니다. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* 페이지 0의 매핑을 허용하지 않습니다.
	   페이지 0을 매핑하는 것은 나쁜 생각일 뿐만 아니라, 만약 허용한다면
	   시스템 콜에 null 포인터를 전달하는 사용자 코드가 memcpy() 등의
	   null 포인터 단언을 통해 커널을 패닉시킬 가능성이 높습니다. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* 괜찮습니다. */
	return true;
}

#ifndef VM
/* 이 블록의 코드는 프로젝트 2 동안만 사용됩니다.
 * 전체 프로젝트 2를 위한 함수를 구현하려면 #ifndef 매크로 외부에 구현하세요. */

/* load() 헬퍼 함수들. */
static bool install_page(void *upage, void *kpage, bool writable);

/* FILE의 오프셋 OFS에서 시작하는 세그먼트를 주소 UPAGE에 로드합니다.
 * 총 READ_BYTES + ZERO_BYTES 바이트의 가상 메모리가 다음과 같이 초기화됩니다:
 *
 * - UPAGE의 READ_BYTES 바이트는 FILE에서 오프셋 OFS부터 읽어야 합니다.
 *
 * - UPAGE + READ_BYTES의 ZERO_BYTES 바이트는 0으로 채워야 합니다.
 *
 * 이 함수에 의해 초기화된 페이지들은 WRITABLE이 true이면 사용자 프로세스가
 * 쓰기 가능해야 하고, 그렇지 않으면 읽기 전용이어야 합니다.
 *
 * 성공하면 true를, 메모리 할당 오류나 디스크 읽기 오류가 발생하면
 * false를 반환합니다. */
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage, uint32_t read_bytes,
			 uint32_t zero_bytes, bool writable)
{
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(upage) == 0);
	ASSERT(ofs % PGSIZE == 0);

	file_seek(file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* 이 페이지를 어떻게 채울지 계산합니다.
		 * FILE에서 PAGE_READ_BYTES 바이트를 읽고
		 * 마지막 PAGE_ZERO_BYTES 바이트를 0으로 채웁니다. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* 메모리 페이지를 가져옵니다. */
		uint8_t *kpage = palloc_get_page(PAL_USER);
		if (kpage == NULL)
			return false;

		/* 이 페이지를 로드합니다. */
		if (file_read(file, kpage, page_read_bytes) != (int)page_read_bytes) {
			palloc_free_page(kpage);
			return false;
		}
		memset(kpage + page_read_bytes, 0, page_zero_bytes);

		/* 프로세스의 주소 공간에 페이지를 추가합니다. */
		if (!install_page(upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page(kpage);
			return false;
		}

		/* 진행합니다. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* USER_STACK에 0으로 채워진 페이지를 매핑하여 최소 스택을 생성합니다. */
static bool setup_stack(struct intr_frame *if_)
{
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page(PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page(((uint8_t *)USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page(kpage);
	}
	return success;
}

/* 사용자 가상 주소 UPAGE에서 커널 가상 주소 KPAGE로의 매핑을 페이지 테이블에
 * 추가합니다. WRITABLE이 true이면, 사용자 프로세스가 페이지를 수정할 수
 * 있습니다. 그렇지 않으면 읽기 전용입니다. UPAGE는 아직 매핑되지 않아야 합니다.
 * KPAGE는 아마도 palloc_get_page()로 사용자 풀에서 얻은 페이지여야 합니다.
 * 성공하면 true를, UPAGE가 이미 매핑되었거나 메모리 할당이 실패하면
 * false를 반환합니다. */
static bool install_page(void *upage, void *kpage, bool writable)
{
	struct thread *t = thread_current();

	/* 해당 가상 주소에 이미 페이지가 없는지 확인한 다음,
	 * 우리의 페이지를 거기에 매핑합니다. */
	return (pml4_get_page(t->pml4, upage) == NULL &&
		pml4_set_page(t->pml4, upage, kpage, writable));
}
#else
/* 여기서부터는 프로젝트 3 이후에 사용될 코드입니다.
 * 프로젝트 2 전용 함수를 구현하려면 위 블록에 구현하세요. */

static bool lazy_load_segment(struct page *page, void *aux)
{
	/* TODO: 파일에서 세그먼트를 로드합니다 */
	/* TODO: 주소 VA에서 첫 번째 페이지 폴트가 발생할 때 호출됩니다. */
	/* TODO: 이 함수를 호출할 때 VA를 사용할 수 있습니다. */
}

/* FILE의 오프셋 OFS에서 시작하는 세그먼트를 주소 UPAGE에 로드합니다.
 * 총 READ_BYTES + ZERO_BYTES 바이트의 가상 메모리가 다음과 같이 초기화됩니다:
 *
 * - UPAGE의 READ_BYTES 바이트는 FILE에서 오프셋 OFS부터 읽어야 합니다.
 *
 * - UPAGE + READ_BYTES의 ZERO_BYTES 바이트는 0으로 채워야 합니다.
 *
 * 이 함수에 의해 초기화된 페이지들은 WRITABLE이 true이면 사용자 프로세스가
 * 쓰기 가능해야 하고, 그렇지 않으면 읽기 전용이어야 합니다.
 *
 * 성공하면 true를, 메모리 할당 오류나 디스크 읽기 오류가 발생하면
 * false를 반환합니다. */
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage, uint32_t read_bytes,
			 uint32_t zero_bytes, bool writable)
{
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(upage) == 0);
	ASSERT(ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* 이 페이지를 어떻게 채울지 계산합니다.
		 * FILE에서 PAGE_READ_BYTES 바이트를 읽고
		 * 마지막 PAGE_ZERO_BYTES 바이트를 0으로 채웁니다. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: lazy_load_segment에 정보를 전달하기 위해 aux를
		 * 설정합니다. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer(VM_ANON, upage, writable, lazy_load_segment,
						    aux))
			return false;

		/* 진행합니다. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* USER_STACK에 스택의 PAGE를 생성합니다. 성공하면 true를 반환합니다. */
static bool setup_stack(struct intr_frame *if_)
{
	bool success = false;
	void *stack_bottom = (void *)(((uint8_t *)USER_STACK) - PGSIZE);

	/* TODO: stack_bottom에 스택을 매핑하고 페이지를 즉시 요구합니다.
	 * TODO: 성공하면, 그에 따라 rsp를 설정합니다.
	 * TODO: 페이지를 스택으로 표시해야 합니다. */
	/* TODO: 여기에 코드를 작성하세요 */

	return success;
}
#endif /* VM */
