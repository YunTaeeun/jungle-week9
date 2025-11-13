#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "threads/init.h"
#include "threads/pte.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "intrinsic.h"

/* Page Directory를 순회하여 가상 주소에 해당하는 Page Table Entry를 찾는 함수 */
static uint64_t *
pgdir_walk (uint64_t *pdp, const uint64_t va, int create) {
	int idx = PDX (va);  // 가상 주소에서 Page Directory 인덱스 추출 (비트 21-29)
	if (pdp) {  // Page Directory가 존재하면
		uint64_t *pte = (uint64_t *) pdp[idx];  // 해당 인덱스의 엔트리 가져오기
		if (!((uint64_t) pte & PTE_P)) {  // Present 비트가 설정되지 않았으면
			if (create) {  // 생성 모드이면
				uint64_t *new_page = palloc_get_page (PAL_ZERO);  // 새 페이지 할당 (0으로 초기화)
				if (new_page)  // 할당 성공 시
					pdp[idx] = vtop (new_page) | PTE_U | PTE_W | PTE_P;  // 물리 주소 변환 후 플래그 설정 (User, Writable, Present)
				else  // 할당 실패 시
					return NULL;  // NULL 반환
			} else  // 생성 모드가 아니면
				return NULL;  // NULL 반환
		}
		return (uint64_t *) ptov (PTE_ADDR (pdp[idx]) + 8 * PTX (va));  // Page Table Entry 주소 반환 (물리 주소를 가상 주소로 변환)
	}
	return NULL;  // Page Directory가 없으면 NULL 반환
}

/* Page Directory Pointer Entry를 순회하여 Page Directory를 찾는 함수 */
static uint64_t *
pdpe_walk (uint64_t *pdpe, const uint64_t va, int create) {
	uint64_t *pte = NULL;  // Page Table Entry 포인터
	int idx = PDPE (va);  // 가상 주소에서 PDPE 인덱스 추출 (비트 30-38)
	int allocated = 0;  // 새 페이지 할당 여부 플래그
	if (pdpe) {  // PDPE가 존재하면
		uint64_t *pde = (uint64_t *) pdpe[idx];  // 해당 인덱스의 Page Directory Entry 가져오기
		if (!((uint64_t) pde & PTE_P)) {  // Present 비트가 설정되지 않았으면
			if (create) {  // 생성 모드이면
				uint64_t *new_page = palloc_get_page (PAL_ZERO);  // 새 페이지 할당
				if (new_page) {  // 할당 성공 시
					pdpe[idx] = vtop (new_page) | PTE_U | PTE_W | PTE_P;  // 물리 주소 변환 후 플래그 설정
					allocated = 1;  // 할당 플래그 설정
				} else  // 할당 실패 시
					return NULL;  // NULL 반환
			} else  // 생성 모드가 아니면
				return NULL;  // NULL 반환
		}
		pte = pgdir_walk (ptov (PTE_ADDR (pdpe[idx])), va, create);  // Page Directory로 재귀 호출
	}
	if (pte == NULL && allocated) {  // PTE를 찾지 못했고 새 페이지를 할당했으면
		palloc_free_page ((void *) ptov (PTE_ADDR (pdpe[idx])));  // 할당한 페이지 해제
		pdpe[idx] = 0;  // 엔트리 초기화
	}
	return pte;  // Page Table Entry 반환
}

/* Returns the address of the page table entry for virtual
 * address VADDR in page map level 4, pml4.
 * If PML4E does not have a page table for VADDR, behavior depends
 * on CREATE.  If CREATE is true, then a new page table is
 * created and a pointer into it is returned.  Otherwise, a null
 * pointer is returned. */
/* 페이지 맵 레벨 4(pml4)에서 가상 주소 VADDR에 대한 페이지 테이블 엔트리의 주소를 반환합니다.
 * PML4E가 VADDR에 대한 페이지 테이블을 가지고 있지 않으면, 동작은 CREATE에 따라 달라집니다.
 * CREATE가 true이면 새 페이지 테이블이 생성되고 그 안의 포인터가 반환됩니다.
 * 그렇지 않으면 null 포인터가 반환됩니다. */
