#include "threads/palloc.h"
#include <bitmap.h>
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "threads/init.h"
#include "threads/loader.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* Page allocator.  Hands out memory in page-size (or
   page-multiple) chunks.  See malloc.h for an allocator that
   hands out smaller chunks.

   System memory is divided into two "pools" called the kernel
   and user pools.  The user pool is for user (virtual) memory
   pages, the kernel pool for everything else.  The idea here is
   that the kernel needs to have memory for its own operations
   even if user processes are swapping like mad.

   By default, half of system RAM is given to the kernel pool and
   half to the user pool.  That should be huge overkill for the
   kernel pool, but that's just fine for demonstration purposes. */
/* 페이지 할당자. 페이지 크기(또는 페이지 배수) 단위로 메모리를 할당합니다.
   더 작은 청크를 할당하는 할당자는 malloc.h를 참조하세요.

   시스템 메모리는 커널 풀과 사용자 풀이라는 두 개의 "풀"로 나뉩니다.
   사용자 풀은 사용자(가상) 메모리 페이지용이고, 커널 풀은 나머지 모든 것용입니다.
   여기서의 아이디어는 사용자 프로세스가 미친 듯이 스와핑을 하더라도
   커널이 자체 작업을 위한 메모리를 가져야 한다는 것입니다.

   기본적으로 시스템 RAM의 절반은 커널 풀에, 절반은 사용자 풀에 할당됩니다.
   커널 풀에는 엄청난 과다 할당이지만, 시연 목적에는 충분합니다. */

/* A memory pool. */
/* 메모리 풀 구조체 */
struct pool
{
    struct lock lock; /* Mutual exclusion. */            /* 상호 배제. */
    struct bitmap *used_map; /* Bitmap of free pages. */ /* 사용 중인 페이지의 비트맵. */
    uint8_t *base; /* Base of pool. */                   /* 풀의 기본 주소. */
};

/* Two pools: one for kernel data, one for user pages. */
/* 두 개의 풀: 하나는 커널 데이터용, 하나는 사용자 페이지용. */
static struct pool kernel_pool, user_pool;  // 커널 풀과 사용자 풀 정적 변수

/* Maximum number of pages to put in user pool. */
/* 사용자 풀에 넣을 최대 페이지 수. */
size_t user_page_limit = SIZE_MAX;  // 사용자 페이지 제한 (기본값: 최대값)
static void init_pool(struct pool *p, void **bm_base, uint64_t start,
                      uint64_t end);  // 풀 초기화 함수 선언

static bool page_from_pool(const struct pool *,
                           void *page);  // 페이지가 풀에 속하는지 확인하는 함수 선언

/* multiboot info */
/* 멀티부트 정보 구조체 */
struct multiboot_info
{
    uint32_t flags;        // 플래그
    uint32_t mem_low;      // 낮은 메모리
    uint32_t mem_high;     // 높은 메모리
    uint32_t __unused[8];  // 사용되지 않는 필드
    uint32_t mmap_len;     // 메모리 맵 길이
    uint32_t mmap_base;    // 메모리 맵 기본 주소
};

/* e820 entry */
/* e820 엔트리 구조체 */
struct e820_entry
{
    uint32_t size;    // 엔트리 크기
    uint32_t mem_lo;  // 메모리 주소 하위 32비트
    uint32_t mem_hi;  // 메모리 주소 상위 32비트
    uint32_t len_lo;  // 길이 하위 32비트
    uint32_t len_hi;  // 길이 상위 32비트
    uint32_t type;    // 메모리 타입
};

/* Represent the range information of the ext_mem/base_mem */
/* ext_mem/base_mem의 범위 정보를 나타내는 구조체 */
struct area
{
    uint64_t start;  // 시작 주소
    uint64_t end;    // 끝 주소
    uint64_t size;   // 크기
};

#define BASE_MEM_THRESHOLD 0x100000  // 기본 메모리 임계값 (1MB)
#define USABLE 1                     // 사용 가능한 메모리 타입
#define ACPI_RECLAIMABLE 3           // ACPI 재사용 가능한 메모리 타입
#define APPEND_HILO(hi, lo) \
    (((uint64_t)((hi)) << 32) + (lo))  // 상위/하위 32비트를 합쳐 64비트로 만드는 매크로

