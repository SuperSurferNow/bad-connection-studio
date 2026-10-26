#include "ui/Diagnostics.h"

#include "core/CameraEnumerator.h"
#include "core/FfmpegUtil.h"

#if defined(BCS_PLATFORM_LINUX)
#include "backend/linux/V4L2Devices.h"
#endif

#include <QAudioDevice>
#include <QMediaDevices>
#include <QTextDocument>

namespace bcs {

namespace {

QString esc(const std::string& s) { return QString::fromStdString(s).toHtmlEscaped(); }

QString badge(bool ok)
{
    return ok ? QStringLiteral("<span style='color:#2e9d4f'>&#10004; ready</span>")
              : QStringLiteral("<span style='color:#c0392b'>&#10008; unavailable</span>");
}

void backendSection(QString& h, const char* title, const std::vector<BackendStatus>& list)
{
    h += QStringLiteral("<h3>%1</h3>").arg(QLatin1String(title));
    if (list.empty()) {
        h += "<p>No backend is implemented for this platform in this build.</p>";
        return;
    }
    for (const auto& s : list) {
        h += QStringLiteral("<p><b>%1</b> &mdash; %2<br>%3</p>").arg(esc(s.displayName), badge(s.available), esc(s.summary));
        if (!s.devices.empty()) {
            h += "<ul>";
            for (const auto& d : s.devices)
                h += QStringLiteral("<li>%1%2</li>")
                         .arg(esc(d.name), d.problem.empty() ? QString() : " <i>(" + esc(d.problem) + ")</i>");
            h += "</ul>";
        }
        if (!s.available)
            h += "<blockquote>" + QString::fromStdString(s.setupHelp) + "</blockquote>";
        else
            h += "<p><i>Setup notes:</i></p><blockquote>" + QString::fromStdString(s.setupHelp) + "</blockquote>";
    }
}

} // namespace

bool runningFromAppImage() { return qEnvironmentVariableIsSet("APPIMAGE"); }

QString buildDiagnosticsHtml(const CapabilityReport& r)
{
    QString h;
    h += "<h2>Bad Connection Studio diagnostics</h2>";
    h += QStringLiteral("<p><b>Version:</b> %1<br><b>System:</b> %2</p>").arg(QLatin1String(BCS_VERSION), esc(r.platform));

    if (runningFromAppImage())
        h += "<p style='background:#fff4d6;color:#000;padding:6px'><b>AppImage:</b> this package bundles Qt, FFmpeg and "
             "codec libraries only. It does <b>not</b> include kernel modules (v4l2loopback), PipeWire, system "
             "services or drivers; those are OS-level prerequisites listed below. Use <b>Set up virtual "
             "camera</b> to install v4l2loopback with your confirmation.</p>";
#if defined(_WIN32)
    h += "<p style='background:#fff4d6;color:#000;padding:6px'>The installer bundles Qt and FFmpeg. Virtual camera and "
         "microphone components are <b>separate</b> installs (see below); they are never installed silently.</p>";
#endif

    backendSection(h, "Virtual camera", r.cameras);
    backendSection(h, "Virtual microphone", r.mics);

    h += "<h3>Physical cameras</h3><ul>";
    auto cams = enumerateCameras(false);
    if (cams.empty())
        h += "<li>None found. Connect a camera, or use the generated test pattern.</li>";
    for (const auto& c : cams)
        h += QStringLiteral("<li>%1 <code>%2</code>%3</li>")
                 .arg(esc(c.name), esc(c.url), c.problem.empty() ? QString() : " &mdash; <b>" + esc(c.problem) + "</b>");
    h += "</ul>";

#if defined(BCS_PLATFORM_LINUX)
    bool video = v4l2::userInGroup("video");
    h += QStringLiteral("<p>Member of group <code>video</code>: %1 (not needed when logind grants access to local "
                        "sessions).</p>")
             .arg(video ? "yes" : "no");
#endif

    h += "<h3>Microphones</h3><ul>";
    for (const QAudioDevice& d : QMediaDevices::audioInputs())
        h += "<li>" + d.description().toHtmlEscaped() + "</li>";
    h += "</ul>";

    const FfmpegBuildInfo ff = ffmpegBuildInfo();
    h += "<h3>Codecs (bundled FFmpeg)</h3>";
    h += QStringLiteral("<p>libavcodec %1 &mdash; license <b>%2</b>%3</p>")
             .arg(esc(ff.avcodecVersion), esc(ff.license),
                  ff.gpl ? QStringLiteral(" (GPL build: the distributed package is covered by the GPL)") : QString());
    h += "<ul>";
    for (const auto& c : videoCodecChoices())
        h += QStringLiteral("<li>Video %1: %2 &mdash; <i>%3</i></li>")
                 .arg(esc(c.displayName), c.available ? "available" : "not in this build", esc(c.note));
    for (const auto& c : audioCodecChoices())
        h += QStringLiteral("<li>Audio %1: %2 &mdash; <i>%3</i></li>")
                 .arg(esc(c.displayName), c.available ? "available" : "not in this build", esc(c.note));
    h += "</ul>";
    return h;
}

QString buildDiagnosticsText(const CapabilityReport& r)
{
    QTextDocument doc;
    doc.setHtml(buildDiagnosticsHtml(r));
    return doc.toPlainText();
}

} // namespace bcs
