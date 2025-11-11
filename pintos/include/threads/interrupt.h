#ifndef THREADS_INTERRUPT_H
#define THREADS_INTERRUPT_H
/* 인터럽트 관련 기능을 선언하는 헤더입니다. */

#include <stdbool.h>
#include <stdint.h>
/* 표준 bool, 정수 타입 헤더 포함. */

/* Interrupts on or off? */
/* 인터럽트 상태를 나타내는 열거형. */
enum intr_level {
	INTR_OFF,             /* Interrupts disabled. */
	                        /* 인터럽트 비활성화. */
	INTR_ON               /* Interrupts enabled. */
	                        /* 인터럽트 활성화. */
};

enum intr_level intr_get_level (void);
enum intr_level intr_set_level (enum intr_level);
enum intr_level intr_enable (void);
enum intr_level intr_disable (void);
/* 인터럽트의 현재 상태를 확인·설정·활성화·비활성화하는 함수들. */

/* Interrupt stack frame. */
/* 인터럽트 스택 프레임 정의. */
struct gp_registers {
	uint64_t r15; /* r15 레지스터 값 */
	uint64_t r14; /* r14 레지스터 값 */
	uint64_t r13; /* r13 레지스터 값 */
	uint64_t r12; /* r12 레지스터 값 */
	uint64_t r11; /* r11 레지스터 값 */
	uint64_t r10; /* r10 레지스터 값 */
	uint64_t r9;  /* r9 레지스터 값 */
	uint64_t r8;  /* r8 레지스터 값 */
	/*x86-64의 함수 호출 규약에서 “두 번째 인자”를 RSI에 넣도록 정해져 있기 때문에 스레드 생성에서 인자를 여기에 넣음음*/
	uint64_t rsi; /* 소스 인덱스 레지스터 그냥 과거에 그랬던 거고 지금은 범용임 */
	uint64_t rdi; /* 목적지 인덱스 레지스터 그냥 과거에 그랬던 거고 지금은 범용임 */
	uint64_t rbp; /* 베이스 포인터 */
	uint64_t rdx; /* 데이터 레지스터 */
	uint64_t rcx; /* 카운터 레지스터 */
	uint64_t rbx; /* 베이스 레지스터 */
	uint64_t rax; /* 누산기 레지스터 */
} __attribute__((packed));
/* 일반 목적 레지스터를 저장하는 구조체로, 16바이트 정렬을 위한 패킹 속성을 가집니다. */

struct intr_frame {
	/* Pushed by intr_entry in intr-stubs.S.
	   These are the interrupted task's saved registers. */
	/* intr-stubs.S의 intr_entry에서 푸시하는 레지스터 저장 영역. */
	struct gp_registers R;
	uint16_t es;        /* 세그먼트 레지스터 es */
	uint16_t __pad1;    /* 정렬을 위한 패딩 */
	uint32_t __pad2;    /* 정렬을 위한 패딩 */
	uint16_t ds;        /* 세그먼트 레지스터 ds */
	uint16_t __pad3;    /* 정렬 패딩 */
	uint32_t __pad4;    /* 정렬 패딩 */
	/* Pushed by intrNN_stub in intr-stubs.S. */
	uint64_t vec_no; /* Interrupt vector number. */
	                    /* 인터럽트 벡터 번호. */
/* Sometimes pushed by the CPU,
   otherwise for consistency pushed as 0 by intrNN_stub.
   The CPU puts it just under `eip', but we move it here. */
	uint64_t error_code; /* 오류 코드 (필요 시 CPU가 제공) */
/* Pushed by the CPU.
   These are the interrupted task's saved registers. */
	uintptr_t rip;  /* 인터럽트 발생 시 명령 포인터 */
	uint16_t cs;    /* 코드 세그먼트 */
	uint16_t __pad5; /* 패딩 */
	uint32_t __pad6; /* 패딩 */
	uint64_t eflags; /* 플래그 레지스터 */
	uintptr_t rsp;  /* 스택 포인터 */
	uint16_t ss;    /* 스택 세그먼트 */
	uint16_t __pad7; /* 패딩 */
	uint32_t __pad8; /* 패딩 */
	/* cs, ss와 플래그, 스택 관련 레지스터를 저장하며, 정렬을 위한 패딩을 포함합니다. */
} __attribute__((packed));
/* 인터럽트 발생 시 CPU가 저장한 레지스터들과 오류 코드 등을 담는 구조체.
   패킹 속성으로 정확한 메모리 레이아웃을 유지합니다. */

typedef void intr_handler_func (struct intr_frame *);
/* 인터럽트 핸들러 함수 포인터 타입. */

void intr_init (void); /* 인터럽트 서브시스템 초기화 */
void intr_register_ext (uint8_t vec, intr_handler_func *, const char *name); /* 외부 인터럽트 핸들러 등록 */
void intr_register_int (uint8_t vec, int dpl, enum intr_level,
                        intr_handler_func *, const char *name); /* 내부 인터럽트 핸들러 등록 */
bool intr_context (void); /* 현재가 인터럽트 컨텍스트인지 확인 */
void intr_yield_on_return (void); /* 인터럽트 복귀 시 스레드 양보 */

void intr_dump_frame (const struct intr_frame *); /* 인터럽트 프레임을 출력 */
const char *intr_name (uint8_t vec); /* 벡터 번호에 해당하는 인터럽트 이름 */

#endif /* threads/interrupt.h */
