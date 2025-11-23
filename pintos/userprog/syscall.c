#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include <round.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/process.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/palloc.h"
#include "filesys/filesys.h"
#include "threads/synch.h"

// syscall.c: 시스템콜 처리를 담당하는 파일. 유저-커널 인터페이스.
void syscall_entry(void);
void syscall_handler(struct intr_frame *);
bool is_valid_user_memory(void *ptr);
bool is_valid_buffer(const void *buffer, unsigned length);
static char *copy_string_from_user_to_kernel(const char *ustr);

/* 시스템콜 함수 선언 */
void halt(void);
void exit(int status);
pid_t fork(const char *thread_name, struct intr_frame *f);
int exec(const char *file);
int wait(pid_t child_tid);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
void close(int fd);
int read(int fd, void *buffer, unsigned length);
int write(int fd, const void *buffer, unsigned length);
void seek(int fd, unsigned position);
unsigned tell(int fd);

// static struct intr_frame *current_syscall_frame;
struct lock filesys_lock;

#define FILE_NAME_MAX 14 /* filesys.c의 NAME_MAX와 동일 */

/* 시스템 콜 처리
 * syscall 명령어는 MSR(Model Specific Register)의 값을 읽어서 동작합니다.
 * 자세한 내용은 매뉴얼을 참고하세요. */

#define MSR_STAR 0xc0000081	    /* 세그먼트 셀렉터 MSR */
#define MSR_LSTAR 0xc0000082	    /* Long 모드에서 SYSCALL의 목적지 주소 */
#define MSR_SYSCALL_MASK 0xc0000084 /* eflags 레지스터 마스크 */

