#pragma once

#include <string>

namespace bcs {

// Maps a quality slider position to a codec target bitrate. The scale is
// logarithmic because perceived quality changes roughly with log(bitrate):
// a linear 50..2000 slider would spend 95% of its travel on "fine" quality.
struct BitrateScale {
    int minKbps;
    int maxKbps;
    int defaultKbps;
    int sliderSteps; // slider range is [0, sliderSteps]

    int kbpsAt(int sliderPos) const;
    int sliderPosFor(int kbps) const;
    // e.g. "Very degraded — 50 kbit/s"
    std::string label(int kbps) const;
    // "Very degraded" .. "Cleaner"
    std::string tier(int kbps) const;
};

const BitrateScale& videoBitrateScale(); // 50 .. 2,000 kbit/s, default 150
const BitrateScale& audioBitrateScale(); // 6 .. 64 kbit/s, default 12

// "2,000 kbit/s"
std::string formatKbps(double kbps);

} // namespace bcs
