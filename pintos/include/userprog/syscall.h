// include/userprog/syscall.h
#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

/* fd_table */
#define FD_INITIAL_CAPACITY 32 /* fd table 최초 생성시 슬롯 개수 */
#define FILE_FD_MAGIC 0xFDCB0FDC /* 임의의 비트 상수. FD+잡음비트 조합. 디버그시 식별목적 */

struct fd_table {
	struct file **files; /* 파일 포인터 배열 */
	int capacity;	     /* 슬롯수 */
	int next_fd;	     /* 다음 빈 슬롯 번호 */
	unsigned magic;	     /* 디버깅을 위한 매직 넘버 */
};

typedef int pid_t;

void syscall_init(void);

extern struct lock filesys_lock; // syscall.c, process.c에서 사용

#endif /* userprog/syscall.h */
