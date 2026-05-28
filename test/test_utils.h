#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int _tests_run = 0;
static int _tests_passed = 0;
static const char *_current_test = NULL;

#define TEST(name) \
    do { \
        _current_test = name; \
        printf("  [RUN ] %s\n", name); \
        _tests_run++; \
    } while (0)

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            printf("  [FAIL] %s:%d: assert(%s)\n", _current_test, __LINE__, #cond); \
            return; \
        } \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            printf("  [FAIL] %s:%d: %s == %s  (left=%d, right=%d)\n", \
                   _current_test, __LINE__, #a, #b, (int)(a), (int)(b)); \
            return; \
        } \
    } while (0)

#define ASSERT_STREQ(a, b) \
    do { \
        if (strcmp((a), (b)) != 0) { \
            printf("  [FAIL] %s:%d: strcmp(%s, %s)  (left=\"%s\", right=\"%s\")\n", \
                   _current_test, __LINE__, #a, #b, (a), (b)); \
            return; \
        } \
    } while (0)

#define ASSERT_NOT_NULL(p) \
    do { \
        if ((p) == NULL) { \
            printf("  [FAIL] %s:%d: %s is NULL\n", _current_test, __LINE__, #p); \
            return; \
        } \
    } while (0)

#define ASSERT_NULL(p) \
    do { \
        if ((p) != NULL) { \
            printf("  [FAIL] %s:%d: %s is not NULL\n", _current_test, __LINE__, #p); \
            return; \
        } \
    } while (0)

#define TEST_PASS() \
    do { \
        _tests_passed++; \
        printf("  [OK  ] %s\n", _current_test); \
    } while (0)

static inline void print_test_summary(void) {
    printf("\n%d/%d tests passed.\n", _tests_passed, _tests_run);
    if (_tests_passed != _tests_run)
        exit(1);
}

#endif // TEST_UTILS_H
