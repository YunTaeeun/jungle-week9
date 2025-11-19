#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "lib/string.h"
#include "threads/init.h"      /* power_off() */
#include "threads/malloc.h"    /* malloc() */
#include "userprog/process.h"  // 프로세스 관련 함수 사용을 위함
#include "threads/synch.h"     // 락 함수를 사용하기 위해 사용
#include "filesys/filesys.h"   // 파일 관련 함수 사용을 위함
#include "filesys/file.h"
#include "devices/input.h"  // 입력 관련 함수 사용을 위함

static struct lock filesys_lock;

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

static bool check_user_vaddr(const void *uaddr, bool writable);
static void check_valid_buffer(const void *buffer, size_t size, bool writable);
static void check_valid_string(const char *str);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

static void sys_halt(struct intr_frame *f);
static void sys_exit(struct intr_frame *f);
static void sys_fork(struct intr_frame *f);
static void sys_exec(struct intr_frame *f);
static void sys_wait(struct intr_frame *f);
static void sys_create(struct intr_frame *f);
static void sys_remove(struct intr_frame *f);
static void sys_open(struct intr_frame *f);
static void sys_filesize(struct intr_frame *f);
static void sys_read(struct intr_frame *f);
static void sys_write(struct intr_frame *f);
static void sys_seek(struct intr_frame *f);
static void sys_tell(struct intr_frame *f);
static void sys_close(struct intr_frame *f);

/* Optional extra credit stubs. */
static void sys_dup2(struct intr_frame *f);

void syscall_init(void)
{
    lock_init(&filesys_lock);  // 락 초기화
    write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 | ((uint64_t)SEL_KCSEG) << 32);
    write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

    /* The interrupt service rountine should not serve any interrupts
     * until the syscall_entry swaps the userland stack to the kernel
     * mode stack. Therefore, we masked the FLAG_FL. */
    write_msr(MSR_SYSCALL_MASK, FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
    if (f == NULL)
    {
        PANIC("syscall_handler: null frame");
    }

    switch (f->R.rax)
    {
        case SYS_HALT:
            sys_halt(f);
            break;
        case SYS_EXIT:
            sys_exit(f);
            break;
        case SYS_FORK:
            sys_fork(f);
            break;
        case SYS_EXEC:
            sys_exec(f);
            break;
        case SYS_WAIT:
            sys_wait(f);
            break;
        case SYS_CREATE:
            sys_create(f);
            break;
        case SYS_REMOVE:
            sys_remove(f);
            break;
        case SYS_OPEN:
            sys_open(f);
            break;
        case SYS_FILESIZE:
            sys_filesize(f);
            break;
        case SYS_READ:
            sys_read(f);
            break;
        case SYS_WRITE:
            sys_write(f);
            break;
        case SYS_SEEK:
            sys_seek(f);
            break;
        case SYS_TELL:
            sys_tell(f);
            break;
        case SYS_CLOSE:
            sys_close(f);
            break;
        case SYS_DUP2:
            sys_dup2(f);
            break;
        default:
            printf("unhandled system call: %lld\n", (long long)f->R.rax);
            thread_exit();
            break;
    }
}

/* 사용자 가상 주소의 유효성을 검증하는 함수
 *
 * 상황별 동작:
 * 1. writable == false (읽기만 필요한 경우)
 *    - 예: sys_write()의 사용자 버퍼, sys_exec()의 파일명 문자열
 *    - 검증: 주소가 유저 영역이고, 페이지가 매핑되어 있으면 통과
 *    - 쓰기 권한은 확인하지 않음 (읽기만 하면 되므로)
 *
 * 2. writable == true (쓰기도 필요한 경우)
 *    - 예: sys_read()의 목적지 버퍼 (커널이 데이터를 써야 함)
 *    - 검증: 주소가 유저 영역이고, 페이지가 매핑되어 있으며,
 *            쓰기 가능(PTE_W)한 페이지여야 통과
 *
 * 반환값:
 *   - true: 주소가 유효하고 요구사항(읽기/쓰기)을 만족
 *   - false: 주소가 유효하지 않거나 요구사항을 만족하지 않음
 *            (호출자는 즉시 프로세스를 종료해야 함)
 */
