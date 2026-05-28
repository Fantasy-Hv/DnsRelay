#include "test_utils.h"
#include "dns/cache.h"
#include "dns/protocol.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>

// =========== helpers ===========

static ResourceRecord* make_a_record(const char* name, uint32_t ttl, uint32_t ip) {
    ResourceRecord *rr = rr_create();
    rr->name = strdup(name);
    rr->type = QTYPE_A;
    rr->class = QCLASS_IN;
    rr->ttl = ttl;
    rr->rdata = malloc(4);
    memcpy(rr->rdata, &ip, 4);
    rr->rdata_length = 4;
    return rr;
}

static ResourceRecord* make_aaaa_record(const char* name, uint32_t ttl,
                                         uint16_t a, uint16_t b, uint16_t c, uint16_t d,
                                         uint16_t e, uint16_t f, uint16_t g, uint16_t h) {
    ResourceRecord *rr = rr_create();
    rr->name = strdup(name);
    rr->type = QTYPE_AAAA;
    rr->class = QCLASS_IN;
    rr->ttl = ttl;
    rr->rdata = malloc(16);
    uint16_t words[8] = {a, b, c, d, e, f, g, h};
    for (int i = 0; i < 8; i++) {
        ((uint16_t*)rr->rdata)[i] = htons(words[i]);
    }
    rr->rdata_length = 16;
    return rr;
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

// =========== put ===========

static void test_cache_put_null_record(void) {
    TEST("dns_cache_put(NULL) returns 1");
    setup();
    ASSERT_EQ(dns_cache_put(NULL), 1);
    teardown();
    TEST_PASS();
}

static void test_cache_put_null_name(void) {
    TEST("dns_cache_put with NULL name returns 1");
    setup();
    ResourceRecord *rr = rr_create();
    rr->type = QTYPE_A;
    rr->class = QCLASS_IN;
    rr->ttl = 60;
    ASSERT_EQ(dns_cache_put(rr), 1);
    rr_free(rr);
    teardown();
    TEST_PASS();
}

static void test_cache_put_ttl_zero(void) {
    TEST("dns_cache_put with ttl=0 returns 1");
    setup();
    ResourceRecord *rr = make_a_record("example.com", 0, 0x7f000001);
    ASSERT_EQ(dns_cache_put(rr), 1);
    rr_free(rr);
    teardown();
    TEST_PASS();
}

static void test_cache_put_success(void) {
    TEST("dns_cache_put valid record returns 0");
    setup();
    ResourceRecord *rr = make_a_record("example.com", 60, 0x7f000001);
    ASSERT_EQ(dns_cache_put(rr), 0);
    rr_free(rr);
    teardown();
    TEST_PASS();
}

// =========== get ===========

static void test_cache_get_null_params(void) {
    TEST("dns_cache_get with NULL params returns 1");
    setup();
    Vector *v = vector_create(4);
    ASSERT_EQ(dns_cache_get(NULL, QTYPE_A, QCLASS_IN, v), 1);
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_A, QCLASS_IN, NULL), 1);
    vector_free(v);
    teardown();
    TEST_PASS();
}

static void test_cache_get_miss_empty(void) {
    TEST("dns_cache_get miss on empty cache");
    setup();
    Vector *v = vector_create(4);
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_A, QCLASS_IN, v), 1);
    ASSERT_EQ(vector_size(v), 0);
    vector_free(v);
    teardown();
    TEST_PASS();
}

static void test_cache_get_miss_different_name(void) {
    TEST("dns_cache_get miss for different name");
    setup();
    ResourceRecord *rr = make_a_record("foo.com", 60, 0x7f000001);
    dns_cache_put(rr);
    rr_free(rr);

    Vector *v = vector_create(4);
    ASSERT_EQ(dns_cache_get("bar.com", QTYPE_A, QCLASS_IN, v), 1);
    vector_free(v);
    teardown();
    TEST_PASS();
}

