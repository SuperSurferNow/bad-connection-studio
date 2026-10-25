#pragma once

#include "core/FfmpegUtil.h"

#include <cstdint>
#include <vector>

namespace bcs {

// Deterministic test material used by the self-test, the benchmark tool and
// the "Test pattern" camera entry. It is generated, never recorded.
class SyntheticVideo {
public:
    SyntheticVideo(int width, int height);
    // YUV420P frame with moving detail, gradients and mild sensor-like noise.
    FramePtr frame(int64_t index);

private:
    int m_w, m_h;
    uint32_t m_rng = 12345;
};

// Speech-like test signal: a glottal pulse train with moving formants and
// syllable-rate amplitude modulation, mono int16 at the given rate.
std::vector<int16_t> syntheticSpeech(int sampleRate, double seconds);

// Peak signal-to-noise ratio of the luma planes, in dB.
double lumaPsnr(const AVFrame* a, const AVFrame* b);

} // namespace bcs
