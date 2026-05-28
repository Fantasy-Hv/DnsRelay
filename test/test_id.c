#include "test_utils.h"
#include "dns/id.h"

static void test_id_init_and_alloc(void) {
    TEST("id init + alloc");
    ASSERT_EQ(id_pool_init(), 0);
    uint16_t id;
    ASSERT_EQ(id_alloc(&id), 0);
    ASSERT(id < 65536);
    TEST_PASS();
}

static void test_id_alloc_null(void) {
    TEST("id alloc with NULL pointer");
    ASSERT_EQ(id_pool_init(), 0);
    ASSERT_EQ(id_alloc(NULL), 1);
    TEST_PASS();
}

static void test_id_free_and_reuse(void) {
    TEST("id free + re-alloc (LIFO)");
    ASSERT_EQ(id_pool_init(), 0);
    uint16_t a, b, c;
    id_alloc(&a);
    id_alloc(&b);
    id_alloc(&c);
    // free b then re-alloc — should get b back (LIFO stack)
    id_free(b);
    uint16_t recovered;
    ASSERT_EQ(id_alloc(&recovered), 0);
    ASSERT_EQ(recovered, b);
    id_free(a);
    id_free(c);
    id_free(recovered);
    TEST_PASS();
}

static void test_id_free_unallocated_no_corruption(void) {
    TEST("free unallocated id → no corruption");
    ASSERT_EQ(id_pool_init(), 0);
    id_free(999); // never allocated, should be a no-op
    uint16_t id;
    ASSERT_EQ(id_alloc(&id), 0);
    ASSERT(id < 65536);
    id_free(id);
    TEST_PASS();
}

static void test_id_exhaustion(void) {
    TEST("alloc all 65536 ids then fail");
    ASSERT_EQ(id_pool_init(), 0);
    uint16_t id;
    for (int i = 0; i < 65536; i++) {
        ASSERT_EQ(id_alloc(&id), 0);
    }
    // pool exhausted
    ASSERT_EQ(id_alloc(&id), 1);
    // free one and re-alloc
    id_free(1);
    ASSERT_EQ(id_alloc(&id), 0);
    id_free(id);
    TEST_PASS();
}

static void test_id_double_free(void) {
    TEST("double free does not corrupt");
    ASSERT_EQ(id_pool_init(), 0);
    uint16_t a, b;
    id_alloc(&a);
    id_alloc(&b);
    id_free(a);
    id_free(a); // double free — should be no-op on second call
    uint16_t recovered;
    id_alloc(&recovered);
    ASSERT_EQ(recovered, a); // still LIFO
    id_free(b);
    id_free(recovered);
    TEST_PASS();
}

int main(void) {
    test_id_init_and_alloc();
    test_id_alloc_null();
    test_id_free_and_reuse();
    test_id_free_unallocated_no_corruption();
    test_id_exhaustion();
    test_id_double_free();
    print_test_summary();
    return 0;
}
