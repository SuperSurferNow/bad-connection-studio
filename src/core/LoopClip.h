#pragma once

#include "core/FfmpegUtil.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace bcs {

// A short recording of the clean (not yet degraded) camera and microphone,
// held in memory only and never written to disk. While it loops it is fed
// into the pipelines in place of the live input, so the codec and the quality
// sliders still apply.
//
// Video is stored as high-quality MJPEG packets (every FFmpeg build has the
// encoder; about a tenth of the size of raw frames). Audio is stored as
// 48 kHz signed 16-bit PCM. Both are timed against the same clock, so video
// and audio stay in sync over any number of loops.
//
// Thread use: the video capture thread calls addVideo()/videoFrameAt(), the
// audio thread addAudio()/readAudio(), the UI the rest. All methods are
// thread-safe.
class LoopClip {
public:
    enum class State { Empty, Recording, Ready };

    static constexpr int64_t kMaxDurationUs = 120'000'000;
    static constexpr size_t kMaxBytes = size_t(600) << 20;
    static constexpr int kAudioRate = 48000;

    LoopClip() = default;
    LoopClip(const LoopClip&) = delete;
    LoopClip& operator=(const LoopClip&) = delete;

    void beginRecording();
    // Ends the recording and trims video and audio to a common length.
    // Returns false (and clears) if nothing usable was recorded.
    bool finishRecording();
    void clear();

    State state() const { return m_state.load(); }
    bool isRecording() const { return m_state == State::Recording; }
    bool isReady() const { return m_state == State::Ready; }
    // Recording stopped accepting data (duration or memory limit).
    bool limitReached() const { return m_full.load(); }

    int64_t recordedUs() const;  // while recording: elapsed time
    int64_t durationUs() const;  // when ready
    size_t bytes() const;
    bool hasVideo() const;
    bool hasAudio() const;

    // ---- recording
    void addVideo(const AVFrame* frame);
    // Interleaved 48 kHz s16. The channel count is fixed by the first call.
    void addAudio(const int16_t* pcm, size_t frames, int channels);

    // ---- playback. The loop position is derived from one shared start time.
    void startPlayback();
    int64_t positionUs(int64_t nowUs) const;
    int64_t playbackStartUs() const { return m_playStartUs.load(); }

    // Decoded frame for a loop position, or nullptr if it is the same frame
    // as `lastIndex` (pass -1 for "any"). `index` receives the frame index.
    FramePtr videoFrameAt(int64_t posUs, int lastIndex, int* index);
    // Copies `frames` samples starting at absolute sample `start` (wrapping
    // around the loop). Returns the channel count, 0 without audio.
    int readAudio(int64_t start, size_t frames, std::vector<int16_t>& out) const;
    int audioChannels() const;

private:
    struct VideoPacket {
        std::vector<uint8_t> data;
        int64_t ptsUs = 0;
    };

    bool openEncoder(int w, int h);
    bool roomFor(size_t more) const;

    mutable std::mutex m_mutex;
    std::atomic<State> m_state{State::Empty};
    std::atomic<bool> m_full{false};
    std::atomic<int64_t> m_recStartUs{0};
    std::atomic<int64_t> m_playStartUs{0};
    int64_t m_durationUs = 0;
    size_t m_bytes = 0;

    std::vector<VideoPacket> m_video;
    std::vector<int16_t> m_audio;
    int m_audioChannels = 0;

    // MJPEG encoder (recording) and decoder (playback); used under m_mutex.
    CodecCtxPtr m_enc;
    CodecCtxPtr m_dec;
    SwsPtr m_sws;
    int m_swsKey[3] = {0, 0, -1};
    FramePtr m_encFrame;
    PacketPtr m_pkt;
};

} // namespace bcs
