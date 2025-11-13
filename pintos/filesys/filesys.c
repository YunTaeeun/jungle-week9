#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"

// filesys.c: 간단한 파일 시스템이 구현되어 있음. 제한사항 꼭 확인. (수정하지 말 것)
/* 제한사항
 *		1. 내부 동기화 없음.
 *      동시 접근시 서로 간섭 발생하니 동기화를 사용해서 한 번의 하나의 프로세스만 파일 시스템
 *			코드를 실행하도록 해야함
 * 		2. 파일 크기는 생성시에 고정됨.
 *		루트 디렉토리만 있고 하위 디렉토리 없음. 생성할 수 있는 파일 개수 제한됨
 *		3. 파일 데이터는 디스크에서 연속적으로 있어야 하고,
 *			오랜 사용 시 외부 단편화가 심각해질 수 있다.
 *		4. 파일 이름은 14까지만.
 *		5. 시스템이 작업 중에 크래시되면 디스크가 자동으로 복구 불가능하게 손상될 수 있음.
 *			복구도구도 없음.
 */

/* The disk that contains the file system. */
struct disk* filesys_disk;

static void do_format(void);

/* Initializes the file system module.
 * If FORMAT is true, reformats the file system. */
void filesys_init(bool format)
{
    filesys_disk = disk_get(0, 1);
    if (filesys_disk == NULL) PANIC("hd0:1 (hdb) not present, file system initialization failed");

    inode_init();

#ifdef EFILESYS
    fat_init();

    if (format) do_format();

    fat_open();
#else
    /* Original FS */
    free_map_init();

    if (format) do_format();

    free_map_open();
#endif
}

/* Shuts down the file system module, writing any unwritten data
 * to disk. */
void filesys_done(void)
{
    /* Original FS */
#ifdef EFILESYS
    fat_close();
#else
    free_map_close();
#endif
}

/* Creates a file named NAME with the given INITIAL_SIZE.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,
 * or if internal memory allocation fails. */
bool filesys_create(const char* name, off_t initial_size)
{
    disk_sector_t inode_sector = 0;
    struct dir* dir = dir_open_root();
    bool success = (dir != NULL && free_map_allocate(1, &inode_sector) &&
                    inode_create(inode_sector, initial_size) && dir_add(dir, name, inode_sector));
    if (!success && inode_sector != 0) free_map_release(inode_sector, 1);
    dir_close(dir);

    return success;
}

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
struct file* filesys_open(const char* name)
{
    struct dir* dir = dir_open_root();
    struct inode* inode = NULL;

    if (dir != NULL) dir_lookup(dir, name, &inode);
    dir_close(dir);

    return file_open(inode);
}

/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails.
 * 파일을 삭제해도 해당 파일이 열려 있다면 블록이 해제되지 않고,
 * 열려 있는 모든 스레드가 닫을 때까지 계속 접근할 수 있음 */
bool filesys_remove(const char* name)
{
    struct dir* dir = dir_open_root();
    bool success = dir != NULL && dir_remove(dir, name);
    dir_close(dir);

    return success;
}

/* Formats the file system. */
static void do_format(void)
{
    printf("Formatting file system...");

#ifdef EFILESYS
    /* Create FAT and save it to the disk. */
    fat_create();
    fat_close();
#else
    free_map_create();                     // 섹터 0에 free map 생성
    if (!dir_create(ROOT_DIR_SECTOR, 16))  // 루트 디렉토리 생성
        PANIC("root directory creation failed");
    free_map_close();
#endif

    printf("done.\n");
}
