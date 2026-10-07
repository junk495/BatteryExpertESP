# BatteryExpertESP – Datenarchitektur & Concurrency Engine

Technisches Konzept für die Datenhaltung auf der ESP32-S3-Bridge (N16R8:
16 MB Flash, 8 MB PSRAM). Beschreibt effizientes PSRAM-Speichermanagement, atomare
Synchronisation und minimalen Bandbreitenverbrauch bei REST-Abfragen.

## 1. Architektur & Checkpoint-Prinzip

Das System entkoppelt drei Aufgaben sauber:

- **Datenerfassung (Producer-Task):** BLE-Polling des Ladegeräts, schreibt Messwerte
  atomar in Puffer.
- **Verarbeitung/Aggregierung:** Completed-Erkennung, Zusammenführen von Änderungen,
  Checkpoint.
- **Datenbereitstellung (HTTP-Loop-Task):** Delta-/Live-Endpunkte für die Clients.

**Zustandsmodell:** Der dauerhafte Systemzustand liegt als JSON-Dokument dauerhaft im
PSRAM. Änderungen werden erst nach Validierung als neuer Checkpoint in den Flash
(LittleFS) geschrieben.

**Inkrementelle Checkpoints:** Jeder gültige Zustandswechsel erhöht einen monoton
steigenden `sequence_id`.

**Client-Synchronisation:** Clients laden nicht den gesamten Zustand, sondern übergeben
ihre letzte bekannte `sequence_id`. Das System liefert nur die Differenz (Delta) seit
diesem Stand. Ist die `sequence_id` zu alt oder ungültig, folgt eine Vollsynchronisation.

### Bounded Change-Log

Damit Deltas ab `since_seq` lieferbar sind, hält die ESP ein **begrenztes Change-Log**
(z. B. die letzten 50 Mutationen) im PSRAM. Reicht ein Client-`since_seq` über das
Fenster hinaus, wird ein Full-Sync erzwungen. Drei Kernentscheidungen fixieren die
Implementierung:

**1. Statischer Puffer & Full-Sync-Fallback.** Das Log ist ein fest allokierter
Ringpuffer ohne Laufzeit-Heap – kein dynamischer `String`, keine `std::vector` im
Puffer selbst. Jeder Eintrag ist eine kompakte `struct` (`seq_id`, `entity`, `op`,
`payload[128]`). Die Full-Sync-Bedingung ist exakt: Ist die Zahl der fehlenden Einträge
`current_seq - since_seq - 1` größer als `count`, fällt der Client aus dem Fenster und
erhält einen Full-Sync. Der Grenzfall `elements_to_read == count` wird noch als Delta
bedient (kein Off-by-one).

**2. Zero-Copy-Serialisierung.** Das Log speichert keine geparsten Objekte, sondern
fertige JSON-Schnipsel (max. 127 Byte) im `payload`-Feld. Der HTTP-Handler hängt diese
bei der Antwort unverändert per `serialized(entry.payload)` in ArduinoJson ein. Damit
entfallen Parsing und erneutes Serialisieren je Request – das spart CPU-Zyklen im
REST-Pfad. Der Ausgabe-`std::vector` wird klassenweit einmal `reserve(...)` und vor
jedem Request nur `clear()` aufgerufen, um Fragmentierung im internen RAM zu vermeiden.

**3. Operationstypen.** Jeder Eintrag trägt einen `op`-Typ `UPSERT` oder `DEL`, damit
der Browser-Client Deltas konfliktfrei mergen kann: `UPSERT` legt eine Entität an bzw.
ersetzt sie, `DEL` entfernt sie anhand der ID. `history` ist append-only und benötigt
daher kein `op`.

**Neustart:** Beim Boot generiert die ESP eine neue Basis-`sequence_id` (z. B. aus dem
Boot-Timestamp). Ein Client mit alter oder ungültiger `sequence_id` erhält dadurch
automatisch einen sauberen Full-Sync.

## 2. REST-API

Zwei getrennte Schnittstellen: **Delta** (persistent, ereignisgesteuert) und **Live**
(transient, gepollt). Diese Trennung ist zwingend – 1-Hz-Messwertänderungen dürfen den
Sequenzzähler nicht hochtreiben.

### 2.1 Delta-API (persistenter Zustand)

| Methode | Pfad | Zweck |
|---|---|---|
| GET | `/api/v1/delta?since_seq=N` | Differenz seit `since_seq`; bei zu altem/fehlendem Wert: Full-Sync |
| POST | `/api/v1/cells` | Zelle anlegen |
| PATCH | `/api/v1/cells/{id}` | Zelle ändern |
| DELETE | `/api/v1/cells/{id}` | Zelle löschen |
| POST/PATCH/DELETE | `/api/v1/cell_types/{id}` | Akkutyp analog |
| POST | `/api/v1/history` | Ergebnis anhängen (append-only) |

