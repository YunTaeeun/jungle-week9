#ifndef THREADS_VADDR_H
#define THREADS_VADDR_H

#include <debug.h>
#include <stdint.h>
#include <stdbool.h>

#include "threads/loader.h"

/* 가상 주소를 다루기 위한 함수와 매크로
 *
 * x86 하드웨어 페이지 테이블 전용 함수와 매크로는 pte.h를 참고하세요. */
#define BITMASK(SHIFT, CNT) (((1ul << (CNT)) - 1) << (SHIFT))

/* 페이지 오프셋 (비트 0~12) */
#define PGSHIFT 0                       /* 오프셋의 첫 번째 비트 인덱스 */
#define PGBITS 12                       /* 오프셋 비트 개수 (12비트 = 4KB 페이지) */
#define PGSIZE (1 << PGBITS)            /* 한 페이지의 크기 (바이트 단위, 4096바이트) */
#define PGMASK BITMASK(PGSHIFT, PGBITS) /* 페이지 오프셋 비트 마스크 (비트 0~12) */

/* 페이지 내에서의 오프셋 추출 (주소의 하위 12비트) */
#define pg_ofs(va) ((uint64_t)(va) & PGMASK)

/* 페이지 번호 추출 (주소를 12비트 오른쪽으로 시프트) */
#define pg_no(va) ((uint64_t)(va) >> PGBITS)

/* 가장 가까운 페이지 경계로 올림 (주소가 페이지 경계가 아니면 다음 페이지의 시작으로) */
#define pg_round_up(va) ((void*)(((uint64_t)(va) + PGSIZE - 1) & ~PGMASK))

/* 가장 가까운 페이지 경계로 내림 (현재 페이지의 시작 주소로) */
#define pg_round_down(va) (void*)((uint64_t)(va) & ~PGMASK)

/* 커널 가상 주소 시작 위치 (기본값: 0x8004000000) */
#define KERN_BASE LOADER_KERN_BASE

/* 유저 스택 시작 주소 */
#define USER_STACK 0x47480000

/* 주어진 가상 주소가 유저 가상 주소인지 확인 (KERN_BASE보다 작으면 유저 영역) */
#define is_user_vaddr(vaddr) (!is_kernel_vaddr((vaddr)))

/* 주어진 가상 주소가 커널 가상 주소인지 확인 (KERN_BASE 이상이면 커널 영역) */
#define is_kernel_vaddr(vaddr) ((uint64_t)(vaddr) >= KERN_BASE)

// FIXME: 검증 로직 추가 필요
/* 물리 주소(PADDR)가 매핑되는 커널 가상 주소를 반환
 * Pintos는 커널 가상 메모리를 물리 메모리와 1:1로 매핑합니다.
 * (커널 가상 주소 = 물리 주소 + KERN_BASE) */
#define ptov(paddr) ((void*)(((uint64_t)paddr) + KERN_BASE))

/* 커널 가상 주소(VADDR)가 매핑되는 물리 주소를 반환
 * 커널 가상 주소만 사용 가능하며, 유저 가상 주소는 불가능합니다.
 * (물리 주소 = 커널 가상 주소 - KERN_BASE) */
#define vtop(vaddr)                                \
    ({                                             \
        ASSERT(is_kernel_vaddr(vaddr));            \
        ((uint64_t)(vaddr) - (uint64_t)KERN_BASE); \
    })

#endif /* threads/vaddr.h */
