#include "filesys/file.h"
#include <debug.h>
#include "filesys/inode.h"
#include "threads/malloc.h"

/* 열린 파일 */
struct file {
	struct inode *inode; /* 파일의 inode */
	off_t pos;	     /* 현재 위치 */
	bool deny_write;     /* file_deny_write()가 호출되었는지 여부 */
};

/* 주어진 INODE에 대한 파일을 열고, 해당 INODE의 소유권을 가져온 뒤
 * 새로운 파일을 반환한다. 메모리 할당에 실패하거나 INODE가 null이면
 * null 포인터를 반환한다. */
struct file *file_open(struct inode *inode)
{
	struct file *file = calloc(1, sizeof *file);
	if (inode != NULL && file != NULL) {
		file->inode = inode;
		file->pos = 0;
		file->deny_write = false;
		return file;
	} else {
		inode_close(inode);
		free(file);
		return NULL;
	}
}

/* FILE과 동일한 inode에 대해 새로운 파일을 열어 반환한다.
 * 실패하면 null 포인터를 반환한다. */
struct file *file_reopen(struct file *file)
{
	return file_open(inode_reopen(file->inode));
}

/* 속성을 포함한 파일 객체를 복제하고, FILE과 동일한 inode에 대한
 * 새로운 파일을 반환한다. 실패하면 null 포인터를 반환한다. */
// file_open은 inode만 같고 position은 0으로 초기화하는 반면
// file_duplicate는 inode + position + deny_write까지 복사한다
struct file *file_duplicate(struct file *file)
{
	struct file *nfile = file_open(inode_reopen(file->inode));
	if (nfile) {
		nfile->pos = file->pos;
		if (file->deny_write)
			file_deny_write(nfile);
	}
	return nfile;
}

/* FILE을 닫는다. */
void file_close(struct file *file)
{
	if (file != NULL) {
		file_allow_write(file);
		inode_close(file->inode);
		free(file);
	}
}

/* FILE에 포함된 inode를 반환한다. */
struct inode *file_get_inode(struct file *file)
{
	return file->inode;
}

/* 파일의 현재 위치부터 FILE에서 SIZE 바이트를 읽어 BUFFER에 저장한다.
 * 실제로 읽은 바이트 수를 반환하며, 파일의 끝에 도달하면 SIZE보다
 * 작을 수 있다. 읽은 바이트 수만큼 FILE의 위치를 전진시킨다. */
off_t file_read(struct file *file, void *buffer, off_t size)
{
	off_t bytes_read = inode_read_at(file->inode, buffer, size, file->pos);
	file->pos += bytes_read;
	return bytes_read;
}

/* 파일의 FILE_OFS 오프셋부터 FILE에서 SIZE 바이트를 읽어 BUFFER에 저장한다.
 * 실제로 읽은 바이트 수를 반환하며, 파일의 끝에 도달하면 SIZE보다
 * 작을 수 있다. 파일의 현재 위치는 변경되지 않는다. */
off_t file_read_at(struct file *file, void *buffer, off_t size, off_t file_ofs)
{
	return inode_read_at(file->inode, buffer, size, file_ofs);
}

/* 파일의 현재 위치부터 BUFFER에서 SIZE 바이트를 FILE에 쓴다.
 * 실제로 쓴 바이트 수를 반환하며, 파일의 끝에 도달하면 SIZE보다
 * 작을 수 있다.
 * (일반적으로는 이 경우 파일을 확장하지만, 파일 확장은 아직 구현되지 않았다.)
 * 읽은 바이트 수만큼 FILE의 위치를 전진시킨다. */
off_t file_write(struct file *file, const void *buffer, off_t size)
{
	// printf("[DEBUG] file_write: size=%d, pos=%d\n", size, file->pos);
	off_t bytes_written = inode_write_at(file->inode, buffer, size, file->pos);
	// printf("[DEBUG] file_write: bytes_written=%d, new_pos=%d\n", bytes_written,
	//    file->pos + bytes_written);

	file->pos += bytes_written;
	return bytes_written;
}

/* 파일의 FILE_OFS 오프셋부터 BUFFER에서 SIZE 바이트를 FILE에 쓴다.
 * 실제로 쓴 바이트 수를 반환하며, 파일의 끝에 도달하면 SIZE보다
 * 작을 수 있다.
 * (일반적으로는 이 경우 파일을 확장하지만, 파일 확장은 아직 구현되지 않았다.)
 * 파일의 현재 위치는 변경되지 않는다. */
off_t file_write_at(struct file *file, const void *buffer, off_t size, off_t file_ofs)
{
	return inode_write_at(file->inode, buffer, size, file_ofs);
}

/* file_allow_write()가 호출되거나 FILE이 닫힐 때까지
 * FILE의 기저 inode에 대한 쓰기 작업을 금지한다. */
void file_deny_write(struct file *file)
{
	ASSERT(file != NULL);
	if (!file->deny_write) {
		file->deny_write = true;
		inode_deny_write(file->inode);
	}
}

/* FILE의 기저 inode에 대한 쓰기 작업을 다시 허용한다.
 * (동일한 inode를 열고 있는 다른 파일에 의해 여전히 쓰기가
 * 거부될 수 있다.) */
void file_allow_write(struct file *file)
{
	ASSERT(file != NULL);
	if (file->deny_write) {
		file->deny_write = false;
		inode_allow_write(file->inode);
	}
}

/* FILE의 크기를 바이트 단위로 반환한다. */
off_t file_length(struct file *file)
{
	ASSERT(file != NULL);
	return inode_length(file->inode);
}

/* FILE의 현재 위치를 파일의 시작으로부터 NEW_POS 바이트로 설정한다. */
void file_seek(struct file *file, off_t new_pos)
{
	ASSERT(file != NULL);
	ASSERT(new_pos >= 0);
	file->pos = new_pos;
}

/* 파일의 시작으로부터의 바이트 오프셋으로 FILE의 현재 위치를 반환한다. */
off_t file_tell(struct file *file)
{
	ASSERT(file != NULL);
	return file->pos;
}