static bool check_user_vaddr(const void *uaddr, bool writable)
{
    // 1단계: 커널 영역 주소인지 확인 (커널 영역 주소는 절대 허용하지 않음)
    if (!is_user_vaddr(uaddr))
    {
        return false;
    }
    // 2단계: 페이지 테이블 엔트리 가져오기 (매핑 여부 확인)
    uint64_t *pte = pml4e_walk(thread_current()->pml4, (uint64_t)uaddr, 0);
    // 사용 가능한 페이지인지 확인
    if (pte == NULL)
    {
        return false;
    }
    // 3단계: 쓰기가 필요한 경우, 쓰기 권한(PTE_W) 확인
    // writable == false면 이 검사는 건너뜀 (읽기만 가능해도 OK)
    if (writable && !(*pte & PTE_W))
    {
        return false;  // 쓰기 불가능한 페이지 (읽기 전용)
    }
    return true;  // 모든 검증 통과
}

/* 버퍼 전체 영역의 유효성을 검증하는 함수
 *
 * 동작:
 * - 버퍼가 여러 페이지에 걸쳐 있을 수 있으므로, 각 페이지마다 검증
 * - 버퍼 시작 주소를 페이지 경계로 내림 (pg_round_down)
 * - 버퍼 끝 주소를 페이지 경계로 올림 (pg_round_up)
 * - 각 페이지의 시작 주소마다 check_user_vaddr() 호출
 *
 * 상황별 동작:
 * 1. writable == false (읽기만 필요한 경우)
 *    - 예: sys_write()의 사용자 버퍼
 *    - 검증: 각 페이지가 유저 영역이고 매핑되어 있으면 통과
 *
 * 2. writable == true (쓰기도 필요한 경우)
 *    - 예: sys_read()의 목적지 버퍼 (커널이 데이터를 써야 함)
 *    - 검증: 각 페이지가 유저 영역이고 매핑되어 있으며 쓰기 가능해야 통과
 *
 * 주의:
 * - 버퍼가 페이지 경계를 넘어가는 경우도 올바르게 처리
 * - 검증 실패 시 즉시 프로세스를 종료 (thread_exit())
 */
static void check_valid_buffer(const void *buffer, size_t size, bool writable)
{
    const uint8_t *addr = (const uint8_t *)pg_round_down(buffer);  // 버퍼 시작을 페이지 경계로 내림
    const uint8_t *end = (const uint8_t *)pg_round_up((const uint8_t *)buffer +
                                                      size);  // 버퍼 끝을 페이지 경계로 올림

    while (addr < end)  // 모든 페이지를 순회
    {
        if (!check_user_vaddr((const void *)addr, writable))  // 각 페이지의 유효성 검증
        {
            thread_current()->exit_status = -1;  // 검증 실패 시 exit_status 설정
            thread_exit();                       // 검증 실패 시 프로세스 종료
        }
        addr += PGSIZE;  // 다음 페이지로 이동
    }
}

/* NULL-종료 문자열의 유효성을 검증하는 함수
 *
 * 동작:
 * - 문자열의 각 바이트가 유효한 사용자 메모리인지 확인
 * - NULL 문자('\0')를 만날 때까지 계속 검사
 * - 페이지 경계를 넘어가는 문자열도 올바르게 처리
 *
 * 사용 예:
 * - sys_exec()의 cmd_line 인자 검증
 * - sys_open(), sys_create()의 파일명 검증
 */
