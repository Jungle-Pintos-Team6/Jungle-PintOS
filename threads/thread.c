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

/* 스레드 구조체의 'magic' 멤버를 위한 무작위 값. 스택 오버플로우 감지에 사용 */
#define THREAD_MAGIC 0xcd6abf4b

/* 기본 스레드를 위한 무작위 값. 수정하지 말 것 */
#define THREAD_BASIC 0xd42df210

/* THREAD_READY 상태의 프로세스 리스트, 즉 실행 준비되었지만 실제로 실행 중이지
 * 않은 프로세스들 */
static struct list ready_list;
static struct list waiting_list;

/* 유휴 스레드 */
static struct thread *idle_thread;

/* 초기 스레드, init.c:main()을 실행하는 스레드 */
static struct thread *initial_thread;

/* allocate_tid()에서 사용하는 잠금 */
static struct lock tid_lock;

/* 스레드 소멸 요청 */
static struct list destruction_req;

/* 통계 */
static long long idle_ticks; /* 유휴 상태에서 보낸 타이머 틱 수 */
static long long kernel_ticks; /* 커널 스레드에서의 타이머 틱 수 */
static long long user_ticks; /* 사용자 프로그램에서의 타이머 틱 수 */

/* 스케줄링 */
#define TIME_SLICE 4		  /* 각 스레드에 주는 타이머 틱 수 */
static unsigned thread_ticks; /* 마지막 양보 이후 타이머 틱 수 */

/* false(기본값)면 라운드 로빈 스케줄러 사용
   true면 다단계 피드백 큐 스케줄러 사용
   커널 명령줄 옵션 "-o mlfqs"로 제어 */
bool thread_mlfqs;

static void kernel_thread(thread_func *, void *aux);
static void idle(void *aux UNUSED);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule(void);
static tid_t allocate_tid(void);
void thread_sleep(int64_t ticks);
void thread_wakeup(int64_t ticks);

/* 유효한 스레드를 가리키는지 확인 */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* 실행 중인 스레드 반환 */
#define running_thread() ((struct thread *)(pg_round_down(rrsp())))

// thread_start를 위한 전역 디스크립터 테이블
// thread_init 이후에 gdt가 설정되므로, 임시 gdt를 먼저 설정해야 함
static uint64_t gdt[3] = {0, 0x00af9a000000ffff, 0x00cf92000000ffff};
// GDT 엔트리: 0(널 디스크립터), 커널 코드 세그먼트, 커널 데이터 세그먼트

/* 스레딩 시스템 초기화 */
void thread_init(void) {
	ASSERT(intr_get_level() == INTR_OFF);

	/* 커널용 임시 gdt 재로드 */
	struct desc_ptr gdt_ds = {.size = sizeof(gdt) - 1,
							  .address = (uint64_t)gdt};
	lgdt(&gdt_ds);

	/* 전역 스레드 컨텍스트 초기화 */
	lock_init(&tid_lock);
	list_init(&waiting_list);
	list_init(&ready_list);
	list_init(&destruction_req);

	/* 실행 중인 스레드를 위한 스레드 구조 설정 */
	initial_thread = running_thread();
	init_thread(initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid();
}

/* 선점형 스레드 스케줄링 시작 */
void thread_start(void) {
	/* 세마포어 선언: idle 스레드의 초기화 완료를 동기화하기 위해 사용 */
	struct semaphore idle_started;

	/* 세마포어 초기화: 초기값을 0으로 설정 */
	/* 이는 idle 스레드가 초기화를 완료할 때까지 메인 스레드가 대기하도록 함 */
	sema_init(&idle_started, 0);

	/* idle 스레드 생성
	 * "idle": 스레드 이름
	 * PRI_MIN: 최소 우선순위로 설정 (CPU가 다른 할 일이 없을 때만 실행됨)
	 * idle: 실행할 함수
	 * &idle_started: 세마포어 전달 (idle 함수 내에서 초기화 완료 시 signal을
	 * 보내기 위해)
	 */
	thread_create("idle", PRI_MIN, idle, &idle_started);

	/* 선점형 스레드 스케줄링 시작
	 * 인터럽트를 활성화하여 스레드 간 컨텍스트 스위칭이 가능하게 함
	 */
	intr_enable();

	/* idle 스레드가 초기화를 완료할 때까지 대기
	 * idle 스레드가 sema_up을 호출할 때까지 이 지점에서 블록됨
	 * 이는 idle 스레드가 완전히 준비된 후에 다음 단계로 진행하기 위함
	 */
	sema_down(&idle_started);
}

/* 타이머 인터럽트 핸들러에 의해 각 타이머 틱마다 호출 */
void thread_tick(void) {
	struct thread *t = thread_current();

	/* 통계 업데이트 */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* 선점 강제 */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return();
}

/* 스레드 통계 출력 */
void thread_print_stats(void) {
	printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
		   idle_ticks, kernel_ticks, user_ticks);
}

