#pragma once
// DataStore — Single Source of Truth für persistenten und transienten Zustand.
//
// Kapselt:
//   - BoundedChangeLog          (Delta-Engine, sequenzbasiert)
//   - DownsamplingBuffer[4]     (Live-Messreihen, transient)
//   - BasicJsonDocument<SpiRamAllocator> (persistentes Dokument, PSRAM)
//   - LittleFS-Checkpoint       (write-behind, atomar via Temp-Datei + Rename)
//   - SNTP-Zeit                 (absolute Zeitstempel für test_results)
//
// Concurrency (siehe docs/DATENHALTUNG.md §5): Persistente Mutationen laufen
// ausschließlich im HTTP-Loop-Task (Single-Thread). Der einzige Cross-Task-
// Einstieg ist pushLivePoint(), der vom Polling-Task unter einem kurzen Mutex
// aufgerufen wird. buildDeltaJson()/buildLiveJson() erstellen die Antworten unter
// einem schnellen Snapshot, nicht unter dauerhaft gehaltenem Mutex.
//
// Hinweis: Diese Klasse ist Arduino-abhängig (String, LittleFS, SNTP). Die .cpp
// inkludiert <Arduino.h>, <LittleFS.h> und <time.h> vor diesem Header.

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <string>
#include <vector>

#include "BoundedChangeLog.h"
#include "DownsamplingBuffer.h"
#include "Mc5000Protocol.h"

// PSRAM-Allocator für ArduinoJson-Dokumente. Im nativen Unit-Test (kein PSRAM)
// fällt er auf den Standard-Heap zurück, damit die Serialisierungslogik host-
// testbar bleibt.
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
struct SpiRamAllocator {
    void* allocate(size_t size) { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM); }
    void deallocate(void* ptr) { if (ptr) heap_caps_free(ptr); }
    void* reallocate(void* ptr, size_t new_size) { return heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM); }
};
#else
#include <stdlib.h>
struct SpiRamAllocator {
    void* allocate(size_t size) { return malloc(size); }
    void deallocate(void* ptr) { if (ptr) free(ptr); }
    void* reallocate(void* ptr, size_t new_size) { return realloc(ptr, new_size); }
};
#endif

class DataStore {
public:
    DataStore();
    ~DataStore();

    // ---- Lifecycle ----
    // Mountet LittleFS, lädt (oder erzeugt) das persistente Dokument, allokiert
    // die vier Downsampling-Puffer im PSRAM und setzt den Change-Log auf eine
    // frische Boot-Epoche (alte Clients erhalten dadurch automatisch Full-Sync).
    bool begin();

    // ---- Zeit ----
    // Startet SNTP und blockiert bis zu timeoutMs auf gültige Wall-Clock-Zeit.
    bool syncTime(uint32_t timeoutMs = 15000);
    bool timeReady() const;
    uint32_t nowSeconds() const;    // absolute Epoche (0 wenn nicht bereit) — für test_results
    uint32_t uptimeSeconds() const; // millis()/1000 — für die Ringpuffer-Ausdünnung

    // ---- Live-Daten (transient, nicht persistiert) ----
    void pushLivePoint(int slot, uint16_t voltage_mv, int16_t current_ma,
                       uint16_t capacity_mah, int8_t temp_c);
    bool buildLiveJson(String& out) const;   // GET /api/live

    // ---- Persistente Mutationen (von ApiServer aufgerufen) ----
    // entity/op kommen aus Route+Methode; id ist die Entitäts-ID; bodyJson ist
    // der rohe JSON-Body (bei DEL null/leer). Gibt false bei Validierungsfehler.
    bool applyMutation(EntityType entity, OpType op, const char* id, const char* bodyJson);

    // ---- Delta-/Full-Sync-Antworten ----
    // GET /api/v1/delta?since_seq=N. Gibt false zurück, wenn ein Full-Sync nötig
    // ist (since_seq zu alt); dann buildFullSyncJson() aufrufen.
    bool buildDeltaJson(uint32_t since_seq, String& out);
    void buildFullSyncJson(String& out);

    // ---- Persistenz ----
    bool checkpoint();   // write-behind: Dokument in LittleFS (Temp+Rename, atomar)
    uint32_t currentSeq() const;

    // ---- Session-Kopplung (transient, nie im Flash) ----
    // Ordnet einem physischen Slot (0..3) eine Zellen-ID und den gestarteten
    // Mode zu. Vom ApiServer aufgerufen, wenn die PWA einen Ladevorgang startet.
    bool assignSlot(int slot, const char* cell_id, const char* mode);

    // Füllt die Lade-/Entlade-Konfiguration aus dem Zelltyp der Zelle. Gibt false
    // zurück, wenn Zelle/Typ nicht gefunden wurden (Aufrufer behält seine Defaults).
    bool getChargeDefaults(const char* cell_id, mc5000::ChargeProfile& out,
                           int& capacityCutoffMah) const;

    // Schließt einen Slot ab (Flankenerkennung "Completed" im Loop-Task):
    // hängt den Ergebnis-Datensatz an test_results an (falls eine Zelle zugeordnet
    // ist) und gibt den Slot wieder frei. Die Energie wird aus dem im
    // pushLivePoint() aufsummierten Leistungs-Integral berechnet. `action`
    // stammt bevorzugt aus dem beim Start gesetzten Mode (pending_mode);
    // `action` ist nur ein Fallback.
    void finalizeSlot(int slot, int capacity_mah, int ir_mohm, const char* action);

private:
    static constexpr size_t MAX_SLOTS = 4;
    static constexpr size_t DOC_CAPACITY = 16384;   // Bytes für das PSRAM-Dokument

    BoundedChangeLog _changeLog;                  // ~6,8 KB statisch (SRAM ausreichend)
    DownsamplingBuffer _buffers[MAX_SLOTS];       // PSRAM-Buffer via allocate()
    BasicJsonDocument<SpiRamAllocator>* _doc = nullptr;  // persistentes Dokument (PSRAM)
    fs::LittleFSFS _dataFs;                       // separate Daten-Partition ("storage")
    std::string active_cell_ids[MAX_SLOTS];       // transient: Zelle je Slot (leer = frei)
    std::string pending_mode[MAX_SLOTS];          // transient: gestarteter Mode je Slot (für action)
    int64_t energy_accumulator[MAX_SLOTS] = {0};  // transient: Σ(V_mV × I_mA) je Sekunde (µJ)
    bool _fsReady = false;
    bool _timeReady = false;
    std::vector<ChangeEntry> _deltaBuf;           // wiederverwendeter Ausgabepuffer

    bool loadDocument();
    bool writeDocumentRaw();       // Rohschreiben (checkpoint() macht Temp+Rename)
    void seedDocument();           // leeres Standard-Dokument anlegen
};
