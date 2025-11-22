#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "userprog/syscall.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "intrinsic.h"

// exception.c: 사용자 프로세스가 권한 밖 작업을 수행해서 발생하는 예외 처리

/* 처리된 페이지 폴트 횟수 */
static long long page_fault_cnt;

static void kill(struct intr_frame *);
static void page_fault(struct intr_frame *);

/* 유저 프로그램이 발생시킬 수 있는 인터럽트(예외)의 핸들러를 등록합니다.

   실제 Unix 계열 OS에서는 대부분의 인터럽트가 시그널(signal) 형태로
   유저 프로세스에게 전달되지만([SV-386] 3-24, 3-25 참조),
   Pintos는 시그널을 구현하지 않습니다. 대신, 예외 발생 시 단순히
   유저 프로세스를 종료시킵니다.

   페이지 폴트는 예외입니다. 현재는 다른 예외들과 동일하게 처리하지만,
   가상 메모리를 구현하려면 이 부분을 수정해야 합니다.

   각 예외에 대한 자세한 설명은 [IA32-v3a] 섹션 5.15
   "Exception and Interrupt Reference"를 참조하세요. */
void exception_init(void)
{
	/* 아래 예외들은 유저 프로그램이 명시적으로 발생시킬 수 있습니다.
   예: INT, INT3, INTO, BOUND 명령어 사용 시
   따라서 DPL==3으로 설정하여 유저 프로그램이 이 명령어들을 통해
   해당 예외를 호출할 수 있도록 허용합니다. */
	intr_register_int(3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
	intr_register_int(4, 3, INTR_ON, kill, "#OF Overflow Exception");
	intr_register_int(5, 3, INTR_ON, kill, "#BR BOUND Range Exceeded Exception");

	/* 아래 예외들은 DPL==0으로 설정되어 유저 프로세스가 INT 명령어로
	   직접 호출할 수 없습니다. 하지만 간접적으로는 발생할 수 있습니다.
	   예: #DE는 0으로 나누기를 시도하면 발생합니다. */
	intr_register_int(0, 0, INTR_ON, kill, "#DE Divide Error");
	intr_register_int(1, 0, INTR_ON, kill, "#DB Debug Exception");
	intr_register_int(6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
	intr_register_int(7, 0, INTR_ON, kill, "#NM Device Not Available Exception");
	intr_register_int(11, 0, INTR_ON, kill, "#NP Segment Not Present");
	intr_register_int(12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
	intr_register_int(13, 0, INTR_ON, kill, "#GP General Protection Exception");
	intr_register_int(16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
	intr_register_int(19, 0, INTR_ON, kill, "#XF SIMD Floating-Point Exception");

	/* 대부분의 예외는 인터럽트를 켠 상태(INTR_ON)에서 처리할 수 있습니다.
	   하지만 페이지 폴트는 인터럽트를 꺼야(INTR_OFF) 합니다.
	   이유: 폴트 주소가 CR2 레지스터에 저장되는데, 다른 인터럽트가 발생하면
	   CR2 값이 바뀔 수 있으므로 보존하기 위해 인터럽트를 비활성화합니다. */
	intr_register_int(14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* 예외 통계를 출력합니다. */
void exception_print_stats(void)
{
	printf("Exception: %lld page faults\n", page_fault_cnt);
}

/* (아마도) 유저 프로세스가 발생시킨 예외를 처리하는 핸들러입니다. */
static void kill(struct intr_frame *f)
{
	/* 이 인터럽트는 (아마도) 유저 프로세스가 발생시킨 것입니다.
	   예: 매핑되지 않은 가상 메모리에 접근 시도 (페이지 폴트)
	   현재는 단순히 유저 프로세스를 종료시킵니다.
	   나중에는 커널에서 페이지 폴트를 처리하도록 수정해야 합니다.
	   실제 Unix 계열 OS는 대부분의 예외를 시그널 형태로 프로세스에 전달하지만,
	   Pintos는 시그널을 구현하지 않습니다. */

	/* 인터럽트 프레임의 코드 세그먼트 값을 보면 예외가 어디서 발생했는지 알 수 있습니다. */
	switch (f->cs) {
	case SEL_UCSEG:
		/* 유저 코드 세그먼트이므로, 예상대로 유저 예외입니다.
		   유저 프로세스를 종료합니다. */
		exit(-1);

	case SEL_KCSEG:
		/* 커널 코드 세그먼트이므로, 이는 커널 버그를 의미합니다.
		   커널 코드는 예외를 발생시키면 안 됩니다. (페이지 폴트는
		   커널 예외를 발생시킬 수 있지만, 여기로 오면 안 됩니다.)
		   커널을 패닉시켜 문제를 명확히 합니다. */
		intr_dump_frame(f);
		PANIC("Kernel bug - unexpected interrupt in kernel");

	default:
		/* 다른 코드 세그먼트? 발생하면 안 됩니다.
		   커널을 패닉시킵니다. */
		printf("Interrupt %#04llx (%s) in unknown segment %04x\n", f->vec_no,
		       intr_name(f->vec_no), f->cs);
		thread_exit();
	}
}

/* 페이지 폴트 핸들러입니다. 가상 메모리를 구현하려면 이 함수를 채워야 합니다.
   Project 2의 일부 솔루션도 이 코드를 수정해야 할 수 있습니다.

   함수 진입 시:
   - 폴트가 발생한 주소는 CR2(Control Register 2)에 저장되어 있습니다.
   - 폴트에 대한 정보는 f->error_code 멤버에 저장되어 있으며,
     exception.h의 PF_* 매크로에 설명된 형식으로 포맷되어 있습니다.
   - 아래 예시 코드는 해당 정보를 파싱하는 방법을 보여줍니다.

   자세한 정보는 [IA32-v3a] 섹션 5.15 "Exception and Interrupt Reference"의
   "Interrupt 14--Page Fault Exception (#PF)" 설명을 참조하세요. */
static void page_fault(struct intr_frame *f)
{
	bool not_present; /* True: 페이지 부재, false: 읽기 전용 페이지에 쓰기 시도 */
	bool write;	  /* True: 쓰기 접근, false: 읽기 접근 */
	bool user;	  /* True: 유저 모드 접근, false: 커널 모드 접근 */
	void *fault_addr; /* 폴트가 발생한 주소 */

	/* 폴트를 발생시킨 가상 주소를 가져옵니다.
	   이 주소는 코드 영역이나 데이터 영역을 가리킬 수 있습니다.
	   주의: 이 주소가 폴트를 일으킨 명령어의 주소는 아닙니다
	   (명령어 주소는 f->rip에 있음). */

	fault_addr = (void *)rcr2();

	/* 인터럽트를 다시 활성화합니다.
	   (인터럽트를 비활성화했던 이유는 CR2가 변경되기 전에
	   확실하게 읽기 위해서였습니다.) */
	intr_enable();

	/* 폴트 발생 원인을 파악합니다. */
	not_present = (f->error_code & PF_P) == 0;
	write = (f->error_code & PF_W) != 0;
	user = (f->error_code & PF_U) != 0;

#ifdef VM
	/* Project 3 이후에 사용됩니다. */
	if (vm_try_handle_fault(f, fault_addr, user, write, not_present))
		return;
#endif

	/* 페이지 폴트 횟수를 증가시킵니다. */
	page_fault_cnt++;

	/* 실제 폴트인 경우, 정보를 출력하고 종료합니다. */
	printf("Page fault at %p: %s error %s page in %s context.\n", fault_addr,
	       not_present ? "not present" : "rights violation", write ? "writing" : "reading",
	       user ? "user" : "kernel");
	kill(f);
}
