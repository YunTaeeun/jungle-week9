#ifndef __LIB_USER_SYSCALL_H
#define __LIB_USER_SYSCALL_H

#include <stdbool.h>
#include <debug.h>
#include <stddef.h>

/* 프로세스 식별자 */
typedef int pid_t;
#define PID_ERROR ((pid_t) - 1)

/* 메모리 맵 영역 식별자 */
typedef int off_t;
#define MAP_FAILED ((void*)NULL)

/* readdir()로 작성되는 파일명의 최대 길이 */
#define READDIR_MAX_LEN 14

/* main()의 일반적인 반환값 및 exit()의 인자값 */
#define EXIT_SUCCESS 0 /* 성공적인 실행 */
#define EXIT_FAILURE 1 /* 실행 실패 */

/* Project 2 이후 구현 */
void halt(void) NO_RETURN;
void exit(int status) NO_RETURN;
pid_t fork(const char* thread_name);
int exec(const char* file);
int wait(pid_t);
bool create(const char* file, unsigned initial_size);
bool remove(const char* file);
int open(const char* file);
int filesize(int fd);
int read(int fd, void* buffer, unsigned length);
int write(int fd, const void* buffer, unsigned length);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);

int dup2(int oldfd, int newfd);

/* Project 3 및 선택적으로 Project 4 구현 */
void* mmap(void* addr, size_t length, int writable, int fd, off_t offset);
void munmap(void* addr);

/* Project 4에서만 구현 */
bool chdir(const char* dir);
bool mkdir(const char* dir);
bool readdir(int fd, char name[READDIR_MAX_LEN + 1]);
bool isdir(int fd);
int inumber(int fd);
int symlink(const char* target, const char* linkpath);

/* 유저 가상 주소를 물리 주소로 변환하는 헬퍼 함수 */
static inline void* get_phys_addr(void* user_addr)
{
    void* pa;
    asm volatile("movq %0, %%rax" ::"r"(user_addr));
    asm volatile("int $0x42");
    asm volatile("\t movq %%rax, %0" : "=r"(pa));
    return pa;
}

/* 파일 시스템의 디스크 읽기 횟수를 반환하는 함수 */
static inline long long get_fs_disk_read_cnt(void)
{
    long long read_cnt;
    asm volatile("movq $0, %rdx");
    asm volatile("movq $1, %rcx");
    asm volatile("int $0x43");
    asm volatile("\t movq %%rax, %0" : "=r"(read_cnt));
    return read_cnt;
}

/* 파일 시스템의 디스크 쓰기 횟수를 반환하는 함수 */
static inline long long get_fs_disk_write_cnt(void)
{
    long long write_cnt;
    asm volatile("movq $0, %rdx");
    asm volatile("movq $1, %rcx");
    asm volatile("int $0x44");
    asm volatile("\t movq %%rax, %0" : "=r"(write_cnt));
    return write_cnt;
}

#endif /* lib/user/syscall.h */
