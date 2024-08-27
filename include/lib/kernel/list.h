#ifndef __LIB_KERNEL_LIST_H
#define __LIB_KERNEL_LIST_H

/* Doubly linked list.
 *
 * This implementation of a doubly linked list does not require
 * use of dynamically allocated memory.  Instead, each structure
 * that is a potential list element must embed a struct list_elem
 * member.  All of the list functions operate on these `struct
 * list_elem's.  The list_entry macro allows conversion from a
 * struct list_elem back to a structure object that contains it.

 * For example, suppose there is a needed for a list of `struct
 * foo'.  `struct foo' should contain a `struct list_elem'
 * member, like so:

 * struct foo {
 *   struct list_elem elem;
 *   int bar;
 *   ...other members...
 * };

 * Then a list of `struct foo' can be be declared and initialized
 * like so:

 * struct list foo_list;

 * list_init (&foo_list);

 * Iteration is a typical situation where it is necessary to
 * convert from a struct list_elem back to its enclosing
 * structure.  Here's an example using foo_list:

 * struct list_elem *e;

 * for (e = list_begin (&foo_list); e != list_end (&foo_list);
 * e = list_next (e)) {
 *   struct foo *f = list_entry (e, struct foo, elem);
 *   ...do something with f...
 * }

 * You can find real examples of list usage throughout the
 * source; for example, malloc.c, palloc.c, and thread.c in the
 * threads directory all use lists.

 * The interface for this list is inspired by the list<> template
 * in the C++ STL.  If you're familiar with list<>, you should
 * find this easy to use.  However, it should be emphasized that
 * these lists do *no* type checking and can't do much other
 * correctness checking.  If you screw up, it will bite you.

 * Glossary of list terms:

 * - "front": The first element in a list.  Undefined in an
 * empty list.  Returned by list_front().

 * - "back": The last element in a list.  Undefined in an empty
 * list.  Returned by list_back().

 * - "tail": The element figuratively just after the last
 * element of a list.  Well defined even in an empty list.
 * Returned by list_end().  Used as the end sentinel for an
 * iteration from front to back.

 * - "beginning": In a non-empty list, the front.  In an empty
 * list, the tail.  Returned by list_begin().  Used as the
 * starting point for an iteration from front to back.

 * - "head": The element figuratively just before the first
 * element of a list.  Well defined even in an empty list.
 * Returned by list_rend().  Used as the end sentinel for an
 * iteration from back to front.

 * - "reverse beginning": In a non-empty list, the back.  In an
 * empty list, the head.  Returned by list_rbegin().  Used as
 * the starting point for an iteration from back to front.
 *
 * - "interior element": An element that is not the head or
 * tail, that is, a real list element.  An empty list does
 * not have any interior elements.*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 리스트 요소 */
struct list_elem {
    struct list_elem *prev; /* 이전 리스트 요소 */
    struct list_elem *next; /* 다음 리스트 요소 */
};

/* 리스트 */
struct list {
    struct list_elem head; /* 리스트 헤드 */
    struct list_elem tail; /* 리스트 테일 */
};

/* LIST_ELEM 포인터를 포함하는 구조체의 포인터로 변환
   STRUCT는 외부 구조체 이름, MEMBER는 리스트 요소의 멤버 이름 */
#define list_entry(LIST_ELEM, STRUCT, MEMBER)                                  \
    ((STRUCT *)((uint8_t *)&(LIST_ELEM)->next - offsetof(STRUCT, MEMBER.next)))

/* 리스트 초기화 */
void list_init(struct list *);

/* 리스트 순회 */
struct list_elem *list_begin(struct list *);  /* 리스트의 첫 번째 요소 반환 */
struct list_elem *list_next(struct list_elem *);  /* 다음 요소 반환 */
struct list_elem *list_end(struct list *);  /* 마지막 요소 다음의 요소 반환 */

struct list_elem *list_rbegin(struct list *);  /* 리스트의 마지막 요소 반환 */
struct list_elem *list_prev(struct list_elem *);  /* 이전 요소 반환 */
struct list_elem *list_rend(struct list *);  /* 첫 번째 요소 이전의 요소 반환 */

struct list_elem *list_head(struct list *);  /* 리스트 헤드 반환 */
struct list_elem *list_tail(struct list *);  /* 리스트 테일 반환 */

/* 리스트 삽입 */
void list_insert(struct list_elem *, struct list_elem *);  /* BEFORE 바로 앞에 ELEM 삽입 */
void list_splice(struct list_elem *before,
                 struct list_elem *first, struct list_elem *last);  /* [FIRST, LAST) 범위의 요소들을 BEFORE 앞으로 이동 */
void list_push_front(struct list *, struct list_elem *);  /* ELEM을 LIST의 시작에 삽입 */
void list_push_back(struct list *, struct list_elem *);  /* ELEM을 LIST의 끝에 삽입 */

/* 리스트 제거 */
struct list_elem *list_remove(struct list_elem *);  /* ELEM을 리스트에서 제거하고 그 다음 요소 반환 */
struct list_elem *list_pop_front(struct list *);  /* LIST의 첫 번째 요소 제거 후 반환 */
struct list_elem *list_pop_back(struct list *);  /* LIST의 마지막 요소 제거 후 반환 */

/* 리스트 요소 */
struct list_elem *list_front(struct list *);  /* LIST의 첫 번째 요소 반환 */
struct list_elem *list_back(struct list *);  /* LIST의 마지막 요소 반환 */

/* 리스트 속성 */
size_t list_size(struct list *);  /* LIST의 요소 개수 반환 */
bool list_empty(struct list *);  /* LIST가 비어있으면 true, 아니면 false 반환 */

/* 기타 */
void list_reverse(struct list *);  /* LIST의 순서를 뒤집음 */

/* 두 리스트 요소 A와 B의 값을 비교하는 함수 타입
   A가 B보다 작으면 true, 크거나 같으면 false 반환 */
typedef bool list_less_func(const struct list_elem *a,
                            const struct list_elem *b, void *aux);

/* 정렬된 요소를 가진 리스트에 대한 연산 */
void list_sort(struct list *, list_less_func *, void *aux);  /* LIST를 LESS 함수에 따라 정렬 */
void list_insert_ordered(struct list *, struct list_elem *,
                         list_less_func *, void *aux);  /* ELEM을 LIST의 적절한 위치에 삽입 */
void list_unique(struct list *, struct list *duplicates,
                 list_less_func *, void *aux);  /* LIST에서 중복 요소 제거 */

/* 최대값과 최소값 */
struct list_elem *list_max(struct list *, list_less_func *, void *aux);  /* LIST에서 가장 큰 값을 가진 요소 반환 */
struct list_elem *list_min(struct list *, list_less_func *, void *aux);  /* LIST에서 가장 작은 값을 가진 요소 반환 */

#endif /* lib/kernel/list.h */