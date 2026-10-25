#pragma once

#include "backend/VirtualMicBackend.h"
#include "core/PcmRingBuffer.h"

#include <atomic>
#include <memory>
#include <mutex>

struct pw_thread_loop;
struct pw_context;
struct pw_core;
struct pw_stream;
struct spa_hook;

namespace bcs {

// Creates a "Bad Connection Microphone" node (media.class = Audio/Source)
// in the user's PipeWire session for as long as the stream is open. Call
// apps see it as a regular microphone. Nothing is written to PipeWire or
// WirePlumber configuration; the node disappears when the stream closes or
// the app exits.
class PipeWireMicBackend final : public VirtualMicBackend {
public:
    static constexpr const char* kDeviceId = "pipewire:bad_connection_mic";
    static constexpr const char* kNodeName = "bad_connection_mic";
    static constexpr const char* kNodeDescription = "Bad Connection Microphone";

    PipeWireMicBackend();
    ~PipeWireMicBackend() override;

    BackendStatus probe() override;
    MediaError open(const std::string& deviceId, int sampleRate, int channels) override;
    void write(const int16_t* interleaved, std::size_t frames) override;
    void close() override;
    bool isOpen() const override { return m_stream != nullptr; }
    MediaError takeAsyncError() override;
    // Only meaningful while an app is recording from the node.
    double bufferedMs() const override { return m_ring && m_streaming ? m_ring->bufferedMs() : 0; }
    std::string describe() const override;

    // Called from the PipeWire real-time / loop thread.
    void handleProcess();
    void handleStateChange(bool error, bool unconnected, bool streaming, const char* message);

private:

    std::unique_ptr<PcmRingBuffer> m_ring;
    pw_thread_loop* m_loop = nullptr;
    pw_context* m_context = nullptr;
    pw_core* m_core = nullptr;
    pw_stream* m_stream = nullptr;
    std::unique_ptr<spa_hook> m_listener;
    std::atomic<bool> m_streaming{false}; // a consumer (call app) is linked
    int m_channels = 1;
    int m_rate = 48000;

    std::mutex m_errMutex;
    MediaError m_asyncError;
};

} // namespace bcs