uint64_t *
pml4e_walk (uint64_t *pml4e, const uint64_t va, int create) {
	uint64_t *pte = NULL;  // Page Table Entry 포인터
	int idx = PML4 (va);  // 가상 주소에서 PML4 인덱스 추출 (비트 39-47)
	int allocated = 0;  // 새 페이지 할당 여부 플래그
	if (pml4e) {  // PML4가 존재하면
		uint64_t *pdpe = (uint64_t *) pml4e[idx];  // 해당 인덱스의 Page Directory Pointer Entry 가져오기
		if (!((uint64_t) pdpe & PTE_P)) {  // Present 비트가 설정되지 않았으면
			if (create) {  // 생성 모드이면
				uint64_t *new_page = palloc_get_page (PAL_ZERO);  // 새 페이지 할당
				if (new_page) {  // 할당 성공 시
					pml4e[idx] = vtop (new_page) | PTE_U | PTE_W | PTE_P;  // 물리 주소 변환 후 플래그 설정
					allocated = 1;  // 할당 플래그 설정
				} else  // 할당 실패 시
					return NULL;  // NULL 반환
			} else  // 생성 모드가 아니면
				return NULL;  // NULL 반환
		}
		pte = pdpe_walk (ptov (PTE_ADDR (pml4e[idx])), va, create);  // PDPE로 재귀 호출
	}
	if (pte == NULL && allocated) {  // PTE를 찾지 못했고 새 페이지를 할당했으면
		palloc_free_page ((void *) ptov (PTE_ADDR (pml4e[idx])));  // 할당한 페이지 해제
		pml4e[idx] = 0;  // 엔트리 초기화
	}
	return pte;  // Page Table Entry 반환
}

/* Creates a new page map level 4 (pml4) has mappings for kernel
 * virtual addresses, but none for user virtual addresses.
 * Returns the new page directory, or a null pointer if memory
 * allocation fails. */
/* 커널 가상 주소에 대한 매핑은 있지만 사용자 가상 주소에 대한 매핑은 없는
 * 새로운 페이지 맵 레벨 4(pml4)를 생성합니다.
 * 새 페이지 디렉토리를 반환하거나, 메모리 할당이 실패하면 null 포인터를 반환합니다. */
uint64_t *
pml4_create (void) {
	uint64_t *pml4 = palloc_get_page (0);  // 새 페이지 할당 (플래그 없음)
	if (pml4)  // 할당 성공 시
		memcpy (pml4, base_pml4, PGSIZE);  // 기본 PML4(커널 매핑 포함)를 복사
	return pml4;  // 새 PML4 반환
}

/* Page Table의 모든 엔트리에 대해 함수를 적용하는 함수 */
static bool
pt_for_each (uint64_t *pt, pte_for_each_func *func, void *aux,
		unsigned pml4_index, unsigned pdp_index, unsigned pdx_index) {
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {  // 페이지 테이블의 모든 엔트리에 대해
		uint64_t *pte = &pt[i];  // i번째 Page Table Entry
		if (((uint64_t) *pte) & PTE_P) {  // Present 비트가 설정되어 있으면
			void *va = (void *) (((uint64_t) pml4_index << PML4SHIFT) |  // 가상 주소 재구성
								 ((uint64_t) pdp_index << PDPESHIFT) |
								 ((uint64_t) pdx_index << PDXSHIFT) |
								 ((uint64_t) i << PTXSHIFT));
			if (!func (pte, va, aux))  // 함수 호출 (실패 시 false 반환)
				return false;  // false 반환
		}
	}
	return true;  // 모든 엔트리 처리 성공
}

/* Page Directory의 모든 엔트리에 대해 함수를 적용하는 함수 */
static bool
pgdir_for_each (uint64_t *pdp, pte_for_each_func *func, void *aux,
		unsigned pml4_index, unsigned pdp_index) {
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {  // Page Directory의 모든 엔트리에 대해
		uint64_t *pte = ptov((uint64_t *) pdp[i]);  // 물리 주소를 가상 주소로 변환
		if (((uint64_t) pte) & PTE_P)  // Present 비트가 설정되어 있으면
			if (!pt_for_each ((uint64_t *) PTE_ADDR (pte), func, aux,  // Page Table로 재귀 호출
					pml4_index, pdp_index, i))
				return false;  // 실패 시 false 반환
	}
	return true;  // 모든 엔트리 처리 성공
}

