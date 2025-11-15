#include "filesys/file.h"
#include <debug.h>
#include "filesys/inode.h"
#include "threads/malloc.h"

/* An open file. */
/* 열린 파일을 나타내는 구조체입니다. */
struct file
{
    struct inode *inode; /* File's inode. */ /* 파일의 inode. */
    off_t pos; /* Current position. */       /* 현재 파일 포인터 위치. */
    bool deny_write;
    /* Has file_deny_write() been called? */ /* file_deny_write()가 호출되었는지 여부. */
};

/* Opens a file for the given INODE, of which it takes ownership,
 * and returns the new file.  Returns a null pointer if an
 * allocation fails or if INODE is null. */
/* 주어진 INODE에 대한 파일을 열고, 소유권을 가져가며 새 파일을 반환합니다.
 * 할당이 실패하거나 INODE가 null이면 null 포인터를 반환합니다. */
struct file *file_open(struct inode *inode)
{
    struct file *file = calloc(1, sizeof *file);  // file 구조체를 0으로 초기화하여 할당
    if (inode != NULL && file != NULL)            // inode와 file 모두 유효한 경우
    {
        file->inode = inode;       // inode 포인터 저장 (소유권 이전)
        file->pos = 0;             // 파일 포인터를 시작 위치(0)로 초기화
        file->deny_write = false;  // 초기에는 쓰기 허용 상태
        return file;               // 생성된 file 구조체 반환
    }
    else  // inode가 NULL이거나 메모리 할당 실패한 경우
    {
        inode_close(inode);  // inode 정리 (참조 카운트 감소)
        free(file);          // 할당했던 file 구조체 메모리 해제
        return NULL;         // 실패를 나타내는 NULL 반환
    }
}

/* Opens and returns a new file for the same inode as FILE.
 * Returns a null pointer if unsuccessful. */
/* FILE과 같은 inode에 대한 새 파일을 열고 반환합니다.
 * 실패하면 null 포인터를 반환합니다. */
struct file *file_reopen(struct file *file)
{
    // file의 inode를 다시 열고 (참조 카운트 증가), 그 inode로 새 file 구조체 생성
    return file_open(inode_reopen(file->inode));
}

/* Duplicate the file object including attributes and returns a new file for the
 * same inode as FILE. Returns a null pointer if unsuccessful. */
/* 속성을 포함하여 파일 객체를 복제하고 FILE과 같은 inode에 대한 새 파일을 반환합니다.
 * 실패하면 null 포인터를 반환합니다. */
struct file *file_duplicate(struct file *file)
{
    // file의 inode를 다시 열고 (참조 카운트 증가), 그 inode로 새 file 구조체 생성
    struct file *nfile = file_open(inode_reopen(file->inode));
    if (nfile)  // 새 파일 생성이 성공한 경우
    {
        nfile->pos = file->pos;  // 원본 파일의 현재 위치를 복사
        if (file->deny_write)
            file_deny_write(nfile);  // 원본이 deny_write 상태였다면 새 파일도 동일하게 설정
    }
    return nfile;  // 복제된 파일 구조체 반환 (실패 시 NULL)
}

/* Closes FILE. */
/* FILE을 닫습니다. */
void file_close(struct file *file)
{
    if (file != NULL)  // 파일 포인터가 유효한 경우에만 처리
    {
        file_allow_write(file);  // 파일이 deny_write 상태였다면 쓰기 허용 (inode 레벨에서도 해제)
        inode_close(file->inode);  // 파일이 참조하는 inode를 닫음 (참조 카운트 감소)
        free(file);                // file 구조체 메모리 해제
    }
}

/* Returns the inode encapsulated by FILE. */
/* FILE에 캡슐화된 inode를 반환합니다. */
struct inode *file_get_inode(struct file *file)
{
    return file->inode;  // file 구조체가 보유한 inode 포인터 반환
}

/* Reads SIZE bytes from FILE into BUFFER,
 * starting at the file's current position.
 * Returns the number of bytes actually read,
 * which may be less than SIZE if end of file is reached.
 * Advances FILE's position by the number of bytes read. */
/* 파일의 현재 위치에서 시작하여 FILE에서 BUFFER로 SIZE 바이트를 읽습니다.
 * 실제로 읽은 바이트 수를 반환하며, 파일 끝에 도달하면 SIZE보다 작을 수 있습니다.
 * 읽은 바이트 수만큼 FILE의 위치를 진행시킵니다. */
off_t file_read(struct file *file, void *buffer, off_t size)
{
    // 현재 파일 위치(file->pos)에서 size 바이트를 읽어 buffer에 저장
    off_t bytes_read = inode_read_at(file->inode, buffer, size, file->pos);
    file->pos += bytes_read;  // 실제로 읽은 바이트 수만큼 파일 포인터 위치 이동
    return bytes_read;        // 실제로 읽은 바이트 수 반환
}

/* Reads SIZE bytes from FILE into BUFFER,
 * starting at offset FILE_OFS in the file.
 * Returns the number of bytes actually read,
 * which may be less than SIZE if end of file is reached.
 * The file's current position is unaffected. */
/* 파일의 오프셋 FILE_OFS에서 시작하여 FILE에서 BUFFER로 SIZE 바이트를 읽습니다.
 * 실제로 읽은 바이트 수를 반환하며, 파일 끝에 도달하면 SIZE보다 작을 수 있습니다.
 * 파일의 현재 위치는 영향을 받지 않습니다. */
off_t file_read_at(struct file *file, void *buffer, off_t size, off_t file_ofs)
{
    // 지정된 오프셋(file_ofs)에서 size 바이트를 읽어 buffer에 저장 (파일 포인터 위치 변경 없음)
    return inode_read_at(file->inode, buffer, size, file_ofs);
}

