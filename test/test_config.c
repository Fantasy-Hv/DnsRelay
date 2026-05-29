#include "test_utils.h"
#include "infra/config.h"
#include "infra/stl.h"
#include <string.h>
#include <stdlib.h>

// parser: string → int (parsed value is heap-allocated int*)
static int int_parser(const char* key, const char* value, T* result) {
    (void)key;
    int* p = malloc(sizeof(int));
    *p = atoi(value);
    *result = p;
    return 0;
}

// cleaner for int parser
static void int_cleaner(const char* key, T value) {
    (void)key;
    free(value);
}

// =========== config ===========

static void test_config_init(void) {
    TEST("config_init");
    ASSERT_EQ(config_init(), 0);
    TEST_PASS();
}

static void test_config_inject_and_get_raw(void) {
    TEST("config_inject + config_get (raw string, no parser)");
    ASSERT_EQ(config_init(), 0);

    config_inject("test", "host", "127.0.0.1");
    config_inject("test", "port", "53");

    char* val = NULL;
    ASSERT_EQ(config_get("test", "host", (T*)&val), 0);
    ASSERT_STREQ(val, "127.0.0.1");

    ASSERT_EQ(config_get("test", "port", (T*)&val), 0);
    ASSERT_STREQ(val, "53");

    TEST_PASS();
}

static void test_config_inject_with_parser(void) {
    TEST("config_inject + config_get with parser (delayed parse)");
    ASSERT_EQ(config_init(), 0);
    config_register_parser("num", int_parser);
    config_register_cleaner("num", int_cleaner);

    config_inject("num", "max", "42");

    int* result = NULL;
    ASSERT_EQ(config_get("num", "max", (T*)&result), 0);
    ASSERT_EQ(*result, 42);
    // second get should still work (now cooked)
    int* result2 = NULL;
    ASSERT_EQ(config_get("num", "max", (T*)&result2), 0);
    ASSERT_EQ(*result2, 42);

    TEST_PASS();
}

static void test_config_set_cooked(void) {
    TEST("config_set with cooked value (no parsing needed)");
    ASSERT_EQ(config_init(), 0);

    int val = 8080;
    config_set("server", "port", (T)(intptr_t)val);

    int result = 0;
    ASSERT_EQ(config_get("server", "port", (T*)&result), 0);
    ASSERT_EQ(result, 8080);

    TEST_PASS();
}

static void test_config_set_overwrite(void) {
    TEST("config_set overwrites previous value");
    ASSERT_EQ(config_init(), 0);
    config_register_parser("s", int_parser);
    config_register_cleaner("s", int_cleaner);

    // inject raw, then overwrite with set
    config_inject("s", "x", "10");
    int* r1 = NULL;
    ASSERT_EQ(config_get("s", "x", (T*)&r1), 0);
    ASSERT_EQ(*r1, 10);

    config_set("s", "x", (T)(intptr_t)99);
    int r2 = 0;
    ASSERT_EQ(config_get("s", "x", (T*)&r2), 0);
    ASSERT_EQ(r2, 99);

    TEST_PASS();
}

static void test_config_get_miss(void) {
    TEST("config_get on missing key → 1");
    ASSERT_EQ(config_init(), 0);

    T dummy = NULL;
    ASSERT_EQ(config_get("nosection", "nokey", &dummy), 1);

    TEST_PASS();
}

static void test_config_get_null_value(void) {
    TEST("config_get with NULL value → -1");
    ASSERT_EQ(config_init(), 0);
    ASSERT_EQ(config_get("x", "y", NULL), -1);
    TEST_PASS();
}

static void test_config_inject_multiple_sections(void) {
    TEST("config_inject multiple sections");
    ASSERT_EQ(config_init(), 0);

    config_inject("db", "host", "localhost");
    config_inject("db", "port", "3306");
    config_inject("cache", "ttl", "60");

    char* v = NULL;
    ASSERT_EQ(config_get("db", "host", (T*)&v), 0);
    ASSERT_STREQ(v, "localhost");
    ASSERT_EQ(config_get("db", "port", (T*)&v), 0);
    ASSERT_STREQ(v, "3306");
    ASSERT_EQ(config_get("cache", "ttl", (T*)&v), 0);
    ASSERT_STREQ(v, "60");

    TEST_PASS();
}

static void test_config_inject_overwrite_raw(void) {
    TEST("config_inject overwrites previous raw value");
    ASSERT_EQ(config_init(), 0);

    config_inject("app", "mode", "debug");
    char* v = NULL;
    ASSERT_EQ(config_get("app", "mode", (T*)&v), 0);
    ASSERT_STREQ(v, "debug");

    config_inject("app", "mode", "release");
    ASSERT_EQ(config_get("app", "mode", (T*)&v), 0);
    ASSERT_STREQ(v, "release");

    TEST_PASS();
}

int main(void) {
    test_config_init();
    test_config_inject_and_get_raw();
    test_config_inject_with_parser();
    test_config_set_cooked();
    test_config_set_overwrite();
    test_config_get_miss();
    test_config_get_null_value();
    test_config_inject_multiple_sections();
    test_config_inject_overwrite_raw();
    print_test_summary();
    return 0;
}
