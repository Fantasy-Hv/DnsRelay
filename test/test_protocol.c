#include "test_utils.h"
#include "dns/protocol.h"
#include <string.h>
#include <stdlib.h>

// =========== helpers ===========

static DnsPacket* make_simple_query(void) {
    DnsPacket *p = pack_create();
    p->header.id = 1234;
    p->header.flags = 0; // QR=0 → query
    p->header.qcount = 1;
    SectionQuestion *q = malloc(sizeof(SectionQuestion));
    q->qname = strdup("\x07" "example" "\x03" "com" "\x00");
    q->qtype = QTYPE_A;
    q->qclass = QCLASS_IN;
    vector_add(p->questions, q);
    return p;
}

static DnsPacket* make_query_with_rd(void) {
    DnsPacket *p = make_simple_query();
    RD_SET(p->header.flags);
    return p;
}

static int packets_equal(const DnsPacket *a, const DnsPacket *b) {
    if (!a || !b) return 0;
    if (a->header.id != b->header.id) return 0;
    if (a->header.flags != b->header.flags) return 0;
    if (a->header.qcount != b->header.qcount) return 0;
    if (a->header.answer_RRs != b->header.answer_RRs) return 0;
    return 1;
}

// =========== rr create / free ===========

static void test_rr_create_and_free(void) {
    TEST("rr_create + rr_free");
    ResourceRecord *rr = rr_create();
    ASSERT_NOT_NULL(rr);
    ASSERT_NULL(rr->name);
    ASSERT_NULL(rr->rdata);
    ASSERT_EQ(rr->rdata_length, 0);
    rr_free(rr);
    TEST_PASS();
}

// =========== pack create / free ===========

static void test_pack_create_and_free(void) {
    TEST("pack_create + pack_free");
    DnsPacket *p = pack_create();
    ASSERT_NOT_NULL(p);
    ASSERT_NOT_NULL(p->questions);
    ASSERT_NOT_NULL(p->answers);
    ASSERT_NOT_NULL(p->authorites);
    ASSERT_NOT_NULL(p->additionals);
    ASSERT_EQ(vector_size(p->questions), 0);
    pack_free(p);
    TEST_PASS();
}

// =========== packet_is_query ===========

static void test_packet_is_query_true(void) {
    TEST("packet_is_query true (flags=0)");
    DnsPacket *p = pack_create();
    p->header.flags = 0;
    ASSERT(packet_is_query(p));
    pack_free(p);
    TEST_PASS();
}

static void test_packet_is_query_false(void) {
    TEST("packet_is_query false (QR=1)");
    DnsPacket *p = pack_create();
    QR_SET(p->header.flags);
    ASSERT(!packet_is_query(p));
    pack_free(p);
    TEST_PASS();
}

// =========== serialize / deserialize roundtrip ===========

static void test_serialize_deserialize_roundtrip(void) {
    TEST("serialize + deserialize roundtrip");
    DnsPacket *orig = make_simple_query();

    char buf[512] = {0};
    int len = pack_serialize(orig, buf,512);
    ASSERT(len > 0);

    DnsPacket *restored = NULL;
    ASSERT_EQ(pack_deserialize(buf, len, &restored), 0);
    ASSERT_NOT_NULL(restored);
    ASSERT_EQ(restored->header.id, orig->header.id);
    ASSERT_EQ(restored->header.flags, orig->header.flags);
    ASSERT_EQ(restored->header.qcount, orig->header.qcount);
    ASSERT_EQ(vector_size(restored->questions), 1);

    pack_free(orig);
    pack_free(restored);
    TEST_PASS();
}

static void test_serialize_deserialize_response(void) {
    TEST("serialize + deserialize response with RRs");
    DnsPacket *resp = pack_create();
    resp->header.id = 9999;
    QR_SET(resp->header.flags);
    resp->header.qcount = 1;
    resp->header.answer_RRs = 1;

    SectionQuestion *q = malloc(sizeof(SectionQuestion));
    q->qname = strdup("\x03" "foo" "\x03" "bar" "\x00");
    q->qtype = QTYPE_A;
    q->qclass = QCLASS_IN;
    vector_add(resp->questions, q);

    ResourceRecord *rr = rr_create();
    rr->name = strdup("\x03" "foo" "\x03" "bar" "\x00");
    rr->type = QTYPE_A;
    rr->class = QCLASS_IN;
    rr->ttl = 3600;
    uint32_t ip = 0x7f000001; // 127.0.0.1
    rr->rdata = malloc(4);
    memcpy(rr->rdata, &ip, 4);
    rr->rdata_length = 4;
    vector_add(resp->answers, rr);

    char buf[512] = {0};
    int len = pack_serialize(resp, buf,512);
    ASSERT(len > 0);

    DnsPacket *restored = NULL;
    ASSERT_EQ(pack_deserialize(buf, len, &restored), 0);
    ASSERT_EQ(restored->header.id, 9999);
    ASSERT(restored->header.flags & 0x8000); // QR bit
    ASSERT_EQ(restored->header.qcount, 1);
    ASSERT_EQ(restored->header.answer_RRs, 1);
    ASSERT_EQ(vector_size(restored->answers), 1);
    ASSERT_EQ(((ResourceRecord*)vector_get(restored->answers, 0))->ttl, 3600);

    pack_free(resp);
    pack_free(restored);
    TEST_PASS();
}

