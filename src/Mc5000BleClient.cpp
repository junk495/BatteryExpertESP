#include "Mc5000BleClient.h"

#include <NimBLEDevice.h>

namespace {
// Identisch zur Android-App (Mc5000BleManager.kt).
const NimBLEUUID SERVICE_UUID("0000ffe0-0000-1000-8000-00805f9b34fb");
const NimBLEUUID CHAR_UUID("0000ffe1-0000-1000-8000-00805f9b34fb");
constexpr int REQUESTED_MTU = 247;
}

void Mc5000BleClient::begin(const std::string& deviceName) {
    NimBLEDevice::init(deviceName);
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

bool Mc5000BleClient::scanAndConnect(uint32_t scanSeconds) {
    if (isConnected()) {
        return true;
    }
    disconnect();

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    NimBLEScanResults results = scan->start(scanSeconds);

    NimBLEAdvertisedDevice* target = nullptr;
    for (int i = 0; i < results.getCount(); ++i) {
        NimBLEAdvertisedDevice* dev = results.getDevice(i);
        if (!dev) {
            continue;
        }
        const std::string name = dev->getName();
        if (name.find("MC5000") != std::string::npos ||
            name.find("SkyRC") != std::string::npos ||
            dev->isAdvertisingService(SERVICE_UUID)) {
            target = dev;
            break;
        }
    }

    if (!target) {
        scan->clearResults();
        return false;
    }

    NimBLEClient* client = NimBLEDevice::createClient();
    if (!client->connect(target)) {
        NimBLEDevice::deleteClient(client);
        scan->clearResults();
        return false;
    }

    client->setMTU(REQUESTED_MTU);

    NimBLERemoteService* svc = client->getService(SERVICE_UUID);
    if (!svc) {
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        scan->clearResults();
        return false;
    }

    NimBLERemoteCharacteristic* chr = svc->getCharacteristic(CHAR_UUID);
    if (!chr) {
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        scan->clearResults();
        return false;
    }

    chr->subscribe(true, [this](NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool, void*) {
        if (_onNotify && len > 0) {
            _onNotify(std::vector<uint8_t>(data, data + len));
        }
    });

    _client = client;
    _chr = chr;
    _connected = true;

    scan->clearResults();
    return true;
}

bool Mc5000BleClient::writePacket(const std::vector<uint8_t>& data) {
    if (!isConnected() || !_chr) {
        return false;
    }
    return _chr->writeValue(data.data(), data.size(), false);   // false = ohne Antwort
}
