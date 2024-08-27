#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* 스레드 구조체의 'magic' 멤버에 대한 임의의 값 */
#define THREAD_MAGIC 0xcd6abf4b

/* 기본 스레드에 대한 임의의 값 */
#define THREAD_BASIC 0xd42df210

/* 실행 준비된 프로세스 목록 */
static struct list ready_list;

/* 유휴 스레드 */
static struct thread *idle_thread;

/* 초기 스레드 */
static struct thread *initial_thread;

/* tid 할당을 위한 락 */
static struct lock tid_lock;

/* 스레드 소멸 요청 목록 */
static struct list destruction_req;

/* 통계 변수들 */
static long long idle_ticks;   /* 유휴 상태 틱 수 */
static long long kernel_ticks; /* 커널 스레드 틱 수 */
static long long user_ticks;   /* 사용자 프로그램 틱 수 */

/* 스케줄링 관련 상수와 변수 */
#define TIME_SLICE 4		  /* 각 스레드의 시간 할당량 */
static unsigned thread_ticks; /* 현재 스레드의 실행 틱 수 */

/* 스케줄러 선택 플래그 */
bool thread_mlfqs;

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule(void);
static tid_t allocate_tid(void);

/* 유효한 스레드 포인터 확인 매크로 */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* 현재 실행 중인 스레드 반환 매크로 */
#define running_thread() ((struct thread *)(pg_round_down(rrsp())))

/* 임시 전역 디스크립터 테이블 */
static uint64_t gdt[3] = {0, 0x00af9a000000ffff, 0x00cf92000000ffff};

void thread_init(void) {
	ASSERT(intr_get_level() == INTR_OFF); // 인터럽트가 비활성화되어 있는지 확인

	/* 임시 GDT 설정 */
	struct desc_ptr gdt_ds = {
		.size = sizeof(gdt) - 1, // GDT 크기 설정
		.address = (uint64_t)gdt // GDT 주소 설정
	};
	lgdt(&gdt_ds); // GDT 로드

	/* 전역 스레드 관련 데이터 구조 초기화 */
	lock_init(&tid_lock);		 // TID 락 초기화
	list_init(&ready_list);		 // 준비 목록 초기화
	list_init(&destruction_req); // 소멸 요청 목록 초기화

	/* 초기 스레드 설정 */
	initial_thread =
		running_thread(); // 현재 실행 중인 스레드를 초기 스레드로 설정
	init_thread(initial_thread, "main", PRI_DEFAULT); // 초기 스레드 초기화
	initial_thread->status =
		THREAD_RUNNING; // 초기 스레드 상태를 RUNNING으로 설정
	initial_thread->tid = allocate_tid(); // 초기 스레드에 TID 할당
}

void thread_start(void) {
	/* 유휴 스레드 생성 및 초기화 */
	struct semaphore idle_started;
	sema_init(&idle_started, 0); // 세마포어 초기화
	thread_create("idle", PRI_MIN, idle, &idle_started); // 유휴 스레드 생성

	/* 인터럽트 활성화로 스케줄링 시작 */
	intr_enable(); // 인터럽트 활성화

	/* 유휴 스레드 초기화 완료 대기 */
	sema_down(&idle_started); // 유휴 스레드 초기화 완료 대기
}

void thread_tick(void) {
	struct thread *t = thread_current(); // 현재 실행 중인 스레드 가져오기

	/* 스레드 유형별 틱 카운터 증가 */
	if (t == idle_thread)
		idle_ticks++; // 유휴 스레드 틱 증가
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++; // 사용자 프로그램 틱 증가
#endif
	else
		kernel_ticks++; // 커널 스레드 틱 증가

	/* 시간 할당량 초과 시 스레드 양보 */
	if (++thread_ticks >= TIME_SLICE) // 스레드 틱 증가 및 시간 할당량 초과 확인
		intr_yield_on_return(); // 인터럽트 반환 시 양보 예약
}

void thread_print_stats(void) {
	printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
		   idle_ticks, kernel_ticks, user_ticks); // 스레드 통계 출력
}

tid_t thread_create(const char *name, int priority, thread_func *function,
					void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT(function != NULL); // 함수 포인터가 NULL이 아닌지 확인

	/* 스레드 구조체 메모리 할당 */
	t = palloc_get_page(PAL_ZERO); // 페이지 할당 및 0으로 초기화
	if (t == NULL)
		return TID_ERROR; // 메모리 할당 실패 시 에러 반환

	/* 스레드 초기화 */
	init_thread(t, name, priority); // 스레드 구조체 초기화
	tid = t->tid = allocate_tid();	// TID 할당

	/* 스레드 컨텍스트 설정 */
	t->tf.rip = (uintptr_t)kernel_thread; // 시작 주소 설정
	t->tf.R.rdi = (uint64_t)function;	  // 첫 번째 인자 설정
	t->tf.R.rsi = (uint64_t)aux;		  // 두 번째 인자 설정
	t->tf.ds = SEL_KDSEG;				  // 데이터 세그먼트 설정
	t->tf.es = SEL_KDSEG;				  // 추가 세그먼트 설정
	t->tf.ss = SEL_KDSEG;				  // 스택 세그먼트 설정
	t->tf.cs = SEL_KCSEG;				  // 코드 세그먼트 설정
	t->tf.eflags = FLAG_IF;				  // 인터럽트 플래그 설정

	/* 스레드를 실행 준비 상태로 전환 */
	thread_unblock(t); // 스레드 언블록

	return tid; // 생성된 스레드의 TID 반환
}

