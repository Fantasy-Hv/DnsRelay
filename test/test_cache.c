#include "test_utils.h"
#include "dns/cache.h"
#include "dns/protocol.h"
#include "infra/config.h"
#include "infra/logger.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>

#define LRU_FILL_SIZE 1024

// =========== helpers ===========

static void free_cv_rrs(CacheValue *cv) {
    if (cv == NULL || cv->rrs == NULL) return;
    for (int i = 0; i < vector_size(cv->rrs); i++)
        rr_free(vector_get(cv->rrs, i));
    vector_free(cv->rrs);
    cv->rrs = NULL;
}

static CacheValue make_cv_a(const char *name, uint32_t ttl, uint32_t ip) {
    CacheValue cv = {0};
    cv.rrs = vector_create(4);
    ResourceRecord *rr = rr_create();
    rr->name = strdup(name);
    rr->type = QTYPE_A;
    rr->class = QCLASS_IN;
    rr->ttl = ttl;
    rr->rdata = malloc(4);
    memcpy(rr->rdata, &ip, 4);
    rr->rdata_length = 4;
    vector_add(cv.rrs, rr);
    cv.answer_RRs = 1;
    return cv;
}

static CacheValue make_cv_aaaa(const char *name, uint32_t ttl,
                                uint16_t a, uint16_t b, uint16_t c, uint16_t d,
                                uint16_t e, uint16_t f, uint16_t g, uint16_t h) {
    CacheValue cv = {0};
    cv.rrs = vector_create(4);
    ResourceRecord *rr = rr_create();
    rr->name = strdup(name);
    rr->type = QTYPE_AAAA;
    rr->class = QCLASS_IN;
    rr->ttl = ttl;
    rr->rdata = malloc(16);
    uint16_t words[8] = {a, b, c, d, e, f, g, h};
    for (int i = 0; i < 8; i++)
        ((uint16_t*)rr->rdata)[i] = htons(words[i]);
    rr->rdata_length = 16;
    vector_add(cv.rrs, rr);
    cv.answer_RRs = 1;
    return cv;
}

static CacheValue make_cv_with_counts(const char *name, uint32_t ttl, uint32_t ip,
                                       uint16_t answer, uint16_t auth, uint16_t addi) {
    CacheValue cv = make_cv_a(name, ttl, ip);
    cv.answer_RRs = answer;
    cv.authority_RRs = auth;
    cv.additional_RRs = addi;
    return cv;
}

static CacheValue make_cv_empty(void) {
    CacheValue cv = {0};
    cv.rrs = vector_create(4);
    return cv;
}

static void setup(void) {
    dns_cache_free();
    ASSERT_EQ(dns_cache_init(), 0);
}

static void teardown(void) {
    dns_cache_free();
}

// =========== init ===========

static void test_cache_init_returns_zero(void) {
    TEST("dns_cache_init returns 0");
    dns_cache_free();
    ASSERT_EQ(dns_cache_init(), 0);
    dns_cache_free();
    TEST_PASS();
}

static void test_cache_init_idempotent(void) {
    TEST("dns_cache_init is idempotent");
    dns_cache_free();
    ASSERT_EQ(dns_cache_init(), 0);
    ASSERT_EQ(dns_cache_init(), 0);
    dns_cache_free();
    TEST_PASS();
}

// =========== put — parameter validation ===========

static void test_cache_put_null_qname(void) {
    TEST("dns_cache_put(NULL qname) returns 1");
    setup();
    CacheValue cv = make_cv_a("test.com", 60, 0x7f000001);
    ASSERT_EQ(dns_cache_put(NULL, QTYPE_A, QCLASS_IN, cv), 1);
    free_cv_rrs(&cv);
    teardown();
    TEST_PASS();
}

static void test_cache_put_null_rrs(void) {
    TEST("dns_cache_put with NULL rrs returns 1");
    setup();
    CacheValue cv = {0};
    ASSERT_EQ(dns_cache_put("example.com", QTYPE_A, QCLASS_IN, cv), 1);
    teardown();
    TEST_PASS();
}

static void test_cache_put_empty_rrs(void) {
    TEST("dns_cache_put with empty rrs vector is accepted (API only checks NULL)");
    setup();
    CacheValue cv = make_cv_empty();
    ASSERT_EQ(dns_cache_put("example.com", QTYPE_A, QCLASS_IN, cv), 0);
    vector_free(cv.rrs);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_A, QCLASS_IN, &result), 0);
    ASSERT_NOT_NULL(result.rrs);
    ASSERT_EQ(vector_size(result.rrs), 0);
    free_cv_rrs(&result);
    teardown();
    TEST_PASS();
}

// =========== put — success ===========

