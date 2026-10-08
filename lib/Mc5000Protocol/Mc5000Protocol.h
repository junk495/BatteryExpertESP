#pragma once

// Reine Protokollschicht für den SkyRC MC5000 (1:1-Port von
// com.batteryexpert.data.ble.ProtocolCodec der Android-App).
//
// Keine Bluetooth-/Arduino-Abhängigkeit -> nativ unit-testbar.
// Referenz: https://github.com/rssdev10/skyrc-mc-rs (docs/PROTOCOL.md)

#include <cstdint>
#include <string>
#include <vector>

namespace mc5000 {

// Live-Status eines Slots (Ergebnis einer 0x91-Antwort).
struct SlotStatus {
    int slot = 0;
    float voltageV = 0.0f;
    float currentA = 0.0f;
    float temperatureC = 0.0f;
    int capacityMah = 0;
    uint32_t elapsedSeconds = 0;
    int internalResistanceMOhm = 0;
    std::string status;     // Standby/Processing/Charging/Discharging/Resting/Completed
    std::string mode;       // Charge/Discharge/Storage/Cycle/Refresh/Break_in
    std::string error;
    std::string chemistry;
};

// Parameter einer Lade-/Entlade-Konfiguration
// (spiegelt ChargeProfileEntity der App wider).
struct ChargeProfile {
    std::string mode = "charge";   // charge/storage/discharge/cycle/refresh/break_in
    int chargeCurrentMa = 0;
    int dischargeCurrentMa = 0;
    int targetVoltageMv = 0;
    int cutoffVoltageMv = 0;
    int terminationCurrentMa = 0;
    int cycleDirection = 0;        // 0=C->D, 1=D->C, 2=C->D->C, 3=D->C->D
    int cycleCount = 1;            // 1..99
    int restChargeMin = 0;
    int restDischargeMin = 0;
    int trickleChargeMa = 0;
    int deltaPeakMv = 0;
    int cutoffTimerMin = 0;
    int maxTimeMin = 0;
};

class Mc5000Protocol {
public:
    using Packet = std::vector<uint8_t>;

    // Paket: [0x0F, length, command, ...data, checksum]
    Packet buildPacket(uint8_t command, const std::vector<uint8_t>& data = {}) const;
    bool checksumValid(const Packet& packet) const;

    Packet buildStatusRequest(int slotBitmask) const;   // 0x91 (Masken 1,2,4,8)
    Packet buildStartStop(int action) const;            // 0x93
    Packet buildChargeConfig(const ChargeProfile& profile, int chemistryCode,
                             int slotBitmask, int capacityCutoffMah = 3000) const; // 0x94

    bool isConfigAckOk(const Packet& packet) const;     // 0x94-ACK (0f 04 94 <slot> 01 <cks>)
    SlotStatus parseStatus(const Packet& packet) const; // 0x91-Antwort (robust gegen Kürzung)

    // Mapping-Helfer (öffentlich für Wiederverwendung/Tests)
    static std::string mapChemistry(int code);
    static std::string mapStatus(int code);
    static std::string mapMode(int code);
    static std::string mapError(int code);
    static int modeCodeFromString(const std::string& mode);
    static std::string mapModeForChemistry(int code, int chemistryCode);
    static int modeCodeForChemistry(const std::string& mode, int chemistryCode);

private:
    static int readByte(const Packet& packet, size_t index, int def = 0);
    static int readUShort(const Packet& packet, size_t start, int def = 0);
    static uint32_t readUInt(const Packet& packet, size_t start, uint32_t def = 0);
    static int clamp(int value, int lo, int hi);
    static std::string toLower(std::string s);
};

} // namespace mc5000