static void test_cache_get_miss_different_type(void) {
    TEST("dns_cache_get miss for different type");
    setup();
    ResourceRecord *rr = make_a_record("example.com", 60, 0x7f000001);
    dns_cache_put(rr);
    rr_free(rr);

    Vector *v = vector_create(4);
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_AAAA, QCLASS_IN, v), 1);
    vector_free(v);
    teardown();
    TEST_PASS();
}

static void test_cache_get_hit(void) {
    TEST("dns_cache_get hit returns cloned RR");
    setup();
    uint32_t ip = 0x7f000001;
    ResourceRecord *rr = make_a_record("example.com", 60, ip);
    dns_cache_put(rr);
    rr_free(rr);

    Vector *v = vector_create(4);
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_A, QCLASS_IN, v), 0);
    ASSERT_EQ(vector_size(v), 1);

    ResourceRecord *got = vector_get(v, 0);
    ASSERT_NOT_NULL(got);
    ASSERT_STREQ(got->name, "example.com");
    ASSERT_EQ(got->type, QTYPE_A);
    ASSERT_EQ(got->class, QCLASS_IN);
    ASSERT_EQ(got->rdata_length, 4);
    ASSERT_EQ(memcmp(got->rdata, &ip, 4), 0);

    // clean up returned clones
    for (int i = 0; i < vector_size(v); i++)
        rr_free(vector_get(v, i));
    vector_free(v);
    teardown();
    TEST_PASS();
}

static void test_cache_get_ttl_is_remaining(void) {
    TEST("dns_cache_get returns remaining TTL <= original");
    setup();
    ResourceRecord *rr = make_a_record("example.com", 3600, 0x7f000001);
    dns_cache_put(rr);
    rr_free(rr);

    Vector *v = vector_create(4);
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_A, QCLASS_IN, v), 0);
    ResourceRecord *got = vector_get(v, 0);
    // remaining TTL should be ~3600 (allow small drift for test execution time)
    ASSERT(got->ttl >= 3599);
    ASSERT(got->ttl <= 3600);

    for (int i = 0; i < vector_size(v); i++)
        rr_free(vector_get(v, i));
    vector_free(v);
    teardown();
    TEST_PASS();
}

// =========== put same key appends ===========

static void test_cache_put_same_key_appends(void) {
    TEST("dns_cache_put same key appends RR to existing entry");
    setup();
    uint32_t ip1 = 0x7f000001;
    uint32_t ip2 = 0x7f000002;
    ResourceRecord *rr1 = make_a_record("example.com", 60, ip1);
    ResourceRecord *rr2 = make_a_record("example.com", 120, ip2);
    ASSERT_EQ(dns_cache_put(rr1), 0);
    ASSERT_EQ(dns_cache_put(rr2), 0);
    rr_free(rr1);
    rr_free(rr2);

    Vector *v = vector_create(4);
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_A, QCLASS_IN, v), 0);
    ASSERT_EQ(vector_size(v), 2);

    for (int i = 0; i < vector_size(v); i++)
        rr_free(vector_get(v, i));
    vector_free(v);
    teardown();
    TEST_PASS();
}

// =========== expiration ===========

static void test_cache_get_expired(void) {
    TEST("dns_cache_get returns 1 for expired entry");
    setup();
    ResourceRecord *rr = make_a_record("short.com", 1, 0x7f000001);
    dns_cache_put(rr);
    rr_free(rr);

    sleep(2);

    Vector *v = vector_create(4);
    ASSERT_EQ(dns_cache_get("short.com", QTYPE_A, QCLASS_IN, v), 1);
    ASSERT_EQ(vector_size(v), 0);
    vector_free(v);
    teardown();
    TEST_PASS();
}

