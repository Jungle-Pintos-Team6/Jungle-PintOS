#include "list.h"
#include "../debug.h"

/* Our doubly linked lists have two header elements: the "head"
   just before the first element and the "tail" just after the
   last element.  The `prev' link of the front header is null, as
   is the `next' link of the back header.  Their other two links
   point toward each other via the interior elements of the list.

   An empty list looks like this:

   +------+     +------+
   <---| head |<--->| tail |--->
   +------+     +------+

   A list with two elements in it looks like this:

   +------+     +-------+     +-------+     +------+
   <---| head |<--->|   1   |<--->|   2   |<--->| tail |<--->
   +------+     +-------+     +-------+     +------+

   The symmetry of this arrangement eliminates lots of special
   cases in list processing.  For example, take a look at
   list_remove(): it takes only two pointer assignments and no
   conditionals.  That's a lot simpler than the code would be
   without header elements.

   (Because only one of the pointers in each header element is used,
   we could in fact combine them into a single header element
   without sacrificing this simplicity.  But using two separate
   elements allows us to do a little bit of checking on some
   operations, which can be valuable.) */

static bool is_sorted(struct list_elem *a, struct list_elem *b,
                      list_less_func *less, void *aux) UNUSED;
// a에서 b까지 리스트 요소 정렬 확인 함수
// UNUSED: 컴파일러에 함수 미사용 가능성 알림

/* ELEM이 헤드인지 확인 */
static inline bool is_head(struct list_elem *elem) {
    return elem != NULL && elem->prev == NULL && elem->next != NULL;
}
// elem이 NULL 아님, 이전 요소 없음, 다음 요소 있음 확인

/* ELEM이 내부 요소인지 확인 */
static inline bool is_interior(struct list_elem *elem) {
    return elem != NULL && elem->prev != NULL && elem->next != NULL;
}
// elem이 NULL 아님, 이전 및 다음 요소 모두 있음 확인

/* ELEM이 꼬리인지 확인 */
static inline bool is_tail(struct list_elem *elem) {
    return elem != NULL && elem->prev != NULL && elem->next == NULL;
}
// elem이 NULL 아님, 이전 요소 있음, 다음 요소 없음 확인

/* LIST를 빈 리스트로 초기화 */
void list_init(struct list *list) {
    ASSERT(list != NULL);
    list->head.prev = NULL;
    list->head.next = &list->tail;
    list->tail.prev = &list->head;
    list->tail.next = NULL;
}
// 헤드와 꼬리 초기화, 서로 연결

/* LIST의 시작 반환 */
struct list_elem *list_begin(struct list *list) {
    ASSERT(list != NULL);
    return list->head.next;
}
// 첫 번째 요소(헤드 다음) 반환

/* ELEM 다음 요소 반환 */
struct list_elem *list_next(struct list_elem *elem) {
    ASSERT(is_head(elem) || is_interior(elem));
    return elem->next;
}
// 다음 요소 반환, 헤드나 내부 요소에만 사용

/* LIST의 꼬리 반환 */
struct list_elem *list_end(struct list *list) {
    ASSERT(list != NULL);
    return &list->tail;
}
// 꼬리(더미 노드) 반환

/* LIST의 역방향 시작 반환 */
struct list_elem *list_rbegin(struct list *list) {
    ASSERT(list != NULL);
    return list->tail.prev;
}
// 마지막 유효 요소 반환

/* ELEM의 이전 요소 반환 */
struct list_elem *list_prev(struct list_elem *elem) {
    ASSERT(is_interior(elem) || is_tail(elem));
    return elem->prev;
}
// 이전 요소 반환, 내부 요소나 꼬리에만 사용

/* LIST의 헤드 반환 */
struct list_elem *list_rend(struct list *list) {
    ASSERT(list != NULL);
    return &list->head;
}
// 헤드(더미 노드) 반환

/* LIST의 헤드 반환 */
struct list_elem *list_head(struct list *list) {
    ASSERT(list != NULL);
    return &list->head;
}
// 헤드 반환

/* LIST의 꼬리 반환 */
struct list_elem *list_tail(struct list *list) {
    ASSERT(list != NULL);
    return &list->tail;
}
// 꼬리 반환

