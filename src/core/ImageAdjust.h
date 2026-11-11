#pragma once

#include "core/FfmpegUtil.h"

#include <cstdint>
#include <vector>

namespace bcs {

// "Cheap webcam" image controls, each -100..100, 0 = unchanged.
struct ImageAdjust {
    int brightness = 0; // luma offset
    int contrast = 0;   // luma gain around mid-grey
    int sharpness = 0;  // < 0 blurs (soft webcam), > 0 over-sharpens (halos)

    bool isNeutral() const { return brightness == 0 && contrast == 0 && sharpness == 0; }
    bool operator==(const ImageAdjust&) const = default;
};

// Adjusts the luma plane of an 8-bit planar/semi-planar YUV frame in place
// (yuv420p, yuvj420p, nv12, ...); other formats are left untouched. Applied
// to the clean frame before encoding, so the codec degrades the result.
// `scratch` is reused between calls to avoid per-frame allocation.
void applyImageAdjust(AVFrame* frame, const ImageAdjust& a, std::vector<uint16_t>& scratch);

} // namespace bcs
