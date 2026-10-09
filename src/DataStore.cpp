#include <Arduino.h>
#include <LittleFS.h>
#include <time.h>
#include <cstring>

#include "DataStore.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// LittleFS paths. Checkpoints are written to a temp file and atomically renamed.
static const char* DOC_PATH = "/mc5000.json";
static const char* DOC_TMP_PATH = "/mc5000.tmp";

// NTP server. Time is kept strictly UTC (offset 0); the PWA formats locally.
static const char* NTP_SERVER = "pool.ntp.org";

// Fallback boot epoch (larger than any realistic short-session sequence) used
// until syncTime() establishes the wall clock.
static const uint32_t BOOT_EPOCH_BASE = 1000000000UL;

// Document keys.
static const char* KEY_CELL_TYPES = "cell_types";
static const char* KEY_CELLS = "cells";
static const char* KEY_TEST_RESULTS = "test_results";

// ---------------------------------------------------------------------------
// File-local helpers
// ---------------------------------------------------------------------------

// Find the index of the object with "id" == id in arr, or -1.
static int findById(JsonArrayConst arr, const char* id) {
    for (size_t i = 0; i < arr.size(); ++i) {
        const char* cur = arr[i]["id"] | "";
        if (strcmp(cur, id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

DataStore::DataStore() = default;

DataStore::~DataStore() {
    delete _doc;
    _doc = nullptr;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool DataStore::begin() {
    // PWA assets live in the default "spiffs" partition (flashed via uploadfs);
    // the persistent checkpoint lives in a dedicated "storage" partition so that
    // re-flashing the filesystem does not erase cell/test-result data.
    LittleFS.begin(true);                                   // "spiffs" -> PWA
    if (!_dataFs.begin(true, "/storage", 10, "storage")) {  // "storage" -> data
        return false;
    }
    _fsReady = true;

    _doc = new BasicJsonDocument<SpiRamAllocator>(DOC_CAPACITY);
    if (!_doc) {
        return false;
    }

    if (!loadDocument()) {
        seedDocument();
    }

    for (int i = 0; i < MAX_SLOTS; ++i) {
        _buffers[i].allocate();
    }

    _deltaBuf.reserve(BoundedChangeLog::maxEntries());

    // Seed with a fallback epoch; syncTime() re-seeds with the wall clock.
    _changeLog.reset(BOOT_EPOCH_BASE + uptimeSeconds());

    return true;
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

bool DataStore::syncTime(uint32_t timeoutMs) {
    configTime(0, 0, NTP_SERVER);

    const uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        const time_t now = time(nullptr);
        if (now >= 1000000000L) {              // valid epoch (>= 2001-09-09)
            _timeReady = true;
            _changeLog.reset((uint32_t)now);   // monotonic wall clock -> stale pre-boot seq
            return true;
        }
        delay(200);
    }
    return false;
}

bool DataStore::timeReady() const {
    return _timeReady;
}

uint32_t DataStore::nowSeconds() const {
    const time_t now = time(nullptr);
    return (now > 0) ? (uint32_t)now : 0;
}

uint32_t DataStore::uptimeSeconds() const {
    return (uint32_t)(millis() / 1000);
}

// ---------------------------------------------------------------------------
// Live data (transient)
// ---------------------------------------------------------------------------

void DataStore::pushLivePoint(int slot, uint16_t voltage_mv, int16_t current_ma,
                              uint16_t capacity_mah, int8_t temp_c) {
    if (slot < 0 || slot >= MAX_SLOTS) {
        return;
    }
    _buffers[slot].push(uptimeSeconds(), voltage_mv, current_ma, capacity_mah, temp_c);

    // Energie-Integral: Leistung (V*I) im Sekundentakt aufsummieren.
    // V_mV * I_mA = µW; aufsummiert über Sekunden ergibt das µJ. Umrechnung
    // auf mWh erfolgt in finalizeSlot() (÷ 3.600.000). Negative Ströme
    // (Entladen) verringern den Akkumulator korrekt.
    energy_accumulator[slot] += (int64_t)voltage_mv * current_ma;
}

bool DataStore::buildLiveJson(String& out) const {
    // The full ring buffer (2000 pts/slot) would exceed any fixed-size document,
    // so only the most recent window is sent. The PWA chart is ~320 px wide, so
    // 512 samples per slot are more than enough resolution. The document lives in
    // PSRAM because 4 x 512 points no longer fit the 16 KiB heap budget.
    static constexpr size_t MAX_POINTS_PER_SLOT = 512;
    static constexpr size_t LIVE_CAPACITY = 262144;   // 256 KiB (PSRAM)

    BasicJsonDocument<SpiRamAllocator> doc(LIVE_CAPACITY);
    JsonArray slots = doc.createNestedArray("slots");
    for (int s = 0; s < MAX_SLOTS; ++s) {
        JsonObject slot = slots.createNestedObject();
        slot["slot"] = s + 1;
        JsonArray points = slot.createNestedArray("points");
        const LivePoint* data = _buffers[s].getData();
        const size_t n = _buffers[s].getCount();
        const size_t start = (n > MAX_POINTS_PER_SLOT) ? (n - MAX_POINTS_PER_SLOT) : 0;
        for (size_t i = start; i < n; ++i) {
            JsonArray p = points.createNestedArray();
            p.add(data[i].timestamp_s);
            p.add(data[i].voltage_mv);
            p.add(data[i].current_ma);
            p.add(data[i].capacity_mah);
            p.add(data[i].temp_c);
        }
    }
    out = "";
    serializeJson(doc, out);
    return true;
}

// ---------------------------------------------------------------------------
// Persistent mutations
// ---------------------------------------------------------------------------

bool DataStore::applyMutation(EntityType entity, OpType op, const char* id,
                              const char* bodyJson) {
    if (!_doc) {
        return false;
    }

    // ---- Delete (cells / types) ----
    if (op == OpType::DEL) {
        if (!id || id[0] == '\0') {
            return false;
        }
        const char* key = (entity == EntityType::CELL) ? KEY_CELLS : KEY_CELL_TYPES;
        JsonArray arr = (*_doc)[key].as<JsonArray>();
        const int idx = findById(arr, id);
        if (idx < 0) {
            return false;   // nothing to delete
        }
        arr.remove(idx);

        char snippet[200];
        StaticJsonDocument<64> snip;
        snip["op"] = "delete";
        snip["id"] = id;
        serializeJson(snip, snippet, sizeof(snippet));

        _changeLog.push(entity, OpType::DEL, snippet);
        return checkpoint();
    }

    // ---- Upsert (cells / types) ----
    if (entity == EntityType::CELL || entity == EntityType::TYPE) {
        StaticJsonDocument<768> tmp;
        const DeserializationError err = deserializeJson(tmp, bodyJson ? bodyJson : "{}");
        if (err || !tmp.is<JsonObject>()) {
            return false;
        }
        JsonObject obj = tmp.as<JsonObject>();
        const char* eid = obj["id"] | id;
        if (!eid || eid[0] == '\0') {
            return false;
        }
        obj["id"] = eid;

        const char* key = (entity == EntityType::CELL) ? KEY_CELLS : KEY_CELL_TYPES;
        const char* wrapper = (entity == EntityType::CELL) ? "cell" : "type";
        JsonArray arr = (*_doc)[key].as<JsonArray>();
        const int idx = findById(arr, eid);
        if (idx >= 0) {
            arr[idx] = obj;   // replace
        } else {
            arr.add(obj);     // append
        }

        char snippet[320];
        StaticJsonDocument<384> snip;
        snip["op"] = "upsert";
        snip[wrapper] = obj;
        serializeJson(snip, snippet, sizeof(snippet));

        _changeLog.push(entity, OpType::UPSERT, snippet);
        return checkpoint();
    }

    // ---- Test result (append-only) ----
    if (entity == EntityType::TEST_RESULT) {
        StaticJsonDocument<768> tmp;
        const DeserializationError err = deserializeJson(tmp, bodyJson ? bodyJson : "{}");
        if (err || !tmp.is<JsonObject>()) {
            return false;
        }
        JsonObject obj = tmp.as<JsonObject>();
        const char* eid = obj["id"] | id;
        if (!eid || eid[0] == '\0') {
            return false;
        }
        obj["id"] = eid;
        if (obj["timestamp_s"].isNull()) {
            obj["timestamp_s"] = nowSeconds();   // inject absolute epoch if missing
        }

        (*_doc)[KEY_TEST_RESULTS].as<JsonArray>().add(obj);   // append

        char snippet[320];
        serializeJson(obj, snippet, sizeof(snippet));

        _changeLog.push(EntityType::TEST_RESULT, OpType::UPSERT, snippet);
        return checkpoint();
    }

    return false;
}

// ---------------------------------------------------------------------------
// Delta / full sync responses
// ---------------------------------------------------------------------------

bool DataStore::buildDeltaJson(uint32_t since_seq, String& out) {
    if (since_seq == 0) {
        return false;   // new client -> full sync
    }
    if (!_changeLog.getDeltasSince(since_seq, _deltaBuf)) {
        return false;   // since_seq too old -> full sync
    }

    DynamicJsonDocument resp(4096);
    resp["current_seq"] = currentSeq();
    resp["full"] = false;
    JsonObject deltas = resp.createNestedObject("deltas");
    JsonArray cells = deltas.createNestedArray("cells");
    JsonArray types = deltas.createNestedArray("types");
    JsonArray results = deltas.createNestedArray("test_results");

    // serialized() links to _deltaBuf's payloads; they stay valid until the next
    // getDeltasSince() call, which happens after this method returns.
    for (const ChangeEntry& e : _deltaBuf) {
        switch (e.entity) {
            case EntityType::CELL:    cells.add(serialized(e.payload)); break;
            case EntityType::TYPE:    types.add(serialized(e.payload)); break;
            case EntityType::TEST_RESULT: results.add(serialized(e.payload)); break;
        }
    }

    out = "";
    serializeJson(resp, out);
    return true;
}

void DataStore::buildFullSyncJson(String& out) {
    // Deep-copies the document into the response (rare path; memory-heavy).
    // TODO: for large documents, serialize the PSRAM document directly.
    DynamicJsonDocument resp(DOC_CAPACITY);
    resp["current_seq"] = currentSeq();
    resp["full"] = true;
    resp[KEY_CELL_TYPES] = (*_doc)[KEY_CELL_TYPES];
    resp[KEY_CELLS] = (*_doc)[KEY_CELLS];
    resp[KEY_TEST_RESULTS] = (*_doc)[KEY_TEST_RESULTS];
    out = "";
    serializeJson(resp, out);
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

bool DataStore::loadDocument() {
    if (!_dataFs.exists(DOC_PATH)) {
        return false;
    }
    File f = _dataFs.open(DOC_PATH, "r");
    if (!f) {
        return false;
    }
    const DeserializationError err = deserializeJson(*_doc, f);
    f.close();
    return !err && _doc->is<JsonObject>();
}

void DataStore::seedDocument() {
    _doc->clear();
    _doc->createNestedArray(KEY_CELL_TYPES);
    _doc->createNestedArray(KEY_CELLS);
    _doc->createNestedArray(KEY_TEST_RESULTS);
}

bool DataStore::writeDocumentRaw() {
    if (!_fsReady || !_doc) {
        return false;
    }
    File f = _dataFs.open(DOC_TMP_PATH, "w");
    if (!f) {
        return false;
    }
    const size_t n = serializeJson(*_doc, f);
    f.close();
    if (n == 0) {
        _dataFs.remove(DOC_TMP_PATH);
        return false;
    }
    _dataFs.remove(DOC_PATH);
    if (!_dataFs.rename(DOC_TMP_PATH, DOC_PATH)) {
        _dataFs.remove(DOC_TMP_PATH);
        return false;
    }
    return true;
}

bool DataStore::checkpoint() {
    return writeDocumentRaw();
}

uint32_t DataStore::currentSeq() const {
    // Last assigned seq (inclusive). The change log stores "next to assign".
    return _changeLog.getCurrentSeq() - 1;
}

// ---------------------------------------------------------------------------
// Session-Kopplung (transient)
// ---------------------------------------------------------------------------

bool DataStore::assignSlot(int slot, const char* cell_id, const char* mode) {
    if (slot < 0 || slot >= MAX_SLOTS) {
        return false;
    }
    active_cell_ids[slot] = cell_id ? cell_id : "";
    pending_mode[slot] = mode ? mode : "";
    energy_accumulator[slot] = 0;   // neuer Laufvorgang -> Integral frisch starten
    return true;
}

bool DataStore::getChargeDefaults(const char* cell_id, mc5000::ChargeProfile& out,
                                  int& capacityCutoffMah) const {
    if (!cell_id || !_doc) return false;
    JsonArray cellsArr = (*_doc)[KEY_CELLS].as<JsonArray>();
    const int cidx = findById(cellsArr, cell_id);
    if (cidx < 0) return false;
    const char* type_id = cellsArr[cidx]["cell_type_id"] | "";
    if (!type_id || type_id[0] == '\0') return false;

    JsonArray typesArr = (*_doc)[KEY_CELL_TYPES].as<JsonArray>();
    const int tidx = findById(typesArr, type_id);
    if (tidx < 0) return false;
    JsonObject t = typesArr[tidx];

    // Nur Felder übernehmen, die im Zelltyp auch gesetzt sind (isNull == absent).
    if (!t["charge_current_ma"].isNull())      out.chargeCurrentMa = t["charge_current_ma"] | 0;
    if (!t["discharge_current_ma"].isNull())   out.dischargeCurrentMa = t["discharge_current_ma"] | 0;
    if (!t["target_voltage_mv"].isNull())      out.targetVoltageMv = t["target_voltage_mv"] | 0;
    if (!t["cutoff_voltage_mv"].isNull())      out.cutoffVoltageMv = t["cutoff_voltage_mv"] | 0;
    if (!t["termination_current_ma"].isNull()) out.terminationCurrentMa = t["termination_current_ma"] | 0;
    if (!t["delta_peak_mv"].isNull())          out.deltaPeakMv = t["delta_peak_mv"] | 0;
    if (!t["trickle_charge_ma"].isNull())      out.trickleChargeMa = t["trickle_charge_ma"] | 0;
    if (!t["capacity_cutoff_mah"].isNull())    capacityCutoffMah = t["capacity_cutoff_mah"] | 0;
    return true;
}

// Returns the more severe of two recommendations (SORT_OUT > WATCH > OK).
static const char* worseRecommendation(const char* a, const char* b) {
    auto rank = [](const char* s) -> int {
        if (strcmp(s, "SORT_OUT") == 0) return 2;
        if (strcmp(s, "WATCH") == 0) return 1;
        return 0;
    };
    return rank(a) >= rank(b) ? a : b;
}

void DataStore::finalizeSlot(int slot, int capacity_mah, int ir_mohm, const char* action) {
    if (slot < 0 || slot >= MAX_SLOTS) {
        return;
    }
    if (active_cell_ids[slot].empty()) {
        return;   // keine Zelle zugeordnet (manuell am Gerät gestartet) -> nicht loggen
    }

    // Energie aus dem Leistungs-Integral: µJ -> mWh (1 mWh = 3.600.000 µJ).
    const int energy_mwh = (int)(energy_accumulator[slot] / 3600000);
    energy_accumulator[slot] = 0;

    // Referenzwerte für die Alterungsbewertung: Kaufwerte der Zelle und
    // Nennkapazität / typischer IR des Zelltyps.
    int nominal_capacity_mah = 0;
    int typical_ir_mohm = 0;
    int purchase_capacity_mah = 0;
    JsonArray cellsArr = (*_doc)[KEY_CELLS].as<JsonArray>();
    JsonArray typesArr = (*_doc)[KEY_CELL_TYPES].as<JsonArray>();
    const int cidx = findById(cellsArr, active_cell_ids[slot].c_str());
    if (cidx >= 0) {
        JsonObject cell = cellsArr[cidx];
        purchase_capacity_mah = cell["purchase_capacity_mah"] | 0;
        const char* type_id = cell["cell_type_id"] | "";
        if (type_id && type_id[0]) {
            const int tidx = findById(typesArr, type_id);
            if (tidx >= 0) {
                JsonObject type = typesArr[tidx];
                nominal_capacity_mah = type["nominal_capacity_mah"] | 0;
                typical_ir_mohm = type["typical_ir_mohm"] | 0;
            }
        }
    }

    const uint32_t ts = nowSeconds();

    char hid[32];
    snprintf(hid, sizeof(hid), "h-%u-%d", ts, slot);

    // action: bevorzugt der beim Slot-Start gesetzte Mode (Task-Typ). Die Referenz
    // meldet keinen Mode im 0x91-Status (Zustand wird abgeleitet); der gestartete
    // Modus ist die verlässliche Quelle. `action` ist nur ein Fallback.
    const char* eff = pending_mode[slot].empty()
                          ? (action && action[0] ? action : "charge")
                          : pending_mode[slot].c_str();

    // SoH relativ zur Nenn- und zur Kaufkapazität.
    int soh_nominal = 0;
    int soh_purchase = 0;
    if (nominal_capacity_mah > 0) {
        soh_nominal = (int)((int64_t)capacity_mah * 100 / nominal_capacity_mah);
    }
    if (purchase_capacity_mah > 0) {
        soh_purchase = (int)((int64_t)capacity_mah * 100 / purchase_capacity_mah);
    }

    // Empfehlung (OK / WATCH / SORT_OUT), analog zur Android-App.
    const char* soh_rec = "WATCH";
    if (nominal_capacity_mah > 0) {
        soh_rec = soh_nominal > 90 ? "OK" : (soh_nominal >= 80 ? "WATCH" : "SORT_OUT");
    }
    const char* ir_rec = "OK";
    if (typical_ir_mohm > 0 && ir_mohm > 0) {
        const float ratio = (float)ir_mohm / (float)typical_ir_mohm;
        ir_rec = ratio < 1.5f ? "OK" : (ratio <= 2.0f ? "WATCH" : "SORT_OUT");
    }
    const char* recommendation = worseRecommendation(soh_rec, ir_rec);

    StaticJsonDocument<512> doc;
    doc["id"] = hid;
    doc["cell_id"] = active_cell_ids[slot].c_str();
    doc["timestamp_s"] = ts;
    doc["action"] = eff;
    doc["measured_capacity_mah"] = capacity_mah;
    doc["measured_ir_mohm"] = ir_mohm;
    doc["energy_mwh"] = energy_mwh;
    doc["soh_nominal_percent"] = soh_nominal;
    doc["soh_purchase_percent"] = soh_purchase;
    doc["recommendation"] = recommendation;

    char body[320];
    serializeJson(doc, body, sizeof(body));

    applyMutation(EntityType::TEST_RESULT, OpType::UPSERT, nullptr, body);
    active_cell_ids[slot].clear();
    pending_mode[slot].clear();
}
