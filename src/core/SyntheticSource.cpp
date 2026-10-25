#include "core/SyntheticSource.h"

#include <algorithm>
#include <cmath>

namespace bcs {

SyntheticVideo::SyntheticVideo(int width, int height) : m_w(width & ~1), m_h(height & ~1) {}

FramePtr SyntheticVideo::frame(int64_t i)
{
    FramePtr f = makeVideoFrame(m_w, m_h, AV_PIX_FMT_YUV420P);
    if (!f)
        return nullptr;
    const double t = double(i) / 30.0;
    // Bouncing box ("a head moving in front of the camera").
    const int bw = m_w / 5, bh = m_h / 3;
    const int bx = int((0.5 + 0.4 * std::sin(t * 1.3)) * (m_w - bw));
    const int by = int((0.5 + 0.4 * std::cos(t * 0.9)) * (m_h - bh));
    for (int y = 0; y < m_h; ++y) {
        uint8_t* row = f->data[0] + y * f->linesize[0];
        for (int x = 0; x < m_w; ++x) {
            // Static textured background (a bookshelf) + fine checker detail.
            int v = 60 + (x * 90) / m_w + (y * 40) / m_h + (((x / 6) ^ (y / 6)) & 1) * 18;
            if (x >= bx && x < bx + bw && y >= by && y < by + bh)
                v = 170 + int(30 * std::sin((x - bx) * 0.15) * std::cos((y - by) * 0.11));
            m_rng = m_rng * 1664525u + 1013904223u;
            v += int((m_rng >> 24) & 3) - 1; // sensor noise
            row[x] = uint8_t(std::clamp(v, 16, 235));
        }
    }
    for (int p = 1; p <= 2; ++p) {
        for (int y = 0; y < m_h / 2; ++y) {
            uint8_t* row = f->data[p] + y * f->linesize[p];
            for (int x = 0; x < m_w / 2; ++x) {
                bool inBox = x * 2 >= bx && x * 2 < bx + bw && y * 2 >= by && y * 2 < by + bh;
                row[x] = uint8_t(inBox ? (p == 1 ? 110 : 160) : (p == 1 ? 128 + (x * 30) / m_w : 128 - (y * 20) / m_h));
            }
        }
    }
    return f;
}

std::vector<int16_t> syntheticSpeech(int rate, double seconds)
{
    const std::size_t n = std::size_t(rate * seconds);
    std::vector<int16_t> out(n);
    const double pi = 3.14159265358979323846;
    double phase = 0;
    // Two resonators (formants) excited by a pulse train.
    double y1[2] = {0, 0}, y2[2] = {0, 0};
    for (std::size_t i = 0; i < n; ++i) {
        double t = double(i) / rate;
        double f0 = 120 + 30 * std::sin(2 * pi * 0.7 * t);
        phase += f0 / rate;
        double excitation = 0;
        if (phase >= 1.0) {
            phase -= 1.0;
            excitation = 1.0;
        }
        double formants[2] = {500 + 300 * std::sin(2 * pi * 2.1 * t), 1500 + 600 * std::sin(2 * pi * 1.3 * t + 1)};
        double s = 0;
        for (int k = 0; k < 2; ++k) {
            double r = 0.97, w = 2 * pi * formants[k] / rate;
            double y = excitation + 2 * r * std::cos(w) * y1[k] - r * r * y2[k];
            y2[k] = y1[k];
            y1[k] = y;
            s += y * (k == 0 ? 1.0 : 0.6);
        }
        double envelope = 0.55 + 0.45 * std::sin(2 * pi * 4.0 * t); // ~4 syllables/s
        out[i] = int16_t(std::clamp(s * envelope * 900.0, -32000.0, 32000.0));
    }
    return out;
}

double lumaPsnr(const AVFrame* a, const AVFrame* b)
{
    if (!a || !b || a->width != b->width || a->height != b->height)
        return 0;
    double se = 0;
    for (int y = 0; y < a->height; ++y) {
        const uint8_t* ra = a->data[0] + y * a->linesize[0];
        const uint8_t* rb = b->data[0] + y * b->linesize[0];
        for (int x = 0; x < a->width; ++x) {
            double d = double(ra[x]) - double(rb[x]);
            se += d * d;
        }
    }
    double mse = se / (double(a->width) * a->height);
    return mse <= 1e-10 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

} // namespace bcs