/* Page Directory Pointer의 모든 엔트리에 대해 함수를 적용하는 함수 */
static bool
pdp_for_each (uint64_t *pdp,
		pte_for_each_func *func, void *aux, unsigned pml4_index) {
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {  // PDPE의 모든 엔트리에 대해
		uint64_t *pde = ptov((uint64_t *) pdp[i]);  // 물리 주소를 가상 주소로 변환
		if (((uint64_t) pde) & PTE_P)  // Present 비트가 설정되어 있으면
			if (!pgdir_for_each ((uint64_t *) PTE_ADDR (pde), func,  // Page Directory로 재귀 호출
					 aux, pml4_index, i))
				return false;  // 실패 시 false 반환
	}
	return true;  // 모든 엔트리 처리 성공
}

/* Apply FUNC to each available pte entries including kernel's. */
/* 커널을 포함한 모든 사용 가능한 pte 엔트리에 FUNC를 적용합니다. */
bool
pml4_for_each (uint64_t *pml4, pte_for_each_func *func, void *aux) {
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {  // PML4의 모든 엔트리에 대해
		uint64_t *pdpe = ptov((uint64_t *) pml4[i]);  // 물리 주소를 가상 주소로 변환
		if (((uint64_t) pdpe) & PTE_P)  // Present 비트가 설정되어 있으면
			if (!pdp_for_each ((uint64_t *) PTE_ADDR (pdpe), func, aux, i))  // PDPE로 재귀 호출
				return false;  // 실패 시 false 반환
	}
	return true;  // 모든 엔트리 처리 성공
}

/* Page Table을 파괴하고 모든 페이지를 해제하는 함수 */
static void
pt_destroy (uint64_t *pt) {
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {  // Page Table의 모든 엔트리에 대해
		uint64_t *pte = ptov((uint64_t *) pt[i]);  // 물리 주소를 가상 주소로 변환
		if (((uint64_t) pte) & PTE_P)  // Present 비트가 설정되어 있으면
			palloc_free_page ((void *) PTE_ADDR (pte));  // 해당 페이지 해제
	}
	palloc_free_page ((void *) pt);  // Page Table 자체 해제
}

/* Page Directory를 파괴하고 모든 하위 테이블을 해제하는 함수 */
static void
pgdir_destroy (uint64_t *pdp) {
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {  // Page Directory의 모든 엔트리에 대해
		uint64_t *pte = ptov((uint64_t *) pdp[i]);  // 물리 주소를 가상 주소로 변환
		if (((uint64_t) pte) & PTE_P)  // Present 비트가 설정되어 있으면
			pt_destroy (PTE_ADDR (pte));  // Page Table 파괴 (재귀 호출) - 물리 주소를 가상 주소로 변환
	}
	palloc_free_page ((void *) pdp);  // Page Directory 자체 해제
}

/* Page Directory Pointer를 파괴하고 모든 하위 테이블을 해제하는 함수 */
static void
pdpe_destroy (uint64_t *pdpe) {
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {  // PDPE의 모든 엔트리에 대해
		uint64_t *pde = ptov((uint64_t *) pdpe[i]);  // 물리 주소를 가상 주소로 변환
		if (((uint64_t) pde) & PTE_P)  // Present 비트가 설정되어 있으면
			pgdir_destroy ((void *) PTE_ADDR (pde));  // Page Directory 파괴 (재귀 호출)
	}
	palloc_free_page ((void *) pdpe);  // PDPE 자체 해제
}

/* Destroys pml4e, freeing all the pages it references. */
/* pml4e를 파괴하고 그것이 참조하는 모든 페이지를 해제합니다. */
void
pml4_destroy (uint64_t *pml4) {
	if (pml4 == NULL)  // PML4가 NULL이면
		return;  // 종료
	ASSERT (pml4 != base_pml4);  // 기본 PML4가 아니어야 함 (커널 페이지 테이블 보호)

	/* if PML4 (vaddr) >= 1, it's kernel space by define. */
	/* PML4 (vaddr) >= 1이면 정의상 커널 공간입니다. */
	uint64_t *pdpe = ptov ((uint64_t *) pml4[0]);  // 첫 번째 엔트리 (사용자 공간) 가져오기
	if (((uint64_t) pdpe) & PTE_P)  // Present 비트가 설정되어 있으면
		pdpe_destroy ((void *) PTE_ADDR (pdpe));  // PDPE 파괴 (재귀 호출)
	palloc_free_page ((void *) pml4);  // PML4 자체 해제
}

