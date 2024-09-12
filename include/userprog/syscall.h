#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "threads/synch.h"

void syscall_init(void);
struct lock filesys_lock;
void check_address(void *addr);
void sys_halt(void);
void sys_exit(int status);
int sys_read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void close(int fd);
int wait(int pid);
int exec(const char *cmd_line);
bool sys_create(const char *file, unsigned initial_size);
bool sys_remove(const char *file);
int sys_open(const char *file);

#endif /* userprog/syscall.h */