/* BEFORE 앞에 ELEM 삽입 */
void list_insert(struct list_elem *before, struct list_elem *elem) {
    ASSERT(is_interior(before) || is_tail(before));
    ASSERT(elem != NULL);

    elem->prev = before->prev;
    elem->next = before;
    before->prev->next = elem;
    before->prev = elem;
}
// 새 요소 특정 위치에 삽입, 포인터 조정

/* FIRST부터 LAST(제외) 요소들 제거 후 BEFORE 앞에 삽입 */
void list_splice(struct list_elem *before, struct list_elem *first,
                 struct list_elem *last) {
    ASSERT(is_interior(before) || is_tail(before));
    if (first == last)
        return;
    last = list_prev(last);

    ASSERT(is_interior(first));
    ASSERT(is_interior(last));

    /* FIRST...LAST 현재 리스트에서 제거 */
    first->prev->next = last->next;
    last->next->prev = first->prev;

    /* FIRST...LAST 새 리스트에 삽입 */
    first->prev = before->prev;
    last->next = before;
    before->prev->next = first;
    before->prev = last;
}
// 리스트 일부 다른 위치로 이동

/* ELEM을 LIST 시작에 삽입 */
void list_push_front(struct list *list, struct list_elem *elem) {
    list_insert(list_begin(list), elem);
}
// 요소를 리스트 맨 앞에 추가

/* ELEM을 LIST 끝에 삽입 */
void list_push_back(struct list *list, struct list_elem *elem) {
    list_insert(list_end(list), elem);
}
// 요소를 리스트 맨 뒤에 추가

/* ELEM을 리스트에서 제거하고 다음 요소 반환 */
struct list_elem *list_remove(struct list_elem *elem) {
    ASSERT(is_interior(elem));
    elem->prev->next = elem->next;
    elem->next->prev = elem->prev;
    return elem->next;
}
// 요소 제거 후 다음 요소 반환

/* LIST 앞 요소 제거 및 반환 */
struct list_elem *list_pop_front(struct list *list) {
    struct list_elem *front = list_front(list);
    list_remove(front);
    return front;
}
// 첫 번째 요소 제거 및 반환

/* LIST 뒤 요소 제거 및 반환 */
struct list_elem *list_pop_back(struct list *list) {
    struct list_elem *back = list_back(list);
    list_remove(back);
    return back;
}
// 마지막 요소 제거 및 반환

/* LIST 앞 요소 반환 */
struct list_elem *list_front(struct list *list) {
    ASSERT(!list_empty(list));
    return list->head.next;
}
// 첫 번째 요소 반환

/* LIST 뒤 요소 반환 */
struct list_elem *list_back(struct list *list) {
    ASSERT(!list_empty(list));
    return list->tail.prev;
}
// 마지막 요소 반환

/* LIST의 요소 개수 반환 */
size_t list_size(struct list *list) {
    struct list_elem *e;
    size_t cnt = 0;

    for (e = list_begin(list); e != list_end(list); e = list_next(e))
        cnt++;
    return cnt;
}
// O(n) 시간 복잡도로 리스트 순회하며 요소 개수 계산

/* LIST가 비어있는지 확인 */
bool list_empty(struct list *list) {
    return list_begin(list) == list_end(list);
}
// 시작과 끝이 같으면 빈 리스트

/* A와 B가 가리키는 포인터 교환 */
static void swap(struct list_elem **a, struct list_elem **b) {
    struct list_elem *t = *a;
    *a = *b;
    *b = t;
}
// 임시 변수 사용하여 포인터 값 교환

/* LIST 순서 뒤집기 */
void list_reverse(struct list *list) {
    if (!list_empty(list)) {
        struct list_elem *e;

        for (e = list_begin(list); e != list_end(list); e = e->prev)
            swap(&e->prev, &e->next);
        swap(&list->head.next, &list->tail.prev);
        swap(&list->head.next->prev, &list->tail.prev->next);
    }
}
// 각 요소의 prev, next 포인터 교환 후 헤드와 테일 조정

/* A부터 B까지 요소들이 LESS에 따라 정렬되어 있는지 확인 */
static bool is_sorted(struct list_elem *a, struct list_elem *b,
                      list_less_func *less, void *aux) {
    if (a != b)
        while ((a = list_next(a)) != b)
            if (less(a, list_prev(a), aux))
                return false;
    return true;
}
// 연속된 요소 쌍 비교하여 정렬 상태 확인

