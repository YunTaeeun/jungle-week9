#ifndef THREADS_LOADER_H
#define THREADS_LOADER_H

/* Constants fixed by the PC BIOS. */
/* PC BIOS에 의해 고정된 상수들. */
/* PC BIOS는 부팅 시 특정 메모리 주소에 부트 로더를 로드합니다.
   이 주소들은 x86 아키텍처 표준에 따라 고정되어 있습니다. */

#define LOADER_BASE 0x7c00 /* Physical address of loader's base. */ /* 로더의 기본 물리 주소. */
/* BIOS가 부트 섹터를 로드하는 표준 물리 주소입니다.
   0x7c00은 x86 아키텍처에서 부트 로더가 시작되는 위치입니다.
   부팅 시 BIOS는 디스크의 첫 번째 섹터(512바이트)를 이 주소로 읽어옵니다. */

#define LOADER_END 0x7e00 /* Physical address of end of loader. */ /* 로더의 끝 물리 주소. */
/* 부트 로더의 끝 주소입니다.
   LOADER_BASE(0x7c00) + 512바이트 = 0x7e00
   부트 섹터는 정확히 512바이트 크기입니다. */

/* Physical address of kernel base. */
/* 커널의 기본 물리 주소. */
#define LOADER_KERN_BASE 0x8004000000
/* 커널이 로드되는 가상 주소입니다.
   이 주소는 커널 가상 메모리 공간의 시작점을 나타냅니다.
   x86-64 아키텍처에서 커널은 높은 주소 공간(0x8004000000 이상)에 매핑됩니다. */

/* Kernel virtual address at which all physical memory is mapped. */
/* 모든 물리 메모리가 매핑되는 커널 가상 주소. */
#define LOADER_PHYS_BASE 0x200000
/* 물리 메모리 0번지가 매핑되는 가상 주소입니다.
   커널은 물리 메모리를 직접 접근하기 위해 이 주소부터 시작하는 가상 주소 공간에
   물리 메모리를 1:1로 매핑합니다.
   예: 물리 주소 0x1000 → 가상 주소 0x201000 */

/* Multiboot infos */
/* Multiboot 정보 구조체의 주소들 */
/* Multiboot는 부트 로더와 OS 간의 표준 인터페이스입니다.
   부트 로더가 메모리 맵, 커널 위치 등의 정보를 특정 메모리 위치에 저장합니다. */
#define MULTIBOOT_INFO 0x7000  // Multiboot 정보 구조체의 시작 주소
/* Multiboot 정보 구조체가 저장되는 물리 주소입니다.
   부트 로더가 시스템 정보(메모리 맵, 커널 위치 등)를 이 주소에 저장합니다. */

#define MULTIBOOT_FLAG MULTIBOOT_INFO  // Multiboot 플래그 필드 (MULTIBOOT_INFO + 0)
/* MultIBOOT_INFO 구조체의 첫 번째 필드로, 어떤 정보가 유효한지를 나타내는 플래그입니다. */

#define MULTIBOOT_MMAP_LEN MULTIBOOT_INFO + 44  // 메모리 맵 길이 필드 (MULTIBOOT_INFO + 44)
/* Multiboot 정보 구조체 내에서 메모리 맵의 길이를 저장하는 필드의 오프셋입니다.
   메모리 맵은 시스템의 물리 메모리 레이아웃을 설명합니다. */

#define MULTIBOOT_MMAP_ADDR MULTIBOOT_INFO + 48  // 메모리 맵 주소 필드 (MULTIBOOT_INFO + 48)
/* Multiboot 정보 구조체 내에서 메모리 맵 데이터의 주소를 저장하는 필드의 오프셋입니다.
   메모리 맵 데이터는 실제 메모리 레이아웃 정보를 담고 있습니다. */

#define E820_MAP MULTIBOOT_INFO + 52  // E820 메모리 맵 시작 주소 (MULTIBOOT_INFO + 52)
/* E820 메모리 맵이 저장되는 주소입니다.
   E820은 x86 아키텍처에서 BIOS가 제공하는 메모리 맵 인터페이스입니다.
   각 메모리 영역의 시작 주소, 크기, 타입(사용 가능, 예약됨 등)을 저장합니다. */

#define E820_MAP4 MULTIBOOT_INFO + 56  // E820 메모리 맵 엔트리 개수 (MULTIBOOT_INFO + 56)
/* E820 메모리 맵의 엔트리 개수를 저장하는 주소입니다.
   메모리 맵은 여러 개의 연속된 메모리 영역으로 구성되며,
   각 영역은 하나의 엔트리로 표현됩니다. */

/* Important loader physical addresses. */
/* 중요한 로더 물리 주소들. */
/* 부트 섹터 내에서 명령줄 인자와 서명이 저장되는 위치입니다.
   메모리 레이아웃 (LOADER_BASE = 0x7c00부터):

   0x7c00: 부트 로더 코드 시작
   ...
   0x7dfe: LOADER_SIG (0xaa55 서명) - 2바이트
   0x7dfc: LOADER_ARGS 시작 (명령줄 인자) - 128바이트
   0x7d7c: LOADER_ARG_CNT (인자 개수) - 4바이트
   0x7e00: 부트 섹터 끝 */

#define LOADER_SIG \
    (LOADER_END - LOADER_SIG_LEN) /* 0xaa55 BIOS signature. */ /* 0xaa55 BIOS 서명. */
/* 부트 섹터의 마지막 2바이트에 저장되는 BIOS 서명입니다.
   값은 0xaa55로 고정되어 있으며, BIOS가 유효한 부트 섹터인지 확인하는 데 사용됩니다.
   계산: 0x7e00 - 2 = 0x7dfe */

