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

    // 인자 가져오기
    uint64_t arg1 = f->R.rdi;
    uint64_t arg2 = f->R.rsi;
    uint64_t arg3 = f->R.rdx;
    uint64_t arg3 = f->R.r10;

    switch (syscall_number)
    {
        case SYS_HALT:
            power_off();
            break;
        case SYS_EXIT:
            // exit 구현
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
            filesize(0);
            break;
        case SYS_READ:
            // read 구현
            break;
        case SYS_WRITE:
            write();
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
            thread_exit();
            break;
    }

    printf("system call!\n");
    thread_exit();
}

int write(int fd, const void* buffer, unsigned length)
{
    // char buf = 123;
    // write (0, &buf, 1);
}

int filesize(int fd) {}
