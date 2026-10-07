#pragma once
// Fixed-size buffer for live measurement series with time-based downsampling.
// No hardware dependency -> natively unit-testable.

#include <cstdint>
#include <cstddef>

// Compile switch: ESP32 uses PSRAM, native tests fall back to malloc/free.
#ifdef ARDUINO_ARCH_ESP32
  #include <esp_heap_caps.h>
#else
  #include <stdlib.h>
  #define heap_caps_malloc(size, caps) malloc(size)
  #define heap_caps_free(ptr) free(ptr)
  #define MALLOC_CAP_SPIRAM 0
#endif

#pragma pack(push, 1)
struct LivePoint {
    uint32_t timestamp_s;
    uint16_t voltage_mv;
    int16_t current_ma;
    uint16_t capacity_mah;
    int8_t temp_c;
};
#pragma pack(pop)

class DownsamplingBuffer {
private:
    static constexpr size_t MAX_POINTS = 2000;

    struct Accumulator {
        uint32_t sum_voltage = 0;
        int32_t sum_current = 0;
        uint32_t last_capacity = 0;
        int32_t sum_temp = 0;
        uint16_t samples = 0;
    };

    LivePoint* buffer = nullptr;
    size_t count = 0;
    uint32_t session_start_s = 0;
    uint32_t last_commit_s = 0;
    Accumulator accumulator;

    uint32_t getIntervalTarget(uint32_t elapsed_s) const {
        if (elapsed_s < 60) return 1;
        if (elapsed_s < 3600) return 10;
        return 30;
    }

    void commitAccumulator(uint32_t timestamp) {
        if (accumulator.samples == 0 || count >= MAX_POINTS) return;
        buffer[count] = {
            timestamp,
            static_cast<uint16_t>(accumulator.sum_voltage / accumulator.samples),
            static_cast<int16_t>(accumulator.sum_current / accumulator.samples),
            static_cast<uint16_t>(accumulator.last_capacity),   // monotonic: keep last value
            static_cast<int8_t>(accumulator.sum_temp / accumulator.samples)
        };
        count++;
        last_commit_s = timestamp;
        accumulator = Accumulator{};
    }

public:
    DownsamplingBuffer() = default;

    ~DownsamplingBuffer() {
        if (buffer) heap_caps_free(buffer);
    }

    // Explicit allocation (not in constructor): the PSRAM driver may not be ready
    // during static initialization.
    bool allocate() {
        if (!buffer) {
            buffer = static_cast<LivePoint*>(heap_caps_malloc(MAX_POINTS * sizeof(LivePoint), MALLOC_CAP_SPIRAM));
        }
        return buffer != nullptr;
    }

    void startSession(uint32_t current_time_s) {
        session_start_s = current_time_s;
        last_commit_s = current_time_s;
        count = 0;
        accumulator = Accumulator{};
    }

    void push(uint32_t timestamp_s, uint16_t voltage, int16_t current, uint16_t capacity, int8_t temp) {
        if (!buffer || count >= MAX_POINTS) return;   // buffer full: stop accumulating

        uint32_t elapsed = timestamp_s - session_start_s;
        uint32_t target_interval = getIntervalTarget(elapsed);

        accumulator.sum_voltage += voltage;
        accumulator.sum_current += current;
        accumulator.last_capacity = capacity;
        accumulator.sum_temp += temp;
        accumulator.samples++;

        if ((timestamp_s - last_commit_s) >= target_interval) {
            commitAccumulator(timestamp_s);
        }
    }

    const LivePoint* getData() const { return buffer; }
    size_t getCount() const { return count; }
};