static void test_cache_prune_removes_expired(void) {
    TEST("dns_cache_prune removes expired entries");
    setup();
    ResourceRecord *rr1 = make_a_record("keep.com", 3600, 0x7f000001);
    ResourceRecord *rr2 = make_a_record("drop.com", 1, 0x7f000002);
    dns_cache_put(rr1);
    dns_cache_put(rr2);
    rr_free(rr1);
    rr_free(rr2);

    sleep(2);

    ASSERT_EQ(dns_cache_prune(), 0);

    // expired entry should be gone
    Vector *v = vector_create(4);
    ASSERT_EQ(dns_cache_get("drop.com", QTYPE_A, QCLASS_IN, v), 1);
    vector_free(v);

    // non-expired entry should still be there
    v = vector_create(4);
    ASSERT_EQ(dns_cache_get("keep.com", QTYPE_A, QCLASS_IN, v), 0);
    ASSERT_EQ(vector_size(v), 1);
    for (int i = 0; i < vector_size(v); i++)
        rr_free(vector_get(v, i));
    vector_free(v);

    teardown();
    TEST_PASS();
}

static void test_cache_prune_null(void) {
    TEST("dns_cache_prune with no cache returns -1");
    dns_cache_free();
    ASSERT_EQ(dns_cache_prune(), -1);
    TEST_PASS();
}

// =========== free ===========

static void test_cache_free_then_get(void) {
    TEST("dns_cache_get returns 1 after free");
    setup();
    ResourceRecord *rr = make_a_record("example.com", 60, 0x7f000001);
    dns_cache_put(rr);
    rr_free(rr);
    dns_cache_free();

    Vector *v = vector_create(4);
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_A, QCLASS_IN, v), 1);
    vector_free(v);
    TEST_PASS();
}

static void test_cache_free_then_put(void) {
    TEST("dns_cache_put returns 1 after free");
    setup();
    dns_cache_free();
    ResourceRecord *rr = make_a_record("example.com", 60, 0x7f000001);
    ASSERT_EQ(dns_cache_put(rr), 1);
    rr_free(rr);
    TEST_PASS();
}

static void test_cache_free_idempotent(void) {
    TEST("dns_cache_free is idempotent");
    setup();
    ASSERT_EQ(dns_cache_free(), 0);
    ASSERT_EQ(dns_cache_free(), 0);
    TEST_PASS();
}

// =========== multiple types ===========

static void test_cache_different_types_independent(void) {
    TEST("A and AAAA records are cached independently");
    setup();
    uint32_t ipv4 = 0x7f000001;
    ResourceRecord *a = make_a_record("example.com", 60, ipv4);
    ResourceRecord *aaaa = make_aaaa_record("example.com", 60,
                                             0x2001, 0x0db8, 0x0000, 0x0000,
                                             0x0000, 0x0000, 0x0000, 0x0001);
    ASSERT_EQ(dns_cache_put(a), 0);
    ASSERT_EQ(dns_cache_put(aaaa), 0);
    rr_free(a);
    rr_free(aaaa);

    Vector *v4 = vector_create(4);
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_A, QCLASS_IN, v4), 0);
    ASSERT_EQ(vector_size(v4), 1);
    ASSERT_EQ(((ResourceRecord*)vector_get(v4, 0))->type, QTYPE_A);
    for (int i = 0; i < vector_size(v4); i++)
        rr_free(vector_get(v4, i));
    vector_free(v4);

    Vector *v6 = vector_create(4);
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_AAAA, QCLASS_IN, v6), 0);
    ASSERT_EQ(vector_size(v6), 1);
    ASSERT_EQ(((ResourceRecord*)vector_get(v6, 0))->type, QTYPE_AAAA);
    for (int i = 0; i < vector_size(v6); i++)
        rr_free(vector_get(v6, i));
    vector_free(v6);

    teardown();
    TEST_PASS();
}

