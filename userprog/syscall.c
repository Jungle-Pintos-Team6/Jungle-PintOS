#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/init.h"
#include "userprog/fd.h"
#include "userprog/process.h"
#include <string.h>
#include "threads/palloc.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void syscall_check_vaddr(uint64_t, struct process *);

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
}

void syscall_check_vaddr(uint64_t va, struct process *curr) {
	int temp;
	if (!is_user_vaddr(va)) {
		curr->exist_status = -1;
		thread_exit();
	}
	temp = *(int *)va;
	return;
}

/* The main system call interface */
/*
   Input argument
   arg1  arg2  arg3  arg4  arg5  arg6
   %rdi, %rsi, %rdx, %r10, %r8,  %r9
   Output argument
   %rax
*/
void syscall_handler(struct intr_frame *f) {
	struct process *current = process_current();
	// Projects 2 syscall
	switch (f->R.rax) {
	case SYS_HALT:
		power_off();
		NOT_REACHED();
		break;
	case SYS_EXIT:
		current->exist_status = f->R.rdi;
		thread_exit();
		NOT_REACHED();
		break;
	case SYS_FORK:
		syscall_check_vaddr(f->R.rdi, current);
		f->R.rax = process_fork((void *)f->R.rdi, f);
		break;
	case SYS_EXEC:
		syscall_check_vaddr(f->R.rdi, current);

		char *fn_copy, *temp_ptr;
		fn_copy = palloc_get_page(0);
		if (fn_copy == NULL) {
			current->exist_status = -1;
			thread_exit();
		}
		strlcpy(fn_copy, f->R.rdi, PGSIZE);

		current->exist_status = process_exec(fn_copy);
		thread_exit();
		break;
	case SYS_WAIT:
		f->R.rax = process_wait(f->R.rdi);
		break;
	case SYS_CREATE:
		syscall_check_vaddr(f->R.rdi, current);
		f->R.rax = filesys_create((void *)f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:
		syscall_check_vaddr(f->R.rdi, current);
		f->R.rax = filesys_remove((void *)f->R.rdi);
		break;
	case SYS_OPEN:
		syscall_check_vaddr(f->R.rdi, current);
		f->R.rax = fd_open((void *)f->R.rdi, *current->fd_list);
		break;
	case SYS_FILESIZE:
		f->R.rax = fd_filesize(f->R.rdi, *current->fd_list);
		break;
	case SYS_READ:
		syscall_check_vaddr(f->R.rsi, current);
		f->R.rax = fd_read(f->R.rdi, (void *)f->R.rsi, f->R.rdx, *current->fd_list);
		break;
	case SYS_WRITE:
		syscall_check_vaddr(f->R.rsi, current);
		f->R.rax = fd_write(f->R.rdi, (void *)f->R.rsi, f->R.rdx, *current->fd_list);
		break;
	case SYS_SEEK:
		fd_seek(f->R.rdi, f->R.rsi, *current->fd_list);
		break;
	case SYS_TELL:
		f->R.rax = fd_tell(f->R.rdi, *current->fd_list);
		break;
	case SYS_CLOSE:
		fd_close(f->R.rdi, *current->fd_list);
		break;
	case SYS_DUP2:
		f->R.rax = fd_dup2(f->R.rdi, f->R.rsi, *current->fd_list);
		break;

	// Projects 3 syscall
	case SYS_MMAP:
	case SYS_MUNMAP:

	// Projects 4 syscall
	case SYS_CHDIR:
	case SYS_MKDIR:
	case SYS_READDIR:
	case SYS_ISDIR:
	case SYS_INUMBER:
	case SYS_SYMLINK:
	case SYS_MOUNT:
	case SYS_UMOUNT:
	default:
		printf("system call %lld not maid\n", f->R.rax);
		thread_exit();
	}
}
