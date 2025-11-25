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

/* Page offset (bits 0:12). */
#define PGSHIFT 0                       /* Index of first offset bit. */
#define PGBITS 12                       /* Number of offset bits. */
#define PGSIZE (1 << PGBITS)            /* Bytes in a page. */
#define PGMASK BITMASK(PGSHIFT, PGBITS) /* Page offset bits (0:12). */

/* Offset within a page. */
#define pg_ofs(va) ((uint64_t)(va)&PGMASK)

#define pg_no(va) ((uint64_t)(va) >> PGBITS)

/* Round up to nearest page boundary. */
#define pg_round_up(va) ((void *)(((uint64_t)(va) + PGSIZE - 1) & ~PGMASK))

/* Round down to nearest page boundary. */
#define pg_round_down(va) (void *)((uint64_t)(va) & ~PGMASK)

/* 커널 가상 주소 공간의 시작 주소 */
#define KERN_BASE LOADER_KERN_BASE

/* 사용자 스택이 시작되는 (최상단) 가상 주소 */
#define USER_STACK 0x47480000

/* 주소가 사용자 영역인지 확인 (커널 영역이 아니면 사용자 영역) */
#define is_user_vaddr(vaddr) (!is_kernel_vaddr((vaddr)))

/* 주소가 커널 영역인지 확인 */
#define is_kernel_vaddr(vaddr) ((uint64_t)(vaddr) >= KERN_BASE)

// FIXME: add checking
/* Returns kernel virtual address at which physical address PADDR
 *  is mapped. */
#define ptov(paddr) ((void *)(((uint64_t)paddr) + KERN_BASE))

/* Returns physical address at which kernel virtual address VADDR
 * is mapped. */
#define vtop(vaddr)                                \
    ({                                             \
        ASSERT(is_kernel_vaddr(vaddr));            \
        ((uint64_t)(vaddr) - (uint64_t)KERN_BASE); \
    })

#endif /* threads/vaddr.h */ /* include guard 종료 */