**Delta-Payload:**

```json
{
  "current_seq": 1048,
  "full": false,
  "deltas": {
    "cells": [
      { "op": "upsert", "cell": { "id": 3, "number": 3, "label": "30Q #3", "cell_type_id": "ct-1" } },
      { "op": "delete", "id": 7 }
    ],
    "types": [
      { "op": "upsert", "type": { "id": 1, "model": "INR18650-30Q", "status": "ACTIVE" } }
    ],
    "history": [
      { "id": "h-2", "cell_id": "c-1", "timestamp": 1711900800, "measured_capacity_mah": 2850, "soh_percent": 95 }
    ]
  }
}
```

- `history` ist append-only (kein `op` nötig).
- `full: true` (statt `deltas`) markiert eine Vollsynchronisation.

### 2.2 Live-API (transienter Zustand)

| Methode | Pfad | Zweck |
|---|---|---|
| GET | `/api/live` | Ausgedünnter Ringpuffer (Messwerte im RAM) für die Diagramme |

Live-Daten (Spannung, Strom, Temperatur) sind **transient** und werden **nicht**
dauerhaft gespeichert. Sie ändern sich bei ~1 Hz und bleiben damit außerhalb des
`sequence_id`-Systems.

## 3. Speicherverwaltung (PSRAM)

Interner SRAM ist knapp; dynamische Allokationen und der Ringpuffer liegen im externen
PSRAM.

- **Custom Allocator:** `BasicJsonDocument<SpiRamAllocator>` für
  Serialisierung/Deserialisierung großer Dokumente.
- **Heap-Management:** SRAM nur für zeitkritische Task-Stacks und
  Synchronisationsobjekte; große Puffer und Verlaufsdaten ausschließlich im PSRAM.

| Speicherbereich | Datentyp / Zweck | Lokation |
|---|---|---|
| Core Task Stacks | Realtime-Tasks, ISR-Handler | Interner SRAM |
| Synchronisation | Mutexes, Semaphores, Task-Notifications | Interner SRAM |
| Live-Data Buffer | Ausgedünnter Ringpuffer | Externer PSRAM |
| JSON Buffers | `SpiRamAllocator` | Externer PSRAM |
| Change-Log | Letzte N Mutationen | Externer PSRAM |

## 4. Ringpuffer für Live-Daten

FIFO-Ringpuffer pro Slot im PSRAM, kompakte Structs (kein JSON).

- **Abtastung:** ~1 Hz (BLE-Polling-Limit des MC5000).
- **Ausdünnung (Downsampling):**
  - Neueste Daten mit voller Granularität (1 s).
  - Ältere Daten schrittweise ausgedünnt: nach 1 Minute -> 10-s-Mittelwerte,
    nach 1 Stunde -> 30-s-/1-min-Mittelwerte.
  - Speicherbedarf bleibt deterministisch, Granularität für Live-Daten maximal.
- **Limit:** z. B. 1000 Punkte pro Slot.
- Erst bei `GET /live` zu JSON serialisieren.

## 5. Thread Safety (Concurrency)

### Entkopplung der Completed-Erkennung

Das Erkennen und Verarbeiten von `Completed` liegt vollständig im **HTTP-Loop-Task**
(nicht im Erfassungs-/BLE-Callback):

1. Erfassungs-Tasks schreiben Daten atomar in die Puffer.
2. Nach dem Schreiben wird nur ein Flag gesetzt / Zähler erhöht.
3. Der Loop-Task prüft im Hauptdurchlauf den Status, erkennt `Completed`,
   konsolidiert das Delta im PSRAM und bedient Clients.

### Mutex- & Lock-Strategie

- **Read/Write-Mutex** auf Ringpuffer und Slot-Cache (cross-task zwischen BLE- und
  Loop-Task).
- **Minimale Haltezeit:** Das Mutex wird nicht über die gesamte JSON-Generierung/
  Sendedauer gehalten. Stattdessen wird unter Mutex ein schneller **Memory-Snapshot**
  im PSRAM erstellt; das Streaming erfolgt ohne Mutex auf dem Snapshot.
- Checkpoint-Logik läuft ausschließlich im Loop-Task -> kein zusätzliches Mutex auf dem
  Dokument nötig.

## 6. Offene Punkte (später festlegen)

- Exakter Inhalt der dauerhaften Strukturen (Felder je Entität).
- Konkrete Checkpoint-Ereignisse (welche Mutationen einen Flash-Write auslösen).
- LittleFS-Partitionsgröße.