#include "core/ImageAdjust.h"

#include <algorithm>
#include <cmath>

namespace bcs {

void applyImageAdjust(AVFrame* f, const ImageAdjust& a, std::vector<uint16_t>& scratch)
{
    if (!f || a.isNeutral())
        return;
    const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(AVPixelFormat(f->format));
    if (!d || (d->flags & (AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_PAL)) || d->nb_components < 3 ||
        d->comp[0].plane != 0 || d->comp[0].step != 1 || d->comp[0].depth != 8)
        return;
    const int w = f->width, h = f->height, ls = f->linesize[0];
    uint8_t* y = f->data[0];

    if (a.sharpness != 0) {
        // Unsharp mask against a box blur whose radius follows the resolution,
        // so the effect looks the same at 360p and 1080p. -100 = full blur.
        const int r = std::max(1, w / 480);
        const double n = double((2 * r + 1) * (2 * r + 1));
        const double k = a.sharpness > 0 ? a.sharpness / 50.0 : a.sharpness / 100.0;
        scratch.resize(size_t(w) * size_t(h));
        for (int row = 0; row < h; ++row) { // horizontal window sums
            const uint8_t* p = y + ptrdiff_t(row) * ls;
            uint16_t* s = scratch.data() + size_t(row) * size_t(w);
            int sum = 0;
            for (int x = -r; x <= r; ++x)
                sum += p[std::clamp(x, 0, w - 1)];
            for (int x = 0; x < w; ++x) {
                s[x] = uint16_t(sum);
                sum += p[std::min(x + r + 1, w - 1)] - p[std::max(x - r, 0)];
            }
        }
        std::vector<uint32_t> col(size_t(w), 0); // vertical window sums
        for (int i = -r; i <= r; ++i) {
            const uint16_t* s = scratch.data() + size_t(std::clamp(i, 0, h - 1)) * size_t(w);
            for (int x = 0; x < w; ++x)
                col[size_t(x)] += s[x];
        }
        for (int row = 0; row < h; ++row) {
            uint8_t* p = y + ptrdiff_t(row) * ls;
            for (int x = 0; x < w; ++x) {
                const double v = p[x] + k * (p[x] - col[size_t(x)] / n);
                p[x] = uint8_t(std::clamp(std::lround(v), 0L, 255L));
            }
            const uint16_t* add = scratch.data() + size_t(std::min(row + r + 1, h - 1)) * size_t(w);
            const uint16_t* sub = scratch.data() + size_t(std::max(row - r, 0)) * size_t(w);
            for (int x = 0; x < w; ++x)
                col[size_t(x)] += uint32_t(add[x]) - sub[x];
        }
    }

    if (a.brightness != 0 || a.contrast != 0) {
        const double gain = 1.0 + a.contrast / 100.0;
        const double offset = a.brightness * 0.8;
        uint8_t lut[256];
        for (int v = 0; v < 256; ++v)
            lut[v] = uint8_t(std::clamp(std::lround((v - 128) * gain + 128 + offset), 0L, 255L));
        for (int row = 0; row < h; ++row) {
            uint8_t* p = y + ptrdiff_t(row) * ls;
            for (int x = 0; x < w; ++x)
                p[x] = lut[p[x]];
        }
    }
}

} // namespace bcs