static void check_valid_string(const char *str)
{
    // NULL 포인터 체크
    if (str == NULL)
    {
        thread_current()->exit_status = -1;  // NULL 포인터는 exit(-1)
        thread_exit();                       // NULL 포인터는 즉시 종료
    }

    // 문자열의 각 바이트를 순회하며 검증
    // NULL 문자를 만날 때까지 계속 확인
    for (const char *p = str;; p++)
    {
        // 각 바이트가 유효한 사용자 메모리인지 확인 (읽기만 필요)
        if (!check_user_vaddr(p, false))
        {
            thread_current()->exit_status = -1;  // 유효하지 않은 주소는 exit(-1)
            thread_exit();  // 유효하지 않은 주소면 프로세스 종료
        }

        // NULL 문자를 만나면 문자열 끝 (검증 완료)
        if (*p == '\0')
        {
            break;
        }
    }
}

static void sys_halt(struct intr_frame *f UNUSED)
{
    power_off();
}

static void sys_exit(struct intr_frame *f UNUSED)
{
    struct thread *t = thread_current();
    int status = f->R.rdi;  // 첫 번째 인자

    // exit_status 설정 및 출력 -> 프로세스 exit 에서 수행
    t->exit_status = status;
    thread_exit();  // 이 함수 내부에서 process_exit()이 호출됨
}

static void sys_fork(struct intr_frame *f UNUSED)
{
    // 첫 번째 인자 포크 할 새 스레드 이름
    const char *name = (const char *)f->R.rdi;
    // 문자열 유효성 검증
    check_valid_string(name);
    f->R.rax = process_fork(name, f);
}

static void sys_exec(struct intr_frame *f UNUSED)
{
    // 첫 번째 인자 exec 할 프로그램 이름
    const char *file = (const char *)f->R.rdi;
    // 문자열 유효성 검증
    check_valid_string(file);
    if (process_exec((void *)file) == -1)
    {  // 실행에 실패 하면 exit
        thread_current()->exit_status = -1;
        thread_exit();
    }
}

static void sys_wait(struct intr_frame *f UNUSED)
{
    // 첫 번째 인자 wait 할 프로그램 pid
    int pid_t = f->R.rdi;
    f->R.rax = process_wait(pid_t);
}

static void sys_create(struct intr_frame *f UNUSED)
{
    // 첫 번째 인자 생성할 파일 이름
    const char *file = (const char *)f->R.rdi;
    // 두 번째 인자 생성할 파일 크기
    unsigned initial_size = (unsigned)f->R.rsi;
    // 문자열 유효성 검증
    check_valid_string(file);
    // 성공 실패 여부 반환
    lock_acquire(&filesys_lock);
    f->R.rax = filesys_create(file, initial_size);
    lock_release(&filesys_lock);
}

static void sys_remove(struct intr_frame *f UNUSED)
{
    // 첫 번째 인자 삭제할 파일 이름
    const char *file = (const char *)f->R.rdi;
    // 문자열 유효성 검증
    check_valid_string(file);
    // 성공 실패 여부 반환
    lock_acquire(&filesys_lock);
    f->R.rax = filesys_remove(file);
    lock_release(&filesys_lock);
}

static void sys_open(struct intr_frame *f UNUSED)
{
    // 첫 번째 인자 열 파일 이름
    const char *file = (const char *)f->R.rdi;
    // 문자열 유효성 검증
    check_valid_string(file);
    // 현재 스레드의 fd에 등록 필요
    struct thread *t = thread_current();
    int i;
    for (i = 2; i < MAX_FD && t->fds[i] != NULL; i++)
        ;
    if (i == MAX_FD)
    {  // 빈칸이 없으면 -1 반환
        f->R.rax = -1;
        return;
    }
    // 찾은 빈칸에 넣고 그 칸 번호 반환환
    lock_acquire(&filesys_lock);
    struct file *opened = filesys_open(file);
    lock_release(&filesys_lock);
    if (opened != NULL)
    {
        t->fds[i] = opened;
        f->R.rax = i;
        return;
    }
    else
    {
        f->R.rax = -1;
        return;
    }
}

