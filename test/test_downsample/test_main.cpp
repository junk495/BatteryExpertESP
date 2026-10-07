// Native unit tests for DownsamplingBuffer.
// Run: pio test -e test

#include <unity.h>
#include "DownsamplingBuffer.h"

void setUp(void) {}
void tearDown(void) {}

void test_phase1_full_resolution(void) {
    DownsamplingBuffer buf;
    TEST_ASSERT_TRUE(buf.allocate());
    buf.startSession(0);
    for (uint32_t t = 1; t <= 59; ++t) {
        buf.push(t, 3000, 1000, (uint16_t)t, 25);
    }
    TEST_ASSERT_EQUAL(59, (int)buf.getCount());
    const LivePoint* d = buf.getData();
    TEST_ASSERT_EQUAL_UINT16(3000, d[0].voltage_mv);
    TEST_ASSERT_EQUAL_INT16(1000, d[0].current_ma);
    TEST_ASSERT_EQUAL_INT8(25, d[0].temp_c);
    TEST_ASSERT_EQUAL_UINT16(59, d[58].capacity_mah);
}

void test_phase2_averaging(void) {
    DownsamplingBuffer buf;
    TEST_ASSERT_TRUE(buf.allocate());
    buf.startSession(0);
    for (uint32_t t = 1; t <= 59; ++t) {
        buf.push(t, 3000, 1000, (uint16_t)t, 25);
    }
    for (uint32_t t = 60; t <= 69; ++t) {
        buf.push(t, 2000, 500, 100, 30);
    }
    TEST_ASSERT_EQUAL(60, (int)buf.getCount());
    const LivePoint* d = buf.getData();
    TEST_ASSERT_EQUAL_UINT32(69, d[59].timestamp_s);
    TEST_ASSERT_EQUAL_UINT16(2000, d[59].voltage_mv);
    TEST_ASSERT_EQUAL_INT16(500, d[59].current_ma);
    TEST_ASSERT_EQUAL_INT8(30, d[59].temp_c);
}

void test_capacity_not_averaged(void) {
    DownsamplingBuffer buf;
    TEST_ASSERT_TRUE(buf.allocate());
    buf.startSession(0);
    for (uint32_t t = 1; t <= 59; ++t) {
        buf.push(t, 3000, 1000, (uint16_t)t, 25);
    }
    for (uint32_t t = 60; t <= 69; ++t) {
        buf.push(t, 2000, 500, (uint16_t)(100 + (t - 60)), 30);
    }
    const LivePoint* d = buf.getData();
    TEST_ASSERT_EQUAL_UINT16(109, d[59].capacity_mah);
}

void test_12h_total_count(void) {
    DownsamplingBuffer buf;
    TEST_ASSERT_TRUE(buf.allocate());
    buf.startSession(0);
    for (uint32_t t = 1; t <= 43200; ++t) {
        buf.push(t, 3000, 1000, (uint16_t)(t % 1000), 25);
    }
    TEST_ASSERT_EQUAL(1733, (int)buf.getCount());
}

void test_max_points_limit(void) {
    DownsamplingBuffer buf;
    TEST_ASSERT_TRUE(buf.allocate());
    buf.startSession(0);
    for (uint32_t t = 1; t <= 72000; ++t) {
        buf.push(t, 3000, 1000, (uint16_t)(t % 1000), 25);
    }
    TEST_ASSERT_EQUAL(2000, (int)buf.getCount());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_phase1_full_resolution);
    RUN_TEST(test_phase2_averaging);
    RUN_TEST(test_capacity_not_averaged);
    RUN_TEST(test_12h_total_count);
    RUN_TEST(test_max_points_limit);
    return UNITY_END();
}