static void test_cache_put_success_a(void) {
    TEST("dns_cache_put valid A record returns 0");
    setup();
    CacheValue cv = make_cv_a("example.com", 60, 0x7f000001);
    ASSERT_EQ(dns_cache_put("example.com", QTYPE_A, QCLASS_IN, cv), 0);
    free_cv_rrs(&cv);
    teardown();
    TEST_PASS();
}

static void test_cache_put_success_aaaa(void) {
    TEST("dns_cache_put valid AAAA record returns 0");
    setup();
    CacheValue cv = make_cv_aaaa("example.com", 60,
                                  0x2001, 0x0db8, 0x0000, 0x0000,
                                  0x0000, 0x0000, 0x0000, 0x0001);
    ASSERT_EQ(dns_cache_put("example.com", QTYPE_AAAA, QCLASS_IN, cv), 0);
    free_cv_rrs(&cv);
    teardown();
    TEST_PASS();
}

static void test_cache_put_overwrite(void) {
    TEST("dns_cache_put overwrites existing entry for same key");
    setup();
    CacheValue cv1 = make_cv_a("example.com", 60, 0x7f000001);
    CacheValue cv2 = make_cv_a("example.com", 120, 0x08080808);
    ASSERT_EQ(dns_cache_put("example.com", QTYPE_A, QCLASS_IN, cv1), 0);
    ASSERT_EQ(dns_cache_put("example.com", QTYPE_A, QCLASS_IN, cv2), 0);
    free_cv_rrs(&cv1);
    free_cv_rrs(&cv2);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_A, QCLASS_IN, &result), 0);
    ASSERT_EQ(result.answer_RRs, 1);
    ASSERT_EQ(vector_size(result.rrs), 1);
    ResourceRecord *got = vector_get(result.rrs, 0);
    ASSERT_EQ(got->ttl, 120);
    uint32_t ip;
    memcpy(&ip, got->rdata, 4);
    ASSERT_EQ(ip, 0x08080808);
    free_cv_rrs(&result);
    teardown();
    TEST_PASS();
}

static void test_cache_put_multiple_rrs(void) {
    TEST("dns_cache_put with multiple RRs in CacheValue");
    setup();
    CacheValue cv = {0};
    cv.rrs = vector_create(4);
    ResourceRecord *rr1 = rr_create();
    rr1->name = strdup("multi.com");
    rr1->type = QTYPE_A;
    rr1->class = QCLASS_IN;
    rr1->ttl = 60;
    uint32_t ip1 = 0x7f000001;
    rr1->rdata = malloc(4);
    memcpy(rr1->rdata, &ip1, 4);
    rr1->rdata_length = 4;
    vector_add(cv.rrs, rr1);

    ResourceRecord *rr2 = rr_create();
    rr2->name = strdup("multi.com");
    rr2->type = QTYPE_A;
    rr2->class = QCLASS_IN;
    rr2->ttl = 120;
    uint32_t ip2 = 0x08080808;
    rr2->rdata = malloc(4);
    memcpy(rr2->rdata, &ip2, 4);
    rr2->rdata_length = 4;
    vector_add(cv.rrs, rr2);
    cv.answer_RRs = 2;

    ASSERT_EQ(dns_cache_put("multi.com", QTYPE_A, QCLASS_IN, cv), 0);
    free_cv_rrs(&cv);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("multi.com", QTYPE_A, QCLASS_IN, &result), 0);
    ASSERT_EQ(result.answer_RRs, 2);
    ASSERT_EQ(vector_size(result.rrs), 2);
    memcpy(&ip1, ((ResourceRecord*)vector_get(result.rrs, 0))->rdata, 4);
    memcpy(&ip2, ((ResourceRecord*)vector_get(result.rrs, 1))->rdata, 4);
    ASSERT_EQ(ip1, 0x7f000001);
    ASSERT_EQ(ip2, 0x08080808);
    free_cv_rrs(&result);
    teardown();
    TEST_PASS();
}

// =========== put — section counts preserved ===========

static void test_cache_put_section_counts(void) {
    TEST("dns_cache_put preserves answer/authority/additional counts");
    setup();
    CacheValue cv = make_cv_with_counts("counts.com", 60, 0x7f000001, 3, 1, 2);
    ASSERT_EQ(dns_cache_put("counts.com", QTYPE_A, QCLASS_IN, cv), 0);
    free_cv_rrs(&cv);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("counts.com", QTYPE_A, QCLASS_IN, &result), 0);
    ASSERT_EQ(result.answer_RRs, 3);
    ASSERT_EQ(result.authority_RRs, 1);
    ASSERT_EQ(result.additional_RRs, 2);
    free_cv_rrs(&result);
    teardown();
    TEST_PASS();
}

