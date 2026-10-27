#include "backend/common/AudioDeviceMicBackend.h"

#include <QMediaDevices>
#include <QStringList>

namespace bcs {

AudioDeviceMicBackend::AudioDeviceMicBackend() = default;

AudioDeviceMicBackend::~AudioDeviceMicBackend() { close(); }

bool AudioDeviceMicBackend::looksVirtual(const QString& d)
{
    static const QStringList hints = {"CABLE Input", "VB-Audio", "Voicemeeter", "Virtual", "Null",
                                      "BadConnection", "bcs_sink", "Loopback", "BlackHole"};
    for (const auto& h : hints)
        if (d.contains(h, Qt::CaseInsensitive))
            return true;
    return false;
}

std::string AudioDeviceMicBackend::setupHelpHtml()
{
    // Both texts are always compiled (raw strings inside a skipped #if
    // block confuse the preprocessor's line-based lexing).
    static constexpr const char* kWindows = R"html(<p>Windows has no user-mode API for creating a microphone; a virtual
audio <b>driver</b> is required. Bad Connection Studio does not ship or install
one. Recommended: <b>VB-Audio Virtual Cable</b> (free for end users,
donationware, signed by its vendor), downloaded from
<code>https://vb-audio.com/Cable/</code> and installed by you.</p>
<ol><li>Install VB-CABLE and reboot if asked.</li>
<li>Here, choose <i>"Play into: CABLE Input (VB-Audio Virtual Cable)"</i> as the
virtual microphone destination.</li>
<li>In the call app, select <i>"CABLE Output (VB-Audio Virtual Cable)"</i> as the
microphone.</li></ol>
<p>Uninstall it from <i>Settings &gt; Apps</i> or with the VB-CABLE setup
program. Your physical microphone is never changed. "CABLE Output" is hidden
from the physical microphone list to prevent a feedback loop.</p>)html";
    static constexpr const char* kLinux = R"html(<p>Fallback for systems without native PipeWire: play into a null sink and
expose its monitor as a source. These commands load temporary modules (until
logout); run them yourself:</p>
<pre>pactl load-module module-null-sink sink_name=bcs_sink sink_properties=device.description=BadConnectionSink
pactl load-module module-remap-source master=bcs_sink.monitor source_name=bcs_mic source_properties=device.description=BadConnectionMic</pre>
<p>Then pick <i>"Play into: BadConnectionSink"</i> here and <i>BadConnectionMic</i>
in the call app.</p>)html";
    static constexpr const char* kMac = R"html(<p>macOS has no API for an app to create a microphone; a virtual audio
<b>driver</b> is needed. Bad Connection Studio does not ship or install one.
Recommended: <b>BlackHole 2ch</b> (free, GPL-3.0) from
<code>https://existential.audio/blackhole/</code>, or
<code>brew install blackhole-2ch</code>.</p>
<ol><li>Install BlackHole 2ch (it asks for your password).</li>
<li>Here, choose <i>"Play into: BlackHole 2ch"</i> as the virtual microphone.</li>
<li>In the call app, select <i>"BlackHole 2ch"</i> as the microphone.</li></ol>
<p>Your physical microphone is never changed. BlackHole is hidden from the
microphone list here to prevent a feedback loop.</p>)html";
#if defined(_WIN32)
    (void)kLinux;
    (void)kMac;
    return kWindows;
#elif defined(__APPLE__)
    (void)kLinux;
    (void)kWindows;
    return kMac;
#else
    (void)kWindows;
    (void)kMac;
    return kLinux;
#endif
}

BackendStatus AudioDeviceMicBackend::probe()
{
    BackendStatus st;
    st.backendId = "audio-output";
#if defined(_WIN32)
    st.displayName = "Virtual audio cable (VB-CABLE or similar, external driver)";
#elif defined(__APPLE__)
    st.displayName = "Virtual audio driver (BlackHole or similar, external driver)";
#else
    st.displayName = "Fallback: play into a null sink (PulseAudio-compatible layer)";
#endif
    st.setupHelp = setupHelpHtml();
    int virtualCount = 0;
    for (const QAudioDevice& d : QMediaDevices::audioOutputs()) {
        bool isVirtual = looksVirtual(d.description());
        virtualCount += isVirtual;
        if (!isVirtual && !m_showAll)
            continue;
        VirtualDeviceInfo info;
        info.id = std::string(kIdPrefix) + d.id().toStdString();
        info.name = "Play into: " + d.description().toStdString();
        if (!isVirtual)
            info.problem = "not a virtual device — others will NOT hear this as a microphone";
        st.devices.push_back(info);
    }
    st.available = virtualCount > 0;
    if (virtualCount)
        st.summary = std::to_string(virtualCount) + " virtual audio output(s) found";
    else
#if defined(_WIN32)
        st.summary = "No virtual audio cable found (install VB-CABLE to get a virtual microphone)";
#elif defined(__APPLE__)
        st.summary = "No virtual audio driver found (install BlackHole 2ch to get a virtual microphone)";
#else
        st.summary = "No null sink / virtual output found";
#endif
    return st;
}

MediaError AudioDeviceMicBackend::open(const std::string& deviceId, int sampleRate, int channels)
{
    const std::string want = deviceId.rfind(kIdPrefix, 0) == 0 ? deviceId.substr(std::string(kIdPrefix).size()) : deviceId;
    for (const QAudioDevice& d : QMediaDevices::audioOutputs()) {
        if (d.id().toStdString() != want)
            continue;
        if (!m_worker) // created on demand: probe() must stay cheap
            m_worker = std::make_unique<AudioSinkWorker>("bcs-vmic-out");
        return m_worker->start(d, sampleRate, channels);
    }
    return MediaError::make(ErrorKind::VirtualDeviceMissing, "The selected virtual audio output is not present.");
}

void AudioDeviceMicBackend::write(const int16_t* data, std::size_t frames)
{
    if (m_worker)
        m_worker->write(data, frames);
}

void AudioDeviceMicBackend::close()
{
    if (m_worker)
        m_worker->stop();
}

bool AudioDeviceMicBackend::isOpen() const { return m_worker && m_worker->isRunning(); }

MediaError AudioDeviceMicBackend::takeAsyncError() { return m_worker ? m_worker->takeAsyncError() : MediaError{}; }

double AudioDeviceMicBackend::bufferedMs() const { return m_worker ? m_worker->bufferedMs() : 0; }

std::string AudioDeviceMicBackend::describe() const
{
    return isOpen() ? "playing into " + m_worker->deviceDescription().toStdString() : "not open";
}

} // namespace bcs
