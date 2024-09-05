#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif

/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING, /* Running thread. */
	THREAD_READY,	/* Not running but ready to run. */
	THREAD_BLOCKED, /* Waiting for an event to trigger. */
	THREAD_DYING	/* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) - 1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0	   /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63	   /* Highest priority. */

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */

struct thread {
	/* thread.c에 의해 관리되는 필드들 */
	tid_t tid; /* 스레드 식별자. 각 스레드의 고유 ID */
	enum thread_status status; /* 스레드 상태. (THREAD_RUNNING, THREAD_READY,
								  THREAD_BLOCKED 등) */
	char name[16]; /* 스레드 이름. 디버깅 목적으로 사용 (최대 15자 + null
					  종료자) */
	int priority; /* 스레드 우선순위. 스케줄링에 사용됨 */
	int64_t wakeup_time; /* 스레드가 깨어나야 할 시간. 타이머 인터럽트와 함께
							사용 */

	/* thread.c와 synch.c 사이에 공유되는 필드 */
	struct list_elem elem; /* 리스트 요소. 스레드를 여러 리스트(예:
							  ready_list)에 넣을 때 사용 */

	int initial_priority;
	
	struct lock *wait_on_lock;
    struct list donations;
    struct list_elem donation_elem;

#ifdef USERPROG
	/* userprog/process.c에 의해 관리되는 필드 */
	uint64_t
		*pml4; /* 페이지 맵 레벨 4. 사용자 프로세스의 가상 메모리 관리에 사용 */
#endif

#ifdef VM
	/* 스레드가 소유한 전체 가상 메모리를 위한 테이블 */
	struct supplemental_page_table
		spt; /* 보조 페이지 테이블. 가상 메모리 관리에 사용 */
#endif

	/* thread.c에 의해 관리되는 필드들 */
	struct intr_frame
		tf; /* 인터럽트 프레임. 컨텍스트 스위칭에 필요한 정보 저장 */
	unsigned magic; /* 스택 오버플로우 감지를 위한 매직 넘버 */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init(void);
void thread_start(void);

void thread_tick(void);
void thread_print_stats(void);

typedef void thread_func(void *aux);
tid_t thread_create(const char *name, int priority, thread_func *, void *);

void thread_block(void);
void thread_unblock(struct thread *);

struct thread *thread_current(void);
tid_t thread_tid(void);
const char *thread_name(void);

void thread_exit(void) NO_RETURN;
void thread_yield(void);

int thread_get_priority(void);
void thread_set_priority(int);

int thread_get_nice(void);
void thread_set_nice(int);
int thread_get_recent_cpu(void);
int thread_get_load_avg(void);

void do_iret(struct intr_frame *tf);

#endif /* threads/thread.h */
