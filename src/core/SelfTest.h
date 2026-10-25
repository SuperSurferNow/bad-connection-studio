#pragma once

#include <string>
#include <vector>

namespace bcs {

struct VideoRoundTripPoint {
    int targetKbps = 0;
    double actualKbps = 0;
    double psnrDb = 0;
    double avgCodecMs = 0; // encode + decode per frame
    int outWidth = 0, outHeight = 0;
    int framesIn = 0, framesOut = 0;
};

struct AudioRoundTripPoint {
    int targetKbps = 0;
    double actualKbps = 0;
    // Mean log-spectral distance to the original (dB, lower = closer).
    // Perceptual codecs do not preserve the waveform, so SNR is meaningless
    // for them; spectral distance tracks the audible bandwidth/detail loss.
    double spectralDistanceDb = 0;
    double codecDelayMs = 0;
};

// Encode/decode synthetic material at several bitrates (milestone 1
// prototype measurement, also used by --self-test and CI on clean machines).
std::vector<VideoRoundTripPoint> measureVideoRoundTrip(const std::string& encoder, int width, int height, int fps,
                                                       const std::vector<int>& kbps, int frames);
std::vector<AudioRoundTripPoint> measureAudioRoundTrip(const std::string& encoder, int channels,
                                                       const std::vector<int>& kbps, double seconds);

struct SelfTestResult {
    bool passed = false;
    std::string report;
};

// Runs codec round-trips, verifies quality rises with bitrate while the
// resolution stays fixed, and appends the virtual-device capability check
// (missing OS components are reported, not treated as failures).
SelfTestResult runSelfTest();

} // namespace bcs
