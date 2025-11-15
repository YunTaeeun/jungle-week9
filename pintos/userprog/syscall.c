#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

// syscall.c: 시스템콜 처리를 담당하는 파일

void syscall_entry(void);
void syscall_handler(struct intr_frame*);
bool is_valid_buffer(const void* buffer, unsigned length);

/* 시스템 콜 처리
 *
 * 과거에는 시스템 콜을 인터럽트 핸들러로 처리했습니다 (예: 리눅스의 int 0x80).
 * 하지만 x86-64에서는 제조사가 더 효율적인 방법인 `syscall` 명령어를 제공합니다.
 *
 * syscall 명령어는 MSR(Model Specific Register)의 값을 읽어서 동작합니다.
 * 자세한 내용은 매뉴얼을 참고하세요. */

#define MSR_STAR 0xc0000081         /* 세그먼트 셀렉터 MSR */
#define MSR_LSTAR 0xc0000082        /* Long 모드에서 SYSCALL의 목적지 주소 */
#define MSR_SYSCALL_MASK 0xc0000084 /* eflags 레지스터 마스크 */

void syscall_init(void)
{
    write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 | ((uint64_t)SEL_KCSEG) << 32);
    write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

    /* syscall_entry가 유저랜드 스택을 커널 모드 스택으로 교체하기 전까지는
     * 인터럽트 서비스 루틴이 어떤 인터럽트도 처리하면 안 됩니다.
     * 따라서 FLAG_FL 등의 플래그를 마스킹합니다. */
    write_msr(MSR_SYSCALL_MASK, FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* 시스템 콜의 메인 인터페이스 */
void syscall_handler(struct intr_frame* f UNUSED)
{
    // 시스템 콜의 반환값은 RAX 레지스터에 저장되어야 합니다.
    // Pintos에서는 struct intr_frame의 rax 필드에 값을 쓰면 됩니다.

    // RAX 레지스터에 시스템 콜 번호가 저장되어 있음
    int syscall_number = f->R.rax;
    printf("[SYSCALL_HANDLER] syscall_number=%d\n", syscall_number);

    // 인자 가져오기
    uint64_t arg1 = f->R.rdi;
    uint64_t arg2 = f->R.rsi;
    uint64_t arg3 = f->R.rdx;
    uint64_t arg4 = f->R.r10;

    switch (syscall_number)
    {
        case SYS_HALT:
            halt();
            break;
        case SYS_EXIT:
            exit((int)arg1);
            break;
        case SYS_FORK:
            // fork 구현
            break;
        case SYS_EXEC:
            // exec 구현
            break;
        case SYS_WAIT:
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
        case SYS_FILESIZE:  // TODO:
            // filesize(0);
            break;
        case SYS_READ:
            // read 구현
            break;
        case SYS_WRITE:
            printf("[SYSCALL_SYS_WRITE] called: fd=%d, buffer=%p, length=%u\n", (int)arg1,
                   (void*)arg2, (unsigned)arg3);
            f->R.rax = write((int)arg1, (const void*)arg2, (unsigned)arg3);
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
    printf("system call!\n");
    printf("[SYSCALL_HANDLER] Finished handling syscall %d\n", syscall_number);
}

// 시스템 종료
void halt(void)
{
    power_off();
}

// 프로세스 종료
void exit(int status)
{
    struct thread* curr = thread_current();
    printf("[EXIT] Called with status=%d, thread=%s (tid=%d)\n", status, curr->name, curr->tid);

    // 종료 상태 저장 (wait에서 사용)
    curr->exit_status = status;

    // 스레드 종료
    printf("[EXIT] About to call thread_exit()\n");
    thread_exit();
    printf("[EXIT] After thread_exit() - THIS SHOULD NOT PRINT\n");
}

/* 검증 목록
❌ NULL pointer
❌ Kernel 영역 주소 (>= KERN_BASE)
❌ 매핑 안된 주소 (page table에 없음)
❌ Code 시작 전 영역 (< 0x400000)
*/
int write(int fd, const void* buffer, unsigned length)
{
    // 1. 버퍼 주소 검증
    if (!is_valid_buffer(buffer, length))
    {
        exit(-1);
    }

    // 2. fd에 따라 분기.
    // fd = 1: console출력. 2= 표준 입력. invalid. write
    if (fd == STDOUT_FILENO)  // fd == 1 : console 출력
    {
        putbuf((const char*)buffer, length);
        return length;
    }
    else if (fd == STDIN_FILENO)  // fd == 0 : 표준 입력
    {
        // 입력이니까 쓸 수 없음
        return -1;
    }
    else if (fd < 0 || fd >= 128)  // 유효하지 않은 fd 범위일 경우
    {
        return -1;
    }
    else
    {
        // 파일 write
        // TODO: 파일 디스크립터 테이블에서 file 가져온다
        // TODO: file_write()
        return -1;
    }
}

bool is_valid_buffer(const void* buffer, unsigned length)
{
    const uint8_t* ptr = (const uint8_t*)buffer;

    // 버퍼 시작부터 끝까지 전부 체크
    for (unsigned i = 0; i < length; i++)
    {
        if (ptr + i == NULL || !is_user_vaddr(ptr + i) ||
            pml4_get_page(thread_current()->pml4, buffer) == NULL)
        {
            return false;
        }
    }
    return true;
}

int filesize(int fd) {}