// =========== get — parameter validation ===========

static void test_cache_get_null_qname(void) {
    TEST("dns_cache_get(NULL qname) returns 1");
    setup();
    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get(NULL, QTYPE_A, QCLASS_IN, &result), 1);
    teardown();
    TEST_PASS();
}

static void test_cache_get_null_result(void) {
    TEST("dns_cache_get(NULL result) returns 1");
    setup();
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_A, QCLASS_IN, NULL), 1);
    teardown();
    TEST_PASS();
}

static void test_cache_get_result_not_touched_on_miss(void) {
    TEST("dns_cache_get does not touch result on miss");
    setup();
    CacheValue result = {0};
    result.rrs = NULL;
    ASSERT_EQ(dns_cache_get("nope.com", QTYPE_A, QCLASS_IN, &result), 1);
    ASSERT_NULL(result.rrs);
    teardown();
    TEST_PASS();
}

// =========== get — miss ===========

static void test_cache_get_miss_empty(void) {
    TEST("dns_cache_get miss on empty cache");
    setup();
    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_A, QCLASS_IN, &result), 1);
    ASSERT_NULL(result.rrs);
    teardown();
    TEST_PASS();
}

static void test_cache_get_miss_different_name(void) {
    TEST("dns_cache_get miss for different domain name");
    setup();
    CacheValue cv = make_cv_a("foo.com", 60, 0x7f000001);
    ASSERT_EQ(dns_cache_put("foo.com", QTYPE_A, QCLASS_IN, cv), 0);
    free_cv_rrs(&cv);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("bar.com", QTYPE_A, QCLASS_IN, &result), 1);
    teardown();
    TEST_PASS();
}

static void test_cache_get_miss_different_type(void) {
    TEST("dns_cache_get miss for different query type");
    setup();
    CacheValue cv = make_cv_a("example.com", 60, 0x7f000001);
    ASSERT_EQ(dns_cache_put("example.com", QTYPE_A, QCLASS_IN, cv), 0);
    free_cv_rrs(&cv);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_AAAA, QCLASS_IN, &result), 1);
    teardown();
    TEST_PASS();
}

static void test_cache_get_miss_different_class(void) {
    TEST("dns_cache_get miss for different query class");
    setup();
    CacheValue cv = make_cv_a("example.com", 60, 0x7f000001);
    ASSERT_EQ(dns_cache_put("example.com", QTYPE_A, QCLASS_IN, cv), 0);
    free_cv_rrs(&cv);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_A, (Class)2, &result), 1);
    teardown();
    TEST_PASS();
}

// =========== get — hit ===========

static void test_cache_get_hit_exact(void) {
    TEST("dns_cache_get hit returns cloned RR with correct data");
    setup();
    uint32_t ip = 0x7f000001;
    CacheValue cv = make_cv_a("example.com", 60, ip);
    ASSERT_EQ(dns_cache_put("example.com", QTYPE_A, QCLASS_IN, cv), 0);
    free_cv_rrs(&cv);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_A, QCLASS_IN, &result), 0);
    ASSERT_NOT_NULL(result.rrs);
    ASSERT_EQ(vector_size(result.rrs), 1);

    ResourceRecord *got = vector_get(result.rrs, 0);
    ASSERT_NOT_NULL(got);
    ASSERT_STREQ(got->name, "example.com");
    ASSERT_EQ(got->type, QTYPE_A);
    ASSERT_EQ(got->class, QCLASS_IN);
    ASSERT_EQ(got->rdata_length, 4);
    ASSERT_EQ(got->ttl, 60);
    ASSERT_EQ(memcmp(got->rdata, &ip, 4), 0);

    free_cv_rrs(&result);
    teardown();
    TEST_PASS();
}

static void test_cache_get_hit_preserves_ttl(void) {
    TEST("dns_cache_get hit returns original TTL on cloned RR");
    setup();
    CacheValue cv = make_cv_a("example.com", 3600, 0x7f000001);
    ASSERT_EQ(dns_cache_put("example.com", QTYPE_A, QCLASS_IN, cv), 0);
    free_cv_rrs(&cv);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_A, QCLASS_IN, &result), 0);
    ResourceRecord *got = vector_get(result.rrs, 0);
    ASSERT_EQ(got->ttl, 3600);

    free_cv_rrs(&result);
    teardown();
    TEST_PASS();
}

