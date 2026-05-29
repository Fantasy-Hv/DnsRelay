#include "test_utils.h"
#include "infra/stl.h"

static int int_comparator(T a, T b) {
    int va = *(int *)a, vb = *(int *)b;
    return (va > vb) - (va < vb);
}

// =========== Vector ===========
static void test_vector_create(void) {
    TEST("vector_create");
    Vector *v = vector_create(4);
    ASSERT_NOT_NULL(v);
    ASSERT_EQ(v->size, 0);
    ASSERT(v->capacity >= 4);
    vector_free(v);
    TEST_PASS();
}

static void test_vector_add_and_get(void) {
    TEST("vector_add / vector_get");
    Vector *v = vector_create(2);
    int a = 10, b = 20, c = 30;
    vector_add(v, &a);
    vector_add(v, &b);
    vector_add(v, &c); // 触发扩容
    ASSERT_EQ(vector_size(v), 3);
    ASSERT_EQ(*(int *)vector_get(v, 0), 10);
    ASSERT_EQ(*(int *)vector_get(v, 1), 20);
    ASSERT_EQ(*(int *)vector_get(v, 2), 30);
    vector_free(v);
    TEST_PASS();
}

static void test_vector_insert(void) {
    TEST("vector_insert");
    Vector *v = vector_create(2);
    int a = 1, b = 2, c = 3;
    vector_add(v, &a);
    vector_add(v, &c);
    vector_insert(v, &b, 1);
    ASSERT_EQ(vector_size(v), 3);
    ASSERT_EQ(*(int *)vector_get(v, 0), 1);
    ASSERT_EQ(*(int *)vector_get(v, 1), 2);
    ASSERT_EQ(*(int *)vector_get(v, 2), 3);
    vector_free(v);
    TEST_PASS();
}

static void test_vector_remove(void) {
    TEST("vector_remove");
    Vector *v = vector_create(3);
    int a = 1, b = 2, c = 3;
    vector_add(v, &a);
    vector_add(v, &b);
    vector_add(v, &c);
    vector_remove(v, 1);
    ASSERT_EQ(vector_size(v), 2);
    ASSERT_EQ(*(int *)vector_get(v, 0), 1);
    ASSERT_EQ(*(int *)vector_get(v, 1), 3);
    vector_free(v);
    TEST_PASS();
}

// =========== LinkedList ===========
static void test_linked_list_create_and_empty(void) {
    TEST("linked_list_create / is_empty");
    LinkedList *list = linked_list_create();
    ASSERT_NOT_NULL(list);
    ASSERT(linked_list_is_empty(list));
    linked_list_free(list);
    TEST_PASS();
}

static void test_linked_list_add_and_remove(void) {
    TEST("linked_list addFirst / addLast / remove");
    LinkedList *list = linked_list_create();
    int a = 1, b = 2, c = 3;
    linked_list_addFirst(list, &a);
    linked_list_addLast(list, &b);
    linked_list_addLast(list, &c);
    ASSERT_EQ(list->size, 3);
    ASSERT_EQ(*(int *)list->head->data, 1);
    ASSERT_EQ(*(int *)list->tail->data, 3);

    linked_list_remove(list, &b, int_comparator);
    ASSERT_EQ(list->size, 2);
    linked_list_clear(list);
    ASSERT(linked_list_is_empty(list));
    linked_list_free(list);
    TEST_PASS();
}

// =========== PriorityQueue ===========
static void test_pq_create_add_pop(void) {
    TEST("priority_queue add / pop");
    PriorityQueue *pq = priority_queue_create(int_comparator);
    ASSERT_NOT_NULL(pq);
    int a = 5, b = 3, c = 8, d = 1;
    priority_queue_add(pq, &a);
    priority_queue_add(pq, &b);
    priority_queue_add(pq, &c);
    priority_queue_add(pq, &d);
    ASSERT_EQ(*(int *)priority_queue_pop(pq), 1);
    ASSERT_EQ(*(int *)priority_queue_pop(pq), 3);
    ASSERT_EQ(*(int *)priority_queue_pop(pq), 5);
    ASSERT_EQ(*(int *)priority_queue_pop(pq), 8);
    ASSERT_NULL(priority_queue_pop(pq));
    priority_queue_free(pq);
    TEST_PASS();
}

static void test_pq_peek(void) {
    TEST("priority_queue peek");
    PriorityQueue *pq = priority_queue_create(int_comparator);
    int a = 9, b = 2;
    priority_queue_add(pq, &a);
    priority_queue_add(pq, &b);
    ASSERT_EQ(*(int *)priority_queue_peek(pq), 2);
    ASSERT_EQ(*(int *)priority_queue_peek(pq), 2); // still 2
    priority_queue_pop(pq);
    ASSERT_EQ(*(int *)priority_queue_peek(pq), 9);
    priority_queue_free(pq);
    TEST_PASS();
}

static void test_pq_remove(void) {
    TEST("priority_queue remove (lazy delete)");
    PriorityQueue *pq = priority_queue_create(int_comparator);
    int a = 7, b = 3, c = 5;
    priority_queue_add(pq, &a);
    priority_queue_add(pq, &b);
    priority_queue_add(pq, &c);
    priority_remove(pq, &b);
    ASSERT_EQ(*(int *)priority_queue_peek(pq), 5);  // 3 is lazy-deleted
    ASSERT_EQ(*(int *)priority_queue_pop(pq), 5);
    ASSERT_EQ(*(int *)priority_queue_pop(pq), 7);
    ASSERT_NULL(priority_queue_pop(pq));
    priority_queue_free(pq);
    TEST_PASS();
}

// =========== HashMap ===========
static void test_hash_map_put_get(void) {
    TEST("hash_map put / get");
    HashMap *map = hash_map_create(hash_func_str, compare_cstr);
    ASSERT_NOT_NULL(map);
    int c=0;
    hash_map_put(map,"server",(T)1);
    ASSERT_EQ(hash_map_get(map,"server",(T*)&c),0);
    ASSERT_EQ(c,1);
    c=0;
    ASSERT_EQ(hash_map_get(map,"log",(T*)&c),1);
    ASSERT_EQ(c,0);
    int a = 42, b = 99;
    hash_map_put(map, "answer", &a);
    hash_map_put(map, "other", &b);

    int *result = NULL;
    ASSERT_EQ(hash_map_get(map, "answer", (T *)&result), 0);
    ASSERT_EQ(*result, 42);
    ASSERT_EQ(hash_map_get(map, "other", (T *)&result), 0);
    ASSERT_EQ(*result, 99);
    ASSERT_EQ(hash_map_get(map, "nope", (T *)&result), 1); // miss
    hash_map_free(map);
    TEST_PASS();
}
void des(K key) {

}
static void test_hash_map_remove(void) {
    TEST("hash_map remove");
    HashMap *map = hash_map_create(hash_func_str, compare_cstr);
    int a = 7;
    hash_map_put(map, "x", &a);
    int *r = NULL;
    ASSERT_EQ(hash_map_get(map, "x", (T *)&r), 0);
    hash_map_remove(map, "x",des);
    ASSERT_EQ(hash_map_get(map, "x", (T *)&r), 1);
    hash_map_free(map);
    TEST_PASS();
}

int main(void) {
    test_vector_create();
    test_vector_add_and_get();
    test_vector_insert();
    test_vector_remove();

    test_linked_list_create_and_empty();
    test_linked_list_add_and_remove();

    test_pq_create_add_pop();
    test_pq_peek();
    test_pq_remove();

    test_hash_map_put_get();
    test_hash_map_remove();

    print_test_summary();
    return 0;
}