/* Loads page directory PD into the CPU's page directory base
 * register. */
/* 페이지 디렉토리 PD를 CPU의 페이지 디렉토리 베이스 레지스터에 로드합니다. */
void
pml4_activate (uint64_t *pml4) {
	lcr3 (vtop (pml4 ? pml4 : base_pml4));  // CR3 레지스터에 PML4의 물리 주소 설정 (pml4가 NULL이면 기본 PML4 사용)
}

/* Looks up the physical address that corresponds to user virtual
 * address UADDR in pml4.  Returns the kernel virtual address
 * corresponding to that physical address, or a null pointer if
 * UADDR is unmapped. */
/* pml4에서 사용자 가상 주소 UADDR에 해당하는 물리 주소를 조회합니다.
 * 해당 물리 주소에 해당하는 커널 가상 주소를 반환하거나,
 * UADDR이 매핑되지 않은 경우 null 포인터를 반환합니다. */
void *
pml4_get_page (uint64_t *pml4, const void *uaddr) {
	ASSERT (is_user_vaddr (uaddr));  // 사용자 가상 주소인지 확인

	uint64_t *pte = pml4e_walk (pml4, (uint64_t) uaddr, 0);  // Page Table Entry 찾기 (생성하지 않음)

	if (pte && (*pte & PTE_P))  // PTE가 존재하고 Present 비트가 설정되어 있으면
		return ptov (PTE_ADDR (*pte)) + pg_ofs (uaddr);  // 물리 주소를 커널 가상 주소로 변환 후 페이지 오프셋 추가
	return NULL;  // 매핑되지 않음
}

/* Adds a mapping in page map level 4 PML4 from user virtual page
 * UPAGE to the physical frame identified by kernel virtual address KPAGE.
 * UPAGE must not already be mapped. KPAGE should probably be a page obtained
 * from the user pool with palloc_get_page().
 * If WRITABLE is true, the new page is read/write;
 * otherwise it is read-only.
 * Returns true if successful, false if memory allocation
 * failed. */
/* 페이지 맵 레벨 4 PML4에 사용자 가상 페이지 UPAGE에서 커널 가상 주소 KPAGE로 식별되는
 * 물리 프레임으로의 매핑을 추가합니다.
 * UPAGE는 이미 매핑되어 있지 않아야 합니다. KPAGE는 아마도 palloc_get_page()로
 * 사용자 풀에서 얻은 페이지여야 합니다.
 * WRITABLE이 true이면 새 페이지는 읽기/쓰기입니다;
 * 그렇지 않으면 읽기 전용입니다.
 * 성공 시 true를 반환하고, 메모리 할당이 실패하면 false를 반환합니다. */
bool
pml4_set_page (uint64_t *pml4, void *upage, void *kpage, bool rw) {
	ASSERT (pg_ofs (upage) == 0);  // 사용자 페이지가 페이지 경계에 정렬되어 있는지 확인
	ASSERT (pg_ofs (kpage) == 0);  // 커널 페이지가 페이지 경계에 정렬되어 있는지 확인
	ASSERT (is_user_vaddr (upage));  // 사용자 가상 주소인지 확인
	ASSERT (pml4 != base_pml4);  // 기본 PML4가 아니어야 함

	uint64_t *pte = pml4e_walk (pml4, (uint64_t) upage, 1);  // Page Table Entry 찾기 (없으면 생성)

	if (pte)  // PTE를 찾았거나 생성했으면
		*pte = vtop (kpage) | PTE_P | (rw ? PTE_W : 0) | PTE_U;  // 물리 주소 변환 후 플래그 설정 (Present, Writable(조건부), User)
	return pte != NULL;  // 성공 여부 반환
}

/* Marks user virtual page UPAGE "not present" in page
 * directory PD.  Later accesses to the page will fault.  Other
 * bits in the page table entry are preserved.
 * UPAGE need not be mapped. */
/* 페이지 디렉토리 PD에서 사용자 가상 페이지 UPAGE를 "not present"로 표시합니다.
 * 이후 페이지에 대한 접근은 폴트가 발생합니다. 페이지 테이블 엔트리의 다른 비트는 보존됩니다.
 * UPAGE는 매핑될 필요가 없습니다. */