static void test_cache_get_hit_multiple_rrs(void) {
    TEST("dns_cache_get hit returns all RRs correctly");
    setup();
    CacheValue cv = {0};
    cv.rrs = vector_create(4);

    ResourceRecord *rr1 = rr_create();
    rr1->name = strdup("multi.com");
    rr1->type = QTYPE_A;
    rr1->class = QCLASS_IN;
    rr1->ttl = 60;
    uint32_t ip1 = 0x7f000001;
    rr1->rdata = malloc(4);
    memcpy(rr1->rdata, &ip1, 4);
    rr1->rdata_length = 4;
    vector_add(cv.rrs, rr1);

    ResourceRecord *rr2 = rr_create();
    rr2->name = strdup("multi.com");
    rr2->type = QTYPE_A;
    rr2->class = QCLASS_IN;
    rr2->ttl = 120;
    uint32_t ip2 = 0x7f000002;
    rr2->rdata = malloc(4);
    memcpy(rr2->rdata, &ip2, 4);
    rr2->rdata_length = 4;
    vector_add(cv.rrs, rr2);

    ResourceRecord *rr3 = rr_create();
    rr3->name = strdup("multi.com");
    rr3->type = QTYPE_A;
    rr3->class = QCLASS_IN;
    rr3->ttl = 180;
    uint32_t ip3 = 0x7f000003;
    rr3->rdata = malloc(4);
    memcpy(rr3->rdata, &ip3, 4);
    rr3->rdata_length = 4;
    vector_add(cv.rrs, rr3);
    cv.answer_RRs = 3;

    ASSERT_EQ(dns_cache_put("multi.com", QTYPE_A, QCLASS_IN, cv), 0);
    free_cv_rrs(&cv);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("multi.com", QTYPE_A, QCLASS_IN, &result), 0);
    ASSERT_EQ(vector_size(result.rrs), 3);
    ASSERT_EQ(((ResourceRecord*)vector_get(result.rrs, 0))->ttl, 60);
    ASSERT_EQ(((ResourceRecord*)vector_get(result.rrs, 1))->ttl, 120);
    ASSERT_EQ(((ResourceRecord*)vector_get(result.rrs, 2))->ttl, 180);

    uint32_t got_ip;
    memcpy(&got_ip, ((ResourceRecord*)vector_get(result.rrs, 0))->rdata, 4);
    ASSERT_EQ(got_ip, ip1);
    memcpy(&got_ip, ((ResourceRecord*)vector_get(result.rrs, 1))->rdata, 4);
    ASSERT_EQ(got_ip, ip2);
    memcpy(&got_ip, ((ResourceRecord*)vector_get(result.rrs, 2))->rdata, 4);
    ASSERT_EQ(got_ip, ip3);

    free_cv_rrs(&result);
    teardown();
    TEST_PASS();
}

// =========== expiration ===========

static void test_cache_get_expired(void) {
    TEST("dns_cache_get returns miss for expired entry (TTL=1)");
    setup();
    CacheValue cv = make_cv_a("short.com", 1, 0x7f000001);
    ASSERT_EQ(dns_cache_put("short.com", QTYPE_A, QCLASS_IN, cv), 0);
    free_cv_rrs(&cv);

    sleep(2);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("short.com", QTYPE_A, QCLASS_IN, &result), 1);
    ASSERT_NULL(result.rrs);
    teardown();
    TEST_PASS();
}

static void test_cache_ttl_zero_expired_immediately(void) {
    TEST("dns_cache entry with TTL=0 is immediately expired");
    setup();
    CacheValue cv = make_cv_a("instant.com", 0, 0x7f000001);
    ASSERT_EQ(dns_cache_put("instant.com", QTYPE_A, QCLASS_IN, cv), 0);
    free_cv_rrs(&cv);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("instant.com", QTYPE_A, QCLASS_IN, &result), 1);
    teardown();
    TEST_PASS();
}

static void test_cache_ttl_never_expires(void) {
    TEST("dns_cache entry with TTL=UINT32_MAX never expires");
    setup();
    CacheValue cv = make_cv_a("forever.com", UINT32_MAX, 0x7f000001);
    ASSERT_EQ(dns_cache_put("forever.com", QTYPE_A, QCLASS_IN, cv), 0);
    free_cv_rrs(&cv);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("forever.com", QTYPE_A, QCLASS_IN, &result), 0);
    ASSERT_EQ(((ResourceRecord*)vector_get(result.rrs, 0))->ttl, UINT32_MAX);
    free_cv_rrs(&result);
    teardown();
    TEST_PASS();
}

