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
| Tests | Unity (nativ) |

## Build, Test, Flashen

```bash
pio run -e esp32s3            # bauen
pio run -e esp32s3 -t upload  # flashen
pio test -e test               # native Codec-Unit-Tests (Host-Compiler nötig)
pio device monitor             # serielle Ausgabe
```

## Projektstruktur

```
lib/Mc5000Protocol/   reine Protokollschicht (Codec, nativ testbar)
src/main.cpp          Wiring: WiFi + BLE + HTTP + Polling
src/Mc5000BleClient   BLE-Central (Scan/Connect/Notify/Write)
src/ApiServer         REST-Endpoints
include/config.h      Defaults (ohne Zugangsdaten)
test/test_protocol    native Unit-Tests (Unity)
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

## Bekannte Risiken

- Exaktes `0x94`-Byte-Layout (Platzhalter) und `0x25`-Handshake unverifiziert.
- BLE↔WiFi-Koexistenz (2,4 GHz).
- Notification-Kürzung auf 20 Bytes (Default-MTU).
- Windows: ESP32-Core-Libs mit Pfaden > 260 Zeichen → `LongPathsEnabled=1` + Neustart.
