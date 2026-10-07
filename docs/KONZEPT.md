# BatteryExpertESP – Konzept

ESP32-Firmware als **BLE→WiFi-Bridge** für den **SkyRC MC5000** (4-Slot-Ladegerät).
Gegenstück zur Android-App `BatteryExpert` (Kotlin), die den MC5000 direkt über BLE
ansteuert. Diese Bridge macht das Ladegerät über **HTTP/JSON im WLAN** erreichbar.

```
+------------------+   BLE (GATT Central)   +-----------------------+   WiFi (REST/JSON)  +---------------------+
|  SkyRC MC5000    | <--------------------> |  ESP32                 | <-----------------> | Browser / App /     |
|  (4-Slot-Lader)  |  Char 0xFFE1 (notify)  |  BLE-Central + WiFi    |  HTTP               | Home Assistant      |
+------------------+                        +-----------------------+                     +---------------------+
```

> **Wichtig:** Der MC5000 akzeptiert nur **einen** BLE-Client gleichzeitig.
> Läuft die Bridge, kann die Android-App **nicht** parallel verbunden sein.

---

## 1. Ziel

1. MC5000 über WLAN erreichbar machen (Monitoring + Steuerung).
2. Live-Daten aller 4 Slots (Spannung, Strom, Temperatur, Kapazität, Innenwiderstand) liefern.
3. Lade-/Entlade-Konfigurationen setzen und Slots starten/stoppen.
4. Als Basis für spätere Anbindungen dienen (Web-UI, Home Assistant, Skripte).

---

## 2. Tech-Stack

| Baustein  | Wahl                                 | Begründung                                              |
|-----------|--------------------------------------|---------------------------------------------------------|
| Build     | PlatformIO + Arduino-Framework       | passt zu VS Code, große Library-Auswahl                 |
| Board     | `esp32dev` (ESP32 DevKit/WROOM-32)   | gängigstes ESP32-Modul, Dual-Core                       |
| BLE       | NimBLE-Arduino (^2.x)                | leichtgewichtiger BLE-Central, kompatibel mit Core 3.x  |
| HTTP      | `WebServer.h` (im Arduino-Core)      | synchron, einfach, im Core enthalten                    |
| JSON      | ArduinoJson (^6.x)                   | Standard                                                |
| Tests     | Unity (native)                       | reine Protokollschicht ohne Hardware testbar            |

> **Hinweis Core-Version:** installiert ist `espressif32@6.9.0` → **Arduino-Core 3.3.11**
> (IDF-5.x-Basis). Deshalb NimBLE-Arduino **2.x** und die im Core enthaltene
> `WebServer`-Bibliothek (kein ESPAsyncWebServer, das mit Core 3.x Probleme macht).

---

## 3. Struktur

```
BatteryExpertESP/
├── platformio.ini
├── include/config.h                  # WiFi-Zugangsdaten + Defaults (Template)
├── lib/Mc5000Protocol/               # reine Protokollschicht (nativ testbar)
│   ├── Mc5000Protocol.h
│   └── Mc5000Protocol.cpp
├── src/
│   ├── main.cpp                      # Wiring: WiFi + BLE + HTTP + Polling
│   ├── Mc5000BleClient.h/.cpp        # BLE-Central (Scan/Connect/Notify/Write)
│   └── ApiServer.h/.cpp              # REST-Endpoints
├── test/test_protocol/test_main.cpp  # native Unit-Tests (Unity)
└── docs/KONZEPT.md
```

---

## 4. BLE-Protokoll (reverse-engineered)

1:1-Port der App (`ProtocolCodec.kt`), Referenz `rssdev10/skyrc-mc-rs` (`docs/PROTOCOL.md`).

- **Service UUID:** `0000ffe0-0000-1000-8000-00805f9b34fb`
- **Characteristic UUID:** `0000ffe1-0000-1000-8000-00805f9b34fb`
  (READ | WRITE_WITHOUT_RESPONSE | NOTIFY), MTU 247, CCCD `0x2902`
- **Paket:** `0x0F | Länge | Kommando | Daten… | Prüfsumme`
  (Prüfsumme = Summe von Kommando bis Ende Daten, mod 256)

| Kommando | Bedeutung                        |
|----------|----------------------------------|
| `0x91`   | Slot-Status lesen                |
| `0x94`   | Lade-/Entlade-Konfiguration      |
| `0x93`   | Start / Stopp                    |
| `0x25`   | evtl. Handshake (noch offen)     |
| `0xEA`   | Telemetrie-Blob (Zweck unbekannt)|
| `0x02`   | Keep-alive / Ack                 |

