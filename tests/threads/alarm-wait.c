/* 이 프로그램은 N개의 스레드를 생성하고, 각 스레드가 서로 다른 고정된 시간 동안
   M번 대기하도록 합니다. 스레드들의 깨어나는 순서를 기록하고 그 순서가 유효한지
   확인합니다. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static void test_sleep(int thread_cnt, int iterations);

// 단일 알람 테스트: 5개의 스레드를 생성하고 각각 1번씩 대기
void test_alarm_single(void) { test_sleep(5, 1); }

// 다중 알람 테스트: 5개의 스레드를 생성하고 각각 7번씩 대기
void test_alarm_multiple(void) { test_sleep(5, 7); }

/* 전체 테스트에 대한 정보를 담는 구조체 */
struct sleep_test {
	int64_t start;	// 테스트 시작 시 현재 시간
	int iterations; // 각 스레드가 대기해야 하는 횟수

	/* 출력 관련 */
	struct lock output_lock; // 출력 버퍼를 보호하기 위한 락
	int *output_pos;		 // 출력 버퍼의 현재 위치
};

/* 개별 스레드에 대한 정보를 담는 구조체 */
struct sleep_thread {
	struct sleep_test *test; // 모든 스레드가 공유하는 테스트 정보
	int id;					 // 스레드 식별자
	int duration;			 // 대기해야 할 틱 수
	int iterations;			 // 현재까지 대기한 횟수
};

static void sleeper(void *);

/* THREAD_CNT 개의 스레드를 생성하고 각각 ITERATIONS 번씩 대기하도록 하는 테스트
 * 함수 */
static void test_sleep(int thread_cnt, int iterations) {
	struct sleep_test test;
	struct sleep_thread *threads;
	int *output, *op;
	int product;
	int i;

	/* 이 테스트는 MLFQS와 호환되지 않습니다 */
	ASSERT(!thread_mlfqs);

	msg("Creating %d threads to sleep %d times each.", thread_cnt, iterations);
	msg("Thread 0 sleeps 10 ticks each time,");
	msg("thread 1 sleeps 20 ticks each time, and so on.");
	msg("If successful, product of iteration count and");
	msg("sleep duration will appear in nondescending order.");

	/* 필요한 메모리 할당 */
	threads = malloc(sizeof *threads * thread_cnt);
	output = malloc(sizeof *output * iterations * thread_cnt * 2);
	if (threads == NULL || output == NULL)
		PANIC("couldn't allocate memory for test");

	// 테스트 구조체 초기화
	test.start = timer_ticks() + 100; // 현재 시간 + 100틱을 시작 시간으로 설정
	test.iterations = iterations;
	lock_init(&test.output_lock);
	test.output_pos = output;

	// 스레드들 생성 및 시작
	ASSERT(output != NULL);
	for (i = 0; i < thread_cnt; i++) {
		struct sleep_thread *t = threads + i;
		char name[16];

		t->test = &test;
		t->id = i;
		t->duration = (i + 1) * 10; // 각 스레드마다 10틱씩 증가하는 대기 시간
		t->iterations = 0;

		snprintf(name, sizeof name, "thread %d", i);
		thread_create(name, PRI_DEFAULT, sleeper, t);
	}

	// 모든 스레드가 완료될 때까지 충분히 대기
	timer_sleep(100 + thread_cnt * iterations * 10 + 100);

	// 혹시 남아있는 스레드를 위해 출력 락 획득
	lock_acquire(&test.output_lock);

	// 스레드들이 깨어난 순서대로 결과 출력 및 검증
	product = 0;
	for (op = output; op < test.output_pos; op++) {
		struct sleep_thread *t;
		int new_prod;

		ASSERT(*op >= 0 && *op < thread_cnt);
		t = threads + *op;

		new_prod = ++t->iterations * t->duration;

		msg("thread %d: duration=%d, iteration=%d, product=%d", t->id,
			t->duration, t->iterations, new_prod);

		// 깨어난 순서가 올바른지 검증
		if (new_prod >= product)
			product = new_prod;
		else
			fail("thread %d woke up out of order (%d > %d)!", t->id, product,
				 new_prod);
	}

	// 각 스레드가 정확히 iterations 횟수만큼 깨어났는지 확인
	for (i = 0; i < thread_cnt; i++)
		if (threads[i].iterations != iterations)
			fail("thread %d woke up %d times instead of %d", i,
				 threads[i].iterations, iterations);

	lock_release(&test.output_lock);
	free(output);
	free(threads);
}

// 개별 스레드가 실행하는 함수
static void sleeper(void *t_) {
	struct sleep_thread *t = t_;
	struct sleep_test *test = t->test;
	int i;

	for (i = 1; i <= test->iterations; i++) {
		int64_t sleep_until = test->start + i * t->duration;
		timer_sleep(sleep_until - timer_ticks());
		lock_acquire(&test->output_lock);
		*test->output_pos++ = t->id; // 깨어난 순서 기록
		lock_release(&test->output_lock);
	}
}