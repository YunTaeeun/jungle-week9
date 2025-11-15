#ifndef THREADS_VADDR_H /* 중복 포함을 방지하는 include guard 시작 */
#define THREADS_VADDR_H

#include <debug.h>   /* ASSERT 등 디버그 매크로 */
#include <stdint.h>  /* 고정 폭 정수 타입 */
#include <stdbool.h> /* bool 타입 */

#include "threads/loader.h" /* 부트로더가 설정한 KERN_BASE 등을 사용 */

/* 가상 주소를 다루기 위한 함수 · 매크로 정의
 * (하드웨어 페이지 테이블 관련 매크로는 pte.h 참고) */

#define BITMASK(SHIFT, CNT) \
    (((1ul << (CNT)) - 1) << (SHIFT)) /* 특정 비트 범위를 1로 채운 마스크 생성 */

/* 페이지 오프셋 비트 범위 정의 (x86-64는 4KB 페이지) */
#define PGSHIFT 0                       /* 오프셋의 시작 비트 위치 */
#define PGBITS 12                       /* 오프셋이 차지하는 비트 수 */
#define PGSIZE (1 << PGBITS)            /* 한 페이지의 바이트 수 (4096) */
#define PGMASK BITMASK(PGSHIFT, PGBITS) /* 오프셋 비트만 추출하는 마스크 */

/* 가상 주소에서 페이지 내부 오프셋만 추출 */
#define pg_ofs(va) ((uint64_t)(va)&PGMASK)

/* 가상 주소를 페이지 번호(상위 비트)로 변환 */
#define pg_no(va) ((uint64_t)(va) >> PGBITS)

/* 주소를 다음 페이지 경계로 올림 */
#define pg_round_up(va) ((void *)(((uint64_t)(va) + PGSIZE - 1) & ~PGMASK))

/* 주소를 페이지 경계로 내림 */
#define pg_round_down(va) (void *)((uint64_t)(va) & ~PGMASK)

/* 커널 가상 주소 공간의 시작 주소 */
#define KERN_BASE LOADER_KERN_BASE

/* 사용자 스택이 시작되는 (최상단) 가상 주소 */
#define USER_STACK 0x47480000

/* 주소가 사용자 영역인지 확인 (커널 영역이 아니면 사용자 영역) */
#define is_user_vaddr(vaddr) (!is_kernel_vaddr((vaddr)))

/* 주소가 커널 영역인지 확인 */
#define is_kernel_vaddr(vaddr) ((uint64_t)(vaddr) >= KERN_BASE)

/* 물리 주소를 대응되는 커널 가상 주소로 변환
 * (커널은 물리 메모리를 1:1로 매핑하므로 KERN_BASE를 더하면 됨) */
#define ptov(paddr) ((void *)(((uint64_t)paddr) + KERN_BASE))

/* 커널 가상 주소를 물리 주소로 변환 (반대로 KERN_BASE를 뺌) */
#define vtop(vaddr)                                \
    ({                                             \
        ASSERT(is_kernel_vaddr(vaddr));            \
        ((uint64_t)(vaddr) - (uint64_t)KERN_BASE); \
    })

#endif /* threads/vaddr.h */ /* include guard 종료 */
