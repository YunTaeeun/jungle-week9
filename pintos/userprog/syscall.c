#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/init.h"  // power_off() 함수를 위해 추가

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

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

void syscall_init(void)
{
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
    // TODO: Your implementation goes here.

    /* 시스템 콜 번호는 rax 레지스터에 저장됨 */
    int syscall_num = f->R.rax;

    switch (syscall_num)
    {
        case SYS_HALT:
            /* halt: 시스템 종료 */
            power_off();
            break;

        case SYS_EXIT:
            /* exit: 프로세스 종료 */
            {
                int status = f->R.rdi;  // 첫 번째 인자
                printf("%s: exit(%d)\n", thread_current()->name, status);
                thread_exit();
            }
            break;

        case SYS_WRITE:
            /* write: 파일에 쓰기 */
            {
                int fd = f->R.rdi;                      // 첫 번째 인자: 파일 디스크립터
                const void *buffer = (void *)f->R.rsi;  // 두 번째 인자: 버퍼
                unsigned size = f->R.rdx;               // 세 번째 인자: 크기

                if (fd == 1)
                {                          // stdout (콘솔 출력)
                    putbuf(buffer, size);  // 버퍼를 콘솔에 출력
                    f->R.rax = size;       // 반환값: 쓴 바이트 수
                }
                else
                {
                    f->R.rax = -1;  // 다른 fd는 아직 미구현
                }
            }
            break;

        default:
            /* 미구현 시스템 콜 */
            printf("system call! (num: %d)\n", syscall_num);
            thread_exit();
            break;
    }
}