static void test_serialize_deserialize_multiple_questions(void) {
    TEST("serialize + deserialize two questions");
    DnsPacket *p = pack_create();
    p->header.id = 1;
    p->header.qcount = 2;
    SectionQuestion *q1 = malloc(sizeof(SectionQuestion));
    q1->qname = strdup("\x03" "www" "\x07" "example" "\x03" "com" "\x00");
    q1->qtype = QTYPE_A;
    q1->qclass = QCLASS_IN;
    vector_add(p->questions, q1);

    SectionQuestion *q2 = malloc(sizeof(SectionQuestion));
    q2->qname = strdup("\x03" "www" "\x07" "example" "\x03" "com" "\x00");
    q2->qtype = QTYPE_AAAA;
    q2->qclass = QCLASS_IN;
    vector_add(p->questions, q2);

    char buf[512] = {0};
    int len = pack_serialize(p, buf,512);
    ASSERT(len > 0);

    DnsPacket *restored = NULL;
    ASSERT_EQ(pack_deserialize(buf, len, &restored), 0);
    ASSERT_EQ(restored->header.qcount, 2);
    ASSERT_EQ(vector_size(restored->questions), 2);

    pack_free(p);
    pack_free(restored);
    TEST_PASS();
}

// =========== packet_clone ===========

static void test_packet_clone(void) {
    TEST("packet_clone");
    DnsPacket *orig = make_simple_query();
    DnsPacket *clone = packet_clone(orig);
    ASSERT_NOT_NULL(clone);
    ASSERT_EQ(clone->header.id, orig->header.id);
    ASSERT_EQ(clone->header.flags, orig->header.flags);
    ASSERT_EQ(vector_size(clone->questions), 1);

    // modify clone, verify orig unchanged
    clone->header.id = 9999;
    ASSERT_EQ(orig->header.id, 1234);

    pack_free(orig);
    pack_free(clone);
    TEST_PASS();
}

// =========== pack_make_query_relay ===========

static void test_pack_make_query_relay(void) {
    TEST("pack_make_query_relay");
    DnsPacket *query = make_query_with_rd();
    DnsPacket *relay = NULL;

    ASSERT_EQ(pack_make_query_relay(query, 7777, &relay), 0);
    ASSERT_NOT_NULL(relay);
    ASSERT_EQ(relay->header.id, 7777); // relay id replaces client id
    ASSERT_EQ(relay->header.flags, query->header.flags);

    pack_free(query);
    pack_free(relay);
    TEST_PASS();
}

// =========== pack_make_response_relay ===========

static void test_pack_make_response_relay(void) {
    TEST("pack_make_response_relay");
    DnsPacket *upstream_resp = pack_create();
    upstream_resp->header.id = 7777;
    QR_SET(upstream_resp->header.flags);
    upstream_resp->header.answer_RRs = 1;

    ResourceRecord *rr = rr_create();
    rr->name = strdup("\x07" "example" "\x03" "com" "\x00");
    rr->type = QTYPE_A;
    rr->class = QCLASS_IN;
    rr->ttl = 60;
    rr->rdata = calloc(4, 1);
    rr->rdata_length = 4;
    vector_add(upstream_resp->answers, rr);

    DnsPacket *client_resp = NULL;
    pack_make_response_relay(upstream_resp, &client_resp, 1234);

    ASSERT_NOT_NULL(client_resp);
    ASSERT_EQ(client_resp->header.id, 1234); // restored client id

    pack_free(upstream_resp);
    pack_free(client_resp);
    TEST_PASS();
}

// =========== pack_make_inner_error ===========

static void test_pack_make_inner_error(void) {
    TEST("pack_make_inner_error → SERVFAIL");
    DnsPacket *query = make_simple_query();
    DnsPacket *err = NULL;

    pack_make_inner_error(query, &err);
    ASSERT_NOT_NULL(err);
    ASSERT_EQ(RCODE(err->header.flags), RCODE_SERVFAIL);

    pack_free(query);
    pack_free(err);
    TEST_PASS();
}

// =========== pack_make_response_local ===========

static void test_response_local_i_query_notimp(void) {
    TEST("response_local: IQUERY → NOTIMP");
    DnsPacket *query = pack_create();
    query->header.id = 1;
    // IQUERY = 1
    query->header.flags = (1 << 12); // Opcode = IQUERY
    query->header.qcount = 0;

    DnsPacket *resp = NULL;
    PacketDirection dir = pack_make_response_local(query, &resp);
    ASSERT_EQ(dir, CLIENT);
    ASSERT_NOT_NULL(resp);
    ASSERT_EQ(RCODE(resp->header.flags), RCODE_NOTIMP);

    pack_free(query);
    pack_free(resp);
    TEST_PASS();
}

static void test_response_local_qcount_zero(void) {
    TEST("response_local: qcount=0 → empty response");
    DnsPacket *query = pack_create();
    query->header.id = 1;
    query->header.flags = 0; // QUERY
    query->header.qcount = 0;

    DnsPacket *resp = NULL;
    PacketDirection dir = pack_make_response_local(query, &resp);
    ASSERT_EQ(dir, CLIENT);
    ASSERT_NOT_NULL(resp);

    pack_free(query);
    pack_free(resp);
    TEST_PASS();
}

int main(void) {
    // create / free
    test_rr_create_and_free();
    test_pack_create_and_free();

    // is_query
    test_packet_is_query_true();
    test_packet_is_query_false();

    // serialize / deserialize
    test_serialize_deserialize_roundtrip();
    test_serialize_deserialize_response();
    test_serialize_deserialize_multiple_questions();

    // clone
    test_packet_clone();

    // relay
    test_pack_make_query_relay();
    test_pack_make_response_relay();

    // error
    test_pack_make_inner_error();

    // response local
    test_response_local_i_query_notimp();
    test_response_local_qcount_zero();

    print_test_summary();
    return 0;
}