/* Writes SIZE bytes from BUFFER into FILE,
 * starting at the file's current position.
 * Returns the number of bytes actually written,
 * which may be less than SIZE if end of file is reached.
 * (Normally we'd grow the file in that case, but file growth is
 * not yet implemented.)
 * Advances FILE's position by the number of bytes read. */
/* 파일의 현재 위치에서 시작하여 BUFFER에서 FILE로 SIZE 바이트를 씁니다.
 * 실제로 쓴 바이트 수를 반환하며, 파일 끝에 도달하면 SIZE보다 작을 수 있습니다.
 * (일반적으로는 그 경우 파일을 확장하지만, 파일 확장은 아직 구현되지 않았습니다.)
 * 읽은 바이트 수만큼 FILE의 위치를 진행시킵니다. */
off_t file_write(struct file *file, const void *buffer, off_t size)
{
    // 현재 파일 위치(file->pos)에 buffer의 size 바이트를 쓰기
    off_t bytes_written = inode_write_at(file->inode, buffer, size, file->pos);
    file->pos += bytes_written;  // 실제로 쓴 바이트 수만큼 파일 포인터 위치 이동
    return bytes_written;        // 실제로 쓴 바이트 수 반환
}

/* Writes SIZE bytes from BUFFER into FILE,
 * starting at offset FILE_OFS in the file.
 * Returns the number of bytes actually written,
 * which may be less than SIZE if end of file is reached.
 * (Normally we'd grow the file in that case, but file growth is
 * not yet implemented.)
 * The file's current position is unaffected. */
/* 파일의 오프셋 FILE_OFS에서 시작하여 BUFFER에서 FILE로 SIZE 바이트를 씁니다.
 * 실제로 쓴 바이트 수를 반환하며, 파일 끝에 도달하면 SIZE보다 작을 수 있습니다.
 * (일반적으로는 그 경우 파일을 확장하지만, 파일 확장은 아직 구현되지 않았습니다.)
 * 파일의 현재 위치는 영향을 받지 않습니다. */
off_t file_write_at(struct file *file, const void *buffer, off_t size, off_t file_ofs)
{
    // 지정된 오프셋(file_ofs)에 buffer의 size 바이트를 쓰기 (파일 포인터 위치 변경 없음)
    return inode_write_at(file->inode, buffer, size, file_ofs);
}

/* Prevents write operations on FILE's underlying inode
 * until file_allow_write() is called or FILE is closed. */
/* file_allow_write()가 호출되거나 FILE이 닫힐 때까지 FILE의 기본 inode에 대한 쓰기 작업을
 * 방지합니다. */
void file_deny_write(struct file *file)
{
    ASSERT(file != NULL);  // 파일 포인터가 NULL이 아니어야 함 (디버그 모드에서만 체크)
    if (!file->deny_write)  // 이미 deny_write 상태가 아닌 경우에만 처리 (중복 호출 방지)
    {
        file->deny_write = true;  // 파일 구조체의 deny_write 플래그를 true로 설정
        inode_deny_write(file->inode);  // 실제 inode 레벨에서 쓰기 방지 (다른 파일 핸들이 같은
                                        // inode를 공유할 수 있으므로)
    }
}

/* Re-enables write operations on FILE's underlying inode.
 * (Writes might still be denied by some other file that has the
 * same inode open.) */
/* FILE의 기본 inode에 대한 쓰기 작업을 다시 활성화합니다.
 * (같은 inode를 열고 있는 다른 파일에 의해 쓰기가 여전히 거부될 수 있습니다.) */
void file_allow_write(struct file *file)
{
    ASSERT(file != NULL);  // 파일 포인터가 NULL이 아니어야 함 (디버그 모드에서만 체크)
    if (file->deny_write)  // deny_write 상태인 경우에만 처리 (중복 호출 방지)
    {
        file->deny_write = false;  // 파일 구조체의 deny_write 플래그를 false로 설정
        inode_allow_write(file->inode);  // 실제 inode 레벨에서 쓰기 허용 (다른 파일 핸들이 같은
                                         // inode를 공유할 수 있으므로)
    }
}

/* Returns the size of FILE in bytes. */
/* FILE의 크기를 바이트 단위로 반환합니다. */
off_t file_length(struct file *file)
{
    ASSERT(file != NULL);  // 파일 포인터가 NULL이 아니어야 함 (디버그 모드에서만 체크)
    return inode_length(file->inode);  // inode의 실제 파일 크기 반환
}

/* Sets the current position in FILE to NEW_POS bytes from the
 * start of the file. */
/* FILE의 현재 위치를 파일 시작으로부터 NEW_POS 바이트로 설정합니다. */
void file_seek(struct file *file, off_t new_pos)
{
    ASSERT(file != NULL);  // 파일 포인터가 NULL이 아니어야 함 (디버그 모드에서만 체크)
    ASSERT(new_pos >= 0);  // 새 위치는 음수가 아니어야 함 (디버그 모드에서만 체크)
    file->pos = new_pos;  // 파일 포인터 위치를 새 위치로 설정
}

/* Returns the current position in FILE as a byte offset from the
 * start of the file. */
/* 파일 시작으로부터의 바이트 오프셋으로 FILE의 현재 위치를 반환합니다. */
off_t file_tell(struct file *file)
{
    ASSERT(file != NULL);  // 파일 포인터가 NULL이 아니어야 함 (디버그 모드에서만 체크)
    return file->pos;  // 현재 파일 포인터 위치 반환
}
