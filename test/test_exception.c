#include "test_utils.h"
#include "infra/exception.h"
#include <string.h>

static void test_catch_returns_0_without_try(void) {
    TEST("ex_catch() == 0 without try");
    ASSERT_EQ(ex_catch(), 0);
    TEST_PASS();
}

static void test_catch_returns_0_after_try_only(void) {
    TEST("ex_try then ex_catch() == 0 (no throw)");
    ex_try();
    ASSERT_EQ(ex_catch(), 0);
    ex_end();
    TEST_PASS();
}

static void test_try_throw_catch(void) {
    TEST("ex_try + ex_throw → ex_catch() == 1");
    ex_try();
    ex_throw("something went wrong");
    ASSERT_EQ(ex_catch(), 1);
    ex_end();
    TEST_PASS();
}

static void test_end_returns_error_string(void) {
    TEST("ex_end() returns non-empty error string");
    ex_try();
    ex_throw("disk full");
    const char *msg = ex_end();
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    ASSERT(strstr(msg, "disk full") != NULL);
    ASSERT_EQ(ex_catch(), 0); // end closes context
    TEST_PASS();
}

static void test_end_returns_empty_without_error(void) {
    TEST("ex_end() returns \"\" without try/throw");
    const char *msg = ex_end();
    ASSERT_STREQ(msg, "");
    TEST_PASS();
}

static void test_end_returns_empty_after_try_only(void) {
    TEST("ex_end() returns \"\" after try only (no throw)");
    ex_try();
    const char *msg = ex_end();
    ASSERT_STREQ(msg, "");
    TEST_PASS();
}

static void test_end_returns_empty_on_second_call(void) {
    TEST("ex_end() returns \"\" on second call");
    ex_try();
    ex_throw("error one");
    const char *first = ex_end();
    ASSERT(strlen(first) > 0);

    const char *second = ex_end();
    ASSERT_STREQ(second, "");
    TEST_PASS();
}

static void test_throw_without_try_auto_opens(void) {
    TEST("ex_throw() without ex_try → auto-opens context");
    ex_throw("orphan error");
    ASSERT_EQ(ex_catch(), 1);
    const char *msg = ex_end();
    ASSERT(strstr(msg, "orphan error") != NULL);
    TEST_PASS();
}

static void test_multiple_throws_stack_errors(void) {
    TEST("multiple ex_throw → errors accumulate in stack");
    ex_try();
    ex_throw("layer1: bad fd");
    ex_throw("layer2: connect failed");
    ex_throw("layer3: timeout");
    const char *msg = ex_end();
    ASSERT(strstr(msg, "layer1: bad fd") != NULL);
    ASSERT(strstr(msg, "layer2: connect failed") != NULL);
    ASSERT(strstr(msg, "layer3: timeout") != NULL);
    // messages are stacked bottom-up, threaded by newline
    ASSERT(strstr(msg, "\n") != NULL);
    TEST_PASS();
}

static void test_throw_with_format_args(void) {
    TEST("ex_throw with format args");
    ex_try();
    ex_throw("retry %d/%d after %dms", 3, 5, 200);
    ASSERT_EQ(ex_catch(), 1);
    const char *msg = ex_end();
    ASSERT(strstr(msg, "retry 3/5 after 200ms") != NULL);
    TEST_PASS();
}

static void test_try_resets_error_state(void) {
    TEST("ex_try after throw → resets state (info lost)");
    ex_try();
    ex_throw("old error");
    ASSERT_EQ(ex_catch(), 1);
    ex_try(); // re-entrant, discards old error
    ASSERT_EQ(ex_catch(), 0);
    ex_end();
    TEST_PASS();
}

int main(void) {
    test_catch_returns_0_without_try();
    test_catch_returns_0_after_try_only();
    test_try_throw_catch();
    test_end_returns_error_string();
    test_end_returns_empty_without_error();
    test_end_returns_empty_after_try_only();
    test_end_returns_empty_on_second_call();
    test_throw_without_try_auto_opens();
    test_multiple_throws_stack_errors();
    test_throw_with_format_args();
    test_try_resets_error_state();
    print_test_summary();
    return 0;
}
