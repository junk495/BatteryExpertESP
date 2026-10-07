#include "Mc5000Protocol.h"

#include <cctype>

namespace mc5000 {

Mc5000Protocol::Packet Mc5000Protocol::buildPacket(uint8_t command, const std::vector<uint8_t>& data) const {
    Packet packet;
    packet.reserve(2 + data.size() + 2);

    const uint8_t length = static_cast<uint8_t>(1 + data.size() + 1); // command + data + checksum
    int sum = command;
    for (uint8_t b : data) {
        sum += b;
    }
    const uint8_t checksum = static_cast<uint8_t>(sum & 0xFF);

    packet.push_back(0x0F);
    packet.push_back(length);
    packet.push_back(command);
    packet.insert(packet.end(), data.begin(), data.end());
    packet.push_back(checksum);
    return packet;
}

bool Mc5000Protocol::checksumValid(const Packet& p) const {
    if (p.size() < 4) return false;
    if (p[0] != 0x0F) return false;
    const int len = p[1];
    if (p.size() < static_cast<size_t>(len + 2)) return false;
    const size_t checksumIndex = 1 + len;
    int sum = 0;
    for (size_t i = 2; i < checksumIndex; ++i) {
        sum += p[i];
    }
    return p[checksumIndex] == static_cast<uint8_t>(sum & 0xFF);
}

Mc5000Protocol::Packet Mc5000Protocol::buildStatusRequest(int slotBitmask) const {
    return buildPacket(0x91, {static_cast<uint8_t>(slotBitmask)});
}

Mc5000Protocol::Packet Mc5000Protocol::buildStartStop(int action) const {
    return buildPacket(0x93, {static_cast<uint8_t>(action)});
}

Mc5000Protocol::Packet Mc5000Protocol::buildChargeConfig(
        const ChargeProfile& profile, int chemistryCode, int slotBitmask, int capacityCutoffMah) const {
    Packet packet(44, 0);
    packet[0] = 0x0F;
    packet[1] = 0x2A;   // Länge = 42
    packet[2] = 0x94;
    packet[3] = static_cast<uint8_t>(slotBitmask);
    packet[4] = static_cast<uint8_t>(modeCodeFromString(profile.mode));

    auto putU16 = [&packet](size_t offset, int value) {
        packet[offset]     = static_cast<uint8_t>((value >> 8) & 0xFF);
        packet[offset + 1] = static_cast<uint8_t>(value & 0xFF);
    };

    putU16(5,  profile.chargeCurrentMa);
    putU16(7,  profile.dischargeCurrentMa);
    putU16(9,  capacityCutoffMah);
    putU16(11, profile.targetVoltageMv);
    putU16(13, profile.cutoffVoltageMv);
    putU16(15, profile.terminationCurrentMa);
    putU16(17, 100);                       // TODO: gegen Referenz verifizieren (wie in der App)
    putU16(19, profile.restChargeMin);
    putU16(21, profile.restDischargeMin);

    packet[23] = static_cast<uint8_t>(clamp(profile.cycleCount, 1, 99));
    packet[24] = static_cast<uint8_t>(clamp(profile.cycleDirection, 0, 3));
    packet[25] = static_cast<uint8_t>(clamp(profile.deltaPeakMv, 0, 255));
    packet[26] = static_cast<uint8_t>(clamp(profile.trickleChargeMa / 10, 0, 255));

    putU16(27, 0);                        // Keep-Spannung (TODO: verifizieren)
    packet[29] = 0x3C;                    // TODO: verifizieren
    putU16(30, profile.cutoffTimerMin);
    putU16(32, profile.maxTimeMin);
    packet[34] = 0x00;
    packet[35] = static_cast<uint8_t>(chemistryCode);

    // Sekundärspannung: für Li-Chemien die Zielspannung, sonst 3.3 V
    const int secondaryVal = (chemistryCode == 0 || chemistryCode == 1 || chemistryCode == 2 ||
                              chemistryCode == 8 || chemistryCode == 9)
                                 ? profile.targetVoltageMv
                                 : 3300;
    putU16(36, secondaryVal);

    int sum = 0;
    for (size_t i = 2; i <= 42; ++i) {
        sum += packet[i];
    }
    packet[43] = static_cast<uint8_t>(sum & 0xFF);

    return packet;
}

bool Mc5000Protocol::isConfigAckOk(const Packet& p) const {
    if (p.size() < 6) return false;
    if (p[0] != 0x0F) return false;
    if (p[1] != 0x04) return false;
    if (p[2] != 0x94) return false;
    return p[4] == 0x01;
}

SlotStatus Mc5000Protocol::parseStatus(const Packet& p) const {
    SlotStatus s;

    switch (readByte(p, 3)) {
        case 1:  s.slot = 1; break;
        case 2:  s.slot = 2; break;
        case 4:  s.slot = 3; break;
        case 8:  s.slot = 4; break;
        default: s.slot = 0; break;
    }

    s.currentA = static_cast<float>(readUShort(p, 4)) / 1000.0f;
    s.voltageV = static_cast<float>(readUShort(p, 6)) / 1000.0f;
    const float rawTemp = static_cast<float>(readUShort(p, 8)) / 1000.0f;
    s.temperatureC = (rawTemp < 1.0f) ? 0.0f : rawTemp;
    s.capacityMah = readUShort(p, 10);
    s.elapsedSeconds = readUInt(p, 12);
    s.internalResistanceMOhm = readUShort(p, 16);
    s.status = mapStatus(readByte(p, 18));
    s.mode = mapMode(readByte(p, 19));
    s.error = mapError(readByte(p, 20));
    s.chemistry = mapChemistry(readByte(p, 21));
    return s;
}

int Mc5000Protocol::readByte(const Packet& p, size_t index, int def) {
    if (index < p.size()) return p[index];
    return def;
}

int Mc5000Protocol::readUShort(const Packet& p, size_t start, int def) {
    if (start + 1 < p.size()) {
        return (p[start] << 8) | p[start + 1];
    }
    if (start < p.size()) {
        return p[start] << 8;
    }
    return def;
}

uint32_t Mc5000Protocol::readUInt(const Packet& p, size_t start, uint32_t def) {
    if (start >= p.size()) return def;
    uint32_t result = 0;
    const size_t available = (p.size() - start) < 4 ? (p.size() - start) : 4;
    for (size_t i = 0; i < available; ++i) {
        result = (result << 8) | p[start + i];
    }
    return result;
}

int Mc5000Protocol::clamp(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

std::string Mc5000Protocol::toLower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

int Mc5000Protocol::modeCodeFromString(const std::string& mode) {
    const std::string m = toLower(mode);
    if (m == "charge" || m == "normal charge" || m == "laden") return 0;
    if (m == "storage" || m == "lagerung") return 1;
    if (m == "discharge" || m == "entladen") return 2;
    if (m == "cycle" || m == "zyklus") return 3;
    if (m == "refresh" || m == "auffrischen") return 4;
    if (m == "break_in" || m == "break-in" || m == "formieren") return 5;
    return 0;
}

std::string Mc5000Protocol::mapChemistry(int code) {
    switch (code) {
        case 0: return "Li-Ion";
        case 1: return "Li-Ion HV";
        case 2: return "LiFePO4";
        case 3: return "NiMH";
        case 4: return "NiCd";
        case 5: return "Eneloop";
        case 6: return "NiZn";
        case 7: return "RAM";
        case 8: return "LTO";
        case 9: return "Na-Ion";
        default: return "Unknown";
    }
}

std::string Mc5000Protocol::mapStatus(int code) {
    switch (code) {
        case 0: return "Standby";
        case 1: return "Processing";
        case 2: return "Charging";
        case 3: return "Discharging";
        case 4: return "Resting";
        case 5:
        case 6: return "Completed";
        default: return "Unknown";
    }
}

std::string Mc5000Protocol::mapMode(int code) {
    switch (code) {
        case 0: return "Charge";
        case 1: return "Discharge";
        case 2: return "Storage";
        case 3: return "Cycle";
        case 4: return "Refresh";
        case 5: return "Break_in";
        default: return "Mode " + std::to_string(code);
    }
}

std::string Mc5000Protocol::mapError(int code) {
    switch (code) {
        case 1: return "Input voltage too low";
        case 2: return "Input voltage too high";
        case 3: return "Connection break";
        case 4: return "Capacity limit reached";
        case 5: return "Time limit reached";
        case 6: return "Internal temperature too high";
        case 7: return "Calibration failed";
        case 8: return "High internal resistance";
        case 9: return "Connection break";
        case 10: return "Battery type error";
        case 11: return "Overload protection";
        case 12: return "Reversed polarity";
        case 13: return "Fully charged";
        default: return "";
    }
}

} // namespace mc5000
