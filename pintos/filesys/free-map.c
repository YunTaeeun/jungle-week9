#include "filesys/free-map.h"
#include <bitmap.h>
#include <debug.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"

static struct file* free_map_file; /* Free map file. */
static struct bitmap* free_map;    /* Free map, one bit per disk sector. */

/* free map 초기화.
   free map: 디스크의 빈 공간을 관리하는 비트맵(bitmap) 자료구조 */
void free_map_init(void)
{
    free_map = bitmap_create(
        disk_size(filesys_disk));  // 메모리 공간 할당하고 0으로 세팅(0은 사용가능을 의미)
    if (free_map == NULL) PANIC("bitmap creation failed--disk is too large");
    bitmap_mark(free_map, FREE_MAP_SECTOR);
    bitmap_mark(free_map, ROOT_DIR_SECTOR);
}

/* Allocates CNT consecutive sectors from the free map and stores
 * the first into *SECTORP.
 * Returns true if successful, false if all sectors were
 * available. */
bool free_map_allocate(size_t cnt, disk_sector_t* sectorp)
{
    disk_sector_t sector = bitmap_scan_and_flip(free_map, 0, cnt, false);
    if (sector != BITMAP_ERROR && free_map_file != NULL && !bitmap_write(free_map, free_map_file))
    {
        bitmap_set_multiple(free_map, sector, cnt, false);
        sector = BITMAP_ERROR;
    }
    if (sector != BITMAP_ERROR) *sectorp = sector;
    return sector != BITMAP_ERROR;
}

/* Makes CNT sectors starting at SECTOR available for use. */
void free_map_release(disk_sector_t sector, size_t cnt)
{
    ASSERT(bitmap_all(free_map, sector, cnt));
    bitmap_set_multiple(free_map, sector, cnt, false);
    bitmap_write(free_map, free_map_file);
}

/* Opens the free map file and reads it from disk. */
void free_map_open(void)
{
    free_map_file = file_open(inode_open(FREE_MAP_SECTOR));
    if (free_map_file == NULL) PANIC("can't open free map");
    if (!bitmap_read(free_map, free_map_file)) PANIC("can't read free map");
}

/* Writes the free map to disk and closes the free map file. */
void free_map_close(void)
{
    file_close(free_map_file);
}

/* free map: 0번 섹터에 저장되는 특별한 bitmap. 전체 디스크의 어느 섹터가 비었는지 기록한다. */
void free_map_create(void)
{
    /* 0번 섹터에 free map용 inode를 생성한다 */
    if (!inode_create(FREE_MAP_SECTOR, bitmap_file_size(free_map)))
        PANIC("free map creation failed");

    /* 0번 섹터의 inode를 읽어서 파일로 열기 */
    free_map_file = file_open(inode_open(FREE_MAP_SECTOR));
    if (free_map_file == NULL) PANIC("can't open free map");

    /* 메모리의 bitmap을 파일(디스크)에 쓰기 */
    if (!bitmap_write(free_map, free_map_file)) PANIC("can't write free map");
}
