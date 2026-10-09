#include "Mc5000BleClient.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "config.h"

namespace {
// Same as the Android app (Mc5000BleManager.kt).
const NimBLEUUID SERVICE_UUID("0000ffe0-0000-1000-8000-00805f9b34fb");
const NimBLEUUID CHAR_UUID("0000ffe1-0000-1000-8000-00805f9b34fb");
constexpr int REQUESTED_MTU = 247;
}

#if MC5000_DEBUG_RAW
// Raw TX/RX hex dump for protocol verification against the real device.
static void dumpBytes(const char* tag, const uint8_t* data, size_t len) {
    Serial.printf("[ble %s] %uB:", tag, (unsigned)len);
    for (size_t i = 0; i < len; ++i) {
        Serial.printf(" %02X", data[i]);
    }
    Serial.println();
}
#endif

void Mc5000BleClient::begin(const std::string& deviceName) {
    NimBLEDevice::init(deviceName);
    NimBLEDevice::setMTU(REQUESTED_MTU);
}

void Mc5000BleClient::setNotifyCallback(NotifyCallback cb) {
    _onNotify = std::move(cb);
}

bool Mc5000BleClient::isConnected() const {
    return _connected && _client != nullptr && _client->isConnected();
}

void Mc5000BleClient::disconnect() {
    if (_client) {
        _client->disconnect();
        NimBLEDevice::deleteClient(_client);
        _client = nullptr;
    }
    _chr = nullptr;
    _connected = false;
}

std::vector<BleDeviceInfo> Mc5000BleClient::scanDevices(uint32_t scanMillis) {
    std::vector<BleDeviceInfo> result;

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    NimBLEScanResults results = scan->getResults(scanMillis);   // blocking scan (Millisekunden!)

    Serial.printf("[ble] Scan beendet: %d Gerät(e) gefunden\n", (int)results.getCount());

    for (int i = 0; i < results.getCount(); ++i) {
        const NimBLEAdvertisedDevice* dev = results.getDevice(i);
        if (!dev) {
            continue;
        }
        BleDeviceInfo info;
        info.name = dev->getName();
        info.address = dev->getAddress().toString();
        result.push_back(info);
        Serial.printf("[ble]   - %s (%s)\n", info.name.c_str(), info.address.c_str());
    }

    scan->clearResults();
    return result;
}

bool Mc5000BleClient::connect(const std::string& address) {
    disconnect();   // disconnect existing connection (if any)

    NimBLEClient* client = NimBLEDevice::createClient();
    if (!client->connect(NimBLEAddress(address, 0))) {   // 0 = public address
        NimBLEDevice::deleteClient(client);
        return false;
    }

    NimBLERemoteService* svc = client->getService(SERVICE_UUID);
    if (!svc) {
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        return false;
    }

    NimBLERemoteCharacteristic* chr = svc->getCharacteristic(CHAR_UUID);
    if (!chr) {
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        return false;
    }

    chr->subscribe(true, [this](NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
#if MC5000_DEBUG_RAW
        dumpBytes("rx", data, len);
#endif
        if (_onNotify && len > 0) {
            _onNotify(std::vector<uint8_t>(data, data + len));
        }
    });

    _client = client;
    _chr = chr;
    _connected = true;
    return true;
}

bool Mc5000BleClient::writePacket(const std::vector<uint8_t>& data) {
    if (!isConnected() || !_chr) {
        return false;
    }
#if MC5000_DEBUG_RAW
    dumpBytes("tx", data.data(), data.size());
#endif
    return _chr->writeValue(data.data(), data.size(), false);   // false = no response
}