static void test_cache_different_classes_independent(void) {
    TEST("different classes are cached independently");
    setup();
    ResourceRecord *rr_in = make_a_record("example.com", 60, 0x7f000001);

    ResourceRecord *rr_cs = rr_create();
    rr_cs->name = strdup("example.com");
    rr_cs->type = QTYPE_A;
    rr_cs->class = 2; // CS
    rr_cs->ttl = 60;
    rr_cs->rdata = malloc(4);
    uint32_t ip = 0x08080808;
    memcpy(rr_cs->rdata, &ip, 4);
    rr_cs->rdata_length = 4;

    dns_cache_put(rr_in);
    dns_cache_put(rr_cs);
    rr_free(rr_in);
    rr_free(rr_cs);

    // IN class
    Vector *v_in = vector_create(4);
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_A, QCLASS_IN, v_in), 0);
    ASSERT_EQ(vector_size(v_in), 1);
    uint32_t got_ip;
    memcpy(&got_ip, ((ResourceRecord*)vector_get(v_in, 0))->rdata, 4);
    ASSERT_EQ(got_ip, 0x7f000001);
    for (int i = 0; i < vector_size(v_in); i++)
        rr_free(vector_get(v_in, i));
    vector_free(v_in);

    // CS class
    Vector *v_cs = vector_create(4);
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_A, 2, v_cs), 0);
    ASSERT_EQ(vector_size(v_cs), 1);
    memcpy(&got_ip, ((ResourceRecord*)vector_get(v_cs, 0))->rdata, 4);
    ASSERT_EQ(got_ip, 0x08080808);
    for (int i = 0; i < vector_size(v_cs); i++)
        rr_free(vector_get(v_cs, i));
    vector_free(v_cs);

    teardown();
    TEST_PASS();
}

// =========== earliest expiry for same key ===========

static void test_cache_same_key_earliest_expiry(void) {
    TEST("same-key RRs use earliest TTL for entry expiry");
    setup();
    ResourceRecord *rr_long = make_a_record("example.com", 3600, 0x7f000001);
    ResourceRecord *rr_short = make_a_record("example.com", 1, 0x7f000002);
    dns_cache_put(rr_long);
    dns_cache_put(rr_short);
    rr_free(rr_long);
    rr_free(rr_short);

    sleep(2);

    // both should be expired because shorter TTL wins
    Vector *v = vector_create(4);
    ASSERT_EQ(dns_cache_get("example.com", QTYPE_A, QCLASS_IN, v), 1);
    vector_free(v);
    teardown();
    TEST_PASS();
}

// =========== get with empty result vector ===========

static void test_cache_get_clears_result_on_miss(void) {
    TEST("dns_cache_get leaves result empty on miss");
    setup();
    Vector *v = vector_create(4);
    ASSERT_EQ(dns_cache_get("nope.com", QTYPE_A, QCLASS_IN, v), 1);
    ASSERT_EQ(vector_size(v), 0);
    vector_free(v);
    teardown();
    TEST_PASS();
}

int main(void) {
    // init
    test_cache_init_returns_zero();
    test_cache_init_idempotent();

    // put
    test_cache_put_null_record();
    test_cache_put_null_name();
    test_cache_put_ttl_zero();
    test_cache_put_success();

    // get
    test_cache_get_null_params();
    test_cache_get_miss_empty();
    test_cache_get_miss_different_name();
    test_cache_get_miss_different_type();
    test_cache_get_hit();
    test_cache_get_ttl_is_remaining();

    // same key appends
    test_cache_put_same_key_appends();

    // expiration
    test_cache_get_expired();
    test_cache_prune_removes_expired();
    test_cache_prune_null();

    // free
    test_cache_free_then_get();
    test_cache_free_then_put();
    test_cache_free_idempotent();

    // multiple types / classes
    test_cache_different_types_independent();
    test_cache_different_classes_independent();

    // earliest expiry
    test_cache_same_key_earliest_expiry();

    // miss result
    test_cache_get_clears_result_on_miss();

    print_test_summary();
    return 0;
}