#define LOADER_ARGS (LOADER_SIG - LOADER_ARGS_LEN) /* Command-line args. */ /* 명령줄 인자. */
/* 커널 명령줄 인자가 저장되는 물리 주소입니다.
   pintos 유틸리티가 이 위치에 null-terminated 문자열 배열로 인자를 저장합니다.
   최대 128바이트까지 저장 가능합니다.
   계산: 0x7dfe - 128 = 0x7d7e */

#define LOADER_ARG_CNT (LOADER_ARGS - LOADER_ARG_CNT_LEN) /* Number of args. */ /* 인자 개수. */
/* 명령줄 인자의 개수를 저장하는 물리 주소입니다.
   4바이트 정수(uint32_t)로 저장되며, LOADER_ARGS에 저장된 문자열의 개수를 나타냅니다.
   계산: 0x7d7e - 4 = 0x7d7a */

/* Sizes of loader data structures. */
/* 로더 데이터 구조체의 크기들. */
#define LOADER_SIG_LEN 2  // BIOS 서명의 크기 (바이트)
/* BIOS 서명은 2바이트입니다 (0xaa55).
   부트 섹터의 마지막 2바이트에 저장됩니다. */

#define LOADER_ARGS_LEN 128  // 명령줄 인자 버퍼의 크기 (바이트)
/* 명령줄 인자를 저장하는 버퍼의 크기입니다.
   최대 128바이트의 명령줄 인자를 저장할 수 있습니다.
   이는 pintos 유틸리티가 커널에 전달할 수 있는 최대 명령줄 길이를 제한합니다. */

#define LOADER_ARG_CNT_LEN 4  // 인자 개수 필드의 크기 (바이트)
/* 인자 개수를 저장하는 필드의 크기입니다.
   uint32_t 타입이므로 4바이트입니다.
   최대 2^32 - 1개의 인자를 저장할 수 있지만, 실제로는 LOADER_ARGS_LEN에 의해 제한됩니다. */

/* GDT selectors defined by loader.
   More selectors are defined by userprog/gdt.h. */
/* 로더에 의해 정의된 GDT 셀렉터들.
   더 많은 셀렉터는 userprog/gdt.h에 정의되어 있습니다. */
/* GDT (Global Descriptor Table)는 x86 아키텍처에서 세그먼트를 정의하는 테이블입니다.
   셀렉터는 GDT 내의 특정 세그먼트를 가리키는 인덱스입니다.
   각 셀렉터는 16비트 값이며, 하위 3비트는 특수 용도로 사용됩니다. */
#define SEL_NULL 0x00 /* Null selector. */ /* Null 셀렉터. */
/* GDT의 첫 번째 엔트리로, 항상 null 세그먼트를 가리킵니다.
   유효하지 않은 세그먼트 접근을 방지하기 위해 사용됩니다.
   모든 GDT는 첫 번째 엔트리를 null로 설정해야 합니다. */

#define SEL_KCSEG 0x08 /* Kernel code selector. */ /* 커널 코드 셀렉터. */
/* 커널 코드 세그먼트를 가리키는 셀렉터입니다.
   커널 코드는 이 세그먼트를 통해 실행됩니다.
   권한 레벨 0 (최고 권한)에서 실행됩니다. */

#define SEL_KDSEG 0x10 /* Kernel data selector. */ /* 커널 데이터 셀렉터. */
/* 커널 데이터 세그먼트를 가리키는 셀렉터입니다.
   커널의 데이터 접근에 사용됩니다.
   권한 레벨 0에서 접근 가능합니다. */

#define SEL_UDSEG 0x1B /* User data selector. */ /* 사용자 데이터 셀렉터. */
/* 사용자 데이터 세그먼트를 가리키는 셀렉터입니다.
   사용자 프로그램의 데이터 접근에 사용됩니다.
   권한 레벨 3 (최저 권한)에서 접근 가능합니다.
   하위 3비트(0x1B = 0b00011011)에서:
   - 비트 0-1: RPL (Requested Privilege Level) = 3
   - 비트 2: GDT/LDT 플래그 = 1 (GDT 사용) */

#define SEL_UCSEG 0x23 /* User code selector. */ /* 사용자 코드 셀렉터. */
/* 사용자 코드 세그먼트를 가리키는 셀렉터입니다.
   사용자 프로그램의 코드 실행에 사용됩니다.
   권한 레벨 3에서 실행됩니다.
   하위 3비트(0x23 = 0b00100011)에서:
   - 비트 0-1: RPL = 3
   - 비트 2: GDT/LDT 플래그 = 1 (GDT 사용) */

#define SEL_TSS 0x28 /* Task-state segment. */ /* Task-state 세그먼트. */
/* TSS (Task-State Segment)를 가리키는 셀렉터입니다.
   TSS는 태스크 전환 시 CPU 상태를 저장하는 데 사용됩니다.
   x86-64에서는 태스크 전환이 deprecated되었지만, TSS는 여전히
   권한 레벨 전환 시 스택 포인터를 찾는 데 사용됩니다. */

#define SEL_CNT 8 /* Number of segments. */ /* 세그먼트 개수. */
/* 로더에 의해 정의된 세그먼트의 총 개수입니다.
   현재 정의된 세그먼트:
   0x00: NULL
   0x08: Kernel Code
   0x10: Kernel Data
   0x18: (사용 안 함)
   0x1B: User Data
   0x23: User Code
   0x28: TSS
   (나머지는 userprog/gdt.h에서 정의) */

#endif /* threads/loader.h */
