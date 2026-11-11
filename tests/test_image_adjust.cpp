#include "check.h"
#include "core/ImageAdjust.h"
#include "core/SyntheticSource.h"

#include <cmath>
#include <cstring>

using namespace bcs;

static double meanLuma(const AVFrame* f)
{
    double sum = 0;
    for (int y = 0; y < f->height; ++y)
        for (int x = 0; x < f->width; ++x)
            sum += f->data[0][y * f->linesize[0] + x];
    return sum / (f->width * f->height);
}

static double lumaVariance(const AVFrame* f)
{
    const double m = meanLuma(f);
    double v = 0;
    for (int y = 0; y < f->height; ++y)
        for (int x = 0; x < f->width; ++x)
            v += std::pow(f->data[0][y * f->linesize[0] + x] - m, 2);
    return v / (f->width * f->height);
}

static FramePtr copyOf(const AVFrame* f)
{
    FramePtr c = makeVideoFrame(f->width, f->height, AVPixelFormat(f->format));
    av_frame_copy(c.get(), f);
    return c;
}

int main()
{
    SyntheticVideo src(640, 360);
    FramePtr ref = src.frame(3);
    std::vector<uint16_t> scratch;

    { // neutral: untouched
        FramePtr f = copyOf(ref.get());
        applyImageAdjust(f.get(), {}, scratch);
        CHECK(lumaPsnr(ref.get(), f.get()) > 90);
    }
    { // brightness moves the mean, chroma untouched
        FramePtr f = copyOf(ref.get());
        applyImageAdjust(f.get(), {50, 0, 0}, scratch);
        CHECK(meanLuma(f.get()) > meanLuma(ref.get()) + 30);
        CHECK(std::memcmp(f->data[1], ref->data[1], size_t(ref->linesize[1]) * 180) == 0);
        FramePtr g = copyOf(ref.get());
        applyImageAdjust(g.get(), {-100, 0, 0}, scratch);
        CHECK(meanLuma(g.get()) < meanLuma(ref.get()) - 50);
    }
    { // contrast -100 flattens to mid-grey
        FramePtr f = copyOf(ref.get());
        applyImageAdjust(f.get(), {0, -100, 0}, scratch);
        CHECK(lumaVariance(f.get()) < 1);
        CHECK(std::abs(meanLuma(f.get()) - 128) < 1);
    }
    { // sharpness: negative blurs (less detail), positive adds detail
        FramePtr soft = copyOf(ref.get()), sharp = copyOf(ref.get());
        applyImageAdjust(soft.get(), {0, 0, -100}, scratch);
        applyImageAdjust(sharp.get(), {0, 0, 100}, scratch);
        CHECK(lumaVariance(soft.get()) < lumaVariance(ref.get()));
        CHECK(lumaVariance(sharp.get()) > lumaVariance(ref.get()));
        // -100 is exactly a box blur (radius 1 at 640 wide): check one interior pixel.
        int sum = 0;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                sum += ref->data[0][(100 + dy) * ref->linesize[0] + 200 + dx];
        CHECK(std::abs(soft->data[0][100 * soft->linesize[0] + 200] - std::lround(sum / 9.0)) <= 1);
    }
    { // a flat image is unchanged by sharpness (edges included)
        FramePtr f = makeVideoFrame(64, 32, AV_PIX_FMT_YUV420P);
        for (int y = 0; y < 32; ++y)
            std::memset(f->data[0] + y * f->linesize[0], 77, 64);
        applyImageAdjust(f.get(), {0, 0, 100}, scratch);
        applyImageAdjust(f.get(), {0, 0, -60}, scratch);
        CHECK(meanLuma(f.get()) == 77 && lumaVariance(f.get()) == 0);
    }
    { // nv12 (luma plane 0) is handled, packed RGB is left alone
        FramePtr n = makeVideoFrame(64, 32, AV_PIX_FMT_NV12);
        for (int y = 0; y < 32; ++y)
            std::memset(n->data[0] + y * n->linesize[0], 100, 64);
        applyImageAdjust(n.get(), {50, 0, 0}, scratch);
        CHECK(n->data[0][0] == 140);
        FramePtr rgb = makeVideoFrame(64, 32, AV_PIX_FMT_RGB24);
        std::memset(rgb->data[0], 100, size_t(rgb->linesize[0]) * 32);
        applyImageAdjust(rgb.get(), {50, 50, 50}, scratch);
        CHECK(rgb->data[0][0] == 100);
    }
    return TEST_RESULT();
}