void
pml4_clear_page (uint64_t *pml4, void *upage) {
	uint64_t *pte;  // Page Table Entry 포인터
	ASSERT (pg_ofs (upage) == 0);  // 페이지가 페이지 경계에 정렬되어 있는지 확인
	ASSERT (is_user_vaddr (upage));  // 사용자 가상 주소인지 확인

	pte = pml4e_walk (pml4, (uint64_t) upage, false);  // Page Table Entry 찾기 (생성하지 않음)

	if (pte != NULL && (*pte & PTE_P) != 0) {  // PTE가 존재하고 Present 비트가 설정되어 있으면
		*pte &= ~PTE_P;  // Present 비트 제거
		if (rcr3 () == vtop (pml4))  // 현재 활성화된 PML4와 같으면
			invlpg ((uint64_t) upage);  // TLB에서 해당 페이지 무효화
	}
}

/* Returns true if the PTE for virtual page VPAGE in PML4 is dirty,
 * that is, if the page has been modified since the PTE was
 * installed.
 * Returns false if PML4 contains no PTE for VPAGE. */
/* PML4에서 가상 페이지 VPAGE에 대한 PTE가 dirty인 경우(즉, PTE가 설치된 이후
 * 페이지가 수정된 경우) true를 반환합니다.
 * PML4에 VPAGE에 대한 PTE가 없으면 false를 반환합니다. */
bool
pml4_is_dirty (uint64_t *pml4, const void *vpage) {
	uint64_t *pte = pml4e_walk (pml4, (uint64_t) vpage, false);  // Page Table Entry 찾기
	return pte != NULL && (*pte & PTE_D) != 0;  // PTE가 존재하고 Dirty 비트가 설정되어 있으면 true
}

/* Set the dirty bit to DIRTY in the PTE for virtual page VPAGE
 * in PML4. */
/* PML4에서 가상 페이지 VPAGE에 대한 PTE의 dirty 비트를 DIRTY로 설정합니다. */
void
pml4_set_dirty (uint64_t *pml4, const void *vpage, bool dirty) {
	uint64_t *pte = pml4e_walk (pml4, (uint64_t) vpage, false);  // Page Table Entry 찾기
	if (pte) {  // PTE가 존재하면
		if (dirty)  // dirty로 설정
			*pte |= PTE_D;  // Dirty 비트 설정
		else  // dirty 해제
			*pte &= ~(uint32_t) PTE_D;  // Dirty 비트 제거

		if (rcr3 () == vtop (pml4))  // 현재 활성화된 PML4와 같으면
			invlpg ((uint64_t) vpage);  // TLB에서 해당 페이지 무효화
	}
}

/* Returns true if the PTE for virtual page VPAGE in PML4 has been
 * accessed recently, that is, between the time the PTE was
 * installed and the last time it was cleared.  Returns false if
 * PML4 contains no PTE for VPAGE. */
/* PML4에서 가상 페이지 VPAGE에 대한 PTE가 최근에 접근되었는지(즉, PTE가 설치된 시점과
 * 마지막으로 지워진 시점 사이) 확인하고 true를 반환합니다.
 * PML4에 VPAGE에 대한 PTE가 없으면 false를 반환합니다. */
bool
pml4_is_accessed (uint64_t *pml4, const void *vpage) {
	uint64_t *pte = pml4e_walk (pml4, (uint64_t) vpage, false);  // Page Table Entry 찾기
	return pte != NULL && (*pte & PTE_A) != 0;  // PTE가 존재하고 Accessed 비트가 설정되어 있으면 true
}

/* Sets the accessed bit to ACCESSED in the PTE for virtual page
   VPAGE in PD. */
/* PD에서 가상 페이지 VPAGE에 대한 PTE의 accessed 비트를 ACCESSED로 설정합니다. */
void
pml4_set_accessed (uint64_t *pml4, const void *vpage, bool accessed) {
	uint64_t *pte = pml4e_walk (pml4, (uint64_t) vpage, false);  // Page Table Entry 찾기
	if (pte) {  // PTE가 존재하면
		if (accessed)  // accessed로 설정
			*pte |= PTE_A;  // Accessed 비트 설정
		else  // accessed 해제
			*pte &= ~(uint32_t) PTE_A;  // Accessed 비트 제거

		if (rcr3 () == vtop (pml4))  // 현재 활성화된 PML4와 같으면
			invlpg ((uint64_t) vpage);  // TLB에서 해당 페이지 무효화
	}
}
