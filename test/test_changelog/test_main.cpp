// Native unit tests for BoundedChangeLog.
// Run: pio test -e test

#include <unity.h>
#include <cstring>
#include "BoundedChangeLog.h"

void setUp(void) {}
void tearDown(void) {}

void test_basic_push_and_seq(void) {
    BoundedChangeLog log;
    TEST_ASSERT_EQUAL_UINT32(1, log.getCurrentSeq());
    TEST_ASSERT_EQUAL(0, (int)log.getCount());

    uint32_t s1 = log.push(EntityType::CELL, OpType::UPSERT, "p1");
    uint32_t s2 = log.push(EntityType::TYPE, OpType::UPSERT, "p2");
    uint32_t s3 = log.push(EntityType::HISTORY, OpType::DEL, "p3");

    TEST_ASSERT_EQUAL_UINT32(1, s1);
    TEST_ASSERT_EQUAL_UINT32(2, s2);
    TEST_ASSERT_EQUAL_UINT32(3, s3);
    TEST_ASSERT_EQUAL_UINT32(4, log.getCurrentSeq());
    TEST_ASSERT_EQUAL(3, (int)log.getCount());
}

void test_deltas_since_basic(void) {
    BoundedChangeLog log;
    log.push(EntityType::CELL, OpType::UPSERT, "A");
    log.push(EntityType::TYPE, OpType::UPSERT, "B");
    log.push(EntityType::HISTORY, OpType::DEL, "C");

    std::vector<ChangeEntry> deltas;

    TEST_ASSERT_TRUE(log.getDeltasSince(0, deltas));
    TEST_ASSERT_EQUAL(3, (int)deltas.size());
    TEST_ASSERT_EQUAL_UINT32(1, deltas[0].seq_id);
    TEST_ASSERT_EQUAL_INT((int)EntityType::CELL, (int)deltas[0].entity);
    TEST_ASSERT_EQUAL_STRING("A", deltas[0].payload);
    TEST_ASSERT_EQUAL_UINT32(3, deltas[2].seq_id);
    TEST_ASSERT_EQUAL_INT((int)OpType::DEL, (int)deltas[2].op);

    TEST_ASSERT_TRUE(log.getDeltasSince(2, deltas));
    TEST_ASSERT_EQUAL(1, (int)deltas.size());
    TEST_ASSERT_EQUAL_UINT32(3, deltas[0].seq_id);

    TEST_ASSERT_TRUE(log.getDeltasSince(3, deltas));
    TEST_ASSERT_EQUAL(0, (int)deltas.size());
}

void test_wrap_around_and_full_sync(void) {
    BoundedChangeLog log;
    for (int i = 0; i < 60; ++i) {
        log.push(EntityType::CELL, OpType::UPSERT, "x");
    }
    TEST_ASSERT_EQUAL_UINT32(61, log.getCurrentSeq());
    TEST_ASSERT_EQUAL(50, (int)log.getCount());

    std::vector<ChangeEntry> deltas;

    TEST_ASSERT_TRUE(log.getDeltasSince(10, deltas));
    TEST_ASSERT_EQUAL(50, (int)deltas.size());
    TEST_ASSERT_EQUAL_UINT32(11, deltas[0].seq_id);
    TEST_ASSERT_EQUAL_UINT32(60, deltas[49].seq_id);

    TEST_ASSERT_FALSE(log.getDeltasSince(9, deltas));

    TEST_ASSERT_TRUE(log.getDeltasSince(50, deltas));
    TEST_ASSERT_EQUAL(10, (int)deltas.size());
    TEST_ASSERT_EQUAL_UINT32(51, deltas[0].seq_id);
    TEST_ASSERT_EQUAL_UINT32(60, deltas[9].seq_id);
}

void test_payload_truncation(void) {
    BoundedChangeLog log;
    log.push(EntityType::CELL, OpType::UPSERT, "hello");
    std::vector<ChangeEntry> deltas;
    TEST_ASSERT_TRUE(log.getDeltasSince(0, deltas));
    TEST_ASSERT_EQUAL_STRING("hello", deltas[0].payload);

    char long_payload[300];
    for (int i = 0; i < 200; ++i) long_payload[i] = (char)120;
    long_payload[200] = 0;
    log.push(EntityType::TYPE, OpType::UPSERT, long_payload);
    TEST_ASSERT_TRUE(log.getDeltasSince(1, deltas));
    TEST_ASSERT_EQUAL(1, (int)deltas.size());
    TEST_ASSERT_EQUAL(199, (int)strlen(deltas[0].payload));
}

void test_reset(void) {
    BoundedChangeLog log;
    log.push(EntityType::CELL, OpType::UPSERT, "a");
    log.push(EntityType::TYPE, OpType::UPSERT, "b");
    TEST_ASSERT_EQUAL_UINT32(3, log.getCurrentSeq());
    TEST_ASSERT_EQUAL(2, (int)log.getCount());

    log.reset(1000000);
    TEST_ASSERT_EQUAL_UINT32(1000000, log.getCurrentSeq());
    TEST_ASSERT_EQUAL(0, (int)log.getCount());

    std::vector<ChangeEntry> deltas;
    // Client fully synced before the reset (last seq = 999999) gets nothing.
    TEST_ASSERT_TRUE(log.getDeltasSince(999999, deltas));
    TEST_ASSERT_EQUAL(0, (int)deltas.size());

    uint32_t s = log.push(EntityType::HISTORY, OpType::UPSERT, "c");
    TEST_ASSERT_EQUAL_UINT32(1000000, s);
    TEST_ASSERT_EQUAL_UINT32(1000001, log.getCurrentSeq());
    TEST_ASSERT_TRUE(log.getDeltasSince(999999, deltas));
    TEST_ASSERT_EQUAL(1, (int)deltas.size());
    TEST_ASSERT_EQUAL_UINT32(1000000, deltas[0].seq_id);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_basic_push_and_seq);
    RUN_TEST(test_deltas_since_basic);
    RUN_TEST(test_wrap_around_and_full_sync);
    RUN_TEST(test_payload_truncation);
    RUN_TEST(test_reset);
    return UNITY_END();
}