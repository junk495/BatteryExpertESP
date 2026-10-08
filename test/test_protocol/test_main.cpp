// Native Unit-Tests für die reine Protokollschicht (lib/Mc5000Protocol).
// Ausführen:  pio test -e test   (benötigt einen Host-C++-Compiler, z.B. MinGW)
//
// Entspricht ProtocolCodecTest.kt der Android-App.

#include <unity.h>
#include "Mc5000Protocol.h"

#include <vector>

using mc5000::Mc5000Protocol;
using mc5000::ChargeProfile;
using mc5000::SlotStatus;

static Mc5000Protocol codec;

void setUp(void) {}
void tearDown(void) {}

static void assertPacket(const Mc5000Protocol::Packet& p,
                         const std::vector<uint8_t>& expected) {
    TEST_ASSERT_EQUAL_INT((int)expected.size(), (int)p.size());
    for (size_t i = 0; i < expected.size() && i < p.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT8(expected[i], p[i]);
    }
}

void test_buildPacket_checksum(void) {
    auto p = codec.buildPacket(0x91, {0x01});
    assertPacket(p, {0x0F, 0x03, 0x91, 0x01, 0x92});
}

void test_checksumValid(void) {
    const std::vector<uint8_t> ok = {0x0F, 0x03, 0x91, 0x01, 0x92};
    TEST_ASSERT_TRUE(codec.checksumValid(ok));

    const std::vector<uint8_t> bad = {0x0F, 0x03, 0x91, 0x01, 0x00};
    TEST_ASSERT_FALSE(codec.checksumValid(bad));
}

void test_buildStatusRequest(void) {
    auto p = codec.buildStatusRequest(1);
    assertPacket(p, {0x0F, 0x03, 0x91, 0x01, 0x92});
}

void test_buildStartStop(void) {
    // "Start all" laut Referenz: 0f 03 93 03 96
    auto p = codec.buildStartStop(3);
    assertPacket(p, {0x0F, 0x03, 0x93, 0x03, 0x96});
}

void test_parseStatus_full(void) {
    std::vector<uint8_t> pkt = {
        0x0F, 0x15, 0x91, 0x01,
        0x07, 0xD0,   // Strom   2000 mA
        0x10, 0x5C,   // Spannung 4188 mV
        0x09, 0x0B,   // Temp     2315 -> 2.315 C
        0x00, 0x64,   // Kapazität 100 mAh
        0x00, 0x00, 0x00, 0x3C,  // Zeit 60 s
        0x00, 0x32,   // IR 50 mOhm
        0x02,         // Status Charging
        0x00,         // Mode Charge
        0x00,         // Fehler -
        0x00          // Chemie Li-Ion
    };
    int sum = 0;
    for (size_t i = 2; i < pkt.size(); ++i) sum += pkt[i];
    pkt.push_back(static_cast<uint8_t>(sum & 0xFF));

    SlotStatus s = codec.parseStatus(pkt);
    TEST_ASSERT_EQUAL_INT(1, s.slot);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, s.currentA);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.188f, s.voltageV);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.315f, s.temperatureC);
    TEST_ASSERT_EQUAL_INT(100, s.capacityMah);
    TEST_ASSERT_EQUAL_UINT32(60, s.elapsedSeconds);
    TEST_ASSERT_EQUAL_INT(50, s.internalResistanceMOhm);
    TEST_ASSERT_EQUAL_STRING("Charging", s.status.c_str());
    TEST_ASSERT_EQUAL_STRING("Charge", s.mode.c_str());
    TEST_ASSERT_EQUAL_STRING("", s.error.c_str());
    TEST_ASSERT_EQUAL_STRING("Li-Ion", s.chemistry.c_str());
}

void test_parseStatus_truncated(void) {
    // 20-Byte-Notification (Default-MTU): Fehler-/Chemie-Bytes fehlen.
    std::vector<uint8_t> pkt = {
        0x0F, 0x12, 0x91, 0x01,
        0x00, 0xFA,   // Strom 250 mA
        0x0F, 0xA0,   // Spannung 4000 mV
        0x07, 0xD0,   // Temp 2000 -> 2.0 C
        0x00, 0x64,   // Kapazität 100 mAh
        0x00, 0x00, 0x00, 0x00,  // Zeit 0
        0x00, 0x32,   // IR 50 mOhm
        0x02,         // [18] Status Charging
        0x00          // [19] Mode Charge
    };

    SlotStatus s = codec.parseStatus(pkt);
    TEST_ASSERT_EQUAL_INT(1, s.slot);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f, s.currentA);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, s.voltageV);
    TEST_ASSERT_EQUAL_INT(100, s.capacityMah);
    TEST_ASSERT_EQUAL_INT(50, s.internalResistanceMOhm);
    TEST_ASSERT_EQUAL_STRING("Charging", s.status.c_str());
    TEST_ASSERT_EQUAL_STRING("Charge", s.mode.c_str());
    TEST_ASSERT_EQUAL_STRING("", s.error.c_str());   // fehlt -> leer
}

