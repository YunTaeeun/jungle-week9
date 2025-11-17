#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/process.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

// syscall.c: 시스템콜 처리를 담당하는 파일

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
bool is_valid_buffer(const void *buffer, unsigned length);

static struct intr_frame *current_syscall_frame;

/* 시스템 콜 처리
 *
 * 과거에는 시스템 콜을 인터럽트 핸들러로 처리했습니다 (예: 리눅스의 int 0x80).
 * 하지만 x86-64에서는 제조사가 더 효율적인 방법인 `syscall` 명령어를
 * 제공합니다.
 *
 * syscall 명령어는 MSR(Model Specific Register)의 값을 읽어서 동작합니다.
 * 자세한 내용은 매뉴얼을 참고하세요. */

#define MSR_STAR 0xc0000081  /* 세그먼트 셀렉터 MSR */
#define MSR_LSTAR 0xc0000082 /* Long 모드에서 SYSCALL의 목적지 주소 */
#define MSR_SYSCALL_MASK 0xc0000084 /* eflags 레지스터 마스크 */

void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
				((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* syscall_entry가 유저랜드 스택을 커널 모드 스택으로 교체하기 전까지는
	 * 인터럽트 서비스 루틴이 어떤 인터럽트도 처리하면 안 됩니다.
	 * 따라서 FLAG_FL 등의 플래그를 마스킹합니다. */
	write_msr(MSR_SYSCALL_MASK,
		  FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* 시스템 콜의 메인 인터페이스 */
void syscall_handler(struct intr_frame *f UNUSED)
{
	// 시스템 콜의 반환값은 RAX 레지스터에 저장되어야 합니다.
	// Pintos에서는 struct intr_frame의 rax 필드에 값을 쓰면 됩니다.

	// RAX 레지스터에 시스템 콜 번호가 저장되어 있음
	int syscall_number = f->R.rax;
	// printf("[SYSCALL_HANDLER] syscall_number=%d\n", syscall_number);

	// 인자 가져오기
	uint64_t arg1 = f->R.rdi;
	uint64_t arg2 = f->R.rsi;
	uint64_t arg3 = f->R.rdx;
	uint64_t arg4 = f->R.r10;

	switch (syscall_number) {
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit((int)arg1);
		break;
	case SYS_FORK:
		current_syscall_frame = f;
		pid_t pid = fork((const char *)arg1);
		f->R.rax = pid; // 부모 프로세스의 입장에서 반환값을 pid로 설정
		break;
	case SYS_EXEC:
		f->R.rax = exec((const char *)arg1);
		// exec 성공시 do_iret으로 새 프로그램으로 전환되어 여기로
		// 돌아오지 않음 실패시에만 -1을 반환하고 여기로 돌아옴
		break;
	case SYS_WAIT: // TODO:
		// wait 구현
		break;
	case SYS_CREATE:
		// create 구현
		break;
	case SYS_REMOVE:
		// remove 구현
		break;
	case SYS_OPEN:
		// open 구현
		break;
	case SYS_FILESIZE:
		// filesize(0);
		break;
	case SYS_READ:
		// read 구현
		break;
	case SYS_WRITE:
		f->R.rax = write((int)arg1, (const void *)arg2, (unsigned)arg3);
		break;
	case SYS_SEEK:
		// seek 구현
		break;
	case SYS_TELL:
		// tell 구현
		break;
	case SYS_CLOSE:
		// close 구현
		break;
	default:
		printf("Unknown system call: %d\n", syscall_number);
		break;
	}
	// printf("system call!\n");
}

int exec(const char *file)
{
	return process_exec(file);
}

// 시스템 종료
void halt(void)
{
	power_off();
}

// 프로세스 종료
void exit(int status)
{
	struct thread *curr = thread_current();
	curr->exit_status = status; // 종료 상태 저장 (wait에서 사용)

	// 스레드 종료
	printf("%s: exit(%d)\n", curr->name, status);
	thread_exit();
}

int write(int fd, const void *buffer, unsigned length)
{
	// 1. 버퍼 주소 검증
	if (!is_valid_buffer(buffer, length)) {
		exit(-1);
	}

	// 2. fd에 따라 분기.
	// fd = 1: console출력. 2= 표준 입력. invalid. write
	if (fd == STDOUT_FILENO) // fd == 1 : console 출력
	{
		putbuf((const char *)buffer, length);
		return length;
	} else if (fd == STDIN_FILENO) // fd == 0 : 표준 입력
	{
		// 입력이니까 쓸 수 없음
		return -1;
	} else if (fd < 0 || fd >= 128) // 유효하지 않은 fd 범위일 경우
	{
		return -1;
	} else {
		// 파일 write
		// TODO: 파일 디스크립터 테이블에서 file 가져온다
		// TODO: file_write()
		return -1;
	}
}

bool is_valid_user_memory(void *ptr)
{
	// 1. 널 포인터 체크
	if (ptr == NULL)
		return false;

	// 2. 커널 메모리 침범 여부 확인
	if (!is_user_vaddr(ptr))
		return false;

	// 3. 가상주소에 매핑된 물리주소가 있는지 체크
	// pml4_get_page에서 코드 시작 전 영역 (0x400000) 인지도 체크하고 있음
	if (pml4_get_page(thread_current()->pml4, ptr) == NULL)
		return false;

	return true; // 모든 검사 통과
}

bool is_valid_buffer(const void *buffer, unsigned length)
{
	const uint8_t *ptr = (const uint8_t *)buffer;

	// 버퍼 시작부터 끝까지 전부 체크
	for (unsigned i = 0; i < length; i++) {
		if (ptr + i == NULL || !(is_user_vaddr(ptr + i)) ||
		    pml4_get_page(thread_current()->pml4, ptr + i) == NULL) {
			return false;
		}
	}
	return true;
}

int filesize(int fd)
{
}

pid_t fork(const char *thread_name)
{
	tid_t pid = process_fork(thread_name, current_syscall_frame);

	if (pid == TID_ERROR)
		return -1;
	return pid;
}