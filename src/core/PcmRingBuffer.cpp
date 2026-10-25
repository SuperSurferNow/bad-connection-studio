#include "core/PcmRingBuffer.h"

#include <algorithm>
#include <cstring>

namespace bcs {

PcmRingBuffer::PcmRingBuffer(int channels, int sampleRate, int targetMs, int maxMs)
    : m_channels(std::max(1, channels))
    , m_rate(std::max(8000, sampleRate))
    , m_target(std::size_t(m_rate) * std::size_t(targetMs) / 1000)
    , m_max(std::size_t(m_rate) * std::size_t(std::max(maxMs, targetMs + 10)) / 1000)
{
    m_capFrames = m_max * 2;
    m_buf.assign(m_capFrames * std::size_t(m_channels), 0);
}

void PcmRingBuffer::write(const int16_t* data, std::size_t frames)
{
    std::lock_guard lk(m_mutex);
    const std::size_t ch = std::size_t(m_channels);
    if (frames > m_capFrames) { // keep only the newest audio
        data += (frames - m_capFrames) * ch;
        m_overrunDrops += frames - m_capFrames;
        frames = m_capFrames;
    }
    for (std::size_t i = 0; i < frames;) {
        std::size_t writePos = (m_read + m_count) % m_capFrames;
        if (m_count == m_capFrames) { // full: drop oldest frame
            m_read = (m_read + 1) % m_capFrames;
            --m_count;
            ++m_overrunDrops;
            continue;
        }
        std::size_t n = std::min({frames - i, m_capFrames - writePos, m_capFrames - m_count});
        std::memcpy(&m_buf[writePos * ch], data + i * ch, n * ch * sizeof(int16_t));
        m_count += n;
        i += n;
    }
    if (m_count > m_max) { // drift: trim back to the target latency
        std::size_t drop = m_count - m_target;
        m_read = (m_read + drop) % m_capFrames;
        m_count -= drop;
        m_overrunDrops += drop;
    }
}

std::size_t PcmRingBuffer::read(int16_t* out, std::size_t frames)
{
    std::lock_guard lk(m_mutex);
    const std::size_t ch = std::size_t(m_channels);
    if (!m_primed) {
        if (m_count < m_target / 2) {
            std::memset(out, 0, frames * ch * sizeof(int16_t));
            return 0;
        }
        m_primed = true;
    }
    std::size_t done = 0;
    while (done < frames && m_count > 0) {
        std::size_t n = std::min({frames - done, m_count, m_capFrames - m_read});
        std::memcpy(out + done * ch, &m_buf[m_read * ch], n * ch * sizeof(int16_t));
        m_read = (m_read + n) % m_capFrames;
        m_count -= n;
        done += n;
    }
    if (done < frames) {
        std::memset(out + done * ch, 0, (frames - done) * ch * sizeof(int16_t));
        ++m_underruns;
        m_primed = false;
    }
    return done;
}

void PcmRingBuffer::clear()
{
    std::lock_guard lk(m_mutex);
    m_read = m_count = 0;
    m_primed = false;
}

std::size_t PcmRingBuffer::bufferedFrames() const
{
    std::lock_guard lk(m_mutex);
    return m_count;
}

double PcmRingBuffer::bufferedMs() const
{
    return double(bufferedFrames()) * 1000.0 / double(m_rate);
}

} // namespace bcs