/* Iterate on the e820 entry, parse the range of basemem and extmem. */
/* e820 엔트리를 순회하며 basemem과 extmem의 범위를 파싱합니다. */
static void resolve_area_info(struct area *base_mem, struct area *ext_mem)
{
    struct multiboot_info *mb_info = ptov(MULTIBOOT_INFO);  // 멀티부트 정보를 가상 주소로 변환
    struct e820_entry *entries = ptov(mb_info->mmap_base);  // 메모리 맵 엔트리들을 가상 주소로 변환
    uint32_t i;                                             // 반복 변수

    for (i = 0; i < mb_info->mmap_len / sizeof(struct e820_entry); i++)
    {                                            // 모든 e820 엔트리 순회
        struct e820_entry *entry = &entries[i];  // 현재 엔트리 가져오기
        if (entry->type == ACPI_RECLAIMABLE || entry->type == USABLE)
        {  // 사용 가능하거나 재사용 가능한 메모리인 경우
            uint64_t start = APPEND_HILO(entry->mem_hi,
                                         entry->mem_lo);  // 시작 주소 계산 (상위/하위 32비트 결합)
            uint64_t size =
                APPEND_HILO(entry->len_hi, entry->len_lo);  // 크기 계산 (상위/하위 32비트 결합)
            uint64_t end = start + size;                    // 끝 주소 계산
            printf("%llx ~ %llx %d\n", start, end, entry->type);  // 디버그 출력

            struct area *area = start < BASE_MEM_THRESHOLD
                                    ? base_mem
                                    : ext_mem;  // 1MB 미만이면 base_mem, 이상이면 ext_mem

            // First entry that belong to this area.
            // 이 영역에 속하는 첫 번째 엔트리.
            if (area->size == 0)
            {  // 영역이 아직 초기화되지 않은 경우
                *area = (struct area){
                    // 구조체 초기화
                    .start = start,  // 시작 주소 설정
                    .end = end,      // 끝 주소 설정
                    .size = size,    // 크기 설정
                };
            }
            else
            {  // otherwise
                // otherwise
                // 그렇지 않은 경우
                // Extend start
                // 시작 주소 확장
                if (area->start > start)  // 현재 시작 주소가 더 앞이면
                    area->start = start;  // 시작 주소 업데이트
                // Extend end
                // 끝 주소 확장
                if (area->end < end)  // 현재 끝 주소가 더 뒤면
                    area->end = end;  // 끝 주소 업데이트
                // Extend size
                // 크기 확장
                area->size += size;  // 크기에 현재 엔트리 크기 추가
            }
        }
    }
}

/*
 * Populate the pool.
 * All the pages are manged by this allocator, even include code page.
 * Basically, give half of memory to kernel, half to user.
 * We push base_mem portion to the kernel as much as possible.
 */
/*
 * 풀을 채웁니다.
 * 모든 페이지는 이 할당자에 의해 관리되며, 코드 페이지도 포함됩니다.
 * 기본적으로 메모리의 절반을 커널에, 절반을 사용자에 할당합니다.
 * 가능한 한 base_mem 부분을 커널에 할당합니다.
 */
