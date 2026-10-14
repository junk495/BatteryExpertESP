// Native unit tests for AssessmentLogic (test-current defaults).
// Run: pio test -e test
//
// Mirrors AssessmentLogicTest of the Android app (computeDefaults).

#include <unity.h>
#include "AssessmentLogic.h"

using namespace assessment;

void setUp(void) {}
void tearDown(void) {}

static void test_computeTestCurrents_lgHe4(void) {
    // LG HE4: 2500 mAh.
    TestCurrents t = computeTestCurrents(2500);
    TEST_ASSERT_EQUAL_INT(2500, t.fastChargeMa);      // 1 C
    TEST_ASSERT_EQUAL_INT(2000, t.fastDischargeMa);   // 1 C, capped at 2 A
    TEST_ASSERT_EQUAL_INT(1250, t.slowChargeMa);      // 0.5 C
    TEST_ASSERT_EQUAL_INT(500,  t.slowDischargeMa);   // 0.2 C
}

static void test_computeTestCurrents_caps(void) {
    // High-capacity cell: charge/discharge capped at the MC5000 limits.
    TestCurrents t = computeTestCurrents(6000);
    TEST_ASSERT_EQUAL_INT(5000, t.fastChargeMa);
    TEST_ASSERT_EQUAL_INT(2000, t.fastDischargeMa);
    TEST_ASSERT_EQUAL_INT(3000, t.slowChargeMa);
    TEST_ASSERT_EQUAL_INT(1200, t.slowDischargeMa);
}

static void test_computeTestCurrents_zero(void) {
    TestCurrents t = computeTestCurrents(0);
    TEST_ASSERT_EQUAL_INT(0, t.fastChargeMa);
    TEST_ASSERT_EQUAL_INT(0, t.fastDischargeMa);
    TEST_ASSERT_EQUAL_INT(0, t.slowChargeMa);
    TEST_ASSERT_EQUAL_INT(0, t.slowDischargeMa);
}

static void test_computeTestCurrents_rounding(void) {
    // 1500 mAh: 1 C = 1500, 0.5 C = 750, 0.2 C = 300 (all exact).
    TestCurrents t = computeTestCurrents(1500);
    TEST_ASSERT_EQUAL_INT(1500, t.fastChargeMa);
    TEST_ASSERT_EQUAL_INT(1500, t.fastDischargeMa);
    TEST_ASSERT_EQUAL_INT(750,  t.slowChargeMa);
    TEST_ASSERT_EQUAL_INT(300,  t.slowDischargeMa);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_computeTestCurrents_lgHe4);
    RUN_TEST(test_computeTestCurrents_caps);
    RUN_TEST(test_computeTestCurrents_zero);
    RUN_TEST(test_computeTestCurrents_rounding);
    return UNITY_END();
}
