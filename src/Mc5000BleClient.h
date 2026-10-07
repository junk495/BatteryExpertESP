#pragma once

// BLE-Central-Client für den SkyRC MC5000 (reine Transport-Schicht).
// Die Protokoll-Logik liegt in mc5000::Mc5000Protocol.
//
// Nutzt NimBLE-Arduino; begin() muss vor scanAndConnect() aufgerufen werden.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class NimBLEClient;
class NimBLERemoteCharacteristic;

class Mc5000BleClient {
public:
    using NotifyCallback = std::function<void(const std::vector<uint8_t>&)>;

    void begin(const std::string& deviceName = "mc5000-bridge");
    bool scanAndConnect(uint32_t scanSeconds = 5);
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
