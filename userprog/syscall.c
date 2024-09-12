#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/palloc.h"
#include "threads/init.h"
#include "userprog/gdt.h"
#include "userprog/process.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include <string.h>
#include "devices/input.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081			/* Segment selector msr */
#define MSR_LSTAR 0xc0000082		/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void) {
	write_msr(MSR_STAR,
			  ((uint64_t)SEL_UCSEG - 0x10) << 48 | ((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			  FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	lock_init(&filesys_lock);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED) {
	int syscall_n = f->R.rax;
	switch (syscall_n) {
	case SYS_HALT:
		sys_halt();
		break;
	case SYS_EXIT:
		sys_exit(f->R.rdi);
		break;
	case SYS_FORK:
		f->R.rax = fork(f->R.rdi, f->R.rsi);
		break;
	case SYS_EXEC:
		f->R.rax = exec(f->R.rdi);
		break;
	case SYS_WAIT:
		f->R.rax = wait(f->R.rdi);
		break;
	case SYS_CREATE:
		f->R.rax = sys_create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:
		f->R.rax = sys_remove(f->R.rdi);
		break;
	case SYS_OPEN:
		f->R.rax = sys_open(f->R.rdi);
		break;
	case SYS_FILESIZE:
		break;
	case SYS_READ:
		f->R.rax = sys_read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK:
		break;
	case SYS_TELL:
		break;
	case SYS_CLOSE:
		close(f->R.rdi);
		break;
	default:
		break;
	}
}

void check_address(void *addr) {
	if (addr == NULL)
		sys_exit(-1);
	if (!is_user_vaddr(addr))
		sys_exit(-1);
	if (pml4_get_page(thread_current()->pml4, addr) == NULL)
		sys_exit(-1);
}

void sys_halt(void) { power_off(); }

void sys_exit(int status) {
	struct thread *t = thread_current();
	t->exit_status = status;
	printf("%s: exit(%d)\n", t->name, status);
	thread_exit();
}

bool sys_create(const char *file, unsigned initial_size) {
	check_address(file);
	if (filesys_create(file, initial_size)) {
		return true;
	} else {
		return false;
	}
}

bool sys_remove(const char *file) {
	check_address(file);
	if (filesys_remove(file)) {
		return true;
	} else {
		return false;
	}
}

int write(int fd, const void *buffer, unsigned size) {
	check_address(buffer);

	if (fd == STDOUT_FILENO) {
		putbuf(buffer, size);
		return size;
	} else if (fd == STDIN_FILENO) {
		return -1;
	}

	struct file *fileobj = process_get_file(fd);

	if (fileobj == NULL) {
		return -1;
	}

	int write_count;
	lock_acquire(&filesys_lock);
	write_count = file_write(fileobj, buffer, size);
	lock_release(&filesys_lock);

	return write_count;
}

int sys_read(int fd, void *buffer, unsigned size) {
	check_address(buffer);
	check_address(buffer + size - 1);

	unsigned char *buf = buffer;
	int read_count = 0;
	struct file *fileobj = process_get_file(fd);

	if (fileobj == NULL) {
		return -1;
	}
	if (fd == STDIN_FILENO) {
		for (read_count = 0; read_count < size; read_count++) {
			char key = input_getc();
			*buf++ = key;
			if (key == '\0') {
				break;
			}
		}
	} else {
		if (fd >= 2) {
			lock_acquire(&filesys_lock);
			read_count = file_read(fileobj, buffer, size);
			lock_release(&filesys_lock);
		} else {
			return -1;
		}
	}
	return read_count;
}

int wait(int pid) { return process_wait(pid); }

int sys_open(const char *file) {
	check_address(file);
	struct file *file_obj = filesys_open(file);
	if (file_obj == NULL) {
		return -1;
	}
	int fd = process_add_file(file_obj);
	if (fd == -1) {
		file_close(file_obj);
	}
	return fd;
}

void close(int fd) {
	struct file *fileobj = process_get_file(fd);
	file_close(fileobj);
	process_close_file(fd);
}

int exec(const char *cmd_line) {
	check_address(cmd_line);

	char *cmd_line_copy = palloc_get_page(0);
	if (cmd_line_copy == NULL) {
		sys_exit(-1);
	}

	strlcpy(cmd_line_copy, cmd_line, PGSIZE);

	int result = process_exec(cmd_line_copy);
	palloc_free_page(cmd_line_copy);

	if (process_exec(cmd_line_copy) == -1) {
		sys_exit(-1);
	}
}

int fork(const *thread_name, struct intr_frame *f) {
	return process_fork(thread_name, f);
}