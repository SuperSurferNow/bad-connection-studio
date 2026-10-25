#include "check.h"
#include "core/QualityMapping.h"

#include <string>

using namespace bcs;

int main()
{
    const auto& v = videoBitrateScale();
    CHECK(v.kbpsAt(0) == 50);
    CHECK(v.kbpsAt(v.sliderSteps) == 2000);
    CHECK(v.label(50) == "Very degraded — 50 kbit/s");
    CHECK(v.label(2000) == "Cleaner — 2,000 kbit/s");
    // Default round-trips through the slider within one snapping step.
    int d = v.kbpsAt(v.sliderPosFor(v.defaultKbps));
    CHECK(d >= 145 && d <= 155);
    int prev = 0;
    for (int p = 0; p <= v.sliderSteps; ++p) {
        int k = v.kbpsAt(p);
        CHECK(k >= prev);
        prev = k;
    }

    const auto& a = audioBitrateScale();
    CHECK(a.kbpsAt(0) == 6);
    CHECK(a.kbpsAt(a.sliderSteps) == 64);
    CHECK(a.label(6) == "Very degraded — 6 kbit/s");
    CHECK(a.label(64) == "Cleaner — 64 kbit/s");
    CHECK(formatKbps(1234567) == "1,234,567 kbit/s");
    return TEST_RESULT();
}