void thread_block(void) {
	ASSERT(!intr_context()); // 인터럽트 컨텍스트가 아님을 확인
	ASSERT(intr_get_level() == INTR_OFF); // 인터럽트가 비활성화되어 있는지 확인
	thread_current()->status =
		THREAD_BLOCKED; // 현재 스레드의 상태를 BLOCKED로 설정
	schedule();			// 다음 실행할 스레드를 스케줄
}

void thread_unblock(struct thread *t) {
	enum intr_level old_level; // 이전 인터럽트 레벨을 저장할 변수

	ASSERT(is_thread(t)); // 유효한 스레드 포인터인지 확인

	old_level = intr_disable(); // 인터럽트 비활성화 및 이전 상태 저장
	ASSERT(t->status == THREAD_BLOCKED); // 스레드가 BLOCKED 상태인지 확인
	list_push_back(&ready_list, &t->elem); // 스레드를 ready_list의 끝에 추가
	t->status = THREAD_READY;  // 스레드 상태를 READY로 변경
	intr_set_level(old_level); // 이전 인터럽트 레벨로 복원
}

/* Returns the name of the running thread. */
const char *thread_name(void) {
	return thread_current()->name; // 현재 실행 중인 스레드의 이름 반환
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *thread_current(void) {
	struct thread *t = running_thread(); // 현재 실행 중인 스레드 가져오기

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT(is_thread(t)); // t가 유효한 스레드인지 확인
	ASSERT(t->status == THREAD_RUNNING); // t의 상태가 RUNNING인지 확인

	return t; // 현재 스레드 반환
}

/* Returns the running thread's tid. */
tid_t thread_tid(void) {
	return thread_current()->tid; // 현재 실행 중인 스레드의 tid 반환
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void thread_exit(void) {
	ASSERT(!intr_context()); // 인터럽트 컨텍스트가 아님을 확인

#ifdef USERPROG
	process_exit(); // 사용자 프로그램 종료 처리
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable(); // 인터럽트 비활성화
	do_schedule(
		THREAD_DYING); // 현재 스레드를 DYING 상태로 설정하고 다른 스레드 스케줄
	NOT_REACHED(); // 이 지점에 도달하면 안 됨
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void thread_yield(void) {
	struct thread *curr = thread_current(); // 현재 스레드 가져오기
	enum intr_level old_level;

	ASSERT(!intr_context()); // 인터럽트 컨텍스트가 아님을 확인

	old_level = intr_disable(); // 인터럽트 비활성화
	if (curr != idle_thread)
		list_push_back(&ready_list,
					   &curr->elem); // 현재 스레드를 ready_list에 추가
	do_schedule(
		THREAD_READY); // 현재 스레드를 READY 상태로 설정하고 다른 스레드 스케줄
	intr_set_level(old_level); // 이전 인터럽트 레벨 복원
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void thread_set_priority(int new_priority) {
	thread_current()->priority = new_priority; // 현재 스레드의 우선순위 설정
}

/* Returns the current thread's priority. */
int thread_get_priority(void) {
	return thread_current()->priority; // 현재 스레드의 우선순위 반환
}

/* Sets the current thread's nice value to NICE. */
void thread_set_nice(int nice UNUSED) {
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int thread_get_nice(void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average. */
int thread_get_load_avg(void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int thread_get_recent_cpu(void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void idle(void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current(); // 현재 스레드를 idle_thread로 설정
	sema_up(idle_started);			// 세마포어 시그널

	for (;;) {
		/* Let someone else run. */
		intr_disable(); // 인터럽트 비활성화
		thread_block(); // 현재 스레드(idle_thread) 블록

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void kernel_thread(thread_func *function, void *aux) {
	ASSERT(function != NULL); // function이 NULL이 아님을 확인

	intr_enable(); /* The scheduler runs with interrupts off. */
	function(aux); /* Execute the thread function. */
	thread_exit(); /* If function() returns, kill the thread. */
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void init_thread(struct thread *t, const char *name, int priority) {
	ASSERT(t != NULL); // t가 NULL이 아님을 확인
	ASSERT(PRI_MIN <= priority &&
		   priority <= PRI_MAX); // priority가 유효한 범위인지 확인
	ASSERT(name != NULL);		 // name이 NULL이 아님을 확인

	memset(t, 0, sizeof *t);	// t를 0으로 초기화
	t->status = THREAD_BLOCKED; // 스레드 상태를 BLOCKED로 설정
	strlcpy(t->name, name, sizeof t->name); // 스레드 이름 설정
	t->tf.rsp = (uint64_t)t + PGSIZE - sizeof(void *); // 스택 포인터 설정
	t->priority = priority;	 // 스레드 우선순위 설정
	t->magic = THREAD_MAGIC; // 매직 넘버 설정
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *next_thread_to_run(void) {
	if (list_empty(&ready_list))
		return idle_thread; // ready_list가 비어있으면 idle_thread 반환
	else
		return list_entry(list_pop_front(&ready_list), struct thread,
						  elem); // ready_list의 첫 번째 스레드 반환
}

/* Use iretq to launch the thread */
void do_iret(struct intr_frame *tf) {
	__asm __volatile("movq %0, %%rsp\n"
					 // ... (어셈블리 코드 생략)
					 :
					 : "g"((uint64_t)tf)
					 : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void thread_launch(struct thread *th) {
	uint64_t tf_cur = (uint64_t)&running_thread()->tf;
	uint64_t tf = (uint64_t)&th->tf;
	ASSERT(intr_get_level() == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile(
		/* Store registers that will be used. */
		"push %%rax\n"
		"push %%rbx\n"
		"push %%rcx\n"
		/* Fetch input once */
		"movq %0, %%rax\n"
		"movq %1, %%rcx\n"
		"movq %%r15, 0(%%rax)\n"
		"movq %%r14, 8(%%rax)\n"
		"movq %%r13, 16(%%rax)\n"
		"movq %%r12, 24(%%rax)\n"
		"movq %%r11, 32(%%rax)\n"
		"movq %%r10, 40(%%rax)\n"
		"movq %%r9, 48(%%rax)\n"
		"movq %%r8, 56(%%rax)\n"
		"movq %%rsi, 64(%%rax)\n"
		"movq %%rdi, 72(%%rax)\n"
		"movq %%rbp, 80(%%rax)\n"
		"movq %%rdx, 88(%%rax)\n"
		"pop %%rbx\n" // Saved rcx
		"movq %%rbx, 96(%%rax)\n"
		"pop %%rbx\n" // Saved rbx
		"movq %%rbx, 104(%%rax)\n"
		"pop %%rbx\n" // Saved rax
		"movq %%rbx, 112(%%rax)\n"
		"addq $120, %%rax\n"
		"movw %%es, (%%rax)\n"
		"movw %%ds, 8(%%rax)\n"
		"addq $32, %%rax\n"
		"call __next\n" // read the current rip.
		"__next:\n"
		"pop %%rbx\n"
		"addq $(out_iret -  __next), %%rbx\n"
		"movq %%rbx, 0(%%rax)\n" // rip
		"movw %%cs, 8(%%rax)\n"	 // cs
		"pushfq\n"
		"popq %%rbx\n"
		"mov %%rbx, 16(%%rax)\n" // eflags
		"mov %%rsp, 24(%%rax)\n" // rsp
		"movw %%ss, 32(%%rax)\n"
		"mov %%rcx, %%rdi\n"
		"call do_iret\n"
		"out_iret:\n"
		:
		: "g"(tf_cur), "g"(tf)
		: "memory");
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void do_schedule(int status) {
	ASSERT(intr_get_level() == INTR_OFF); // 인터럽트가 비활성화되어 있는지 확인
	ASSERT(thread_current()->status ==
		   THREAD_RUNNING); // 현재 스레드가 RUNNING 상태인지 확인
	while (!list_empty(&destruction_req)) {
		struct thread *victim =
			list_entry(list_pop_front(&destruction_req), struct thread, elem);
		palloc_free_page(victim); // 종료된 스레드의 페이지 해제
	}
	thread_current()->status = status; // 현재 스레드의 상태 변경
	schedule();						   // 다음 스레드 스케줄
}

static void schedule(void) {
	struct thread *curr = running_thread(); // 현재 실행 중인 스레드
	struct thread *next = next_thread_to_run(); // 다음에 실행할 스레드

	ASSERT(intr_get_level() == INTR_OFF); // 인터럽트가 비활성화되어 있는지 확인
	ASSERT(curr->status !=
		   THREAD_RUNNING); // 현재 스레드가 RUNNING 상태가 아님을 확인
	ASSERT(is_thread(next)); // next가 유효한 스레드인지 확인

	/* Mark us as running. */
	next->status = THREAD_RUNNING; // 다음 스레드의 상태를 RUNNING으로 설정

	/* Start new time slice. */
	thread_ticks = 0; // 시간 슬라이스 초기화

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate(next); // 새 주소 공간 활성화
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT(curr != next);
			list_push_back(
				&destruction_req,
				&curr->elem); // 종료 중인 스레드를 destruction_req에 추가
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch(next); // 다음 스레드로 전환
	}
}

/* Returns a tid to use for a new thread. */
static tid_t allocate_tid(void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire(&tid_lock); // tid_lock 획득
	tid = next_tid++;		 // 새로운 tid 할당
	lock_release(&tid_lock); // tid_lock 해제

	return tid; // 할당된 tid 반환
}