static void populate_pools(struct area *base_mem, struct area *ext_mem)
{
    extern char _end;  // 링커가 기록한 커널의 끝 (kernel.lds.S 참조)
    void *free_start =
        pg_round_up(&_end);  // 커널 끝을 페이지 경계로 올림 (사용 가능한 메모리 시작점)

    uint64_t total_pages = (base_mem->size + ext_mem->size) / PGSIZE;  // 전체 페이지 수 계산
    uint64_t user_pages = total_pages / 2 > user_page_limit
                              ?  // 전체의 절반이 제한보다 크면
                              user_page_limit
                              : total_pages / 2;  // 제한값 사용, 아니면 절반 사용
    uint64_t kern_pages = total_pages - user_pages;  // 커널 페이지 수 = 전체 - 사용자 페이지 수

    // Parse E820 map to claim the memory region for each pool.
    // 각 풀에 대한 메모리 영역을 요청하기 위해 E820 맵을 파싱합니다.
    enum
    {
        KERN_START,
        KERN,
        USER_START,
        USER
    } state = KERN_START;       // 상태 머신: 커널 시작, 커널, 사용자 시작, 사용자
    uint64_t rem = kern_pages;  // 남은 커널 페이지 수
    uint64_t region_start = 0, end = 0, start, size,
             size_in_pg;  // 영역 시작, 끝, 시작, 크기, 페이지 단위 크기

    struct multiboot_info *mb_info = ptov(MULTIBOOT_INFO);  // 멀티부트 정보를 가상 주소로 변환
    struct e820_entry *entries = ptov(mb_info->mmap_base);  // 메모리 맵 엔트리들을 가상 주소로 변환

    uint32_t i;  // 반복 변수
    for (i = 0; i < mb_info->mmap_len / sizeof(struct e820_entry); i++)
    {                                            // 모든 e820 엔트리 순회
        struct e820_entry *entry = &entries[i];  // 현재 엔트리 가져오기
        if (entry->type == ACPI_RECLAIMABLE || entry->type == USABLE)
        {  // 사용 가능하거나 재사용 가능한 메모리인 경우
            start = (uint64_t)ptov(
                APPEND_HILO(entry->mem_hi, entry->mem_lo));  // 시작 주소를 가상 주소로 변환
            size = APPEND_HILO(entry->len_hi, entry->len_lo);  // 크기 계산
            end = start + size;                                // 끝 주소 계산
            size_in_pg = size / PGSIZE;                        // 페이지 단위 크기 계산

            if (state == KERN_START)
            {                          // 커널 풀 시작 상태인 경우
                region_start = start;  // 영역 시작 주소 설정
                state = KERN;          // 커널 상태로 전환
            }

            switch (state)
            {               // 현재 상태에 따라 분기
                case KERN:  // 커널 풀 처리 중
                    if (rem > size_in_pg)
                    {                       // 남은 커널 페이지가 현재 영역보다 크면
                        rem -= size_in_pg;  // 남은 페이지 수 감소
                        break;              // 다음 엔트리로
                    }
                    // generate kernel pool
                    // 커널 풀 생성
                    init_pool(&kernel_pool,  // 커널 풀 초기화
                              &free_start, region_start,
                              start + rem * PGSIZE);  // 시작부터 필요한 만큼만
                    // Transition to the next state
                    // 다음 상태로 전환
                    if (rem == size_in_pg)
                    {  // 남은 페이지가 현재 영역과 정확히 같으면
                        rem = user_pages;  // 남은 페이지를 사용자 페이지 수로 설정
                        state = USER_START;  // 사용자 시작 상태로 전환
                    }
                    else
                    {                                         // 그렇지 않으면
                        region_start = start + rem * PGSIZE;  // 영역 시작을 현재 영역 내로 이동
                        rem = user_pages - size_in_pg + rem;  // 남은 사용자 페이지 수 계산
                        state = USER;                         // 사용자 상태로 전환
                    }
                    break;
                case USER_START:           // 사용자 풀 시작 상태
                    region_start = start;  // 영역 시작 주소 설정
                    state = USER;          // 사용자 상태로 전환
                    break;
                case USER:  // 사용자 풀 처리 중
                    if (rem > size_in_pg)
                    {  // 남은 사용자 페이지가 현재 영역보다 크면
                        rem -= size_in_pg;  // 남은 페이지 수 감소
                        break;              // 다음 엔트리로
                    }
                    ASSERT(rem == size);  // 남은 페이지가 크기와 같아야 함 (디버그 확인)
                    break;
                default:            // 예상치 못한 상태
                    NOT_REACHED();  // 도달하면 안 되는 지점
            }
        }
    }

    // generate the user pool
    // 사용자 풀 생성
    init_pool(&user_pool, &free_start, region_start, end);  // 사용자 풀 초기화

    // Iterate over the e820_entry. Setup the usable.
    // e820_entry를 순회합니다. 사용 가능한 페이지를 설정합니다.
    uint64_t usable_bound = (uint64_t)free_start;  // 사용 가능한 메모리 경계 (커널 끝 이후)
    struct pool *pool;                             // 현재 처리 중인 풀 포인터
    void *pool_end;                                // 풀의 끝 주소
    size_t page_idx, page_cnt;                     // 페이지 인덱스, 페이지 개수

    for (i = 0; i < mb_info->mmap_len / sizeof(struct e820_entry); i++)
    {                                            // 모든 e820 엔트리 다시 순회
        struct e820_entry *entry = &entries[i];  // 현재 엔트리 가져오기
        if (entry->type == ACPI_RECLAIMABLE || entry->type == USABLE)
        {  // 사용 가능하거나 재사용 가능한 메모리인 경우
            uint64_t start = (uint64_t)ptov(
                APPEND_HILO(entry->mem_hi, entry->mem_lo));  // 시작 주소를 가상 주소로 변환
            uint64_t size = APPEND_HILO(entry->len_hi, entry->len_lo);  // 크기 계산
            uint64_t end = start + size;                                // 끝 주소 계산

            // TODO: add 0x1000 ~ 0x200000, This is not a matter for now.
            // TODO: 0x1000 ~ 0x200000 추가, 지금은 문제가 되지 않습니다.
            // All the pages are unuable
            // 모든 페이지가 사용 불가능합니다
            if (end < usable_bound)  // 끝 주소가 사용 가능 경계보다 앞이면
                continue;            // 이 엔트리는 건너뛰기

            start = (uint64_t)pg_round_up(
                start >= usable_bound ? start : usable_bound);  // 시작 주소를 페이지 경계로 올림
        split:
            if (page_from_pool(&kernel_pool, (void *)start))  // 시작 주소가 커널 풀에 속하면
                pool = &kernel_pool;                          // 커널 풀 사용
            else if (page_from_pool(&user_pool, (void *)start))  // 시작 주소가 사용자 풀에 속하면
                pool = &user_pool;                               // 사용자 풀 사용
            else
                NOT_REACHED();  // 어느 풀에도 속하지 않으면 오류

            pool_end = pool->base + bitmap_size(pool->used_map) * PGSIZE;  // 풀의 끝 주소 계산
            page_idx = pg_no(start) - pg_no(pool->base);  // 시작 주소의 페이지 인덱스 계산
            if ((uint64_t)pool_end < end)
            {  // 풀의 끝이 엔트리의 끝보다 앞이면
                page_cnt = ((uint64_t)pool_end - start) / PGSIZE;  // 풀 끝까지의 페이지 개수 계산
                bitmap_set_multiple(pool->used_map, page_idx, page_cnt,
                                    false);  // 해당 페이지들을 사용 가능으로 표시
                start = (uint64_t)pool_end;  // 시작 주소를 풀 끝으로 이동
                goto split;                  // 다시 분할 처리 (다음 풀로 넘어가기)
            }
            else
            {  // 풀의 끝이 엔트리의 끝보다 뒤이거나 같으면
                page_cnt = ((uint64_t)end - start) / PGSIZE;  // 엔트리 끝까지의 페이지 개수 계산
                bitmap_set_multiple(pool->used_map, page_idx, page_cnt,
                                    false);  // 해당 페이지들을 사용 가능으로 표시
            }
        }
    }
}