void test_buildChargeConfig(void) {
    ChargeProfile p;
    p.mode = "charge";
    p.chargeCurrentMa = 2000;
    p.dischargeCurrentMa = 1000;
    p.targetVoltageMv = 4200;
    p.cutoffVoltageMv = 3200;
    p.terminationCurrentMa = 100;
    p.restChargeMin = 10;
    p.restDischargeMin = 10;

    auto pkt = codec.buildChargeConfig(p, 0, 1, 3000);
    TEST_ASSERT_EQUAL_INT(44, (int)pkt.size());
    TEST_ASSERT_EQUAL_UINT8(0x0F, pkt[0]);
    TEST_ASSERT_EQUAL_UINT8(0x2A, pkt[1]);
    TEST_ASSERT_EQUAL_UINT8(0x94, pkt[2]);
    TEST_ASSERT_EQUAL_UINT8(0x01, pkt[3]);   // Slot-Bitmaske
    TEST_ASSERT_EQUAL_UINT8(0x00, pkt[4]);   // Modus charge
    TEST_ASSERT_EQUAL_UINT8(0x00, pkt[35]);  // Chemie Li-Ion

    int sum = 0;
    for (size_t i = 2; i <= 42; ++i) sum += pkt[i];
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(sum & 0xFF), pkt[43]);
}

void test_isConfigAckOk(void) {
    // ACK Slot 1: 0f 04 94 01 01 96
    const uint8_t cks = (uint8_t)((0x94 + 0x01 + 0x01) & 0xFF);
    const std::vector<uint8_t> ack = {0x0F, 0x04, 0x94, 0x01, 0x01, cks};
    TEST_ASSERT_TRUE(codec.isConfigAckOk(ack));
}

// Mode-Byte-Mapping (kanonisch, Li-Ion) gemäß rssdev10/skyrc-mc-rs:
// 0=Charge, 1=Storage, 2=Discharge, 3=Cycle, 4=Refresh, 5=BreakIn.
void test_mapMode(void) {
    TEST_ASSERT_EQUAL_STRING("Charge",    Mc5000Protocol::mapMode(0).c_str());
    TEST_ASSERT_EQUAL_STRING("Storage",   Mc5000Protocol::mapMode(1).c_str());
    TEST_ASSERT_EQUAL_STRING("Discharge", Mc5000Protocol::mapMode(2).c_str());
    TEST_ASSERT_EQUAL_STRING("Cycle",     Mc5000Protocol::mapMode(3).c_str());
    TEST_ASSERT_EQUAL_STRING("Refresh",   Mc5000Protocol::mapMode(4).c_str());
    TEST_ASSERT_EQUAL_STRING("Break_in",  Mc5000Protocol::mapMode(5).c_str());
}

void test_modeCodeFromString(void) {
    TEST_ASSERT_EQUAL_INT(0, Mc5000Protocol::modeCodeFromString("charge"));
    TEST_ASSERT_EQUAL_INT(1, Mc5000Protocol::modeCodeFromString("storage"));
    TEST_ASSERT_EQUAL_INT(2, Mc5000Protocol::modeCodeFromString("discharge"));
    TEST_ASSERT_EQUAL_INT(3, Mc5000Protocol::modeCodeFromString("cycle"));
    TEST_ASSERT_EQUAL_INT(4, Mc5000Protocol::modeCodeFromString("refresh"));
    TEST_ASSERT_EQUAL_INT(5, Mc5000Protocol::modeCodeFromString("break_in"));

    // Groß-/Kleinschreibung und Aliase
    TEST_ASSERT_EQUAL_INT(1, Mc5000Protocol::modeCodeFromString("Storage"));
    TEST_ASSERT_EQUAL_INT(2, Mc5000Protocol::modeCodeFromString("DISCHARGE"));
    TEST_ASSERT_EQUAL_INT(1, Mc5000Protocol::modeCodeFromString("Lagerung"));
}

