// sync.js — delta-sync polling loop against the ESP32 REST API.
//
// Polls GET /api/v1/delta?since_seq=N every intervalMs. When the device answers
// with full:true (the sequence id fell out of its bounded change-log window,
// e.g. after the PWA was offline too long), the local state is discarded and
// rebuilt from the full snapshot. Transient network errors keep the local state
// and retry on the next tick — only an explicit full:true triggers a reset.

import { Store } from './state.js';

export async function syncOnce() {
  const resp = await fetch(`/api/v1/delta?since_seq=${Store.seq}`);
  if (!resp.ok) {
    throw new Error(`delta request failed: HTTP ${resp.status}`);
  }
  const data = await resp.json();

  if (data.full) {
    // Stale sequence id -> drop local state, rebuild from the full snapshot.
    Store.reset(data);
  } else {
    Store.applyDelta(data);
  }
  return data;
}

export function startSync(intervalMs = 2000, onChange = () => {}) {
  (async function loop() {
    for (;;) {
      try {
        await syncOnce();
        onChange();
      } catch (err) {
        // Transient failure (network blip, restart): keep local state, retry.
        console.warn('[sync] poll failed:', err);
      }
      await new Promise((r) => setTimeout(r, intervalMs));
    }
  })();
}
