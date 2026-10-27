#include "backend/common/WindowOutputBackend.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QRasterWindow>

namespace bcs {

namespace {

constexpr const char* kDeviceId = "output-window";

bool haveGui() { return qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr; }

} // namespace

// Borderless, so a window capture contains only the video. Drag anywhere to
// move it. It is closed by the app, not by the user.
class OutputWindow final : public QRasterWindow {
public:
    OutputWindow()
    {
        setTitle(QString::fromUtf8(WindowOutputBackend::kWindowTitle));
        setFlags(Qt::Window | Qt::FramelessWindowHint);
    }

    void setImage(QImage img)
    {
        m_image = std::move(img);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(QRect(QPoint(), size()), Qt::black);
        if (m_image.isNull())
            return;
        // Scaled to fit, aspect kept, in case the window was resized.
        QSize s = m_image.size().scaled(size(), Qt::KeepAspectRatio);
        QRect r(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(r, m_image);
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton)
            startSystemMove();
    }

private:
    QImage m_image;
};

WindowOutputBackend::WindowOutputBackend() = default;

WindowOutputBackend::~WindowOutputBackend() { close(nullptr); }

std::string WindowOutputBackend::setupHelpHtml()
{
    return R"html(<p>The <b>output window</b> needs no driver: the degraded video is shown
in a borderless window titled <i>"Bad Connection Studio &mdash; Output"</i>, and
<b>OBS Studio</b> (free, <code>https://obsproject.com</code>) turns it into a
webcam.</p>
<ol><li>Choose <i>"Output window (for OBS)"</i> here and start the video.</li>
<li>In OBS, add a source: <i>Window Capture</i> (macOS: <i>macOS Screen
Capture</i> &rarr; Window) and pick that window.</li>
<li>Click <i>Start Virtual Camera</i> in OBS.</li>
<li>In the call app, choose <i>"OBS Virtual Camera"</i>.</li></ol>
<p>Keep the window open and not minimised (it may be behind other windows);
drag it to move it. It closes when the video stops.</p>)html";
}

BackendStatus WindowOutputBackend::probe()
{
    BackendStatus st;
    st.backendId = kId;
    st.displayName = "Output window (captured by OBS, no driver needed)";
    st.setupHelp = setupHelpHtml();
    // Needs nothing installed, so it is always available; only open() needs
    // a display (the headless self-test cannot show a window).
    st.available = true;
    st.summary = "ready (use OBS Window Capture + Virtual Camera)";
    VirtualDeviceInfo d;
    d.id = kDeviceId;
    d.name = "Output window (for OBS)";
#if !defined(__APPLE__)
    // A fallback here: never chosen automatically over the native camera.
    d.problem = "fallback, needs OBS";
#endif
    st.devices.push_back(d);
    return st;
}

namespace {

// GUI thread: makes the window match the requested state and shows the
// newest frame. Every change is funnelled through here.
template <typename S>
void syncWindow(const std::shared_ptr<S>& sh)
{
    std::unique_lock lk(sh->mutex);
    if (!sh->wantOpen) {
        OutputWindow* w = sh->window;
        sh->window = nullptr;
        sh->image = QImage();
        lk.unlock();
        delete w;
        return;
    }
    if (!sh->window) {
        sh->window = new OutputWindow;
        sh->window->resize(sh->size);
        sh->window->show();
    } else if (sh->window->size() != sh->size) {
        sh->window->resize(sh->size);
    }
    QImage next = std::move(sh->image);
    sh->image = QImage();
    sh->frameScheduled = false;
    OutputWindow* w = sh->window;
    lk.unlock();
    if (!next.isNull())
        w->setImage(std::move(next));
}

template <typename S>
void postSync(const std::shared_ptr<S>& sh)
{
    if (QCoreApplication* app = QCoreApplication::instance())
        QMetaObject::invokeMethod(app, [sh] { syncWindow(sh); }, Qt::QueuedConnection);
}

} // namespace

MediaError WindowOutputBackend::open(const std::string&, int width, int height, int)
{
    if (!haveGui())
        return MediaError::make(ErrorKind::VirtualDeviceMissing, "The output window needs the graphical app.");
    m_width = width;
    m_height = height;
    m_open = true;
    {
        std::lock_guard lk(m_shared->mutex);
        m_shared->wantOpen = true;
        m_shared->size = QSize(width, height);
    }
    postSync(m_shared);
    return {};
}

QImage WindowOutputBackend::convert(const AVFrame* f)
{
    if (!m_sws || m_swsW != f->width || m_swsH != f->height || m_swsFmt != f->format) {
        m_sws.reset(sws_getContext(f->width, f->height, AVPixelFormat(f->format), f->width, f->height,
                                   AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr));
        if (!m_sws)
            return {};
        m_swsW = f->width;
        m_swsH = f->height;
        m_swsFmt = f->format;
    }
    QImage img(f->width, f->height, QImage::Format_RGB888);
    uint8_t* dst[4] = {img.bits(), nullptr, nullptr, nullptr};
    int stride[4] = {int(img.bytesPerLine()), 0, 0, 0};
    sws_scale(m_sws.get(), f->data, f->linesize, 0, f->height, dst, stride);
    return img;
}

MediaError WindowOutputBackend::writeFrame(const AVFrame* f)
{
    if (!m_open)
        return MediaError::make(ErrorKind::VirtualDeviceError, "The output window is not open.");
    QImage img = convert(f);
    if (img.isNull())
        return MediaError::make(ErrorKind::UnsupportedFormat, "Cannot convert frames for the output window.");
    m_width = f->width;
    m_height = f->height;
    bool schedule = false;
    {
        std::lock_guard lk(m_shared->mutex);
        m_shared->size = QSize(f->width, f->height);
        m_shared->image = std::move(img);
        // A busy GUI thread just gets the newest image when it gets to it.
        schedule = !m_shared->frameScheduled;
        m_shared->frameScheduled = true;
    }
    if (schedule)
        postSync(m_shared);
    return {};
}

void WindowOutputBackend::close(const AVFrame*)
{
    // No placeholder: the window goes away, and OBS shows its own blank frame.
    if (!m_open)
        return;
    m_open = false;
    m_sws.reset();
    {
        std::lock_guard lk(m_shared->mutex);
        m_shared->wantOpen = false;
    }
    postSync(m_shared);
}

bool WindowOutputBackend::isOpen() const { return m_open; }

std::string WindowOutputBackend::describe() const
{
    if (!m_open)
        return "not open";
    return "output window (" + std::to_string(m_width) + "x" + std::to_string(m_height) + ", capture it in OBS)";
}

} // namespace bcs
