#pragma once
// Bounded change log for delta synchronization. Fixed-size ring buffer for the
// entries (no runtime heap allocation for the log itself). Natively unit-testable.

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>

enum class EntityType : uint8_t { CELL, TYPE, HISTORY };
enum class OpType : uint8_t { UPSERT, DEL };   // DEL (not DELETE, which collides with macros)

struct ChangeEntry {
    uint32_t seq_id;
    EntityType entity;
    OpType op;
    char payload[200];   // reicht auch für history-Schnipsel (~134 Zeichen)
};

class BoundedChangeLog {
private:
    static constexpr size_t MAX_ENTRIES = 50;

    ChangeEntry buffer[MAX_ENTRIES];
    size_t head = 0;
    size_t count = 0;
    uint32_t current_seq;

public:
    explicit BoundedChangeLog(uint32_t start_seq = 1)
        : current_seq(start_seq) {}

    uint32_t getCurrentSeq() const { return current_seq; }
    size_t getCount() const { return count; }
    static size_t maxEntries() { return MAX_ENTRIES; }

    // Re-initialize to an empty log starting at start_seq. Used on boot to
    // establish a fresh sync epoch so stale clients trigger a full sync.
    void reset(uint32_t start_seq) {
        head = 0;
        count = 0;
        current_seq = start_seq;
    }

    // Appends a mutation and increments the sequence id. Returns the new id.
    uint32_t push(EntityType entity, OpType op, const char* json_payload) {
        ChangeEntry& entry = buffer[head];
        entry.seq_id = current_seq++;
        entry.entity = entity;
        entry.op = op;

        if (json_payload) {
            strncpy(entry.payload, json_payload, sizeof(entry.payload) - 1);
            entry.payload[sizeof(entry.payload) - 1] = '\0';
        } else {
            entry.payload[0] = '\0';
        }

        head = (head + 1) % MAX_ENTRIES;
        if (count < MAX_ENTRIES) count++;

        return entry.seq_id;
    }

    // Fills out_deltas with all changes after since_seq (exclusive).
    // Returns false if a full sync is required (since_seq fell out of the window).
    bool getDeltasSince(uint32_t since_seq, std::vector<ChangeEntry>& out_deltas) const {
        out_deltas.clear();

        if (since_seq >= current_seq) {
            return true;   // client is up to date
        }

        const size_t elements_to_read = current_seq - since_seq - 1;
        if (elements_to_read > count) {
            return false;   // full sync required
        }

        size_t read_idx = (head + MAX_ENTRIES - elements_to_read) % MAX_ENTRIES;
        for (size_t i = 0; i < elements_to_read; ++i) {
            out_deltas.push_back(buffer[read_idx]);
            read_idx = (read_idx + 1) % MAX_ENTRIES;
        }
        return true;
    }
};