// TODO: 시스템콜 핸들러는 유저 포인터를 절대 직접 쓰지 않는다는 철학이 있다.
// 이 철학에 맞춰 각 시스템콜에 검증로직 추가.
void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 | ((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* syscall_entry가 유저랜드 스택을 커널 모드 스택으로 교체하기 전까지는
	 * 인터럽트 서비스 루틴이 어떤 인터럽트도 처리하면 안 됩니다.
	 * 따라서 FLAG_FL 등의 플래그를 마스킹합니다. */
	write_msr(MSR_SYSCALL_MASK, FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	lock_init(&filesys_lock);
	// create_fd_table(&fdt);
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
		//	current_syscall_frame = f; // 현재 프로세스의 CPU 레지스터 상태
		pid_t pid = fork((const char *)arg1, f);
		f->R.rax = pid; // 부모 프로세스의 입장에서 반환값을 pid로 설정
		break;
	case SYS_EXEC:
		// exec 성공시 do_iret으로 새 프로그램으로 전환되어 여기로
		// 돌아오지 않음 실패시에만 -1을 반환하고 여기로 돌아옴
		f->R.rax = exec((const char *)arg1);
		break;
	case SYS_WAIT:
		f->R.rax = wait((pid_t)arg1);
		break;
	case SYS_CREATE:
		f->R.rax = create((const char *)arg1, (unsigned)arg2);
		break;
	case SYS_REMOVE:
		f->R.rax = remove((const char *)arg1);
		break;
	case SYS_OPEN:
		f->R.rax = open((const char *)arg1);
		break;
	case SYS_FILESIZE:
		f->R.rax = filesize((int)arg1);
		break;
	case SYS_READ:
		f->R.rax = read((int)arg1, (void *)arg2, (unsigned)arg3);
		break;
	case SYS_WRITE:
		f->R.rax = write((int)arg1, (const void *)arg2, (unsigned)arg3);
		break;
	case SYS_SEEK:
		seek((int)arg1, (unsigned)arg2);
		break;
	case SYS_TELL:
		f->R.rax = tell((int)arg1);
		break;
	case SYS_CLOSE:
		close((int)arg1);
		break;
	default:
		printf("Unknown system call: %d\n", syscall_number);
		break;
	}
	// printf("system call!\n");
}

// Returns the position of the next byte to be read or written in open file fd,
// expressed in bytes from the beginning of the file.
unsigned tell(int fd)
{
	// 1. fd 검증
	if (fd < 3) // 유효하지 않은 fd (쓰기 전용)
		return 0;

	// 2. 파일 객체 가져오기
	struct file *f = thread_current()->fdt->files[fd];
	if (f == NULL)
		return -1;

	// 3. 구현되어 있는 함수 호출
	return (file_tell(f));
}

/**
 * 열린 파일 fd에 대해 다음에 읽거나 쓸 바이트의 위치를 position으로 변경합니다.
 * 이때 position은 파일의 시작부터 바이트 단위로 계산된 값입니다.
 */
// TODO: 프로젝트 2에선 테스트가 경쟁을 안 보기 때문에 굳이 lock을 안 걸어도 됩니다.
//  하지만 원칙상으론 필요하므로 나중엔 inode RW-lock을 씌워야 한다
void seek(int fd, unsigned position)
{
	// 1. fd 검증
	if (fd <= 2) // 유효하지 않은 fd (쓰기 전용), 음수 방어
		return;

	// 2. 파일 객체 가져오기
	struct file *f = thread_current()->fdt->files[fd];
	if (f == NULL)
		return;

	// 3. 구현되어 있는 함수 호출
	file_seek(f, position);
}

/**
 * fd로 열린 파일에서 size만큼 데이터를 읽어서 buffer에 저장합니다.
 * 실제로 읽은 바이트 수를 반환합니다. 파일의 끝까지 읽으면 0을 반환합니다.
 * 읽는 도중 문제가 생겨서 읽을 수 없으면 -1을 반환합니다. (단순히 파일 끝에 도달한 경우는 아님)
 * fd가 0이면 키보드 입력을 받아서 input_getc()를 사용해 데이터를 읽습니다.
 */
int read(int fd, void *buffer, unsigned length)
{
	// 1. fd 검증
	if (fd == 1 || fd == 2) // 유효하지 않은 fd (쓰기 전용)
		return -1;

	struct fd_table *fdt = thread_current()->fdt;
	if (fd >= fdt->capacity || fd < 0) // fd는 슬롯보다 크거나 음수일 수 없음
		return -1;
	if (fdt->files[fd] == NULL) // 빈 슬롯
		return -1;

	// 2. 버퍼 유저메모리인지 검증 (read/write는 페이지 할당 여부 체크 안함)
	if (buffer == NULL || !is_user_vaddr(buffer) || !is_valid_buffer(buffer, length)) {
		exit(-1);
	}

	// 3. fd = 0 처리
	if (fd == 0) // 키보드 입력
	{
		int count;
		for (count = 0; count < length; count++) {
			((char *)buffer)[count] = input_getc(); // FIXME: 추후 다시 공부
		}
		return count;
	}

	// 3. fd > 2 (유효한 파일인 경우)
	if (fd > 2) {
		lock_acquire(&filesys_lock);
		unsigned ready_bytes = file_read(fdt->files[fd], buffer, length);
		lock_release(&filesys_lock);
		// 바이트 체크
		if (ready_bytes == 0)
			return 0;
		else
			return ready_bytes; // 파일 중간까지 읽음
	}

	return -1; // 에러는 -1 리턴
}

/*
 * 잘못된 fd는 조용히 무시 (아무것도 안 함)
 * 치명적인 에러만 exit(-1) (예: 메모리 접근 위반) */
void close(int fd)
{
	struct fd_table *fdt = thread_current()->fdt;

	// 1. 범위 검증
	if (fd < 3 || fd >= fdt->capacity) {
		return;
	}
	// 2. fd의 슬롯이 NULL인 경우 (이미 닫힌 슬롯)
	if (fdt->files[fd] == NULL)
		return;

	// 3. 되감기
	if (fd < fdt->next_fd) {
		fdt->next_fd = fd;
	}

	// 4. 파일 객체 회수
	file_close(fdt->files[fd]);
	fdt->files[fd] = NULL;
}

/**
 * 각 프로세스는 자신만의 fd 테이블을 가진다.
 * 0, 1은 예약(stdin/stdout) → open은 3 이상부터 반환
 * 한 파일을 여러 번 열어도 별도 fd 생성 → 각각 독립적 close
 * fd 테이블은 커널 메모리에 있고,
 * syscall.c (또는 struct thread) 안에서 직접 관리
 * 성공하면 3 이상의 정수인 "파일 디스크립터(fd)"를 반환
 * 실패하면 -1 반환,  메모리 오류는 exit(-1)
 */
int open(const char *file)
{
	// [1] open함수
	// 0. 널포인터 검증 후 exit(-1)
	if (file == NULL)
		exit(-1);

	// 1. 보안을 위해 시작 주소 검증
	if (!is_valid_user_memory(file))
		exit(-1);

	// 2. 유저메모리에 있던 파일명을 커널메모리로 복사 (동적할당)
	char *kernel_file = copy_string_from_user_to_kernel(file); // const 제거 (free 위해)
	if (kernel_file == NULL)
		return -1;

	// 파일명 길이 체크 등 추가 검증
	if (strlen(kernel_file) == 0 || strlen(kernel_file) > FILE_NAME_MAX) {
		palloc_free_page(kernel_file);
		return -1;
	}

	// 3. 파일 열기
	lock_acquire(&filesys_lock);
	struct file *opened_file = filesys_open(kernel_file);
	lock_release(&filesys_lock);
	if (opened_file == NULL) {
		palloc_free_page(kernel_file);
		return -1;
	}

	// 4. capacity 확인

	struct fd_table *fdt = thread_current()->fdt;
	if (fdt->next_fd >= fdt->capacity) // capacity 가득참
	{
		// capacity * 2 만큼 realloc.
		// 실패시 file_close 로 롤백하고 return -1;
		int new_pages = DIV_ROUND_UP(fdt->capacity * 2 * sizeof(struct file *), PGSIZE);
		struct file **new = palloc_get_multiple(PAL_ZERO, new_pages);
		/* [수정] 메모리 할당 실패(OOM) 체크 */
		if (new == NULL) {
			file_close(opened_file);
			palloc_free_page(kernel_file);
			return -1;
		}

		memcpy(new, fdt->files, fdt->capacity * sizeof(struct file *));

		// 기존 페이지 free
		palloc_free_multiple(fdt->files,
				     DIV_ROUND_UP(fdt->capacity * sizeof(struct file *), PGSIZE));

		// 테이블 포인터 변경
		fdt->files = new;

		// capacity 변경
		fdt->capacity *= 2;
	}

	// 5. fd_table에 추가하고 fd를 돌려받는다. fd_next ++한다.
	int fd = fdt->next_fd;
	fdt->files[fd] = opened_file;
	(fdt->next_fd)++;

	// 6. 해당 fd번호를 반환한다.
	palloc_free_page(kernel_file);
	return fd;
}

bool remove(const char *file)
{
	// 0. 널포인터 검증 후 exit(-1)
	if (file == NULL)
		exit(-1);

	// 1. 보안을 위해 시작 주소 검증
	if (!is_valid_user_memory(file))
		exit(-1);

	// 2. 유저메모리에 있던 파일명을 커널메모리로 복사 (동적할당)
	const char *kernel_file = copy_string_from_user_to_kernel(file);
	if (kernel_file == NULL || (strlen(kernel_file) == 0) ||
	    (strlen(kernel_file) > FILE_NAME_MAX)) {
		palloc_free_page(kernel_file);
		return false;
	}

	// 3. 파일 삭제
	lock_acquire(&filesys_lock);
	bool result = filesys_remove(kernel_file);
	lock_release(&filesys_lock);

	// 4. 메모리 해제
	palloc_free_page(kernel_file);

	return result;
}

// 지정한 이름으로 size바이트 짜리 빈 파일을 디스크에 만들기. open 없음.
bool create(const char *file, unsigned initial_size)
{
	// 0. 널포인터 검증 후 exit(-1)
	if (file == NULL)
		exit(-1);

	// 1. 보안을 위해 시작 주소 검증
	if (!is_valid_user_memory(file))
		exit(-1);

	// 2. 유저메모리에 있던 파일명을 커널메모리로 복사 (동적할당)
	const char *kernel_file = copy_string_from_user_to_kernel(file);
	if (kernel_file == NULL || (strlen(kernel_file) == 0) ||
	    (strlen(kernel_file) > FILE_NAME_MAX)) {
		palloc_free_page(kernel_file);
		return false;
	}

	// 3. 파일 생성
	lock_acquire(&filesys_lock);
	bool result = filesys_create(kernel_file, initial_size);
	lock_release(&filesys_lock);

	// 4. 메모리 해제
	palloc_free_page(kernel_file);

	return result;
}

int wait(pid_t child_tid)
{
	return process_wait(child_tid);
}

int exec(const char *file)
{
	// 0. 널포인터 검증 후 exit(-1)
	if (file == NULL)
		exit(-1);

	// 1. 보안을 위해 시작 주소 검증
	if (!is_valid_user_memory(file))
		exit(-1);

	// 2. 유저메모리에 있던 파일명을 커널메모리로 복사 (동적할당)
	const char *kernel_file = copy_string_from_user_to_kernel(file);
	if (kernel_file == NULL || (strlen(kernel_file) == 0)) {
		palloc_free_page(kernel_file);
		return -1;
	}

	int result = process_exec(kernel_file);

	// 메모리 해제
	palloc_free_page(kernel_file);

	return result;
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
	/* [수정] running_info가 유효한지 확인 (NULL 체크 및 Sentinel 값 체크) */
	if (curr->running_info && curr->running_info != (struct child_info *)0xDEADBEEF) {
		curr->running_info->exit_status = status;
	}

	// 스레드 종료
	/* [수정] running_info가 NULL일 경우(예: initd) 안전하게 status 출력 */
	printf("%s: exit(%d)\n", curr->name, status);
	thread_exit();
}

int write(int fd, const void *buffer, unsigned length)
{
	// [검증1] 버퍼 주소 검증 (read/write는 페이지 할당 여부 체크 안함)
	if (buffer == NULL || !is_user_vaddr(buffer) || !is_valid_buffer(buffer, length)) {
		exit(-1);
	}

	// [검증2] fd 검증 : 입력 스트림에 쓰기 불가
	if (fd == STDIN_FILENO)
		return -1;

	// [검증2] fd 검증 : 콘솔 출력
	if (fd == STDOUT_FILENO) {
		putbuf((const char *)buffer, length);
		return length;
	}

	// fd >= 2 : 파일 쓰기
	struct fd_table *fdt = thread_current()->fdt;
	// fd 범위 검증
	if (fd < 0 || fd >= fdt->capacity)
		return -1;

	// 파일이 열려있는지 확인
	if (fdt->files[fd] == NULL)
		return -1;

	// 실제 파일에 쓰기
	lock_acquire(&filesys_lock);
	off_t written = file_write(fdt->files[fd], buffer, length);
	lock_release(&filesys_lock);

	// 실제 사용한 바이트 수 변환
	return written;
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

static char *copy_string_from_user_to_kernel(const char *user_str)
{
	if (user_str == NULL || !is_valid_user_memory(user_str))
		return NULL;

	char *kernel_str = palloc_get_page(0);
	if (kernel_str == NULL)
		return NULL;

	// 한 바이트씩 안전하게 복사
	size_t i;
	for (i = 0; i < PGSIZE; i++) {
		if (!is_user_vaddr(user_str + i)) {
			palloc_free_page(kernel_str);
			return NULL;
		}

		kernel_str[i] = user_str[i];

		if (kernel_str[i] == '\0')
			return kernel_str;
	}

	// 문자열이 너무 길거나(PGSIZE 초과) 널 종료문자가 없다
	palloc_free_page(kernel_str);
	return NULL;
}

int filesize(int fd)
{
	// fd 범위 검증
	struct fd_table *fdt = thread_current()->fdt;
	if (fd < 0 || fd >= fdt->capacity)
		return -1;

	// 파일이 열려있는지 확인
	if (fdt->files[fd] == NULL)
		return -1;

	// 파일 크기 반환
	lock_acquire(&filesys_lock);
	off_t size = file_length(fdt->files[fd]);
	lock_release(&filesys_lock);

	return size;
}

pid_t fork(const char *thread_name, struct intr_frame *f)
{
	// 0. 널포인터 검증 후 exit(-1)
	if (thread_name == NULL)
		exit(-1);

	// 1. 보안을 위해 시작 주소 검증
	if (!is_valid_user_memory(thread_name))
		exit(-1);

	// 2. 유저메모리에 있던 파일명을 커널메모리로 복사 (동적할당)
	const char *kernel_thread_name = copy_string_from_user_to_kernel(thread_name);
	if (kernel_thread_name == NULL || (strlen(kernel_thread_name) == 0)) {
		palloc_free_page(kernel_thread_name);
		return -1;
	}

	tid_t pid = process_fork(kernel_thread_name, f);
	palloc_free_page(kernel_thread_name);

	if (pid == TID_ERROR)
		return -1;

	return pid;
}