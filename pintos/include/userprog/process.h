#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

/* 프로세스의 sys를 위한 값들. */
struct child_info
{
    tid_t tid;                   // 자식의 TID
    int exit_status;             // 종료 상태 (초기값: 불확실하면 -1 등)
    struct semaphore wait_sema;  // 대기용 세마포어
    struct list_elem elem;       // 부모의 children 리스트 연결용
};

/* 프로세스의 fork시 생성을 위한 구조체. */
struct fork_data
{
    struct thread *parent;
    struct intr_frame *parent_if;
    struct semaphore child_create;
    bool success;
    struct child_info *child_info;
};

tid_t process_create_initd(const char *file_name);
tid_t process_fork(const char *name, struct intr_frame *if_);
int process_exec(void *f_name);
int process_wait(tid_t);
void process_exit(void);
void process_activate(struct thread *next);

#endif /* userprog/process.h */
