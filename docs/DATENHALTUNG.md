# BatteryExpertESP – Datenhaltung & App (Konzept-Entwurf)

## 1. Überblick

Die ESP32-S3-Bridge dient nicht nur als BLE<->WiFi-Übersetzer, sondern auch als
**Datenhalter** (Quelle der Wahrheit) für die Akku-Verwaltung. Eine App im Browser ist
der Client: Sie lädt beim Start den dauerhaften Stand, arbeitet live und meldet
Ereignisse zurück.

```
[MC5000] --BLE--> [ESP32-S3: Bridge + Datenhaltung] <--HTTP--> [App im Browser]
                      |-- Flash (LittleFS): Sammelfile (dauerhaft)
                      |-- RAM: Live-Daten (transient)
```

## 2. Rollenverteilung

| Komponente | Aufgabe |
|---|---|
| ESP32-S3 | BLE-Anbindung, Live-Daten halten, dauerhafte Daten speichern (Flash), Sammelfile liefern/annehmen |
| App (Browser) | Oberfläche, Fachlogik (Alterungsbewertung, SOH), Diagramme; Daten holen, Ereignisse melden |

Die ESP **rechnet nicht** (keine Fachlogik) – sie **speichert** nur und übersetzt das
BLE-Protokoll.

## 3. Speicher-Ebenen

1. **Flash (LittleFS) – dauerhaft:** eine JSON-**Sammelfile** mit Akkutypen, Zellen
   (mit Nummer) und der Historie abgeschlossener Ergebnisse. Wird nur bei Checkpoints
   geschrieben (atomar: Temp-Datei -> Umbenennen).
2. **RAM (ESP) – transient:** Live-Messwerte für die Diagramme (aktuelle Ladekurve).
   Nur im Arbeitsspeicher; geht bei Neustart verloren.
3. **App (Browser) – Arbeitskopie:** hält die geladenen Daten während der Sitzung,
   hat aber keinen eigenen dauerhaften Speicher.

## 4. Checkpoint-Prinzip (write-behind)

- Während des Betriebs wird nur im RAM gearbeitet -> keine Flash-Abnutzung.
- Nur bei **bestimmten Ereignissen** wird die Sammelfile in den Flash geschrieben.
- Die konkreten Ereignisse werden **im Laufe der Entwicklung** festgelegt (voraussichtlich:
  Abschluss einer Ladung/Entladung, Anlegen/Ändern einer Zelle oder eines Akkutyps).
- Das Schreiben erfolgt **atomar** (Temp-Datei -> Rename), damit ein Stromausfall die
  Sammelfile nicht korrumpiert.

## 5. Sammelfile – JSON-Schema (Entwurf)

> Platzhalter – Felder werden im Laufe der Entwicklung konkretisiert.

```json
{
  "version": 1,
  "cell_types": [
    { "id": "ct-1", "manufacturer": "...", "model": "...", "chemistry": "Li-Ion",
      "nominal_capacity_mah": 3000, "nominal_voltage_v": 3.7, "typical_ir_mohm": 25 }
  ],
  "cells": [
    { "id": "c-1", "number": 1, "cell_type_id": "ct-1", "label": "30Q #1" }
  ],
  "history": [
    { "id": "h-1", "cell_id": "c-1", "type": "discharge_test",
      "measured_capacity_mah": 2850, "measured_ir_mohm": 26,
      "soh_percent": 95, "result": "ok", "timestamp": "2026-10-07T12:00:00Z" }
  ]
}
```

## 6. REST-API (Entwurf)

| Methode | Pfad | Zweck |
|---|---|---|
| GET | `/data` | Sammelfile (JSON) holen -> App lädt sie beim Start; dient zugleich als Backup/Export |
| PUT/POST | `/data` | Sammelfile an die ESP zurückschreiben (Import/Änderungen) |
| GET | `/live` | Live-Diagrammdaten (Messwerte im RAM) für die Anzeige |
| POST | `/event` | Ereignis melden (z. B. "Ladung abgeschlossen") -> löst ggf. Checkpoint aus |

> Die bestehenden Endpunkte (`/api/status`, `/api/charge`, ...) bleiben für die
> Ladegerät-Steuerung erhalten.

## 7. Backup / Export

Der dauerhafte Stand ist **eine einzige JSON-Datei** – ein Backup ist daher trivial:
`GET /data` liefert die Datei, die als Kopie gespeichert oder wieder importiert werden
kann (`PUT /data`).

## 8. Offene Punkte (später festlegen)

- Konkrete **Checkpoint-Ereignisse** (wann wird geschrieben).
- **Exakter Inhalt** der Sammelfile (welche Felder dauerhaft, welche nur live).
- LittleFS-Partitionsgröße.