/* 새 커널 스레드 생성 */
tid_t thread_create(const char *name, int priority, thread_func *function,
					void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT(function != NULL);

	/* 스레드 할당 */
	t = palloc_get_page(PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* 스레드 초기화 */
	init_thread(t, name, priority);
	tid = t->tid = allocate_tid();

	/* 커널 스레드 설정 */
	t->tf.rip = (uintptr_t)kernel_thread;
	t->tf.R.rdi = (uint64_t)function;
	t->tf.R.rsi = (uint64_t)aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* 실행 큐에 추가 */
	thread_unblock(t);

	return tid;
}

/* 현재 실행 중인 스레드를 슬립(차단) 상태로 전환 */
void thread_block(void) {
	// 인터럽트 컨텍스트에서 호출되지 않았는지 확인
	ASSERT(!intr_context());
	// 인터럽트가 비활성화되어 있는지 확인
	ASSERT(intr_get_level() == INTR_OFF);

	// 현재 스레드의 상태를 THREAD_BLOCKED로 변경
	thread_current()->status = THREAD_BLOCKED;

	// 다른 스레드로 CPU 제어권을 넘김
	schedule();
}

/* 차단된 스레드 T를 실행 준비 상태로 전환 */
void thread_unblock(struct thread *t) {
	enum intr_level old_level;

	// 매개변수 t가 유효한 스레드 구조체인지 확인
	ASSERT(is_thread(t));

	// 인터럽트를 비활성화하고 이전 상태를 저장
	old_level = intr_disable();

	// 스레드 t의 상태가 THREAD_BLOCKED인지 확인
	ASSERT(t->status == THREAD_BLOCKED);

	// 스레드 t를 ready_list의 끝에 추가
	list_push_back(&ready_list, &t->elem);

	// 스레드 t의 상태를 THREAD_READY로 변경
	t->status = THREAD_READY;

	// 이전 인터럽트 상태로 복원
	intr_set_level(old_level);
}

/* 실행 중인 스레드의 이름 반환 */
const char *thread_name(void) { return thread_current()->name; }

/* 실행 중인 스레드 반환 */
struct thread *thread_current(void) {
	struct thread *t = running_thread();

	/* T가 실제 스레드인지 확인 */
	ASSERT(is_thread(t));
	ASSERT(t->status == THREAD_RUNNING);

	return t;
}

void thread_exit(void) {
	ASSERT(!intr_context());
	// 현재 코드가 인터럽트 컨텍스트에서 실행되고 있지 않음을 확인합니다.
	// 인터럽트 핸들러 내에서 스레드를 종료시키는 것은 안전하지 않기 때문에,
	// 이 ASSERT문으로 확인합니다.

#ifdef USERPROG
	process_exit();
#endif
	// USERPROG이 정의되어 있을 경우, 현재 프로세스와 관련된 자원 정리 및 종료를
	// 수행하는 process_exit() 함수를 호출합니다. 이 부분은 주로 사용자
	// 프로그램의 종료 처리를 담당합니다.

	/* 상태를 THREAD_DYING으로 설정하고 다른 프로세스 스케줄 */
	intr_disable();
	// 인터럽트를 비활성화합니다. 스레드 종료 과정에서는 컨텍스트 스위칭이
	// 일어나거나 다른 인터럽트가 발생하면 안 되므로, 이를 방지하기 위해
	// 인터럽트를 비활성화합니다.

	do_schedule(THREAD_DYING);
	// 현재 스레드의 상태를 THREAD_DYING으로 설정하고, 다른 스레드를
	// 스케줄링합니다. do_schedule() 함수는 스레드의 상태를 변경하고, CPU에서
	// 실행할 다른 스레드를 선택하여 스위칭을 수행합니다. THREAD_DYING 상태가 된
	// 스레드는 이후에 스케줄러에 의해 완전히 소멸될 것입니다.

	NOT_REACHED();
	// 이 코드는 도달할 수 없는 지점이어야 합니다.
	// do_schedule() 함수 호출 이후에는 컨트롤이 현재 스레드로 돌아오지 않기
	// 때문에, 이 라인에 도달하면 안 됩니다. 만약 이 지점에 도달했다면, 이는
	// 심각한 오류입니다.
}

/* CPU 양보. 현재 스레드는 슬립 상태가 아니며 즉시 다시 스케줄될 수 있음 */
void thread_yield(void) {
	struct thread *curr = thread_current(); // 현재 실행 중인 스레드 포인터 획득
	enum intr_level old_level;

	ASSERT(!intr_context()); // 인터럽트 컨텍스트가 아님을 확인

	old_level = intr_disable(); // 인터럽트 비활성화 및 이전 인터럽트 레벨 저장

	if (curr != idle_thread) // 현재 스레드가 idle 스레드가 아니면
		list_push_back(&ready_list,
					   &curr->elem); // ready_list 끝에 현재 스레드 추가

	do_schedule(THREAD_READY); // 스케줄러 호출, 현재 스레드 상태를 READY로 설정

	intr_set_level(old_level); // 이전 인터럽트 레벨 복원
}

void thread_sleep(int64_t ticks) {
	struct thread *cur;
	enum intr_level old_level;

	old_level = intr_disable(); // 인터럽트 off
	cur = thread_current();

	ASSERT(cur != idle_thread);

	cur->wakeup_time = ticks;				   // 일어날 시간을 저장
	list_push_back(&waiting_list, &cur->elem); // sleep_list 에 추가
	thread_block();							   // block 상태로 변경

	intr_set_level(old_level); // 인터럽트 on
}

void thread_wakeup(int64_t ticks) {
	struct list_elem *e = list_begin(&waiting_list);

	while (e != list_end(&waiting_list)) {
		struct thread *t = list_entry(e, struct thread, elem);
		if (t->wakeup_time <= ticks) { // 스레드가 일어날 시간이 되었는지 확인
			e = list_remove(e); // sleep list 에서 제거
			thread_unblock(t);	// 스레드 unblock
		} else
			e = list_next(e);
	}
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void thread_set_priority(int new_priority) {
	thread_current()->priority = new_priority;
}

/* Returns the current thread's priority. */
int thread_get_priority(void) { return thread_current()->priority; }

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

	// idle_thread 초기화
	idle_thread = thread_current();
	// thread_start() 계속 실행을 위한 세마포어 신호
	sema_up(idle_started);

	for (;;) {
		/* 다른 스레드에게 실행 기회 양보 */
		intr_disable();
		thread_block();

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
		// 인터럽트 재활성화 및 다음 인터럽트 대기
		asm volatile("sti; hlt" : : : "memory");
	}
}

/* 커널 스레드의 기본 함수 */
static void kernel_thread(thread_func *function, void *aux) {
	ASSERT(function != NULL);

	intr_enable(); // 인터럽트 활성화
	function(aux); // 스레드 함수 실행
	thread_exit(); // 함수 종료 시 스레드 종료
}

/* 스레드 T를 NAME과 PRIORITY로 초기화 */
static void init_thread(struct thread *t, const char *name, int priority) {
	// 스레드 구조체 포인터가 NULL이 아닌지 확인
	ASSERT(t != NULL);
	// 우선순위가 유효한 범위 내에 있는지 확인
	ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
	// 이름 문자열이 NULL이 아닌지 확인
	ASSERT(name != NULL);

	// 스레드 구조체를 0으로 초기화
	memset(t, 0, sizeof *t);
	// 스레드 상태를 BLOCKED로 설정
	t->status = THREAD_BLOCKED;
	// 스레드 이름을 설정 (최대 길이 제한)
	strlcpy(t->name, name, sizeof t->name);
	// 스레드의 스택 포인터 초기화 (페이지 맨 위에서 시작)
	t->tf.rsp = (uint64_t)t + PGSIZE - sizeof(void *);
	// 스레드 우선순위 설정
	t->priority = priority;
	// 디버깅을 위한 매직 넘버 설정
	t->magic = THREAD_MAGIC;
}

/* 다음에 실행할 스레드 선택
   실행 큐가 비어있지 않으면 큐에서 스레드 반환
   비어있으면 idle_thread 반환 */
static struct thread *next_thread_to_run(void) {
	if (list_empty(&ready_list))
		return idle_thread;
	else
		return list_entry(list_pop_front(&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void do_iret(struct intr_frame *tf) {
	__asm __volatile("movq %0, %%rsp\n"
					 "movq 0(%%rsp),%%r15\n"
					 "movq 8(%%rsp),%%r14\n"
					 "movq 16(%%rsp),%%r13\n"
					 "movq 24(%%rsp),%%r12\n"
					 "movq 32(%%rsp),%%r11\n"
					 "movq 40(%%rsp),%%r10\n"
					 "movq 48(%%rsp),%%r9\n"
					 "movq 56(%%rsp),%%r8\n"
					 "movq 64(%%rsp),%%rsi\n"
					 "movq 72(%%rsp),%%rdi\n"
					 "movq 80(%%rsp),%%rbp\n"
					 "movq 88(%%rsp),%%rdx\n"
					 "movq 96(%%rsp),%%rcx\n"
					 "movq 104(%%rsp),%%rbx\n"
					 "movq 112(%%rsp),%%rax\n"
					 "addq $120,%%rsp\n"
					 "movw 8(%%rsp),%%ds\n"
					 "movw (%%rsp),%%es\n"
					 "addq $32, %%rsp\n"
					 "iretq"
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
	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(thread_current()->status == THREAD_RUNNING);
	while (!list_empty(&destruction_req)) {
		struct thread *victim =
			list_entry(list_pop_front(&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current()->status = status;
	schedule();
}

static void schedule(void) {
	struct thread *curr = running_thread();
	struct thread *next = next_thread_to_run();

	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(curr->status != THREAD_RUNNING);
	ASSERT(is_thread(next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate(next);
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
			list_push_back(&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch(next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t allocate_tid(void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire(&tid_lock);
	tid = next_tid++;
	lock_release(&tid_lock);

	return tid;
}