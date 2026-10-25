#include "core/QualityMapping.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bcs {

namespace {
// Round to "nice" values so the label does not show 147 or 1,983.
int snap(int kbps, int maxKbps)
{
    int step;
    if (maxKbps <= 128)
        step = 1;
    else if (kbps < 200)
        step = 5;
    else if (kbps < 500)
        step = 10;
    else
        step = 25;
    return std::max(step, int(std::lround(double(kbps) / step)) * step);
}
} // namespace

int BitrateScale::kbpsAt(int pos) const
{
    pos = std::clamp(pos, 0, sliderSteps);
    double t = double(pos) / double(sliderSteps);
    double v = std::exp(std::log(double(minKbps)) + t * (std::log(double(maxKbps)) - std::log(double(minKbps))));
    return std::clamp(snap(int(std::lround(v)), maxKbps), minKbps, maxKbps);
}

int BitrateScale::sliderPosFor(int kbps) const
{
    kbps = std::clamp(kbps, minKbps, maxKbps);
    double t = (std::log(double(kbps)) - std::log(double(minKbps))) /
               (std::log(double(maxKbps)) - std::log(double(minKbps)));
    return std::clamp(int(std::lround(t * sliderSteps)), 0, sliderSteps);
}

std::string BitrateScale::tier(int kbps) const
{
    // Tier thresholds are placed on the same log scale as the slider.
    double t = (std::log(double(std::clamp(kbps, minKbps, maxKbps))) - std::log(double(minKbps))) /
               (std::log(double(maxKbps)) - std::log(double(minKbps)));
    if (t < 0.15)
        return "Very degraded";
    if (t < 0.40)
        return "Degraded";
    if (t < 0.62)
        return "Poor";
    if (t < 0.85)
        return "Fair";
    return "Cleaner";
}

std::string BitrateScale::label(int kbps) const
{
    return tier(kbps) + " — " + formatKbps(kbps);
}

const BitrateScale& videoBitrateScale()
{
    static const BitrateScale s{50, 2000, 150, 1000};
    return s;
}

const BitrateScale& audioBitrateScale()
{
    static const BitrateScale s{6, 64, 12, 580};
    return s;
}

std::string formatKbps(double kbps)
{
    long v = std::lround(kbps);
    std::string digits = std::to_string(std::labs(v));
    std::string out;
    int n = int(digits.size());
    for (int i = 0; i < n; ++i) {
        out += digits[std::size_t(i)];
        if ((n - i - 1) % 3 == 0 && i != n - 1)
            out += ',';
    }
    return (v < 0 ? "-" : "") + out + " kbit/s";
}

} // namespace bcs