/* Initializes the page allocator and get the memory size */
/* 페이지 할당자를 초기화하고 메모리 크기를 가져옵니다 */
uint64_t palloc_init(void)
{
    /* End of the kernel as recorded by the linker.
       See kernel.lds.S. */
    /* 링커가 기록한 커널의 끝.
       kernel.lds.S를 참조하세요. */
    extern char _end;                    // 링커 심볼: 커널의 끝 주소
    struct area base_mem = {.size = 0};  // 기본 메모리 영역 초기화
    struct area ext_mem = {.size = 0};   // 확장 메모리 영역 초기화

    resolve_area_info(&base_mem, &ext_mem);  // e820 맵을 파싱하여 메모리 영역 정보 해결
    printf("Pintos booting with: \n");       // 부팅 메시지 출력
    printf("\tbase_mem: 0x%llx ~ 0x%llx (Usable: %'llu kB)\n",  // 기본 메모리 정보 출력
           base_mem.start, base_mem.end, base_mem.size / 1024);
    printf("\text_mem: 0x%llx ~ 0x%llx (Usable: %'llu kB)\n",  // 확장 메모리 정보 출력
           ext_mem.start, ext_mem.end, ext_mem.size / 1024);
    populate_pools(&base_mem, &ext_mem);  // 메모리 풀 채우기
    return ext_mem.end;                   // 확장 메모리의 끝 주소 반환
}

