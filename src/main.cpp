#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>

#include "config.h"
#include "Mc5000Protocol.h"
#include "Mc5000BleClient.h"
#include "ApiServer.h"

// ---------- Globale Zustände ----------
static mc5000::Mc5000Protocol g_protocol;
static Mc5000BleClient g_ble;
static ApiServer g_api;

static SemaphoreHandle_t g_stateMutex = nullptr;
static mc5000::SlotStatus g_slots[4];
static bool g_slotValid[4] = {false, false, false, false};

static unsigned long g_lastPoll = 0;
static unsigned long g_lastReconnect = 0;

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
    DynamicJsonDocument doc(256);
    doc["device"] = "mc5000-bridge";
    doc["hostname"] = HOSTNAME;
    doc["bleConnected"] = g_ble.isConnected();
    doc["slots"] = 4;
    String out;
    serializeJson(doc, out);
    return out;
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
static void setupWifi() {
    Serial.println("[wifi] verbinde mit " WIFI_SSID " ...");
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    const unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
        delay(500);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[wifi] verbunden, IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[wifi] kein WLAN gefunden -> starte Access-Point " AP_SSID);
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASSWORD);
        Serial.printf("[wifi] AP-IP: %s\n", WiFi.softAPIP().toString().c_str());
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

    g_stateMutex = xSemaphoreCreateMutex();

    setupWifi();

    g_api.infoProvider = buildInfoJson;
    g_api.statusProvider = buildStatusJson;
    g_api.startStopHandler = handleStartStop;
    g_api.chargeHandler = handleCharge;
    g_api.begin();

    g_ble.setNotifyCallback(onMc5000Notify);
    g_ble.begin();
    if (g_ble.scanAndConnect(5)) {
        Serial.println("[ble] MC5000 verbunden");
    } else {
        Serial.println("[ble] MC5000 nicht gefunden (wird weiter versucht)");
    }
}

void loop() {
    g_api.handle();

    if (g_ble.isConnected()) {
        if (millis() - g_lastPoll >= POLL_INTERVAL_MS) {
            g_lastPoll = millis();
            const int masks[4] = {1, 2, 4, 8};
            for (int i = 0; i < 4; ++i) {
                g_ble.writePacket(g_protocol.buildStatusRequest(masks[i]));
                delay(POLL_SLOT_GAP_MS);
            }
        }
    } else {
        if (millis() - g_lastReconnect >= RECONNECT_DELAY_MS) {
            g_lastReconnect = millis();
            Serial.println("[ble] versuche Verbindung ...");
            g_ble.scanAndConnect(5);
        }
    }
}
