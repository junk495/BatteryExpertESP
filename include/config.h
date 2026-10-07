#pragma once
// Zentrale Konfiguration der MC5000-WiFi-Bridge.
//
// WICHTIG: WLAN-Zugangsdaten hier eintragen und diese Datei NICHT committen
// (am besten als config.example.h im Repo ablegen).

// ---- WiFi (Station, Heimnetz) ----
#define WIFI_SSID       "DEIN_WLAN_NAME"
#define WIFI_PASSWORD   "DEIN_WLAN_PASSWORT"

// ---- Fallback-Access-Point (für Erstkonfiguration) ----
// Wird gestartet, wenn sich das Gerät nicht ins Heimnetz einwählen kann.
#define AP_SSID         "MC5000-Setup"
#define AP_PASSWORD     "mc5000setup"   // min. 8 Zeichen

// ---- Netzwerk ----
#define HTTP_PORT       80
#define HOSTNAME        "mc5000-bridge"   // mDNS: http://mc5000-bridge.local

// ---- MC5000 BLE ----
#define MC5000_BLE_NAME_FILTER   "MC5000"   // Teilstring zur Geräteerkennung

// ---- Polling (0x91-Status) ----
#define POLL_INTERVAL_MS        1000   // Zyklus aller 4 Slots
#define POLL_SLOT_GAP_MS        150    // Pause zwischen den Slots
#define RECONNECT_DELAY_MS      5000   // Wartezeit vor neuem Scan/Connect

// ---- WiFi-Verbindungstimeout ----
#define WIFI_CONNECT_TIMEOUT_MS 15000