/* Obtains and returns a group of PAGE_CNT contiguous free pages.
   If PAL_USER is set, the pages are obtained from the user pool,
   otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
   then the pages are filled with zeros.  If too few pages are
   available, returns a null pointer, unless PAL_ASSERT is set in
   FLAGS, in which case the kernel panics. */
/* PAGE_CNT개의 연속된 빈 페이지 그룹을 얻어 반환합니다.
   PAL_USER가 설정되면 페이지는 사용자 풀에서 얻고,
   그렇지 않으면 커널 풀에서 얻습니다. FLAGS에 PAL_ZERO가 설정되면
   페이지는 0으로 채워집니다. 사용 가능한 페이지가 너무 적으면
   null 포인터를 반환하며, FLAGS에 PAL_ASSERT가 설정된 경우는 제외하고
   이 경우 커널이 패닉합니다. */
void *palloc_get_multiple(enum palloc_flags flags, size_t page_cnt)
{
    struct pool *pool = flags & PAL_USER ? &user_pool : &kernel_pool;  // 플래그에 따라 풀 선택

    lock_acquire(&pool->lock);  // 풀의 락 획득 (동시성 제어)
    size_t page_idx = bitmap_scan_and_flip(pool->used_map, 0, page_cnt,
                                           false);  // 연속된 빈 페이지 찾기 및 사용 중으로 표시
    lock_release(&pool->lock);                      // 풀의 락 해제
    void *pages;                                    // 할당된 페이지의 주소

    if (page_idx != BITMAP_ERROR)                // 페이지를 찾은 경우
        pages = pool->base + PGSIZE * page_idx;  // 페이지 주소 계산
    else
        pages = NULL;  // 찾지 못한 경우 NULL

    if (pages)
    {                                             // 페이지를 성공적으로 할당한 경우
        if (flags & PAL_ZERO)                     // PAL_ZERO 플래그가 설정된 경우
            memset(pages, 0, PGSIZE * page_cnt);  // 페이지를 0으로 초기화
    }
    else
    {                                           // 페이지 할당 실패한 경우
        if (flags & PAL_ASSERT)                 // PAL_ASSERT 플래그가 설정된 경우
            PANIC("palloc_get: out of pages");  // 패닉 발생
    }

    return pages;  // 할당된 페이지 주소 반환 (실패 시 NULL)
}

/* Obtains a single free page and returns its kernel virtual
   address.
   If PAL_USER is set, the page is obtained from the user pool,
   otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
   then the page is filled with zeros.  If no pages are
   available, returns a null pointer, unless PAL_ASSERT is set in
   FLAGS, in which case the kernel panics. */
/* 단일 빈 페이지를 얻어 커널 가상 주소를 반환합니다.
   PAL_USER가 설정되면 페이지는 사용자 풀에서 얻고,
   그렇지 않으면 커널 풀에서 얻습니다. FLAGS에 PAL_ZERO가 설정되면
   페이지는 0으로 채워집니다. 사용 가능한 페이지가 없으면
   null 포인터를 반환하며, FLAGS에 PAL_ASSERT가 설정된 경우는 제외하고
   이 경우 커널이 패닉합니다. */
void *palloc_get_page(enum palloc_flags flags)
{
    return palloc_get_multiple(flags, 1);  // 단일 페이지 할당 (다중 페이지 할당 함수 호출)
}

