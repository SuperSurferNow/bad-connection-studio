#include "check.h"
#include "core/BoundedQueue.h"
#include "core/PcmRingBuffer.h"

#include <chrono>
#include <vector>

using namespace bcs;
using namespace std::chrono_literals;

int main()
{
    // Bounded queue keeps the newest items.
    BoundedQueue<int> q(2);
    CHECK(q.push(1) == 0);
    CHECK(q.push(2) == 0);
    CHECK(q.push(3) == 1); // dropped "1"
    CHECK(q.pop(1ms).value_or(-1) == 2);
    CHECK(q.pop(1ms).value_or(-1) == 3);
    CHECK(!q.pop(1ms).has_value());
    q.close();
    CHECK(q.push(4) == 1);

    // PCM ring: a writer that runs ahead (clock drift) never builds up more
    // than the configured maximum latency.
    PcmRingBuffer ring(1, 48000, 40, 120);
    std::vector<int16_t> chunk(480, 1000);
    for (int i = 0; i < 500; ++i) // 5 s written, nothing read
        ring.write(chunk.data(), chunk.size());
    CHECK(ring.bufferedMs() <= 120.5);
    CHECK(ring.overrunDrops() > 0);

    // Reader gets audio, then silence on underrun.
    std::vector<int16_t> out(48000);
    std::size_t got = ring.read(out.data(), out.size());
    CHECK(got > 0 && got < out.size());
    CHECK(out[0] == 1000);
    CHECK(out.back() == 0);
    CHECK(ring.underruns() == 1);

    // Stereo interleaving survives the wrap-around.
    PcmRingBuffer st(2, 48000, 10, 40);
    std::vector<int16_t> s(2 * 300);
    for (std::size_t i = 0; i < 300; ++i) {
        s[2 * i] = 1;
        s[2 * i + 1] = -1;
    }
    for (int i = 0; i < 20; ++i)
        st.write(s.data(), 300);
    std::vector<int16_t> o(2 * 200);
    st.read(o.data(), 200);
    CHECK(o[0] == 1 && o[1] == -1 && o[398] == 1 && o[399] == -1);
    return TEST_RESULT();
}