static void test_cache_entry_with_one_expired_rr(void) {
    TEST("entry with mixed TTL: one expired RR → entire entry expired");
    setup();
    CacheValue cv = {0};
    cv.rrs = vector_create(4);

    ResourceRecord *rr1 = rr_create();
    rr1->name = strdup("mixed.com");
    rr1->type = QTYPE_A;
    rr1->class = QCLASS_IN;
    rr1->ttl = 3600;  // long TTL
    rr1->rdata = malloc(4);
    uint32_t ip1 = 0x7f000001;
    memcpy(rr1->rdata, &ip1, 4);
    rr1->rdata_length = 4;
    vector_add(cv.rrs, rr1);

    ResourceRecord *rr2 = rr_create();
    rr2->name = strdup("mixed.com");
    rr2->type = QTYPE_A;
    rr2->class = QCLASS_IN;
    rr2->ttl = 1;  // short TTL
    rr2->rdata = malloc(4);
    uint32_t ip2 = 0x7f000002;
    memcpy(rr2->rdata, &ip2, 4);
    rr2->rdata_length = 4;
    vector_add(cv.rrs, rr2);
    cv.answer_RRs = 2;

    ASSERT_EQ(dns_cache_put("mixed.com", QTYPE_A, QCLASS_IN, cv), 0);
    free_cv_rrs(&cv);

    sleep(2);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("mixed.com", QTYPE_A, QCLASS_IN, &result), 1);
    teardown();
    TEST_PASS();
}

// =========== prune ===========

static void test_cache_prune_removes_expired(void) {
    TEST("dns_cache_prune removes expired entries");
    setup();
    CacheValue cv_keep = make_cv_a("keep.com", 3600, 0x7f000001);
    CacheValue cv_drop = make_cv_a("drop.com", 1, 0x7f000002);
    ASSERT_EQ(dns_cache_put("keep.com", QTYPE_A, QCLASS_IN, cv_keep), 0);
    ASSERT_EQ(dns_cache_put("drop.com", QTYPE_A, QCLASS_IN, cv_drop), 0);
    free_cv_rrs(&cv_keep);
    free_cv_rrs(&cv_drop);

    sleep(2);
    ASSERT_EQ(dns_cache_prune(), 0);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("drop.com", QTYPE_A, QCLASS_IN, &result), 1);

    result = (CacheValue){0};
    ASSERT_EQ(dns_cache_get("keep.com", QTYPE_A, QCLASS_IN, &result), 0);
    ASSERT_EQ(vector_size(result.rrs), 1);
    free_cv_rrs(&result);
    teardown();
    TEST_PASS();
}

static void test_cache_prune_null(void) {
    TEST("dns_cache_prune with no cache returns -1");
    dns_cache_free();
    ASSERT_EQ(dns_cache_prune(), -1);
    TEST_PASS();
}

// =========== LRU eviction ===========

static void test_cache_lru_eviction_on_full(void) {
    TEST("LRU eviction: oldest entry evicted when capacity full");
    setup();
    char name[64];
    for (int i = 0; i < LRU_FILL_SIZE; i++) {
        snprintf(name, sizeof(name), "lru%04d.com", i);
        CacheValue cv = make_cv_a(name, 3600, 0x10000000 + (uint32_t)i);
        ASSERT_EQ(dns_cache_put(name, QTYPE_A, QCLASS_IN, cv), 0);
        free_cv_rrs(&cv);
    }

    CacheValue cv_new = make_cv_a("overflow.com", 60, 0x7f000099);
    ASSERT_EQ(dns_cache_put("overflow.com", QTYPE_A, QCLASS_IN, cv_new), 0);
    free_cv_rrs(&cv_new);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("lru0000.com", QTYPE_A, QCLASS_IN, &result), 1);

    result = (CacheValue){0};
    ASSERT_EQ(dns_cache_get("overflow.com", QTYPE_A, QCLASS_IN, &result), 0);
    ASSERT_EQ(vector_size(result.rrs), 1);
    free_cv_rrs(&result);

    result = (CacheValue){0};
    ASSERT_EQ(dns_cache_get("lru0001.com", QTYPE_A, QCLASS_IN, &result), 0);

    free_cv_rrs(&result);
    teardown();
    TEST_PASS();
}

