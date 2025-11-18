#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"

// filesys.c: 간단한 파일 시스템이 구현되어 있음. 제한사항 꼭 확인.
// 수정하지 말것! - gitbook project2/introduction.html 참고
/* 제한사항
 *		1. 내부 동기화 없음.
 *      동시 접근시 서로 간섭 발생하니 동기화를 사용해서 한 번의 하나의
 *프로세스만 파일 시스템 코드를 실행하도록 해야함
 * 		2. 파일 크기는 생성시에 고정됨.
 *		루트 디렉토리만 있고 하위 디렉토리 없음. 생성할 수 있는 파일
 *개수 제한됨
 *		3. 파일 데이터는 디스크에서 연속적으로 있어야 하고,
 *			오랜 사용 시 외부 단편화가 심각해질 수 있다.
 *		4. 파일 이름은 14까지만.
 *		5. 시스템이 작업 중에 크래시되면 디스크가 자동으로 복구
 *불가능하게 손상될 수 있음. 복구도구도 없음.
 */

/* 파일 시스템이 저장된 디스크 */
struct disk *filesys_disk;

static void do_format(void);

/* 파일 시스템 모듈을 초기화한다.
 * FORMAT이 true이면 파일 시스템을 재포맷한다. */
void filesys_init(bool format)
{
	filesys_disk = disk_get(0, 1);
	if (filesys_disk == NULL)
		PANIC("hd0:1 (hdb) not present, file system initialization "
		      "failed");

	inode_init();

#ifdef EFILESYS
	fat_init();

	if (format)
		do_format();

	fat_open();
#else
	/* 기본 파일 시스템 */
	free_map_init();

	if (format)
		do_format();

	free_map_open();
#endif
}

/* 파일 시스템 모듈을 종료하고, 아직 기록되지 않은 모든 데이터를
 * 디스크에 기록한다. */
void filesys_done(void)
{
	/* 기본 파일 시스템 */
#ifdef EFILESYS
	fat_close();
#else
	free_map_close();
#endif
}

/* NAME이라는 이름으로 주어진 INITIAL_SIZE 크기의 파일을 생성한다.
 * 성공하면 true를, 그렇지 않으면 false를 반환한다.
 * NAME이라는 이름의 파일이 이미 존재하거나
 * 내부 메모리 할당이 실패하면 실패한다. */
bool filesys_create(const char *name, off_t initial_size)
{
	// printf("[FILESYS_CREATE] Called with name='%s', size=%d\n", name,
	// initial_size);

	disk_sector_t inode_sector = 0;
	struct dir *dir = dir_open_root();

	// printf("[FILESYS_CREATE] dir_open_root() returned: %p\n", dir);
	if (dir == NULL) {
		printf("[FILESYS_CREATE] FAILED: dir is NULL\n");
		return false;
	}

	bool alloc_success = free_map_allocate(1, &inode_sector);
	// printf("[FILESYS_CREATE] free_map_allocate() returned: %d,
	// sector=%d\n", alloc_success, inode_sector);

	if (!alloc_success) {
		printf("[FILESYS_CREATE] FAILED: free_map_allocate failed\n");
		dir_close(dir);
		return false;
	}

	bool inode_success = inode_create(inode_sector, initial_size);
	// printf("[FILESYS_CREATE] inode_create() returned: %d\n",
	// inode_success);

	if (!inode_success) {
		printf("[FILESYS_CREATE] FAILED: inode_create failed\n");
		free_map_release(inode_sector, 1);
		dir_close(dir);
		return false;
	}

	bool dir_add_success = dir_add(dir, name, inode_sector);
	// printf("[FILESYS_CREATE] dir_add() returned: %d\n", dir_add_success);

	if (!dir_add_success) {
		// printf("[FILESYS_CREATE] FAILED: dir_add failed\n");
		free_map_release(inode_sector, 1);
		dir_close(dir);
		return false;
	}

	dir_close(dir);
	// printf("[FILESYS_CREATE] SUCCESS\n");
	return true;
}

/* 주어진 NAME을 가진 파일을 연다.
 * 성공하면 새 파일을 반환하고, 그렇지 않으면
 * null 포인터를 반환한다.
 * NAME이라는 이름의 파일이 존재하지 않거나
 * 내부 메모리 할당이 실패하면 실패한다. */
struct file *filesys_open(const char *name)
{
	struct dir *dir = dir_open_root();
	struct inode *inode = NULL;

	if (dir != NULL)
		dir_lookup(dir, name, &inode);
	dir_close(dir);

	return file_open(inode);
}

/* NAME이라는 이름의 파일을 삭제한다.
 * 성공하면 true를, 실패하면 false를 반환한다.
 * NAME이라는 이름의 파일이 존재하지 않거나
 * 내부 메모리 할당이 실패하면 실패한다.
 * 파일을 삭제해도 해당 파일이 열려 있다면 블록이 해제되지 않고,
 * 열려 있는 모든 스레드가 닫을 때까지 계속 접근할 수 있음 */
bool filesys_remove(const char *name)
{
	struct dir *dir = dir_open_root();
	bool success = dir != NULL && dir_remove(dir, name);
	dir_close(dir);

	return success;
}

/* 파일 시스템을 포맷한다. */
static void do_format(void)
{
	printf("Formatting file system...");

#ifdef EFILESYS
	/* FAT를 생성하고 디스크에 저장한다. */
	fat_create();
	fat_close();
#else
	free_map_create();		      // 섹터 0에 free map 생성
	if (!dir_create(ROOT_DIR_SECTOR, 16)) // 루트 디렉토리 생성
		PANIC("root directory creation failed");
	free_map_close();
#endif

	printf("done.\n");
}
