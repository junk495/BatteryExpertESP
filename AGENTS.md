# AGENTS.md

Leitfaden für KI-Agenten und Mitwirkende an diesem Repository.

## Überblick

**BatteryExpertESP** ist eine ESP32-Firmware, die als **BLE→WiFi-Bridge** für das
**SkyRC MC5000** (4-Slot-Ladegerät) dient. Das Ladegerät wird über BLE angebunden
und über eine HTTP/JSON-Schnittstelle im WLAN erreichbar gemacht.

Gegenstück ist die Android-App **BatteryExpert** (Kotlin); beide teilen sich das
reverse-engineered BLE-Protokoll. Details: [`docs/KONZEPT.md`](docs/KONZEPT.md).

## Tech-Stack

| Baustein | Wahl |
|---|---|
| Build | PlatformIO + Arduino-Framework |
| Board | `esp32-s3-devkitc-1` (N16R8: 16 MB Flash, 8 MB PSRAM) |
| Core | `espressif32` ≥ 6.x → Arduino-Core 3.x (getestet 6.9.0 / 3.3.11) |
| BLE | NimBLE-Arduino ^2.x |
| HTTP | `WebServer.h` (im Arduino-Core) |
| JSON | ArduinoJson ^6.x |
| Filesystem | LittleFS (im Arduino-Core) |
| Tests | Unity (nativ) |

## Build, Test, Flashen

```bash
pio run -e esp32s3            # bauen
pio run -e esp32s3 -t upload  # flashen
pio run -e esp32s3 -t uploadfs # PWA (data/) ins LittleFS flashen
pio test -e test               # native Codec-Unit-Tests (Host-Compiler nötig)
pio device monitor             # serielle Ausgabe
```

## Projektstruktur

```
data/                 PWA-Webclient (wird ins LittleFS geflasht)
lib/                  reine, hardwareunabhängige Bibliotheken (nativ testbar)
src/main.cpp          Wiring: WiFi + BLE + HTTP + Polling
src/Mc5000BleClient   BLE-Central (Scan/Connect/Notify/Write)
src/DataStore         persistente Datenhaltung (LittleFS-Checkpoint, Delta-Sync)
src/ApiServer         REST-Endpoints + PWA-Serving
include/config.h      Defaults (ohne Zugangsdaten)
test/                 native Unit-Tests (Unity, 3 Suiten)
docs/KONZEPT.md       Konzept & Protokoll-Referenz
```

## Konventionen

- Dokumentation: Deutsch. Code (Bezeichner, Kommentare): Englisch.
- `lib/` bleibt frei von Arduino-Abhängigkeiten (nativ testbar).
- Arduino-spezifischer Code liegt in `src/`.
- WLAN-Zugangsdaten werden nur auf dem Gerät (NVS) gespeichert, nicht im Code.

## Protokoll

Die Protokollschicht (`lib/Mc5000Protocol`) ist ein 1:1-Port der Android-App
(`ProtocolCodec.kt`). Kommandos: `0x91` Status, `0x94` Konfig, `0x93` Start/Stop.
Byte-Layout nur nach Verifikation gegen die Referenz (`rssdev10/skyrc-mc-rs`)
oder das echte Gerät ändern. Offene `0x94`-Platzhalter sind mit `TODO` markiert.

Mode-Byte (`0x94` Byte 4 / `0x91` Status): `0=Charge, 1=Storage, 2=Discharge,
3=Cycle, 4=Refresh, 5=Break-in` (Li-Ion-Mapping, verifiziert gegen die Referenz).
NiMH/NiCd/Eneloop/NiZn: verschobenes Layout `0=Charge, 1=Refresh, 2=Break-in,
3=Discharge, 4=Cycle` (`Refresh`-Wert noch zu verifizieren).

## Bekannte Risiken

- Exaktes `0x94`-Byte-Layout (Platzhalter) und `0x25`-Handshake unverifiziert.
- `0x91`-Status-Mode-Byte unverifiziert: die Referenz meldet im Status kein eigenes
  Mode-Byte; die `action`-Historie nutzt deshalb den beim Start gesetzten Modus.
- NiMH-`Refresh`-Byte-Wert widersprüchlich (Referenz-Doc `0x05` vs. Code `0x01`).
- BLE↔WiFi-Koexistenz (2,4 GHz).
- Notification-Kürzung auf 20 Bytes (Default-MTU).
- Windows: ESP32-Core-Libs mit Pfaden > 260 Zeichen → `LongPathsEnabled=1` + Neustart.