static void test_cache_lru_touch_on_get(void) {
    TEST("LRU: get touches entry so it survives eviction");
    setup();

    CacheValue cv_hot = make_cv_a("hot.com", 3600, 0x7f000001);
    CacheValue cv_cold = make_cv_a("cold.com", 3600, 0x7f000002);
    ASSERT_EQ(dns_cache_put("hot.com", QTYPE_A, QCLASS_IN, cv_hot), 0);
    ASSERT_EQ(dns_cache_put("cold.com", QTYPE_A, QCLASS_IN, cv_cold), 0);
    free_cv_rrs(&cv_hot);
    free_cv_rrs(&cv_cold);

    CacheValue tmp = {0};
    ASSERT_EQ(dns_cache_get("hot.com", QTYPE_A, QCLASS_IN, &tmp), 0);
    free_cv_rrs(&tmp);

    char name[64];
    for (int i = 0; i < LRU_FILL_SIZE - 2; i++) {
        snprintf(name, sizeof(name), "lrufill%04d.com", i);
        CacheValue cv = make_cv_a(name, 3600, 0x20000000 + (uint32_t)i);
        ASSERT_EQ(dns_cache_put(name, QTYPE_A, QCLASS_IN, cv), 0);
        free_cv_rrs(&cv);
    }

    CacheValue cv_new = make_cv_a("overflow.com", 60, 0x7f000099);
    ASSERT_EQ(dns_cache_put("overflow.com", QTYPE_A, QCLASS_IN, cv_new), 0);
    free_cv_rrs(&cv_new);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("hot.com", QTYPE_A, QCLASS_IN, &result), 0);
    ASSERT_EQ(vector_size(result.rrs), 1);
    free_cv_rrs(&result);

    result = (CacheValue){0};
    ASSERT_EQ(dns_cache_get("cold.com", QTYPE_A, QCLASS_IN, &result), 1);

    teardown();
    TEST_PASS();
}

static void test_cache_lru_touch_on_put_overwrite(void) {
    TEST("LRU: put overwrite touches entry");
    setup();

    CacheValue cv_a = make_cv_a("touch.com", 3600, 0x7f000001);
    CacheValue cv_b = make_cv_a("untouch.com", 3600, 0x7f000002);
    ASSERT_EQ(dns_cache_put("touch.com", QTYPE_A, QCLASS_IN, cv_a), 0);
    ASSERT_EQ(dns_cache_put("untouch.com", QTYPE_A, QCLASS_IN, cv_b), 0);
    free_cv_rrs(&cv_a);
    free_cv_rrs(&cv_b);

    CacheValue cv_overwrite = make_cv_a("touch.com", 3600, 0x7f0000aa);
    ASSERT_EQ(dns_cache_put("touch.com", QTYPE_A, QCLASS_IN, cv_overwrite), 0);
    free_cv_rrs(&cv_overwrite);

    char name[64];
    for (int i = 0; i < LRU_FILL_SIZE - 2; i++) {
        snprintf(name, sizeof(name), "tfill%04d.com", i);
        CacheValue cv = make_cv_a(name, 3600, 0x30000000 + (uint32_t)i);
        ASSERT_EQ(dns_cache_put(name, QTYPE_A, QCLASS_IN, cv), 0);
        free_cv_rrs(&cv);
    }

    CacheValue cv_new = make_cv_a("overflow2.com", 60, 0x7f0000bb);
    ASSERT_EQ(dns_cache_put("overflow2.com", QTYPE_A, QCLASS_IN, cv_new), 0);
    free_cv_rrs(&cv_new);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("touch.com", QTYPE_A, QCLASS_IN, &result), 0);
    uint32_t ip;
    memcpy(&ip, ((ResourceRecord*)vector_get(result.rrs, 0))->rdata, 4);
    ASSERT_EQ(ip, 0x7f0000aa); // overwritten value
    free_cv_rrs(&result);

    result = (CacheValue){0};
    ASSERT_EQ(dns_cache_get("untouch.com", QTYPE_A, QCLASS_IN, &result), 1);

    teardown();
    TEST_PASS();
}

// =========== free ===========

static void test_cache_free_then_get(void) {
    TEST("dns_cache_get returns 1 after free");
    setup();
    CacheValue cv = make_cv_a("example.com", 60, 0x7f000001);
    ASSERT_EQ(dns_cache_put("example.com", QTYPE_A, QCLASS_IN, cv), 0);
    free_cv_rrs(&cv);
    dns_cache_free();

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_A, QCLASS_IN, &result), 1);
    TEST_PASS();
}

static void test_cache_free_then_put(void) {
    TEST("dns_cache_put returns 1 after free");
    setup();
    dns_cache_free();
    CacheValue cv = make_cv_a("example.com", 60, 0x7f000001);
    ASSERT_EQ(dns_cache_put("example.com", QTYPE_A, QCLASS_IN, cv), 1);
    free_cv_rrs(&cv);
    TEST_PASS();
}

static void test_cache_free_idempotent(void) {
    TEST("dns_cache_free is idempotent");
    setup();
    ASSERT_EQ(dns_cache_free(), 0);
    ASSERT_EQ(dns_cache_free(), 0);
    TEST_PASS();
}

// =========== different types / classes ===========