/* Frees the PAGE_CNT pages starting at PAGES. */
/* PAGES에서 시작하는 PAGE_CNT개의 페이지를 해제합니다. */
void palloc_free_multiple(void *pages, size_t page_cnt)
{
    struct pool *pool;  // 페이지가 속한 풀
    size_t page_idx;    // 페이지 인덱스

    ASSERT(pg_ofs(pages) == 0);  // 페이지가 페이지 경계에 정렬되어 있는지 확인
    if (pages == NULL || page_cnt == 0)  // 페이지가 NULL이거나 개수가 0이면
        return;                          // 아무것도 하지 않고 반환

    if (page_from_pool(&kernel_pool, pages))     // 페이지가 커널 풀에 속하면
        pool = &kernel_pool;                     // 커널 풀 사용
    else if (page_from_pool(&user_pool, pages))  // 페이지가 사용자 풀에 속하면
        pool = &user_pool;                       // 사용자 풀 사용
    else
        NOT_REACHED();  // 어느 풀에도 속하지 않으면 오류

    page_idx = pg_no(pages) - pg_no(pool->base);  // 페이지 인덱스 계산

#ifndef NDEBUG
    memset(pages, 0xcc,
           PGSIZE * page_cnt);  // 디버그 모드에서 해제된 메모리를 0xcc로 채움 (사용 후 사용 감지)
#endif
    ASSERT(bitmap_all(pool->used_map, page_idx, page_cnt));  // 모든 페이지가 사용 중인지 확인
    bitmap_set_multiple(pool->used_map, page_idx, page_cnt,
                        false);  // 페이지들을 사용 가능으로 표시
}

/* Frees the page at PAGE. */
/* PAGE의 페이지를 해제합니다. */
void palloc_free_page(void *page)
{
    palloc_free_multiple(page, 1);  // 단일 페이지 해제 (다중 페이지 해제 함수 호출)
}

/* Initializes pool P as starting at START and ending at END */
/* 풀 P를 START에서 시작하여 END에서 끝나도록 초기화합니다 */
static void init_pool(struct pool *p, void **bm_base, uint64_t start, uint64_t end)
{
    /* We'll put the pool's used_map at its base.
       Calculate the space needed for the bitmap
       and subtract it from the pool's size. */
    /* 풀의 used_map을 기본 주소에 둡니다.
       비트맵에 필요한 공간을 계산하고
       풀의 크기에서 빼냅니다. */
    uint64_t pgcnt = (end - start) / PGSIZE;  // 풀의 페이지 개수 계산
    size_t bm_pages = DIV_ROUND_UP(bitmap_buf_size(pgcnt), PGSIZE) *
                      PGSIZE;  // 비트맵에 필요한 페이지 수 계산 (페이지 경계로 올림)

    lock_init(&p->lock);                                            // 풀의 락 초기화
    p->used_map = bitmap_create_in_buf(pgcnt, *bm_base, bm_pages);  // 비트맵을 버퍼에 생성
    p->base = (void *)start;                                        // 풀의 기본 주소 설정

    // Mark all to unusable.
    // 모든 페이지를 사용 불가능으로 표시합니다.
    bitmap_set_all(p->used_map, true);  // 모든 비트를 1로 설정 (사용 중으로 표시)

    *bm_base += bm_pages;  // 비트맵 버퍼 포인터를 비트맵 크기만큼 이동
}

/* Returns true if PAGE was allocated from POOL,
   false otherwise. */
/* PAGE가 POOL에서 할당된 것이면 true를 반환하고,
   그렇지 않으면 false를 반환합니다. */
static bool page_from_pool(const struct pool *pool, void *page)
{
    size_t page_no = pg_no(page);           // 페이지의 페이지 번호 계산
    size_t start_page = pg_no(pool->base);  // 풀의 시작 페이지 번호 계산
    size_t end_page = start_page + bitmap_size(pool->used_map);  // 풀의 끝 페이지 번호 계산
    return page_no >= start_page && page_no < end_page;  // 페이지가 풀 범위 내에 있는지 확인
}
