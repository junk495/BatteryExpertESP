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

std::vector<BleDeviceInfo> Mc5000BleClient::scanDevices(uint32_t scanSeconds) {
    std::vector<BleDeviceInfo> result;

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    NimBLEScanResults results = scan->start(scanSeconds);

    for (int i = 0; i < results.getCount(); ++i) {
        NimBLEAdvertisedDevice* dev = results.getDevice(i);
        if (!dev) {
            continue;
        }
        BleDeviceInfo info;
        info.name = dev->getName();
        info.address = dev->getAddress().toString();
        result.push_back(info);
    }

    scan->clearResults();
    return result;
}

bool Mc5000BleClient::connect(const std::string& address) {
    disconnect();   // bestehende Verbindung (falls vorhanden) trennen

    NimBLEClient* client = NimBLEDevice::createClient();
    if (!client->connect(NimBLEAddress(address))) {
        NimBLEDevice::deleteClient(client);
        return false;
    }

    client->setMTU(REQUESTED_MTU);

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

    chr->subscribe(true, [this](NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool, void*) {
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
    return _chr->writeValue(data.data(), data.size(), false);   // false = ohne Antwort
}