// encode -> decode muss für die kanonischen Modi identisch runden.
void test_modeRoundTrip(void) {
    const char* modes[] = {"charge", "storage", "discharge", "cycle", "refresh", "break_in"};
    const char* expected[] = {"Charge", "Storage", "Discharge", "Cycle", "Refresh", "Break_in"};
    for (int i = 0; i < 6; ++i) {
        const int code = Mc5000Protocol::modeCodeFromString(modes[i]);
        TEST_ASSERT_EQUAL_STRING(expected[i], Mc5000Protocol::mapMode(code).c_str());
    }
}

// Das 0x94-Konfigpaket muss das Mode-Byte gemäß kanonischem Mapping tragen.
void test_buildChargeConfig_modes(void) {
    ChargeProfile p;
    p.mode = "discharge";
    p.dischargeCurrentMa = 1000;
    auto pkt = codec.buildChargeConfig(p, 0, 2, 3000);
    TEST_ASSERT_EQUAL_UINT8(0x02, pkt[4]);   // Discharge = 0x02

    p.mode = "storage";
    pkt = codec.buildChargeConfig(p, 0, 3, 3000);
    TEST_ASSERT_EQUAL_UINT8(0x01, pkt[4]);   // Storage = 0x01
}

// NiMH/NiCd/Eneloop/NiZn verwenden ein verschobenes Mode-Byte-Layout.
void test_modeCodeForChemistry_nimh(void) {
    TEST_ASSERT_EQUAL_INT(0, Mc5000Protocol::modeCodeForChemistry("charge", 3));
    TEST_ASSERT_EQUAL_INT(2, Mc5000Protocol::modeCodeForChemistry("break_in", 3));
    TEST_ASSERT_EQUAL_INT(3, Mc5000Protocol::modeCodeForChemistry("discharge", 3));
    TEST_ASSERT_EQUAL_INT(4, Mc5000Protocol::modeCodeForChemistry("cycle", 4));
    // Li-Chemien bleiben beim Li-Ion-Mapping
    TEST_ASSERT_EQUAL_INT(2, Mc5000Protocol::modeCodeForChemistry("discharge", 0));
    TEST_ASSERT_EQUAL_INT(1, Mc5000Protocol::modeCodeForChemistry("storage", 0));
}

void test_mapModeForChemistry_nimh(void) {
    TEST_ASSERT_EQUAL_STRING("Charge",    Mc5000Protocol::mapModeForChemistry(0, 3).c_str());
    TEST_ASSERT_EQUAL_STRING("Break_in",  Mc5000Protocol::mapModeForChemistry(2, 3).c_str());
    TEST_ASSERT_EQUAL_STRING("Discharge", Mc5000Protocol::mapModeForChemistry(3, 3).c_str());
    TEST_ASSERT_EQUAL_STRING("Cycle",     Mc5000Protocol::mapModeForChemistry(4, 4).c_str());
    // Li-Chemien bleiben beim Li-Ion-Mapping
    TEST_ASSERT_EQUAL_STRING("Discharge", Mc5000Protocol::mapModeForChemistry(2, 0).c_str());
    TEST_ASSERT_EQUAL_STRING("Cycle",     Mc5000Protocol::mapModeForChemistry(3, 0).c_str());
}

void test_buildChargeConfig_nimh(void) {
    ChargeProfile p;
    p.mode = "discharge";
    p.dischargeCurrentMa = 1000;
    auto pkt = codec.buildChargeConfig(p, 3, 2, 3000);   // NiMH
    TEST_ASSERT_EQUAL_UINT8(0x03, pkt[4]);   // NiMH Discharge = 0x03

    p.mode = "cycle";
    pkt = codec.buildChargeConfig(p, 4, 3, 3000);        // NiCd
    TEST_ASSERT_EQUAL_UINT8(0x04, pkt[4]);   // NiCd Cycle = 0x04
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_buildPacket_checksum);
    RUN_TEST(test_checksumValid);
    RUN_TEST(test_buildStatusRequest);
    RUN_TEST(test_buildStartStop);
    RUN_TEST(test_parseStatus_full);
    RUN_TEST(test_parseStatus_truncated);
    RUN_TEST(test_buildChargeConfig);
    RUN_TEST(test_isConfigAckOk);
    RUN_TEST(test_mapMode);
    RUN_TEST(test_modeCodeFromString);
    RUN_TEST(test_modeRoundTrip);
    RUN_TEST(test_buildChargeConfig_modes);
    RUN_TEST(test_modeCodeForChemistry_nimh);
    RUN_TEST(test_mapModeForChemistry_nimh);
    RUN_TEST(test_buildChargeConfig_nimh);
    return UNITY_END();
}
