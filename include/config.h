#pragma once
// Zentrale Konfiguration der MC5000-WiFi-Bridge.
//
// Hinweis: WLAN-Zugangsdaten werden NICHT im Code abgelegt. Sie werden über
// die Weboberfläche eingegeben und nur auf dem Gerät (NVS) gespeichert.

// ---- Version ----
// Wird unter /api/info als "version" ausgeliefert. Bei jedem Release erhöhen —
// die PWA zeigt dann den Update-Hinweis an.
#define FW_VERSION      "0.1.0"

// ---- Fallback-Access-Point (für die Ersteinrichtung) ----
// Wird gestartet, wenn keine WLAN-Zugangsdaten hinterlegt sind oder die
// Verbindung fehlschlägt. Über diesen offenen AP wird die Weboberfläche
// erreicht (Standard-IP: http://192.168.4.1).
#define AP_SSID         "MC5000-Setup"

// ---- Netzwerk ----
#define HTTP_PORT       80
#define HOSTNAME        "mc5000-bridge"   // mDNS: http://mc5000-bridge.local

// ---- Polling (0x91-Status) ----
#define POLL_INTERVAL_MS        1000   // Zyklus aller 4 Slots
#define POLL_SLOT_GAP_MS        150    // Pause zwischen den Slots
#define RECONNECT_DELAY_MS      5000   // Wartezeit vor neuem Scan/Connect

// ---- WiFi-Verbindungstimeout ----
#define WIFI_CONNECT_TIMEOUT_MS 15000

// ---- Debug ----
// 1 = BLE-GATT-Werte (TX/RX) hexadezimal auf den seriellen Monitor ausgeben.
// Sicht der NimBLE-Anwendungsschicht (nach L2CAP/ATT) — nicht die rohe Funk-Ebene.
// Für die Protokoll-Verifikation am echten Gerät (z. B. NiMH-Mode-Byte).
#define MC5000_DEBUG_RAW   0
