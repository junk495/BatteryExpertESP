# BatteryExpertESP

ESP32-Firmware als **BLE→WiFi-Bridge** für das **SkyRC MC5000** Ladegerät.
Macht das Gerät über eine einfache HTTP/JSON-Schnittstelle im WLAN erreichbar.

> Gegenstück zur Android-App **BatteryExpert** (Kotlin). Beide teilen sich das
> reverse-engineered BLE-Protokoll – siehe [`docs/KONZEPT.md`](docs/KONZEPT.md).

## Hardware

- Beliebiges ESP32-Board (entwickelt für `esp32dev` / DevKit-WROOM-32).
- **Keine Verdrahtung zum Ladegerät nötig** – Verbindung läuft über BLE.
- Nur die ESP32-Stromversorgung (USB/Netzteil) anschließen.

> **Achtung:** Der MC5000 erlaubt nur **einen** BLE-Client gleichzeitig.
> Wenn die Bridge verbunden ist, kann die Android-App nicht gleichzeitig verbinden.

## Voraussetzungen

- [PlatformIO](https://platformio.org/) (bei dir über die VS-Code-Erweiterung installiert)
- ESP32-Toolchain `espressif32` (bei dir: `6.9.0` / Arduino-Core 3.3.11)

## Einrichtung

1. `include/config.h` bearbeiten und die WLAN-Zugangsdaten eintragen:

   ```cpp
   #define WIFI_SSID       "DEIN_WLAN_NAME"
   #define WIFI_PASSWORD   "DEIN_WLAN_PASSWORT"
   ```

2. Bauen und flashen (aus dem Projektordner):

   ```powershell
   pio run -e esp32dev
   pio run -e esp32dev -t upload
   ```

3. Serielle Ausgabe ansehen:

   ```powershell
   pio device monitor
   ```

Nach dem Start verbindet sich das Gerät ins Heimnetz. Schlägt das fehl, startet
ein Fallback-Access-Point (`SSID: MC5000-Setup`, Passwort: `mc5000setup`).
Erreichbar ist die Bridge dann unter `http://mc5000-bridge.local` (mDNS) oder
unter der ausgegebenen IP.

## REST-API

| Methode | Pfad             | Body                | Zweck                       |
|---------|------------------|---------------------|-----------------------------|
| GET     | `/api/info`      | –                   | Geräte-/Verbindungsinfo     |
| GET     | `/api/status`    | –                   | Status aller 4 Slots        |
| POST    | `/api/charge`    | JSON (siehe unten)  | Konfig setzen + Slot starten|
| POST    | `/api/startstop` | `{"action": 0..8}`  | Start/Stopp (`0x93`)        |

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

Die reine Protokollschicht (`lib/Mc5000Protocol`) ist nativ testbar (analog
`ProtocolCodecTest.kt` der App):

```powershell
pio test -e test
```

> Benötigt einen Host-C++-Compiler (z.B. MinGW oder MSVC). Ohne Host-Compiler
> lassen sich die Tests nicht lokal ausführen – die Firmware kann trotzdem mit
> `pio run -e esp32dev` gebaut werden.

## Projektstruktur

```
lib/Mc5000Protocol/   reine Protokollschicht (Codec, nativ testbar)
src/main.cpp          Wiring: WiFi + BLE + HTTP + Polling
src/Mc5000BleClient   BLE-Central (Scan/Connect/Notify/Write)
src/ApiServer         REST-Endpoints
include/config.h      WiFi-Zugangsdaten + Defaults
test/test_protocol    native Unit-Tests (Unity)
docs/KONZEPT.md       Konzept & Protokoll-Referenz
```