static void test_cache_different_types_independent(void) {
    TEST("A and AAAA records are cached independently");
    setup();
    uint32_t ipv4 = 0x7f000001;
    CacheValue cv_a = make_cv_a("example.com", 60, ipv4);
    CacheValue cv_aaaa = make_cv_aaaa("example.com", 60,
                                       0x2001, 0x0db8, 0x0000, 0x0000,
                                       0x0000, 0x0000, 0x0000, 0x0001);
    ASSERT_EQ(dns_cache_put("example.com", QTYPE_A, QCLASS_IN, cv_a), 0);
    ASSERT_EQ(dns_cache_put("example.com", QTYPE_AAAA, QCLASS_IN, cv_aaaa), 0);
    free_cv_rrs(&cv_a);
    free_cv_rrs(&cv_aaaa);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_A, QCLASS_IN, &result), 0);
    ASSERT_EQ(vector_size(result.rrs), 1);
    ASSERT_EQ(((ResourceRecord*)vector_get(result.rrs, 0))->type, QTYPE_A);
    free_cv_rrs(&result);

    result = (CacheValue){0};
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_AAAA, QCLASS_IN, &result), 0);
    ASSERT_EQ(vector_size(result.rrs), 1);
    ASSERT_EQ(((ResourceRecord*)vector_get(result.rrs, 0))->type, QTYPE_AAAA);
    free_cv_rrs(&result);
    teardown();
    TEST_PASS();
}

static void test_cache_different_classes_independent(void) {
    TEST("different classes are cached independently");
    setup();
    uint32_t ip_in = 0x7f000001;
    uint32_t ip_cs = 0x08080808;
    CacheValue cv_in = make_cv_a("example.com", 60, ip_in);
    CacheValue cv_cs = make_cv_a("example.com", 60, ip_cs);

    ASSERT_EQ(dns_cache_put("example.com", QTYPE_A, QCLASS_IN, cv_in), 0);
    ASSERT_EQ(dns_cache_put("example.com", QTYPE_A, (Class)2, cv_cs), 0);
    free_cv_rrs(&cv_in);
    free_cv_rrs(&cv_cs);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_A, QCLASS_IN, &result), 0);
    ASSERT_EQ(vector_size(result.rrs), 1);
    uint32_t got_ip;
    memcpy(&got_ip, ((ResourceRecord*)vector_get(result.rrs, 0))->rdata, 4);
    ASSERT_EQ(got_ip, ip_in);
    free_cv_rrs(&result);

    result = (CacheValue){0};
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_A, (Class)2, &result), 0);
    ASSERT_EQ(vector_size(result.rrs), 1);
    memcpy(&got_ip, ((ResourceRecord*)vector_get(result.rrs, 0))->rdata, 4);
    ASSERT_EQ(got_ip, ip_cs);
    free_cv_rrs(&result);
    teardown();
    TEST_PASS();
}

// =========== put get roundtrip ===========

static void test_cache_put_get_roundtrip_cname(void) {
    TEST("dns_cache put/get roundtrip with CNAME record");
    setup();
    CacheValue cv = {0};
    cv.rrs = vector_create(4);
    ResourceRecord *rr = rr_create();
    rr->name = strdup("alias.example.com");
    rr->type = QTYPE_CNAME;
    rr->class = QCLASS_IN;
    rr->ttl = 300;
    const char *target = "\x06target\x07example\x03com\x00";
    int target_len = (int)strlen(target) + 1;
    rr->rdata = malloc((size_t)target_len);
    memcpy(rr->rdata, target, (size_t)target_len);
    rr->rdata_length = (uint16_t)target_len;
    vector_add(cv.rrs, rr);
    cv.answer_RRs = 1;

    ASSERT_EQ(dns_cache_put("alias.example.com", QTYPE_CNAME, QCLASS_IN, cv), 0);
    free_cv_rrs(&cv);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("alias.example.com", QTYPE_CNAME, QCLASS_IN, &result), 0);
    ASSERT_EQ(vector_size(result.rrs), 1);
    ResourceRecord *got = vector_get(result.rrs, 0);
    ASSERT_EQ(got->type, QTYPE_CNAME);
    ASSERT_EQ(got->rdata_length, (uint16_t)target_len);
    ASSERT_EQ(memcmp(got->rdata, target, (size_t)target_len), 0);
    free_cv_rrs(&result);
    teardown();
    TEST_PASS();
}

static void test_cache_long_domain_name(void) {
    TEST("dns_cache put/get with long domain name");
    setup();
    const char *long_name = "very.long.subdomain.that.tests.cache.key.handling.example.com";
    CacheValue cv = make_cv_a(long_name, 60, 0x7f000099);
    ASSERT_EQ(dns_cache_put(long_name, QTYPE_A, QCLASS_IN, cv), 0);
    free_cv_rrs(&cv);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get(long_name, QTYPE_A, QCLASS_IN, &result), 0);
    ASSERT_STREQ(((ResourceRecord*)vector_get(result.rrs, 0))->name, long_name);
    free_cv_rrs(&result);
    teardown();
    TEST_PASS();
}