/* A에서 시작하여 B를 넘지 않는 범위에서 비감소 순서의 연속된 요소들 찾기 */
static struct list_elem *find_end_of_run(struct list_elem *a,
                                         struct list_elem *b,
                                         list_less_func *less, void *aux) {
    ASSERT(a != NULL);
    ASSERT(b != NULL);
    ASSERT(less != NULL);
    ASSERT(a != b);

    do {
        a = list_next(a);
    } while (a != b && !less(a, list_prev(a), aux));
    return a;
}
// 연속된 비감소 순서의 요소들 끝 지점 찾기

/* A0부터 A1B0까지와 A1B0부터 B1까지 병합 */
static void inplace_merge(struct list_elem *a0, struct list_elem *a1b0,
                          struct list_elem *b1, list_less_func *less,
                          void *aux) {
    ASSERT(a0 != NULL);
    ASSERT(a1b0 != NULL);
    ASSERT(b1 != NULL);
    ASSERT(less != NULL);
    ASSERT(is_sorted(a0, a1b0, less, aux));
    ASSERT(is_sorted(a1b0, b1, less, aux));

    while (a0 != a1b0 && a1b0 != b1)
        if (!less(a1b0, a0, aux))
            a0 = list_next(a0);
        else {
            a1b0 = list_next(a1b0);
            list_splice(a0, list_prev(a1b0), a1b0);
        }
}
// 두 정렬된 부분 리스트 병합

/* LIST를 LESS에 따라 정렬 */
void list_sort(struct list *list, list_less_func *less, void *aux) {
    size_t output_run_cnt;

    ASSERT(list != NULL);
    ASSERT(less != NULL);

    do {
        struct list_elem *a0;
        struct list_elem *a1b0;
        struct list_elem *b1;

        output_run_cnt = 0;
        for (a0 = list_begin(list); a0 != list_end(list); a0 = b1) {
            output_run_cnt++;

            a1b0 = find_end_of_run(a0, list_end(list), less, aux);
            if (a1b0 == list_end(list))
                break;
            b1 = find_end_of_run(a1b0, list_end(list), less, aux);

            inplace_merge(a0, a1b0, b1, less, aux);
        }
    } while (output_run_cnt > 1);

    ASSERT(is_sorted(list_begin(list), list_end(list), less, aux));
}
// 자연 병합 정렬 구현, O(n log n) 시간, O(1) 공간 복잡도

/* ELEM을 LIST의 적절한 위치에 삽입 */
void list_insert_ordered(struct list *list, struct list_elem *elem,
                         list_less_func *less, void *aux) {
    struct list_elem *e;

    ASSERT(list != NULL);
    ASSERT(elem != NULL);
    ASSERT(less != NULL);

    for (e = list_begin(list); e != list_end(list); e = list_next(e))
        if (less(elem, e, aux))
            break;
    return list_insert(e, elem);
}
// 정렬된 리스트에 새 요소 삽입, 평균 O(n) 시간 복잡도

/* LIST에서 중복 요소 제거 */
void list_unique(struct list *list, struct list *duplicates,
                 list_less_func *less, void *aux) {
    struct list_elem *elem, *next;

    ASSERT(list != NULL);
    ASSERT(less != NULL);
    if (list_empty(list))
        return;

    elem = list_begin(list);
    while ((next = list_next(elem)) != list_end(list))
        if (!less(elem, next, aux) && !less(next, elem, aux)) {
            list_remove(next);
            if (duplicates != NULL)
                list_push_back(duplicates, next);
        } else
            elem = next;
}
// 인접한 중복 요소 제거, 옵션으로 중복 요소 별도 리스트에 저장

/* LIST에서 최대값 요소 반환 */
struct list_elem *list_max(struct list *list, list_less_func *less, void *aux) {
    struct list_elem *max = list_begin(list);
    if (max != list_end(list)) {
        struct list_elem *e;

        for (e = list_next(max); e != list_end(list); e = list_next(e))
            if (less(max, e, aux))
                max = e;
    }
    return max;
}
// 리스트 순회하며 최대값 요소 찾기

/* LIST에서 최소값 요소 반환 */
struct list_elem *list_min(struct list *list, list_less_func *less, void *aux) {
    struct list_elem *min = list_begin(list);
    if (min != list_end(list)) {
        struct list_elem *e;

        for (e = list_next(min); e != list_end(list); e = list_next(e))
            if (less(e, min, aux))
                min = e;
    }
    return min;
}
// 리스트 순회하며 최소값 요소 찾기