#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

/* 자식 프로세스의 종료 상태와 동기화를 관리하는 구조체 */
/* 이 구조체는 malloc으로 힙에 할당되어, 자식 스레드가 죽어도 부모가 free하기 전까지 살아있습니다.
 */
struct child_info {
	tid_t tid;		    /* 자식 스레드 ID */
	int exit_status;	    /* 종료 상태 값 */
	struct semaphore wait_sema; /* 종료 대기용 세마포어 (기존 dead) */
	struct list_elem elem;	    /* 부모의 child_list 연결용 */
};

tid_t process_create_initd(const char *file_name);
tid_t process_fork(const char *name, struct intr_frame *if_);
int process_exec(void *f_name);
int process_wait(tid_t);
void process_wait_initd(void); /* 추가 */
void process_exit(void);
void process_activate(struct thread *next);

#endif /* userprog/process.h */
