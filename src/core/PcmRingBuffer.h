#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace bcs {

// Interleaved int16 ring buffer between the codec thread (writer) and an
// audio output callback (reader). Bounded in both directions:
//  - if the writer gets ahead by more than maxLatencyFrames (clock drift
//    between the capture device and the output graph), the oldest audio is
//    discarded down to targetFrames, so delay cannot accumulate;
//  - if the reader underruns it gets silence.
class PcmRingBuffer {
public:
    PcmRingBuffer(int channels, int sampleRate, int targetMs = 40, int maxMs = 120);

    void write(const int16_t* interleaved, std::size_t frames);
    // Always fills `frames` frames; returns how many were real audio.
    std::size_t read(int16_t* interleaved, std::size_t frames);
    void clear();

    int channels() const { return m_channels; }
    int sampleRate() const { return m_rate; }
    std::size_t bufferedFrames() const;
    double bufferedMs() const;

    uint64_t underruns() const { return m_underruns.load(); }
    uint64_t overrunDrops() const { return m_overrunDrops.load(); }

private:
    const int m_channels;
    const int m_rate;
    const std::size_t m_target;
    const std::size_t m_max;
    std::vector<int16_t> m_buf; // capacity frames * channels
    std::size_t m_capFrames;
    std::size_t m_read = 0;  // frame index
    std::size_t m_count = 0; // frames stored
    bool m_primed = false;   // wait for target fill after an underrun
    mutable std::mutex m_mutex;
    std::atomic<uint64_t> m_underruns{0};
    std::atomic<uint64_t> m_overrunDrops{0};
};

} // namespace bcs
