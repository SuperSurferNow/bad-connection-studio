#pragma once

#include "backend/VirtualCameraBackend.h" // BackendStatus, VirtualDeviceInfo
#include "core/Errors.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace bcs {

// Publishes decoded PCM as something call apps can select as a microphone.
// Input is always interleaved int16 at 48 kHz with 1 or 2 channels.
//
// Thread model: probe/open/close from the UI or audio-control thread;
// write() from the audio processing thread. Implementations buffer through a
// bounded PcmRingBuffer and pull from their own real-time callback.
class VirtualMicBackend {
public:
    virtual ~VirtualMicBackend() = default;

    virtual BackendStatus probe() = 0;

    virtual MediaError open(const std::string& deviceId, int sampleRate, int channels) = 0;
    virtual void write(const int16_t* interleaved, std::size_t frames) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // Asynchronous failure (server went away, device unplugged), or empty.
    virtual MediaError takeAsyncError() = 0;
    virtual double bufferedMs() const = 0;
    virtual std::string describe() const = 0;
};

} // namespace bcs
