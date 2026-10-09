#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

#include "config.h"
#include "Mc5000Protocol.h"
#include "Mc5000BleClient.h"
#include "DataStore.h"
#include "ApiServer.h"

// ---------- Globale Zustände ----------
static mc5000::Mc5000Protocol g_protocol;
static Mc5000BleClient g_ble;
static DataStore g_store;
static ApiServer g_api(g_store);

static SemaphoreHandle_t g_stateMutex = nullptr;
static mc5000::SlotStatus g_slots[4];
static bool g_slotValid[4] = {false, false, false, false};
static std::string last_status[4];   // vorheriger Slot-Status (Completed-Flankenerkennung)

static unsigned long g_lastPoll = 0;
static unsigned long g_lastReconnect = 0;

// Gewählte Ladegerät-MAC (persistent gespeichert); leer = keine Auswahl.
static String g_bleAddress;

// ---------- Persistenz (Preferences/NVS) ----------
static void loadAddress() {
    Preferences prefs;
    prefs.begin("mc5000", true);
    g_bleAddress = prefs.getString("addr", "");
    prefs.end();
}

static void saveAddress(const String& addr) {
    g_bleAddress = addr;
    Preferences prefs;
    prefs.begin("mc5000", false);
    prefs.putString("addr", addr);
    prefs.end();
}

static void clearAddress() {
    g_bleAddress = "";
    Preferences prefs;
    prefs.begin("mc5000", false);
    prefs.remove("addr");
    prefs.end();
}

// WLAN-Zugangsdaten (nur auf dem Gerät gespeichert, nicht im Code).
static String g_wifiSsid;
static String g_wifiPass;

static void loadWifi() {
    Preferences prefs;
    prefs.begin("mc5000", true);
    g_wifiSsid = prefs.getString("ssid", "");
    g_wifiPass = prefs.getString("pass", "");
    prefs.end();
}