static void test_cache_same_key_different_rdata_replaced(void) {
    TEST("put same key replaces old value, not append");
    setup();
    uint32_t ip1 = 0x7f000001;
    uint32_t ip2 = 0x08080808;
    uint32_t ip3 = 0x01010101;

    CacheValue cv1 = make_cv_a("replace.com", 60, ip1);
    ASSERT_EQ(dns_cache_put("replace.com", QTYPE_A, QCLASS_IN, cv1), 0);
    free_cv_rrs(&cv1);

    CacheValue cv2 = make_cv_a("replace.com", 120, ip2);
    ASSERT_EQ(dns_cache_put("replace.com", QTYPE_A, QCLASS_IN, cv2), 0);
    free_cv_rrs(&cv2);

    CacheValue cv3 = make_cv_a("replace.com", 30, ip3);
    ASSERT_EQ(dns_cache_put("replace.com", QTYPE_A, QCLASS_IN, cv3), 0);
    free_cv_rrs(&cv3);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("replace.com", QTYPE_A, QCLASS_IN, &result), 0);
    ASSERT_EQ(vector_size(result.rrs), 1);
    ResourceRecord *got = vector_get(result.rrs, 0);
    ASSERT_EQ(got->ttl, 30);
    uint32_t got_ip;
    memcpy(&got_ip, got->rdata, 4);
    ASSERT_EQ(got_ip, ip3);
    free_cv_rrs(&result);
    teardown();
    TEST_PASS();
}

static void test_cache_zero_length_rdata(void) {
    TEST("dns_cache put/get with zero-length rdata");
    setup();
    CacheValue cv = {0};
    cv.rrs = vector_create(4);
    ResourceRecord *rr = rr_create();
    rr->name = strdup("empty.com");
    rr->type = QTYPE_TXT;
    rr->class = QCLASS_IN;
    rr->ttl = 60;
    rr->rdata = malloc(1);  // still need allocated pointer
    rr->rdata_length = 0;
    vector_add(cv.rrs, rr);
    cv.answer_RRs = 1;

    ASSERT_EQ(dns_cache_put("empty.com", QTYPE_TXT, QCLASS_IN, cv), 0);
    free_cv_rrs(&cv);

    CacheValue result = {0};
    ASSERT_EQ(dns_cache_get("empty.com", QTYPE_TXT, QCLASS_IN, &result), 0);
    ASSERT_EQ(((ResourceRecord*)vector_get(result.rrs, 0))->rdata_length, 0);
    free_cv_rrs(&result);
    teardown();
    TEST_PASS();
}

int main(void) {
    config_init();
    logger_init();

    // init
    test_cache_init_returns_zero();
    test_cache_init_idempotent();

    // put — parameter validation
    test_cache_put_null_qname();
    test_cache_put_null_rrs();
    test_cache_put_empty_rrs();

    // put — success
    test_cache_put_success_a();
    test_cache_put_success_aaaa();
    test_cache_put_overwrite();
    test_cache_put_multiple_rrs();
    test_cache_put_section_counts();

    // get — parameter validation
    test_cache_get_null_qname();
    test_cache_get_null_result();
    test_cache_get_result_not_touched_on_miss();

    // get — miss
    test_cache_get_miss_empty();
    test_cache_get_miss_different_name();
    test_cache_get_miss_different_type();
    test_cache_get_miss_different_class();

    // get — hit
    test_cache_get_hit_exact();
    test_cache_get_hit_preserves_ttl();
    test_cache_get_hit_multiple_rrs();

    // expiration
    test_cache_get_expired();
    test_cache_ttl_zero_expired_immediately();
    test_cache_ttl_never_expires();
    test_cache_entry_with_one_expired_rr();

    // prune
    test_cache_prune_removes_expired();
    test_cache_prune_null();

    // LRU
    test_cache_lru_eviction_on_full();
    test_cache_lru_touch_on_get();
    test_cache_lru_touch_on_put_overwrite();

    // free
    test_cache_free_then_get();
    test_cache_free_then_put();
    test_cache_free_idempotent();

    // different types / classes
    test_cache_different_types_independent();
    test_cache_different_classes_independent();

    // roundtrip edge cases
    test_cache_put_get_roundtrip_cname();
    test_cache_long_domain_name();
    test_cache_same_key_different_rdata_replaced();
    test_cache_zero_length_rdata();

    print_test_summary();
    return 0;
}
