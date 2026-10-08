# BatteryExpertESP

ESP32-Firmware als **BLE→WiFi-Bridge** für das **SkyRC MC5000** Ladegerät.
Macht das Gerät über eine einfache HTTP/JSON-Schnittstelle im WLAN erreichbar.

> Gegenstück zur Android-App **BatteryExpert** (Kotlin). Beide teilen sich das
> reverse-engineered BLE-Protokoll – siehe [`docs/KONZEPT.md`](docs/KONZEPT.md).

## Hardware

- ESP32-S3-DevKitC-1 (N16R8: 16 MB Flash, 8 MB PSRAM).
- **Keine Verdrahtung zum Ladegerät nötig** – Verbindung läuft über BLE.
- Nur die ESP32-Stromversorgung (USB/Netzteil) anschließen.

> **Achtung:** Der MC5000 erlaubt nur **einen** BLE-Client gleichzeitig.
> Wenn die Bridge verbunden ist, kann die Android-App nicht gleichzeitig verbinden.

## Voraussetzungen

- [PlatformIO](https://platformio.org/) (über die VS-Code-Erweiterung oder die eigenständige CLI)
- ESP32-Toolchain `espressif32` (getestet mit `6.9.0` / Arduino-Core 3.3.11)

## Einrichtung

1. Bauen und flashen (aus dem Projektordner):

   ```powershell
   pio run -e esp32s3
   pio run -e esp32s3 -t upload
   pio run -e esp32s3 -t uploadfs   # PWA (data/) ins LittleFS flashen
   ```

2. Beim ersten Start sind keine WLAN-Zugangsdaten hinterlegt, daher startet
   die Bridge einen offenen Access-Point (`SSID: MC5000-Setup`).

3. Mit diesem AP verbinden und im Browser `http://192.168.4.1` öffnen.

4. Auf der Seite die eigenen WLAN-Zugangsdaten (SSID + Passwort) eintragen und
   speichern. Die Bridge verbindet sich dann mit dem Heimnetz; die Zugangsdaten
   liegen nur auf dem Gerät (NVS), nicht im Code.

5. Erreichbar ist die Bridge danach unter `http://mc5000-bridge.local` (mDNS)
   oder unter der im seriellen Monitor ausgegebenen IP:

   ```powershell
   pio device monitor
   ```

## Verbindung zum Ladegerät

1. Im Browser `http://mc5000-bridge.local` öffnen und den Reiter „Einstellungen" wählen.
2. Unter „Ladegerät (BLE)" auf „Suchen" klicken und das Gerät in der Liste wählen.
3. „Verbinden" klicken.

Die gewählte MAC-Adresse wird **persistent gespeichert**: Nach einem Neustart
oder Verbindungsverlust verbindet sich die Bridge automatisch wieder mit
demselben Gerät. Erst „Trennen" hebt die Auswahl auf.

## Web-Oberfläche (PWA)

Die Weboberfläche unter `/` ist eine Vanilla-JS-PWA im Verzeichnis `data/`. Sie wird
separat als LittleFS-Image geflasht (`pio run -e esp32s3 -t uploadfs`). Nicht-`/api/*`-
Pfade liefert der Server als statische Dateien aus (MIME nach Endung, `sw.js` als
`application/javascript`). Ohne `uploadfs` fällt `/` auf die eingebettete Setup-Seite
zurück (Verbindung + WLAN-Setup).

Ansichten: **Dashboard** (Live-Slots), **Zell-Register** (Zellen/Zelltypen),
**Historie** (Lade-/Entlade-Verläufe) und **Einstellungen** (BLE-Kopplung + WLAN).

## REST-API

| Methode | Pfad              | Body                            | Zweck                               |
|---------|-------------------|---------------------------------|-------------------------------------|
| GET     | `/`               | –                               | PWA (aus LittleFS)  |
| GET     | `/api/info`       | –                               | Geräte-/Verbindungsinfo             |
| GET     | `/api/status`     | –                               | Status aller 4 Slots                |
| GET     | `/api/scan`       | –                               | BLE-Geräte suchen (Liste)           |
| POST    | `/api/connect`    | `{"address":"…"}`               | Gerät wählen + verbinden            |
| POST    | `/api/disconnect` | –                               | Trennen + Auswahl löschen           |
| GET     | `/api/wifi`       | –                               | WLAN-Status (SSID, verbunden, IP)   |
| POST    | `/api/wifi`       | `{"ssid":"…","password":"…"}`   | WLAN-Zugangsdaten speichern         |
| POST    | `/api/charge`     | JSON (siehe unten)              | Konfig setzen + Slot starten        |
| POST    | `/api/startstop`  | `{"action": 0..8}`              | Start/Stopp (`0x93`)                |

### Beispiele

Status abrufen:

```powershell
curl http://mc5000-bridge.local/api/status
```

Slot 1 als Li-Ion mit 2000 mA laden:

```powershell
curl -X POST http://mc5000-bridge.local/api/charge ^
  -H "Content-Type: application/json" ^
  -d "{\"slot\":1,\"chemistry\":0,\"mode\":\"charge\",\"chargeCurrentMa\":2000,\"dischargeCurrentMa\":1000,\"targetVoltageMv\":4200,\"cutoffVoltageMv\":3200}"
```

Alles stoppen (`0x93`-Aktion 0):

```powershell
curl -X POST http://mc5000-bridge.local/api/startstop -H "Content-Type: application/json" -d "{\"action\":0}"
```

### `0x93`-Aktionen

`0=Stop alle, 1=Slot 1, 2=Slot 2, 3=Start alle, 4=Slot 3, 8=Slot 4`

## Tests

Die hardwareunabhängigen Bibliotheken sind nativ testbar (analog
`ProtocolCodecTest.kt` der App) — Codec, Change-Log (Delta-Sync) und Downsampling:

```powershell
pio test -e test
```

> Benötigt einen Host-C++-Compiler (z.B. MinGW oder MSVC). Ohne Host-Compiler
> lassen sich die Tests nicht lokal ausführen – die Firmware kann trotzdem mit
> `pio run -e esp32s3` gebaut werden.

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
