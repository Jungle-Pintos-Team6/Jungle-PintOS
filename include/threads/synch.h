#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* 카운팅 세마포어. */
struct semaphore {
    unsigned value;             /* 현재 값. */
    struct list waiters;        /* 대기 중인 스레드 리스트. */
};

/* SEMA를 VALUE로 초기화. */
void sema_init(struct semaphore *sema, unsigned value);

/* 세마포어에 대한 다운(P) 연산. SEMA의 값이 양수가 될 때까지 대기 후 감소. */
void sema_down(struct semaphore *sema);

/* 세마포어에 대한 다운(P) 연산 시도. 세마포어 값이 0이 아닐 때만 수행.
   세마포어 값 감소 시 true 반환, 그렇지 않으면 false 반환. */
bool sema_try_down(struct semaphore *sema);

/* 세마포어에 대한 업(V) 연산. SEMA의 값을 증가시키고, 대기 중인 스레드가 있으면 하나를 깨움. */
void sema_up(struct semaphore *sema);

/* sema_try_down()과 sema_up() 구현의 원자성을 확인하는 세마포어 자체 테스트. */
void sema_self_test(void);

/* 락(Lock). */
struct lock {
    struct thread *holder;      /* 락을 보유 중인 스레드 (디버깅용). */
    struct semaphore semaphore; /* 접근을 제어하는 이진 세마포어. */
};

/* LOCK 초기화. 락은 처음 초기화 시 어떤 스레드도 소유하지 않음. */
void lock_init(struct lock *lock);

/* LOCK 획득. 필요 시 사용 가능해질 때까지 대기. 현재 스레드가 이미 락을 보유 중이면 안 됨. */
void lock_acquire(struct lock *lock);

/* LOCK 획득 시도. 성공 시 true, 실패 시 false 반환. 현재 스레드가 이미 락을 보유 중이면 안 됨. */
bool lock_try_acquire(struct lock *lock);

/* 현재 스레드가 소유한 LOCK 해제. */
void lock_release(struct lock *lock);

/* 현재 스레드의 LOCK 보유 여부 확인. 보유 중이면 true, 아니면 false 반환. */
bool lock_held_by_current_thread(const struct lock *lock);

/* 조건 변수. */
struct condition {
    struct list waiters;        /* 대기 중인 스레드 리스트. */
};

/* 조건 변수 COND 초기화. */
void cond_init(struct condition *cond);

/* LOCK을 원자적으로 해제하고 COND에 시그널이 올 때까지 대기. 시그널 수신 후 반환 전 LOCK 재획득. */
void cond_wait(struct condition *cond, struct lock *lock);

/* COND(LOCK으로 보호)에서 대기 중인 스레드 하나에 깨우라는 시그널 전송. 호출 전 LOCK 보유 필요. */
void cond_signal(struct condition *cond, struct lock *lock);

/* COND(LOCK으로 보호)에서 대기 중인 모든 스레드를 깨움. 호출 전 LOCK 보유 필요. */
void cond_broadcast(struct condition *cond, struct lock *lock);

/* 최적화 장벽.
 * 컴파일러는 최적화 장벽을 가로질러 연산을 재정렬하지 않음.
 * 자세한 정보는 레퍼런스 가이드의 "최적화 장벽" 섹션 참조. */
#define barrier() asm volatile("" : : : "memory")

#endif /* threads/synch.h */