### `0x91`-Antwort-Offsets (big-endian, `packet[2]=0x91`)

| Bytes     | Bedeutung        | Einheit   |
|-----------|------------------|-----------|
| `[4:6]`   | Strom            | /1000 → A |
| `[6:8]`   | Spannung         | /1000 → V |
| `[8:10]`  | Temperatur       | /1000 → °C |
| `[10:12]` | Kapazität        | mAh       |
| `[12:16]` | Zeit             | Sekunden  |
| `[16:18]` | Innenwiderstand  | mΩ        |
| `[18]`    | Status           | –         |
| `[19]`    | Modus            | –         |
| `[20]`    | Fehler           | –         |
| `[21]`    | Chemie           | –         |

- **Chemie:** `0=Li-Ion, 1=Li-Ion HV, 2=LiFePO4, 3=NiMH, 4=NiCd, 5=Eneloop, 6=NiZn, 7=RAM, 8=LTO, 9=Na-Ion`
- **Status:** `0=Standby, 1=Processing, 2=Charging, 3=Discharging, 4=Resting, 5/6=Completed`

### `0x93`-Aktionen

`0=Stop alle, 1=Slot 1, 2=Slot 2, 3=Start alle, 4=Slot 3, 8=Slot 4`

## 5. REST-API (Phase 1)

| Methode | Pfad             | Body                 | Zweck                       |
|---------|------------------|----------------------|-----------------------------|
| GET     | `/api/info`      | –                    | Geräte-/Verbindungsinfo     |
| GET     | `/api/status`    | –                    | Status aller 4 Slots (JSON) |
| POST    | `/api/charge`    | siehe unten          | Konfig setzen + Slot starten|
| POST    | `/api/startstop` | `{"action": 0..8}`   | `0x93` (Start/Stopp)        |

`POST /api/charge` (Felder optional, Defaults in Klammern):

```json
{
  "slot": 1,
  "chemistry": 0,
  "mode": "charge",
  "chargeCurrentMa": 2000,
  "dischargeCurrentMa": 1000,
  "targetVoltageMv": 4200,
  "cutoffVoltageMv": 3200,
  "terminationCurrentMa": 100,
  "cycleDirection": 0,
  "cycleCount": 1,
  "restChargeMin": 10,
  "restDischargeMin": 10,
  "trickleChargeMa": 0,
  "deltaPeakMv": 0,
  "cutoffTimerMin": 0,
  "maxTimeMin": 0
}
```

---

## 6. Risiken / offene Punkte

1. **Exaktes `0x94`-Byte-Layout** hat noch Platzhalter (`putU16(17,100)`, `packet[29]=0x3C`,
   `putU16(27,0)`, `packet[34]=0x00`) → gegen Referenz/echtes Gerät verifizieren.
2. **`0x25`-Handshake** evtl. nötig, um Schreib-Kommandos zu „entriegeln".
3. **BLE↔WiFi-Koexistenz** (2,4 GHz teilen sich das Funkmodul) → Polling nicht zu aggressiv.
4. **Notification-Kürzung** auf 20 Bytes (Default-MTU) → Parser robust gehalten.

→ **Phase 1 = Protokoll-Codec + Unit-Tests** (Fundament), danach gegen das echte
Gerät validieren (wie bei der App: „Phase 1 = Protokoll-Spike mit Debug-Screen").

---

## 7. Phasen

0. Projekt-Setup + Protokoll-Codec + Unit-Tests (dieses Repo-Stadium)
1. BLE nur-Lesen (Monitoring) gegen echtes Gerät validieren
2. BLE Konfig schreiben (`0x94` + `0x93`) validieren
3. REST-API verfeinern (WebSocket / Web-UI, optional)
4. Home-Assistant-/MQTT-Anbindung (optional)

---

## 8. Referenzen

- `https://github.com/rssdev10/skyrc-mc-rs` – Rust, komplette MC5000-BLE-Implementierung
- `https://github.com/kolinger/skyrc-mc3000` – Python, MC5000-BLE-Monitor
- `https://www.skyrc.com/MC5000` – offizielle Produktseite
- Android-App `BatteryExpert` (`docs/KONZEPT.md`, `data/ble/ProtocolCodec.kt`)