static void sys_filesize(struct intr_frame *f UNUSED)
{
    // 사이즈 확인할 파일
    int fd = f->R.rdi;
    struct thread *t = thread_current();
    // 확인할 파일이 있으면
    if (fd < 2 || fd >= MAX_FD || t->fds[fd] == NULL)
    {
        f->R.rax = -1;
        return;
    }
    lock_acquire(&filesys_lock);
    f->R.rax = file_length(t->fds[fd]);
    lock_release(&filesys_lock);
}

static void sys_read(struct intr_frame *f UNUSED)
{
    int fd = f->R.rdi;                // 첫 번째 인자: 파일 디스크립터
    void *buffer = (void *)f->R.rsi;  // 두 번째 인자: 버퍼
    unsigned length = f->R.rdx;       // 세 번째 인자: 길이
    // 버퍼 전체 영역의 유효성을 검증
    check_valid_buffer(buffer, length, true);

    struct thread *t = thread_current();
    if (fd == 0)
    {  // 일반 입력 상황 처리
        uint8_t *buf = (uint8_t *)buffer;
        unsigned i;
        for (i = 0; i < length; i++)
        {
            buf[i] = input_getc();
        }
        f->R.rax = i;
        return;
    }
    else if (fd < 0 || fd >= MAX_FD || t->fds[fd] == NULL)
    {  // 범위 오류시 -1
        f->R.rax = -1;
        return;
    }

    // 일반 읽기 수행
    lock_acquire(&filesys_lock);
    f->R.rax = file_read(t->fds[fd], buffer, length);
    lock_release(&filesys_lock);
}

static void sys_write(struct intr_frame *f UNUSED)
{
    int fd = f->R.rdi;                      // 첫 번째 인자: 파일 디스크립터
    const void *buffer = (void *)f->R.rsi;  // 두 번째 인자: 버퍼
    unsigned size = f->R.rdx;               // 세 번째 인자: 크기
    // 버퍼 전체 영역의 유효성을 검증
    check_valid_buffer(buffer, size, false);

    if (fd == 1)
    {
        putbuf(buffer, size);  // 버퍼를 콘솔에 출력
        f->R.rax = size;       // 반환값: 쓴 바이트 수
        return;
    }
    else if (fd >= 2 && fd < MAX_FD && thread_current()->fds[fd] != NULL)
    {
        lock_acquire(&filesys_lock);
        f->R.rax = file_write(thread_current()->fds[fd], buffer, size);
        lock_release(&filesys_lock);
        return;
    }
    f->R.rax = -1;
}

static void sys_seek(struct intr_frame *f UNUSED)
{
    int fd = f->R.rdi;
    unsigned position = f->R.rsi;
    struct thread *t = thread_current();

    if (fd < 2 || fd >= MAX_FD || t->fds[fd] == NULL)
    {
        return;
    }
    lock_acquire(&filesys_lock);
    file_seek(t->fds[fd], position);
    lock_release(&filesys_lock);
}

static void sys_tell(struct intr_frame *f UNUSED)
{
    int fd = f->R.rdi;
    struct thread *t = thread_current();

    if (fd < 2 || fd >= MAX_FD || t->fds[fd] == NULL)
    {
        f->R.rax = -1;
        return;
    }
    lock_acquire(&filesys_lock);
    f->R.rax = file_tell(t->fds[fd]);
    lock_release(&filesys_lock);
}

static void sys_close(struct intr_frame *f UNUSED)
{
    int fd = f->R.rdi;
    struct thread *t = thread_current();
    if (fd < 2 || fd >= MAX_FD || t->fds[fd] == NULL)
    {
        return;
    }
    // 파일 닫고
    lock_acquire(&filesys_lock);
    file_close(t->fds[fd]);
    lock_release(&filesys_lock);
    // 닫은 슬롯 초기화 해줘야함
    t->fds[fd] = NULL;
}

static void sys_dup2(struct intr_frame *f UNUSED)
{
    /* TODO: implement dup2 (optional) */
}