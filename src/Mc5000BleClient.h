#pragma once

// BLE-Central-Client für den SkyRC MC5000 (reine Transport-Schicht).
// Die Protokoll-Logik liegt in mc5000::Mc5000Protocol.
//
// Nutzt NimBLE-Arduino; begin() muss vor scanDevices()/connect() aufgerufen werden.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class NimBLEClient;
class NimBLERemoteCharacteristic;

// Ein beim Scannen gefundenes BLE-Gerät (für die Verbindungsauswahl).
struct BleDeviceInfo {
    std::string name;      // beworbener Name (kann leer sein)
    std::string address;   // MAC-Adresse, z. B. "aa:bb:cc:dd:ee:ff"
};

class Mc5000BleClient {
public:
    using NotifyCallback = std::function<void(const std::vector<uint8_t>&)>;

    void begin(const std::string& deviceName = "mc5000-bridge");
    std::vector<BleDeviceInfo> scanDevices(uint32_t scanMillis = 5000);
    bool connect(const std::string& address);   // per MAC-Adresse verbinden
    void disconnect();
    bool isConnected() const;
    bool writePacket(const std::vector<uint8_t>& data);   // WRITE WITHOUT RESPONSE
    void setNotifyCallback(NotifyCallback cb);

private:
    NimBLEClient* _client = nullptr;
    NimBLERemoteCharacteristic* _chr = nullptr;
    NotifyCallback _onNotify;
    bool _connected = false;
};
