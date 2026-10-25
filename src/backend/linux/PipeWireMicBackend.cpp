#include "backend/linux/PipeWireMicBackend.h"

#include "backend/linux/PipeWireApi.h"

#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace bcs {

namespace {

void processThunk(void* self) { static_cast<PipeWireMicBackend*>(self)->handleProcess(); }

void stateThunk(void* self, pw_stream_state, pw_stream_state state, const char* error)
{
    static_cast<PipeWireMicBackend*>(self)->handleStateChange(
        state == PW_STREAM_STATE_ERROR, state == PW_STREAM_STATE_UNCONNECTED, state == PW_STREAM_STATE_STREAMING, error);
}

pw_stream_events makeEvents()
{
    pw_stream_events ev{};
    ev.version = PW_VERSION_STREAM_EVENTS;
    ev.state_changed = stateThunk;
    ev.process = processThunk;
    return ev;
}

const pw_stream_events kEvents = makeEvents();

std::string fallbackHelpHtml()
{
    return R"html(<p>The native backend needs a running <b>PipeWire</b> user session
(default on Fedora 34+ and most current distributions). Check with
<code>systemctl --user status pipewire wireplumber</code>.</p>
<p>On systems with plain PulseAudio, use the <b>fallback</b> backend instead:
create a null sink plus a remapped source yourself (temporary, until logout):</p>
<pre>pactl load-module module-null-sink sink_name=bcs_sink sink_properties=device.description=BadConnectionSink
pactl load-module module-remap-source master=bcs_sink.monitor source_name=bcs_mic source_properties=device.description=BadConnectionMic</pre>
<p>Then choose <i>"Play into: BadConnectionSink"</i> as the virtual microphone
destination, and select <i>BadConnectionMic</i> in the call app.
Remove with <code>pactl unload-module module-remap-source</code> and
<code>pactl unload-module module-null-sink</code>.</p>)html";
}

} // namespace

PipeWireMicBackend::PipeWireMicBackend() = default;

PipeWireMicBackend::~PipeWireMicBackend() { close(); }

BackendStatus PipeWireMicBackend::probe()
{
    BackendStatus st;
    st.backendId = "pipewire";
    st.displayName = "PipeWire virtual source (native)";
    st.setupHelp = fallbackHelpHtml();
    const auto& a = pw::api();
    if (!a.loaded) {
        st.summary = a.error;
        return st;
    }
    // Connect once to see whether a PipeWire daemon is reachable.
    pw_thread_loop* loop = a.thread_loop_new("bcs-pw-probe", nullptr);
    bool ok = false;
    if (loop) {
        a.thread_loop_lock(loop);
        pw_context* ctx = a.context_new(a.thread_loop_get_loop(loop), nullptr, 0);
        if (ctx) {
            pw_core* core = a.context_connect(ctx, nullptr, 0);
            if (core) {
                ok = true;
                a.core_disconnect(core);
            }
            a.context_destroy(ctx);
        }
        a.thread_loop_unlock(loop);
        a.thread_loop_destroy(loop);
    }
    if (!ok) {
        st.summary = "PipeWire library found, but no PipeWire daemon is running for this user";
        return st;
    }
    st.available = true;
    st.summary = std::string("PipeWire ") + a.get_library_version() + " — will create \"" + kNodeDescription + "\"";
    st.devices.push_back({kDeviceId, std::string(kNodeDescription) + " (new PipeWire source)", true, {}});
    return st;
}

