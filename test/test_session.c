#include "test_utils.h"
#include "server/session.h"
#include "dns/protocol.h"
#include "infra/socket.h"
#include "infra/utils.h"
#include <arpa/inet.h>
#include <time.h>

static NetEnd make_client(uint32_t ipv4) {
    NetEnd ep;
    ep.version = IPV4;
    ep.addr.ipv4 = ipv4;
    ep.port = htons(12345);
    return ep;
}

static DnsPacket* make_relay_packet(uint16_t relay_id) {
    DnsPacket *p = pack_create();
    p->header.id = relay_id;
    p->header.flags = 0;
    p->header.qcount = 1;
    SectionQuestion *q = malloc(sizeof(SectionQuestion));
    q->qname = strdup("\x07example\x03com\x00");
    q->qtype = QTYPE_A;
    q->qclass = QCLASS_IN;
    vector_add(p->questions, q);
    return p;
}

// =========== session ===========

static void test_session_init(void) {
    TEST("session_factory_init");
    ASSERT_EQ(session_factory_init(), 0);
    ASSERT_NULL(session_peek());
    TEST_PASS();
}

static void test_session_open_and_get(void) {
    TEST("session_open + session_get");
    ASSERT_EQ(session_factory_init(), 0);

    NetEnd client = make_client(0x7f000001); // 127.0.0.1
    DnsPacket *relay = make_relay_packet(42);
    ASSERT_EQ(session_open(100, client, relay), 0);

    // retrieve by relay response (matched on relay_id=42)
    DnsPacket *response = make_relay_packet(42);
    Session *s = session_get(response);
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(s->client_id, 100);
    ASSERT_EQ(s->relay_info.retry_times, 0);

    pack_free(response);
    session_close(s);
    pack_free(relay);
    TEST_PASS();
}

static void test_session_get_miss(void) {
    TEST("session_get miss → NULL");
    ASSERT_EQ(session_factory_init(), 0);

    DnsPacket *unknown = make_relay_packet(999);
    ASSERT_NULL(session_get(unknown));

    pack_free(unknown);
    TEST_PASS();
}

static void test_session_get_null(void) {
    TEST("session_get(NULL) → NULL");
    ASSERT_EQ(session_factory_init(), 0);
    ASSERT_NULL(session_get(NULL));
    TEST_PASS();
}

static void test_session_peek_empty(void) {
    TEST("session_peek on empty queue → NULL");
    ASSERT_EQ(session_factory_init(), 0);
    ASSERT_NULL(session_peek());
    TEST_PASS();
}

static void test_session_timeout_remain(void) {
    TEST("get_session_timeout_remain");
    ASSERT_EQ(session_factory_init(), 0);

    NetEnd client = make_client(0);
    DnsPacket *relay = make_relay_packet(1);
    session_open(1, client, relay);

    Session *s = session_peek();
    ASSERT_NOT_NULL(s);

    ms remain;
    ASSERT_EQ(get_session_timeout_remain(s, 3000, &remain), 0);
    // just opened → should have positive remaining time
    ASSERT(remain > 0);

    ASSERT_EQ(get_session_timeout_remain(NULL, 3000, &remain), -1);

    session_close(s);
    pack_free(relay);
    TEST_PASS();
}

static void test_session_wait_resets_timestamp(void) {
    TEST("session_wait resets timestamp");
    ASSERT_EQ(session_factory_init(), 0);

    NetEnd client = make_client(0);
    DnsPacket *relay = make_relay_packet(2);
    session_open(2, client, relay);

    Session *s = session_peek();
    ms ts1 = s->relay_info.timestamp;

    // busy-wait until time advances at least 1ms
    while (sys_time_ms() == ts1) {}

    session_wait(s);
    ms ts2 = s->relay_info.timestamp;
    ASSERT(ts2 > ts1);

    session_close(s);
    pack_free(relay);
    TEST_PASS();
}

static void test_session_multiple_ordering(void) {
    TEST("session queue ordering by timestamp");
    ASSERT_EQ(session_factory_init(), 0);

    NetEnd client = make_client(0);
    DnsPacket *r1 = make_relay_packet(1);
    DnsPacket *r2 = make_relay_packet(2);
    DnsPacket *r3 = make_relay_packet(3);

    session_open(1, client, r1);
    // small delay to ensure distinct timestamps
    struct timespec nap = {0, 2000000}; // 2ms
    nanosleep(&nap, NULL);
    session_open(2, client, r2);
    nanosleep(&nap, NULL);
    session_open(3, client, r3);

    // peek returns earliest (session 1)
    Session *earliest = session_peek();
    ASSERT_NOT_NULL(earliest);
    ASSERT_EQ(earliest->client_id, 1);

    session_close(earliest);
    earliest = session_peek();
    ASSERT_EQ(earliest->client_id, 2);

    session_close(earliest);
    earliest = session_peek();
    ASSERT_EQ(earliest->client_id, 3);

    session_close(earliest);
    ASSERT_NULL(session_peek());

    pack_free(r1); pack_free(r2); pack_free(r3);
    TEST_PASS();
}

int main(void) {
    test_session_init();
    test_session_open_and_get();
    test_session_get_miss();
    test_session_get_null();
    test_session_peek_empty();
    test_session_timeout_remain();
    test_session_wait_resets_timestamp();
    test_session_multiple_ordering();
    print_test_summary();
    return 0;
}
