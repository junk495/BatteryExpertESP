// state.js — client-side in-memory store (single source of truth for the PWA).
//
// Holds the synced data (cells, types, test results) plus the last known sequence id.
// reset() rebuilds the whole state from a `full:true` snapshot; applyDelta()
// atomically merges a delta batch (UPSERT/DEL).
//
// Wire format of the ESP32 (see src/DataStore.cpp):
//   Full sync:  { current_seq, full:true,  cell_types:[...], cells:[...], test_results:[...] }
//   Delta:      { current_seq, full:false, deltas:{ cells:[...], types:[...], test_results:[...] } }
//     cells/type entry:  { op:"upsert", cell|type:{...} }   |  { op:"delete", id:"..." }
//     test_results entry: full object (append-only, no op wrapper)

export const Store = {
  cells: new Map(),   // id -> cell object
  types: new Map(),   // id -> cell_type object
  testResults: [],   // append-only test results (chronological)
  seq: 0,             // last known current_seq from the ESP32

  // Full rebuild: discard everything, repopulate from a full-sync payload.
  reset(payload) {
    this.cells = new Map((payload.cells || []).map((c) => [c.id, c]));
    this.types = new Map((payload.cell_types || []).map((t) => [t.id, t]));
    this.testResults = Array.isArray(payload.test_results) ? payload.test_results.slice() : [];
    this.seq = payload.current_seq || 0;
  },

  // Atomic merge of one delta batch.
  applyDelta(payload) {
    const d = payload.deltas || {};

    for (const e of d.cells || []) {
      if (e.op === 'delete') {
        this.cells.delete(e.id);
      } else if (e.op === 'upsert' && e.cell && e.cell.id) {
        this.cells.set(e.cell.id, e.cell);
      }
    }

    for (const e of d.types || []) {
      if (e.op === 'delete') {
        this.types.delete(e.id);
      } else if (e.op === 'upsert' && e.type && e.type.id) {
        this.types.set(e.type.id, e.type);
      }
    }

    if (Array.isArray(d.test_results)) {
      for (const h of d.test_results) {
        this.testResults.push(h);
      }
    }

    if (payload.current_seq != null) {
      this.seq = payload.current_seq;
    }
  },

  // Convenience views for the UI layer.
  get cellsList() {
    return Array.from(this.cells.values());
  },
  get typesList() {
    return Array.from(this.types.values());
  },
};