MediaError PipeWireMicBackend::open(const std::string&, int sampleRate, int channels)
{
    close();
    const auto& a = pw::api();
    if (!a.loaded)
        return MediaError::make(ErrorKind::VirtualDeviceMissing, "PipeWire is not available.", a.error);

    m_rate = sampleRate;
    m_channels = std::clamp(channels, 1, 2);
    m_ring = std::make_unique<PcmRingBuffer>(m_channels, m_rate, 30, 90);
    m_streaming = false;
    {
        std::lock_guard lk(m_errMutex);
        m_asyncError = {};
    }

    m_loop = a.thread_loop_new("bcs-pipewire", nullptr);
    if (!m_loop)
        return MediaError::make(ErrorKind::VirtualDeviceError, "Could not start the PipeWire thread.");
    a.thread_loop_lock(m_loop);
    m_context = a.context_new(a.thread_loop_get_loop(m_loop), nullptr, 0);
    m_core = m_context ? a.context_connect(m_context, nullptr, 0) : nullptr;
    if (!m_core) {
        int e = errno;
        a.thread_loop_unlock(m_loop);
        close();
        return MediaError::make(ErrorKind::VirtualDeviceMissing, "Could not connect to the PipeWire daemon.",
                                std::strerror(e));
    }

    const std::string latency = std::to_string(m_rate / 100) + "/" + std::to_string(m_rate); // 10 ms quantum
    pw_properties* props = a.properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE, "Communication",
        PW_KEY_MEDIA_CLASS, "Audio/Source", // makes it a selectable microphone
        PW_KEY_NODE_NAME, kNodeName,
        PW_KEY_NODE_DESCRIPTION, kNodeDescription,
        PW_KEY_NODE_LATENCY, latency.c_str(),
        PW_KEY_PRIORITY_SESSION, "1", // never preferred as the default source
        PW_KEY_APP_NAME, "Bad Connection Studio",
        nullptr);
    m_stream = a.stream_new(m_core, kNodeDescription, props);
    if (!m_stream) {
        a.thread_loop_unlock(m_loop);
        close();
        return MediaError::make(ErrorKind::VirtualDeviceError, "Could not create the PipeWire source node.");
    }
    m_listener = std::make_unique<spa_hook>();
    std::memset(m_listener.get(), 0, sizeof(spa_hook));
    a.stream_add_listener(m_stream, m_listener.get(), &kEvents, this);

    uint8_t podBuf[1024];
    spa_pod_builder b;
    spa_pod_builder_init(&b, podBuf, sizeof(podBuf));
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_S16;
    info.rate = uint32_t(m_rate);
    info.channels = uint32_t(m_channels);
    if (m_channels == 1) {
        info.position[0] = SPA_AUDIO_CHANNEL_MONO;
    } else {
        info.position[0] = SPA_AUDIO_CHANNEL_FL;
        info.position[1] = SPA_AUDIO_CHANNEL_FR;
    }
    const spa_pod* params[1] = {spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info)};

    int r = a.stream_connect(m_stream, SPA_DIRECTION_OUTPUT, PW_ID_ANY,
                             pw_stream_flags(PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS), params, 1);
    a.thread_loop_unlock(m_loop);
    if (r < 0) {
        close();
        return MediaError::make(ErrorKind::VirtualDeviceError, "Could not publish the PipeWire source node.",
                                std::strerror(-r));
    }
    if (a.thread_loop_start(m_loop) < 0) {
        close();
        return MediaError::make(ErrorKind::VirtualDeviceError, "Could not start the PipeWire thread.");
    }
    return {};
}

void PipeWireMicBackend::handleProcess()
{
    const auto& a = pw::api();
    pw_buffer* pb = a.stream_dequeue_buffer(m_stream);
    if (!pb)
        return;
    spa_buffer* buf = pb->buffer;
    auto* dst = static_cast<int16_t*>(buf->datas[0].data);
    if (dst) {
        const uint32_t stride = uint32_t(sizeof(int16_t)) * uint32_t(m_channels);
        uint32_t frames = buf->datas[0].maxsize / stride;
        if (pb->requested)
            frames = std::min<uint32_t>(frames, uint32_t(pb->requested));
        m_ring->read(dst, frames);
        buf->datas[0].chunk->offset = 0;
        buf->datas[0].chunk->stride = int32_t(stride);
        buf->datas[0].chunk->size = frames * stride;
    }
    a.stream_queue_buffer(m_stream, pb);
}

void PipeWireMicBackend::handleStateChange(bool error, bool unconnected, bool streaming, const char* message)
{
    // PAUSED <-> STREAMING follows whether any application records from the
    // node; nothing to report, but the UI shows it.
    m_streaming = streaming;
    if (!error && !unconnected)
        return;
    std::lock_guard lk(m_errMutex);
    m_asyncError = MediaError::make(error ? ErrorKind::VirtualDeviceError : ErrorKind::DeviceLost,
                                    "The PipeWire virtual microphone was disconnected.", message ? message : "");
}

void PipeWireMicBackend::write(const int16_t* data, std::size_t frames)
{
    if (m_ring)
        m_ring->write(data, frames);
}

void PipeWireMicBackend::close()
{
    const auto& a = pw::api();
    if (!a.loaded)
        return;
    if (m_loop)
        a.thread_loop_stop(m_loop);
    if (m_stream) {
        a.stream_destroy(m_stream);
        m_stream = nullptr;
    }
    if (m_core) {
        a.core_disconnect(m_core);
        m_core = nullptr;
    }
    if (m_context) {
        a.context_destroy(m_context);
        m_context = nullptr;
    }
    if (m_loop) {
        a.thread_loop_destroy(m_loop);
        m_loop = nullptr;
    }
    m_listener.reset();
}

MediaError PipeWireMicBackend::takeAsyncError()
{
    std::lock_guard lk(m_errMutex);
    MediaError e = m_asyncError;
    m_asyncError = {};
    return e;
}

std::string PipeWireMicBackend::describe() const
{
    if (!m_stream)
        return "not open";
    return std::string("PipeWire node \"") + kNodeDescription + "\" (" + std::to_string(m_rate) + " Hz, " +
           (m_channels == 1 ? "mono" : "stereo") + ") \u2014 " +
           (m_streaming ? "an app is listening" : "idle, no app is using it yet");
}

} // namespace bcs