static void saveWifi(const String& ssid, const String& pass) {
    g_wifiSsid = ssid;
    g_wifiPass = pass;
    Preferences prefs;
    prefs.begin("mc5000", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();
}

// ---------- BLE-Notification-Callback (läuft im BLE-Task) ----------
static void onMc5000Notify(const std::vector<uint8_t>& data) {
    if (data.size() < 4) return;
    if (data[2] != 0x91) return;   // nur Status-Antworten auswerten

    mc5000::SlotStatus s = g_protocol.parseStatus(data);
    const int idx = s.slot - 1;
    if (idx < 0 || idx >= 4) return;

    if (g_stateMutex && xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_slots[idx] = s;
        g_slotValid[idx] = true;
        xSemaphoreGive(g_stateMutex);
    }
}

// ---------- JSON-Provider ----------
static String buildStatusJson() {
    DynamicJsonDocument doc(2048);
    doc["connected"] = g_ble.isConnected();

    JsonArray arr = doc.createNestedArray("slots");
    const bool locked = g_stateMutex && (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE);
    for (int i = 0; i < 4; ++i) {
        JsonObject o = arr.createNestedObject();
        o["slot"] = i + 1;
        const bool valid = locked && g_slotValid[i];
        o["valid"] = valid;
        if (valid) {
            const mc5000::SlotStatus& s = g_slots[i];
            o["voltageV"] = s.voltageV;
            o["currentA"] = s.currentA;
            o["temperatureC"] = s.temperatureC;
            o["capacityMah"] = s.capacityMah;
            o["elapsedSeconds"] = (uint32_t)s.elapsedSeconds;
            o["internalResistanceMOhm"] = s.internalResistanceMOhm;
            o["status"] = s.status;
            o["mode"] = s.mode;
            o["error"] = s.error;
            o["chemistry"] = s.chemistry;
        }
    }
    if (locked) xSemaphoreGive(g_stateMutex);

    String out;
    serializeJson(doc, out);
    return out;
}

static String buildInfoJson() {
    DynamicJsonDocument doc(384);
    doc["device"] = "mc5000-bridge";
    doc["hostname"] = HOSTNAME;
    doc["bleConnected"] = g_ble.isConnected();
    doc["address"] = g_bleAddress;
    doc["slots"] = 4;
    doc["version"] = FW_VERSION;
    String out;
    serializeJson(doc, out);
    return out;
}

static String buildScanJson() {
    auto devices = g_ble.scanDevices(5000);
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.createNestedArray("devices");
    for (const auto& d : devices) {
        JsonObject o = arr.createNestedObject();
        o["name"] = d.name.c_str();
        o["address"] = d.address.c_str();
    }
    String out;
    serializeJson(doc, out);
    return out;
}

// ---------- Verbindungs-Handler ----------
static bool handleConnect(const String& address) {
    saveAddress(address);
    return g_ble.connect(address.c_str());
}

static bool handleDisconnect() {
    g_ble.disconnect();
    clearAddress();
    return true;
}

// ---------- Kommando-Handler ----------
static bool handleCharge(const JsonObject& j) {
    const int slot = j["slot"] | 0;                 // 1..4
    const int chemistry = j["chemistry"] | 0;
    if (slot < 1 || slot > 4) return false;

    mc5000::ChargeProfile p;
    p.mode = (const char*)(j["mode"] | "charge");
    p.chargeCurrentMa = j["chargeCurrentMa"] | 1000;
    p.dischargeCurrentMa = j["dischargeCurrentMa"] | 1000;
    p.targetVoltageMv = j["targetVoltageMv"] | 4200;
    p.cutoffVoltageMv = j["cutoffVoltageMv"] | 3200;
    p.terminationCurrentMa = j["terminationCurrentMa"] | 100;
    p.cycleDirection = j["cycleDirection"] | 0;
    p.cycleCount = j["cycleCount"] | 1;
    p.restChargeMin = j["restChargeMin"] | 10;
    p.restDischargeMin = j["restDischargeMin"] | 10;
    p.trickleChargeMa = j["trickleChargeMa"] | 0;
    p.deltaPeakMv = j["deltaPeakMv"] | 0;
    p.cutoffTimerMin = j["cutoffTimerMin"] | 0;
    p.maxTimeMin = j["maxTimeMin"] | 0;

    const int mask = (slot == 1) ? 1 : (slot == 2) ? 2 : (slot == 3) ? 4 : 8;

    if (!g_ble.writePacket(g_protocol.buildChargeConfig(p, chemistry, mask))) {
        return false;
    }
    return g_ble.writePacket(g_protocol.buildStartStop(mask));   // Slot starten
}

static bool handleStartStop(int action) {
    return g_ble.writePacket(g_protocol.buildStartStop(action));
}

// ---------- WiFi ----------
static bool wifiIsConnected() {
    return WiFi.status() == WL_CONNECTED;
}

static void startAp() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID);   // offener Setup-Access-Point (kein Passwort im Code)
    Serial.printf("[wifi] Access-Point %s, IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

static bool connectWifi() {
    if (g_wifiSsid.length() == 0) {
        return false;
    }
    Serial.printf("[wifi] verbinde mit %s ...\n", g_wifiSsid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.setHostname(HOSTNAME);
    WiFi.begin(g_wifiSsid.c_str(), g_wifiPass.c_str());

    const unsigned long start = millis();
    while (!wifiIsConnected() && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
        delay(500);
    }

    if (wifiIsConnected()) {
        Serial.printf("[wifi] verbunden, IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    }
    Serial.println("[wifi] Verbindung fehlgeschlagen");
    return false;
}

static String buildWifiJson() {
    DynamicJsonDocument doc(256);
    doc["ssid"] = g_wifiSsid;
    doc["connected"] = wifiIsConnected();
    doc["ip"] = WiFi.localIP().toString();
    String out;
    serializeJson(doc, out);
    return out;
}

static bool handleWifiSave(const String& ssid, const String& pass) {
    if (ssid.length() == 0) {
        return false;
    }
    saveWifi(ssid, pass);
    if (!connectWifi()) {
        startAp();   // Fallback, bis eine Verbindung gelingt
    }
    return true;
}

static void setupWifi() {
    loadWifi();

    if (g_wifiSsid.length() == 0) {
        Serial.println("[wifi] keine Zugangsdaten hinterlegt -> Access-Point für die Ersteinrichtung");
        startAp();
    } else if (!connectWifi()) {
        startAp();
    }

    if (MDNS.begin(HOSTNAME)) {
        Serial.println("[mdns] http://" HOSTNAME ".local");
    }
}

// ---------- Setup / Loop ----------
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n[M5000Bridge] Start");

    // PSRAM-Runtime-Check (N16R8: 8 MB OPI-PSRAM).
    Serial.printf("PSRAM free: %u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    g_stateMutex = xSemaphoreCreateMutex();
    loadAddress();

    setupWifi();

    g_store.begin();
    g_store.syncTime(10000);   // NTP nach WLAN-Verbindung (absolute Zeitstempel)

    g_api.infoProvider = buildInfoJson;
    g_api.statusProvider = buildStatusJson;
    g_api.scanProvider = buildScanJson;
    g_api.connectHandler = handleConnect;
    g_api.disconnectHandler = handleDisconnect;
    g_api.wifiProvider = buildWifiJson;
    g_api.wifiSaveHandler = handleWifiSave;
    g_api.startStopHandler = handleStartStop;
    g_api.chargeHandler = handleCharge;
    g_api.begin();

    g_ble.setNotifyCallback(onMc5000Notify);
    g_ble.begin();

    if (g_bleAddress.length() > 0) {
        Serial.printf("[ble] gespeichertes Ladegerät: %s\n", g_bleAddress.c_str());
        if (g_ble.connect(g_bleAddress.c_str())) {
            Serial.println("[ble] verbunden");
        } else {
            Serial.println("[ble] Verbindung fehlgeschlagen (wird erneut versucht)");
        }
    } else {
        Serial.println("[ble] kein Ladegerät gewählt -> Verbindung über die Webseite herstellen");
    }
}

// Verarbeitet den letzten BLE-Slot-Status (1 Hz): Live-Daten pushen + Completed-Flanke.
static void processStatus() {
    int completed[4];
    int completedCount = 0;
    int cap[4], ir[4];
    std::string action[4];   // echter mode, unter dem Mutex kopiert

    if (g_stateMutex && xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (int i = 0; i < 4; ++i) {
            if (!g_slotValid[i]) continue;
            const mc5000::SlotStatus& s = g_slots[i];

            g_store.pushLivePoint(
                i,
                (uint16_t)(s.voltageV * 1000.0f),   // V -> mV
                (int16_t)(s.currentA * 1000.0f),    // A -> mA
                (uint16_t)s.capacityMah,            // mAh
                (int8_t)(s.temperatureC + 0.5f)     // °C
            );

            // Flankenerkennung: Übergang auf "Completed"
            if (last_status[i] != "Completed" && s.status == "Completed") {
                completed[completedCount] = i;
                cap[completedCount] = s.capacityMah;
                ir[completedCount] = s.internalResistanceMOhm;
                action[completedCount] = s.mode;   // echter mode -> action (charge/discharge/...)
                completedCount++;
            }
            last_status[i] = s.status;
        }
        xSemaphoreGive(g_stateMutex);
    }

    // Abschluss außerhalb des Mutex (finalizeSlot schreibt via Checkpoint in LittleFS).
    for (int k = 0; k < completedCount; ++k) {
        g_store.finalizeSlot(completed[k], cap[k], ir[k], action[k].c_str());
    }
}

void loop() {
    g_api.handle();

    if (g_ble.isConnected()) {
        if (millis() - g_lastPoll >= POLL_INTERVAL_MS) {
            g_lastPoll = millis();
            processStatus();   // vorherige Poll-Ergebnisse (während der Wartezeit angekommen)
            const int masks[4] = {1, 2, 4, 8};
            for (int i = 0; i < 4; ++i) {
                g_ble.writePacket(g_protocol.buildStatusRequest(masks[i]));
                delay(POLL_SLOT_GAP_MS);
            }
        }
    } else if (g_bleAddress.length() > 0) {
        // Gewähltes Ladegerät erneut verbinden (bis explizit getrennt wird).
        if (millis() - g_lastReconnect >= RECONNECT_DELAY_MS) {
            g_lastReconnect = millis();
            Serial.println("[ble] versuche Verbindung ...");
            g_ble.connect(g_bleAddress.c_str());
        }
    }
}
