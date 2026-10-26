#include "ui/MainWindow.h"

#include "app/Logging.h"
#include "app/PowerMonitor.h"
#include "core/FfmpegUtil.h"
#include "core/QualityMapping.h"
#include "core/VideoCapture.h"
#include "ui/DiagnosticsDialog.h"
#include "ui/LevelMeter.h"
#if defined(BCS_PLATFORM_LINUX)
#include "ui/LoopbackSetupDialog.h"
#endif
#include "ui/VideoPreview.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFileSystemWatcher>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLoggingCategory>
#include <QMediaDevices>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY(lcUi, "bcs.ui")

namespace bcs {

namespace {

// Inputs that are (very likely) virtual devices fed by this app or a cable
// driver. Hidden from the physical microphone list by default: selecting one
// would route the degraded output back into the input (feedback loop).
bool isVirtualInput(const QString& d)
{
    static const QStringList hints = {"Bad Connection", "BadConnectionMic", "bcs_mic", "CABLE Output", "VB-Audio",
                                      "Voicemeeter Out", "Monitor of"};
    for (const auto& h : hints)
        if (d.contains(h, Qt::CaseInsensitive))
            return true;
    return false;
}

struct Res {
    int w, h;
    const char* label;
};
const Res kOutputRes[] = {{320, 180, "320 x 180"}, {640, 360, "640 x 360"},     {854, 480, "854 x 480"},
                          {1280, 720, "1280 x 720"}, {1920, 1080, "1920 x 1080"}};
const Res kCaptureRes[] = {{0, 0, "Match output"}, {640, 480, "640 x 480"}, {1280, 720, "1280 x 720"},
                           {1920, 1080, "1920 x 1080"}};

QString qs(const std::string& s) { return QString::fromStdString(s); }

QLabel* hint(const QString& text)
{
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() * 0.9);
    l->setFont(f);
    l->setStyleSheet("color: palette(mid);");
    return l;
}

// Sentinel stored for an explicit "None" destination; an empty value means
// "never chosen" and auto-selects the first usable destination.
const QString kNoDestination = QStringLiteral("none");

void selectData(QComboBox* c, const QVariant& v)
{
    if (!v.isValid() || (v.typeId() == QMetaType::QString && v.toString().isEmpty()))
        return;
    int i = c->findData(v);
    if (i >= 0)
        c->setCurrentIndex(i);
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(tr("Bad Connection Studio"));
    setWindowIcon(QIcon(":/bad-connection-studio.svg"));
    m_settings = AppSettings::load();

    auto* left = new QWidget;
    auto* leftLay = new QVBoxLayout(left);
    leftLay->addWidget(buildDevicesBox());
    leftLay->addWidget(buildVideoBox());
    leftLay->addWidget(buildAudioBox());
    leftLay->addWidget(buildButtons());
    leftLay->addStretch(1);
    auto* scroll = new QScrollArea;
    scroll->setWidget(left);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumWidth(left->sizeHint().width() + 24);

    auto* split = new QSplitter;
    split->addWidget(scroll);
    split->addWidget(buildRightPane());
    split->setStretchFactor(1, 1);
    setCentralWidget(split);

    m_backendLabel = new QLabel;
    statusBar()->addPermanentWidget(m_backendLabel);
    buildMenus();

    m_mediaDevices = new QMediaDevices(this);
#if defined(BCS_PLATFORM_LINUX)
    // udev creates/removes /dev/video* on hot-plug and module (re)load.
    m_devWatcher = new QFileSystemWatcher(QStringList{"/dev"}, this);
    connect(m_devWatcher, &QFileSystemWatcher::directoryChanged, this, &MainWindow::scheduleRescan);
#endif
    m_power = new PowerMonitor(this);

    m_populating = true;
    refreshCameras();
    refreshMics();
    refreshDestinations();
    applySettingsToUi();
    m_populating = false;
    connectSignals();
    updateButtons();
    resize(1180, 760);
}

MainWindow::~MainWindow()
{
    m_video.stop();
    m_audio.stop();
}

// ------------------------------------------------------------------ UI build

QWidget* MainWindow::buildDevicesBox()
{
    auto* box = new QGroupBox(tr("Devices"));
    auto* f = new QFormLayout(box);
    m_cameraCombo = new QComboBox;
    m_micCombo = new QComboBox;
    m_vcamCombo = new QComboBox;
    m_vmicCombo = new QComboBox;
    for (auto* c : {m_cameraCombo, m_micCombo, m_vcamCombo, m_vmicCombo}) {
        c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        c->setMinimumContentsLength(24);
    }
    f->addRow(tr("Physical camera"), m_cameraCombo);
    f->addRow(tr("Physical microphone"), m_micCombo);
    f->addRow(tr("Virtual camera"), m_vcamCombo);
    f->addRow(tr("Virtual microphone"), m_vmicCombo);
    m_showVirtualInputs = new QCheckBox(tr("List virtual devices as inputs (risk of feedback)"));
    m_showAllOutputs = new QCheckBox(tr("Offer all audio outputs as virtual-mic fallback"));
    f->addRow(m_showVirtualInputs);
    f->addRow(m_showAllOutputs);
    return box;
}

QWidget* MainWindow::buildVideoBox()
{
    auto* box = new QGroupBox(tr("Video quality"));
    auto* v = new QVBoxLayout(box);
    const auto& sc = videoBitrateScale();
    m_videoSlider = new QSlider(Qt::Horizontal);
    m_videoSlider->setRange(0, sc.sliderSteps);
    m_videoSlider->setPageStep(sc.sliderSteps / 10);
    m_videoLabel = new QLabel;
    QFont bold = m_videoLabel->font();
    bold.setBold(true);
    m_videoLabel->setFont(bold);
    m_videoActual = new QLabel(tr("measured: —"));
    auto* row = new QHBoxLayout;
    row->addWidget(m_videoLabel, 1);
    row->addWidget(m_videoActual);
    v->addLayout(row);
    v->addWidget(m_videoSlider);
    auto* ends = new QHBoxLayout;
    ends->addWidget(new QLabel(qs(formatKbps(sc.minKbps))));
    ends->addStretch(1);
    ends->addWidget(new QLabel(qs(formatKbps(sc.maxKbps))));
    v->addLayout(ends);
    v->addWidget(hint(tr("Sets the encoder's target bitrate. Resolution only changes if you change it below.")));

    m_videoAdvToggle = new QToolButton;
    m_videoAdvToggle->setText(tr("Advanced video settings"));
    m_videoAdvToggle->setCheckable(true);
    m_videoAdvToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_videoAdvToggle->setArrowType(Qt::RightArrow);
    m_videoAdvToggle->setAutoRaise(true);
    v->addWidget(m_videoAdvToggle);

    m_videoAdv = new QWidget;
    auto* f = new QFormLayout(m_videoAdv);
    m_videoCodec = new QComboBox;
    for (const auto& c : videoCodecChoices()) {
        m_videoCodec->addItem(qs(c.displayName) + (c.available ? "" : tr(" (not in this build)")), qs(c.encoder));
        int i = m_videoCodec->count() - 1;
        m_videoCodec->setItemData(i, qs(c.note), Qt::ToolTipRole);
        if (!c.available)
            m_videoCodec->setItemData(i, 0, Qt::UserRole - 1); // disable item
    }
    m_outputRes = new QComboBox;
    for (const auto& r : kOutputRes)
        m_outputRes->addItem(r.label, QSize(r.w, r.h));
    m_captureRes = new QComboBox;
    for (const auto& r : kCaptureRes)
        m_captureRes->addItem(r.label, QSize(r.w, r.h));
    m_captureFormat = new QComboBox;
    m_captureFormat->addItem(tr("Auto"), QString());
    m_captureFormat->addItem(tr("MJPEG"), QString("mjpeg"));
    m_captureFormat->addItem(tr("YUYV (raw)"), QString("yuyv422"));
    m_fps = new QSpinBox;
    m_fps->setRange(5, 60);
    m_fps->setSuffix(" fps");
    m_keyframe = new QDoubleSpinBox;
    m_keyframe->setRange(0.5, 20);
    m_keyframe->setSingleStep(0.5);
    m_keyframe->setSuffix(" s");
    m_preset = new QComboBox;
    m_preset->addItem(tr("Fastest (lowest CPU)"), int(EncoderPreset::Fastest));
    m_preset->addItem(tr("Fast"), int(EncoderPreset::Fast));
    m_preset->addItem(tr("Balanced"), int(EncoderPreset::Balanced));
    m_preset->addItem(tr("Quality (more CPU)"), int(EncoderPreset::Quality));
    m_lowLatency = new QCheckBox(tr("Low latency (no look-ahead, small rate buffer)"));
    m_frameDrop = new QCheckBox(tr("Allow encoder to skip frames when starved"));
    m_yuyv = new QCheckBox(tr("Virtual camera format YUYV (compatibility)"));
    f->addRow(tr("Codec"), m_videoCodec);
    f->addRow(tr("Output resolution"), m_outputRes);
    f->addRow(tr("Frame rate"), m_fps);
    f->addRow(tr("Keyframe interval"), m_keyframe);
    f->addRow(tr("Encoder preset"), m_preset);
    f->addRow(m_lowLatency);
    f->addRow(m_frameDrop);
    f->addRow(tr("Camera capture size"), m_captureRes);
    f->addRow(tr("Camera format"), m_captureFormat);
#if defined(BCS_PLATFORM_LINUX)
    f->addRow(m_yuyv);
#else
    m_yuyv->hide();
#endif
    m_videoAdv->setVisible(false);
    v->addWidget(m_videoAdv);
    return box;
}

QWidget* MainWindow::buildAudioBox()
{
    auto* box = new QGroupBox(tr("Microphone quality"));
    auto* v = new QVBoxLayout(box);
    const auto& sc = audioBitrateScale();
    m_audioSlider = new QSlider(Qt::Horizontal);
    m_audioSlider->setRange(0, sc.sliderSteps);
    m_audioSlider->setPageStep(sc.sliderSteps / 10);
    m_audioLabel = new QLabel;
    QFont bold = m_audioLabel->font();
    bold.setBold(true);
    m_audioLabel->setFont(bold);
    m_audioActual = new QLabel(tr("measured: —"));
    auto* row = new QHBoxLayout;
    row->addWidget(m_audioLabel, 1);
    row->addWidget(m_audioActual);
    v->addLayout(row);
    v->addWidget(m_audioSlider);
    auto* ends = new QHBoxLayout;
    ends->addWidget(new QLabel(qs(formatKbps(sc.minKbps))));
    ends->addStretch(1);
    ends->addWidget(new QLabel(qs(formatKbps(sc.maxKbps))));
    v->addLayout(ends);
    v->addWidget(hint(tr("Sets the speech codec's bitrate. No gain, noise or EQ effects are applied.")));

    m_audioAdvToggle = new QToolButton;
    m_audioAdvToggle->setText(tr("Advanced microphone settings"));
    m_audioAdvToggle->setCheckable(true);
    m_audioAdvToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_audioAdvToggle->setArrowType(Qt::RightArrow);
    m_audioAdvToggle->setAutoRaise(true);
    v->addWidget(m_audioAdvToggle);

    m_audioAdv = new QWidget;
    auto* f = new QFormLayout(m_audioAdv);
    m_audioCodec = new QComboBox;
    for (const auto& c : audioCodecChoices()) {
        m_audioCodec->addItem(qs(c.displayName) + (c.available ? "" : tr(" (not in this build)")), qs(c.encoder));
        int i = m_audioCodec->count() - 1;
        m_audioCodec->setItemData(i, qs(c.note), Qt::ToolTipRole);
        if (!c.available)
            m_audioCodec->setItemData(i, 0, Qt::UserRole - 1);
    }
    m_channels = new QComboBox;
    m_channels->addItem(tr("Mono"), 1);
    m_channels->addItem(tr("Stereo"), 2);
    m_sampleRate = new QComboBox;
    for (int r : {48000, 24000, 16000, 12000, 8000})
        m_sampleRate->addItem(QString("%1 Hz").arg(r), r);
    m_speechMode = new QCheckBox(tr("Opus speech (VoIP) mode"));
    f->addRow(tr("Codec"), m_audioCodec);
    f->addRow(tr("Channels"), m_channels);
    f->addRow(tr("Codec sample rate"), m_sampleRate);
    f->addRow(m_speechMode);
    m_audioAdv->setVisible(false);
    v->addWidget(m_audioAdv);
    return box;
}

QWidget* MainWindow::buildButtons()
{
    auto* w = new QWidget;
    auto* g = new QGridLayout(w);
    g->setContentsMargins(0, 0, 0, 0);
    m_videoBtn = new QPushButton;
    m_audioBtn = new QPushButton;
    m_bothBtn = new QPushButton(tr("Start both"));
    m_restoreBtn = new QPushButton(tr("Restore / Stop all"));
    m_restoreBtn->setToolTip(tr("Stops everything and releases the cameras, microphones and virtual devices."));
    m_bothBtn->setDefault(true);
    g->addWidget(m_videoBtn, 0, 0);
    g->addWidget(m_audioBtn, 0, 1);
    g->addWidget(m_bothBtn, 1, 0);
    g->addWidget(m_restoreBtn, 1, 1);
    return w;
}

QWidget* MainWindow::buildRightPane()
{
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    auto* previewBox = new QGroupBox(tr("Degraded output preview"));
    auto* pv = new QVBoxLayout(previewBox);
    m_preview = new VideoPreview;
    pv->addWidget(m_preview);
    v->addWidget(previewBox, 3);

    auto* audioBox = new QGroupBox(tr("Microphone levels"));
    auto* af = new QFormLayout(audioBox);
    m_inMeter = new LevelMeter;
    m_outMeter = new LevelMeter;
    af->addRow(tr("Physical input"), m_inMeter);
    af->addRow(tr("Degraded output"), m_outMeter);
    m_monitor = new QCheckBox(tr("Listen to the degraded mic for 30 s (use headphones)"));
    m_monitorLabel = new QLabel;
    auto* mrow = new QHBoxLayout;
    mrow->addWidget(m_monitor);
    mrow->addStretch(1);
    mrow->addWidget(m_monitorLabel);
    af->addRow(mrow);
    v->addWidget(audioBox);

    auto* statusBox = new QGroupBox(tr("Status"));
    auto* sl = new QHBoxLayout(statusBox);
    m_videoStatus = new QLabel;
    m_audioStatus = new QLabel;
    for (auto* l : {m_videoStatus, m_audioStatus}) {
        l->setTextFormat(Qt::RichText);
        l->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->setWordWrap(true);
        sl->addWidget(l, 1);
    }
    v->addWidget(statusBox, 1);

    auto* msgBox = new QGroupBox(tr("Messages"));
    auto* ml = new QVBoxLayout(msgBox);
    m_messages = new QListWidget;
    m_messages->setWordWrap(true);
    m_messages->setMinimumHeight(90);
    ml->addWidget(m_messages);
    v->addWidget(msgBox, 1);
    return w;
}

void MainWindow::buildMenus()
{
    auto* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("Restore / Stop all"), this, &MainWindow::restoreAndStopAll);
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);
    auto* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&Diagnostics and setup..."), this, &MainWindow::showDiagnostics);
#if defined(BCS_PLATFORM_LINUX)
    help->addAction(tr("Set up &virtual camera..."), this, &MainWindow::showLoopbackSetup);
#endif
    help->addAction(tr("Open log folder"), this,
                    [] { QDesktopServices::openUrl(QUrl::fromLocalFile(logDirectory())); });
    help->addAction(tr("&About"), this, &MainWindow::showAbout);
}

// ------------------------------------------------------------------ wiring

void MainWindow::connectSignals()
{
    connect(m_videoSlider, &QSlider::valueChanged, this, &MainWindow::onVideoSlider);
    connect(m_audioSlider, &QSlider::valueChanged, this, &MainWindow::onAudioSlider);

    // Debounce: dragging a slider should not restart a codec 60 times.
    m_videoDebounce.setSingleShot(true);
    m_videoDebounce.setInterval(120);
    connect(&m_videoDebounce, &QTimer::timeout, this,
            [this] { m_video.setBitrate(videoBitrateScale().kbpsAt(m_videoSlider->value())); });
    m_audioDebounce.setSingleShot(true);
    m_audioDebounce.setInterval(150);
    connect(&m_audioDebounce, &QTimer::timeout, this,
            [this] { m_audio.setBitrate(audioBitrateScale().kbpsAt(m_audioSlider->value())); });

    connect(m_videoAdvToggle, &QToolButton::toggled, this, [this](bool on) {
        m_videoAdv->setVisible(on);
        m_videoAdvToggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    });
    connect(m_audioAdvToggle, &QToolButton::toggled, this, [this](bool on) {
        m_audioAdv->setVisible(on);
        m_audioAdvToggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    });

    for (auto* c : {m_videoCodec, m_outputRes, m_preset})
        connect(c, &QComboBox::currentIndexChanged, this, &MainWindow::onVideoAdvancedChanged);
    connect(m_fps, &QSpinBox::valueChanged, this, &MainWindow::onVideoAdvancedChanged);
    connect(m_keyframe, &QDoubleSpinBox::valueChanged, this, &MainWindow::onVideoAdvancedChanged);
    connect(m_lowLatency, &QCheckBox::toggled, this, &MainWindow::onVideoAdvancedChanged);
    connect(m_frameDrop, &QCheckBox::toggled, this, &MainWindow::onVideoAdvancedChanged);
    for (auto* c : {m_cameraCombo, m_vcamCombo, m_captureRes, m_captureFormat})
        connect(c, &QComboBox::currentIndexChanged, this, &MainWindow::onVideoInputChanged);
    connect(m_yuyv, &QCheckBox::toggled, this, &MainWindow::onVideoInputChanged);

    for (auto* c : {m_audioCodec, m_channels, m_sampleRate})
        connect(c, &QComboBox::currentIndexChanged, this, &MainWindow::onAudioAdvancedChanged);
    connect(m_speechMode, &QCheckBox::toggled, this, &MainWindow::onAudioAdvancedChanged);
    for (auto* c : {m_micCombo, m_vmicCombo})
        connect(c, &QComboBox::currentIndexChanged, this, &MainWindow::onAudioInputChanged);

    connect(m_showVirtualInputs, &QCheckBox::toggled, this, [this] {
        refreshCameras();
        refreshMics();
    });
    connect(m_showAllOutputs, &QCheckBox::toggled, this, &MainWindow::refreshDestinations);

    connect(m_videoBtn, &QPushButton::clicked, this, [this] {
        if (m_video.state() == VideoPipeline::State::Stopped)
            startVideo();
        else
            stopVideo();
    });
    connect(m_audioBtn, &QPushButton::clicked, this, [this] {
        if (m_audio.state() == AudioPipeline::State::Stopped)
            startAudio();
        else
            stopAudio();
    });
    connect(m_bothBtn, &QPushButton::clicked, this, &MainWindow::startBoth);
    connect(m_restoreBtn, &QPushButton::clicked, this, &MainWindow::restoreAndStopAll);
    connect(m_monitor, &QCheckBox::toggled, this, &MainWindow::setMonitoring);

    connect(&m_video, &VideoPipeline::previewFrame, this, [this](const QImage& img) {
        m_preview->setImage(img);
        m_video.previewConsumed();
    });
    connect(&m_video, &VideoPipeline::problem, this,
            [this](int k, const QString& m, const QString& d) { onProblem(tr("Video"), k, m, d); });
    connect(&m_audio, &AudioPipeline::problem, this,
            [this](int k, const QString& m, const QString& d) { onProblem(tr("Microphone"), k, m, d); });
    connect(&m_video, &VideoPipeline::stateChanged, this, [this](VideoPipeline::State s, const QString& detail) {
        if (s == VideoPipeline::State::Failed)
            QTimer::singleShot(0, this, &MainWindow::stopVideo); // join worker threads
        if (s == VideoPipeline::State::Reconnecting)
            m_preview->clear(detail);
        if (s == VideoPipeline::State::Running)
            addMessage(tr("Video running."), false);
        updateButtons();
    });
    connect(&m_audio, &AudioPipeline::stateChanged, this, [this](AudioPipeline::State s, const QString&) {
        if (s == AudioPipeline::State::Running)
            addMessage(tr("Microphone running."), false);
        updateButtons();
    });

    connect(m_mediaDevices, &QMediaDevices::audioInputsChanged, this, [this] {
        refreshMics();
        m_audio.onAudioInputsChanged();
    });
    connect(m_mediaDevices, &QMediaDevices::audioOutputsChanged, this, &MainWindow::refreshDestinations);

    m_rescanDebounce.setSingleShot(true);
    m_rescanDebounce.setInterval(600);
    connect(&m_rescanDebounce, &QTimer::timeout, this, [this] {
        refreshCameras();
        refreshDestinations();
    });
    connect(m_power, &PowerMonitor::devicesChanged, this, &MainWindow::scheduleRescan);
    connect(m_power, &PowerMonitor::aboutToSleep, this, &MainWindow::onSleep);
    connect(m_power, &PowerMonitor::resumed, this, &MainWindow::onResume);

    m_monitorTimeout.setSingleShot(true);
    m_monitorTimeout.setInterval(30000);
    connect(&m_monitorTimeout, &QTimer::timeout, this, [this] { m_monitor->setChecked(false); });

    m_statsTimer.setInterval(250);
    connect(&m_statsTimer, &QTimer::timeout, this, &MainWindow::updateStats);
    m_statsTimer.start();
    m_statsClock.start();
}

void MainWindow::applySettingsToUi()
{
    const auto& s = m_settings;
    m_showVirtualInputs->setChecked(s.showVirtualInputs);
    m_showAllOutputs->setChecked(s.showAllAudioOutputs);
    m_yuyv->setChecked(s.v4l2Yuyv);
    selectData(m_cameraCombo, s.cameraId);
    selectData(m_micCombo, s.micId);
    selectData(m_vcamCombo, s.virtualCameraKey);
    selectData(m_vmicCombo, s.virtualMicKey);

    m_videoSlider->setValue(videoBitrateScale().sliderPosFor(s.video.bitrateKbps));
    onVideoSlider(m_videoSlider->value());
    selectData(m_videoCodec, qs(s.video.encoder));
    selectData(m_outputRes, QSize(s.video.width, s.video.height));
    selectData(m_captureRes, QSize(s.captureWidth == s.video.width && s.captureHeight == s.video.height ? 0 : s.captureWidth,
                                   s.captureWidth == s.video.width && s.captureHeight == s.video.height ? 0 : s.captureHeight));
    selectData(m_captureFormat, s.captureInputFormat);
    m_fps->setValue(s.video.fps);
    m_keyframe->setValue(s.video.keyframeIntervalSec);
    selectData(m_preset, int(s.video.preset));
    m_lowLatency->setChecked(s.video.lowLatency);
    m_frameDrop->setChecked(s.video.allowFrameDrop);

    m_audioSlider->setValue(audioBitrateScale().sliderPosFor(s.audio.bitrateKbps));
    onAudioSlider(m_audioSlider->value());
    selectData(m_audioCodec, qs(s.audio.encoder));
    selectData(m_channels, s.audio.channels);
    selectData(m_sampleRate, s.audio.sampleRate);
    m_speechMode->setChecked(s.audio.speechMode);
}

// ------------------------------------------------------------------ devices

void MainWindow::refreshCameras()
{
    const bool was = m_populating;
    m_populating = true;
    const QString current = m_cameraCombo->currentData().toString().isEmpty() ? m_settings.cameraId
                                                                              : m_cameraCombo->currentData().toString();
    m_cameras = enumerateCameras(m_showVirtualInputs && m_showVirtualInputs->isChecked());
    m_cameras.push_back(testPatternCamera());
    m_cameraCombo->clear();
    bool found = false;
    for (const auto& c : m_cameras) {
        QString label = qs(c.name);
        if (!c.problem.empty())
            label += " — " + qs(c.problem);
        m_cameraCombo->addItem(label, qs(c.id));
        found |= qs(c.id) == current;
    }
    if (!found && !current.isEmpty() && current != kTestPatternCameraId) {
        // Keep the saved camera selectable while it is unplugged so that a
        // running pipeline (or the next start) reconnects to the same device.
        CameraDevice gone;
        gone.id = current.toStdString();
        gone.url = gone.id;
#if defined(BCS_PLATFORM_LINUX)
        gone.format = "video4linux2";
#else
        gone.format = "dshow";
#endif
        gone.name = tr("Saved camera (not connected)").toStdString();
        m_cameras.insert(m_cameras.begin(), gone);
        m_cameraCombo->insertItem(0, qs(gone.name), current);
    }
    selectData(m_cameraCombo, current);
    m_populating = was;
}

void MainWindow::refreshMics()
{
    const bool was = m_populating;
    m_populating = true;
    const QString current =
        m_micCombo->currentData().toString().isEmpty() ? m_settings.micId : m_micCombo->currentData().toString();
    m_micCombo->clear();
    const bool showVirtual = m_showVirtualInputs->isChecked();
    QString defaultId;
    const QAudioDevice def = QMediaDevices::defaultAudioInput();
    for (const QAudioDevice& d : QMediaDevices::audioInputs()) {
        const bool virt = isVirtualInput(d.description());
        if (virt && !showVirtual)
            continue;
        m_micCombo->addItem(d.description() + (virt ? tr(" (virtual — feedback risk)") : QString()),
                            QString::fromUtf8(d.id()));
        if (d.id() == def.id() && !virt)
            defaultId = QString::fromUtf8(d.id());
    }
    if (m_micCombo->count() == 0)
        m_micCombo->addItem(tr("No microphone found"), QString());
    int idx = m_micCombo->findData(current);
    if (idx < 0)
        idx = m_micCombo->findData(defaultId);
    m_micCombo->setCurrentIndex(std::max(0, idx));
    m_populating = was;
}

CapabilityReport MainWindow::probe() { return probeBackends(backendOptions()); }

void MainWindow::refreshDestinations()
{
    const bool was = m_populating;
    m_populating = true;
    CapabilityReport rep = probe();

    auto fill = [](QComboBox* combo, const std::vector<BackendStatus>& list, const QString& saved, const QString& none) {
        const QString current = combo->currentData().toString().isEmpty() ? saved : combo->currentData().toString();
        combo->clear();
        int firstUsable = -1;
        for (const auto& b : list) {
            for (const auto& d : b.devices) {
                QString label = qs(d.name);
                if (!d.problem.empty())
                    label += " — " + qs(d.problem);
                combo->addItem(label, qs(makeDestinationKey(b.backendId, d.id)));
                int i = combo->count() - 1;
                combo->setItemData(i, qs(b.displayName), Qt::ToolTipRole);
                if (!d.usable)
                    combo->setItemData(i, 0, Qt::UserRole - 1);
                else if (firstUsable < 0 && d.problem.empty())
                    firstUsable = i;
            }
        }
        combo->addItem(none, kNoDestination);
        int idx = current.isEmpty() ? -1 : combo->findData(current);
        if (idx < 0 && current.isEmpty())
            idx = firstUsable;
        combo->setCurrentIndex(idx >= 0 ? idx : combo->count() - 1);
    };
    fill(m_vcamCombo, rep.cameras, m_settings.virtualCameraKey, tr("None — preview only"));
    fill(m_vmicCombo, rep.mics, m_settings.virtualMicKey, tr("None — meter / monitor only"));

    const BackendStatus* cam = rep.activeCamera();
    const BackendStatus* mic = rep.activeMic();
    m_backendLabel->setText(tr("Camera backend: %1  |  Mic backend: %2")
                                .arg(cam ? qs(cam->backendId) : tr("<unavailable>"),
                                     mic ? qs(mic->backendId) : tr("<unavailable>")));
    m_populating = was;
}

void MainWindow::scheduleRescan() { m_rescanDebounce.start(); }

void MainWindow::runStartupCheck()
{
    CapabilityReport rep = probe();
    QStringList missing;
    for (const auto& s : rep.cameras)
        addMessage(tr("Virtual camera backend %1: %2").arg(qs(s.backendId), qs(s.summary)), !s.available);
    for (const auto& s : rep.mics)
        addMessage(tr("Virtual microphone backend %1: %2").arg(qs(s.backendId), qs(s.summary)), !s.available);
    if (!rep.activeCamera())
        missing << tr("virtual camera");
    if (!rep.activeMic())
        missing << tr("virtual microphone");
    if (!missing.isEmpty()) {
        statusBar()->showMessage(tr("Unavailable: %1 — see Help > Diagnostics").arg(missing.join(", ")), 15000);
        if (offerLoopbackSetup())
            return;
        if (!m_settings.skipStartupDiagnostics)
            QTimer::singleShot(300, this, &MainWindow::showDiagnostics);
    }
}

bool MainWindow::offerLoopbackSetup()
{
#if defined(BCS_PLATFORM_LINUX)
    if (m_settings.skipLoopbackPrompt)
        return false;
    loopback::DistroInfo d;
    loopback::KernelInfo k;
    const loopback::State s = LoopbackSetupDialog::currentState(&d, &k);
    if (s == loopback::State::Ready)
        return false; // loaded: whatever is missing is not the driver
    QTimer::singleShot(300, this, [this, s, d, k] {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Information);
        box.setWindowTitle(tr("Virtual camera not set up"));
        box.setText(tr("<b>The virtual camera driver (v4l2loopback) is not ready.</b>"));
        box.setInformativeText(QString::fromStdString(loopback::describeState(s, d, k)) + "\n\n" +
                               tr("Bad Connection Studio can set it up for you. You will see every step before "
                                  "anything runs, and your password is asked by the system, not by this app."));
        auto* setup = box.addButton(s == loopback::State::NeedsReboot ? tr("Details...") : tr("Set up now..."),
                                    QMessageBox::AcceptRole);
        box.addButton(tr("Not now"), QMessageBox::RejectRole);
        auto* never = box.addButton(tr("Don't ask again"), QMessageBox::DestructiveRole);
        box.setDefaultButton(setup);
        box.exec();
        if (box.clickedButton() == setup) {
            showLoopbackSetup();
        } else if (box.clickedButton() == never) {
            m_settings.skipLoopbackPrompt = true;
            m_settings.save();
            addMessage(tr("Virtual camera setup is still available under Help > Set up virtual camera."), false);
        }
    });
    return true;
#else
    return false;
#endif
}

void MainWindow::showLoopbackSetup()
{
#if defined(BCS_PLATFORM_LINUX)
    LoopbackSetupDialog dlg(this);
    dlg.exec();
    refreshDestinations();
#endif
}

// ------------------------------------------------------------------ config

BackendOptions MainWindow::backendOptions() const
{
    BackendOptions o;
    o.v4l2Yuyv = m_yuyv && m_yuyv->isChecked();
    o.showAllAudioOutputs = m_showAllOutputs && m_showAllOutputs->isChecked();
    return o;
}

VideoCodecConfig MainWindow::videoCodecFromUi() const
{
    VideoCodecConfig c;
    c.encoder = m_videoCodec->currentData().toString().toStdString();
    QSize out = m_outputRes->currentData().toSize();
    c.width = out.width();
    c.height = out.height();
    c.fps = m_fps->value();
    c.bitrateKbps = videoBitrateScale().kbpsAt(m_videoSlider->value());
    c.keyframeIntervalSec = m_keyframe->value();
    c.preset = EncoderPreset(m_preset->currentData().toInt());
    c.lowLatency = m_lowLatency->isChecked();
    c.allowFrameDrop = m_frameDrop->isChecked();
    return c;
}

AudioCodecConfig MainWindow::audioCodecFromUi() const
{
    AudioCodecConfig c;
    c.encoder = m_audioCodec->currentData().toString().toStdString();
    c.bitrateKbps = audioBitrateScale().kbpsAt(m_audioSlider->value());
    c.channels = m_channels->currentData().toInt();
    c.sampleRate = m_sampleRate->currentData().toInt();
    c.speechMode = m_speechMode->isChecked();
    return c;
}

VideoPipelineConfig MainWindow::videoConfigFromUi()
{
    VideoPipelineConfig cfg;
    const QString camId = m_cameraCombo->currentData().toString();
    for (const auto& c : m_cameras)
        if (qs(c.id) == camId)
            cfg.camera = c;
    cfg.codec = videoCodecFromUi();
    QSize cap = m_captureRes->currentData().toSize();
    cfg.capture.width = cap.width() > 0 ? cap.width() : cfg.codec.width;
    cfg.capture.height = cap.height() > 0 ? cap.height() : cfg.codec.height;
    cfg.capture.fps = cfg.codec.fps;
    cfg.capture.inputFormat = m_captureFormat->currentData().toString().toStdString();
    std::string backend, device;
    if (splitDestinationKey(m_vcamCombo->currentData().toString().toStdString(), backend, device)) {
        cfg.cameraBackendId = backend;
        cfg.cameraDeviceId = device;
    }
    cfg.backendOptions = backendOptions();
    cfg.autoReconnect = m_settings.autoReconnect;
    cfg.placeholder = placeholderImage(cfg.codec.width, cfg.codec.height);
    return cfg;
}

AudioPipelineConfig MainWindow::audioConfigFromUi()
{
    AudioPipelineConfig cfg;
    const QByteArray id = m_micCombo->currentData().toString().toUtf8();
    for (const QAudioDevice& d : QMediaDevices::audioInputs())
        if (d.id() == id)
            cfg.input = d;
    cfg.codec = audioCodecFromUi();
    std::string backend, device;
    if (splitDestinationKey(m_vmicCombo->currentData().toString().toStdString(), backend, device)) {
        cfg.micBackendId = backend;
        cfg.micDeviceId = device;
    }
    cfg.backendOptions = backendOptions();
    cfg.autoReconnect = m_settings.autoReconnect;
    return cfg;
}

void MainWindow::storeSettings()
{
    auto& s = m_settings;
    s.cameraId = m_cameraCombo->currentData().toString();
    s.micId = m_micCombo->currentData().toString();
    s.virtualCameraKey = m_vcamCombo->currentData().toString();
    s.virtualMicKey = m_vmicCombo->currentData().toString();
    s.showVirtualInputs = m_showVirtualInputs->isChecked();
    s.showAllAudioOutputs = m_showAllOutputs->isChecked();
    s.v4l2Yuyv = m_yuyv->isChecked();
    s.video = videoCodecFromUi();
    QSize cap = m_captureRes->currentData().toSize();
    s.captureWidth = cap.width() > 0 ? cap.width() : s.video.width;
    s.captureHeight = cap.height() > 0 ? cap.height() : s.video.height;
    s.captureInputFormat = m_captureFormat->currentData().toString();
    s.audio = audioCodecFromUi();
    s.save();
}

QImage MainWindow::placeholderImage(int w, int h) const
{
    QImage img(w, h, QImage::Format_RGB888);
    img.fill(QColor(32, 34, 38));
    QPainter p(&img);
    p.setPen(QColor(200, 200, 205));
    QFont f = p.font();
    f.setPixelSize(std::max(12, h / 18));
    p.setFont(f);
    p.drawText(img.rect(), Qt::AlignCenter, tr("Bad Connection Studio\nvideo paused"));
    return img;
}

// ------------------------------------------------------------------ actions

void MainWindow::startVideo()
{
    if (m_cameraCombo->currentData().toString().isEmpty()) {
        addMessage(tr("Select a camera first."), true);
        return;
    }
    storeSettings();
    VideoPipelineConfig cfg = videoConfigFromUi();
    qCInfo(lcUi).noquote() << "starting video from" << qs(cfg.camera.name) << "to"
                           << (cfg.cameraBackendId.empty() ? QString("preview only") : qs(cfg.cameraDeviceId));
    m_preview->clear(tr("Opening camera..."));
    m_video.start(cfg);
    updateButtons();
}

void MainWindow::stopVideo()
{
    m_video.stop();
    m_preview->clear(tr("Video stopped"));
    updateButtons();
}

void MainWindow::startAudio()
{
    AudioPipelineConfig cfg = audioConfigFromUi();
    if (cfg.input.isNull()) {
        addMessage(tr("Select a physical microphone first."), true);
        return;
    }
    if (isVirtualInput(cfg.input.description())) {
        auto answer = QMessageBox::warning(
            this, tr("Possible feedback loop"),
            tr("'%1' looks like a virtual device. Using it as the input can feed the degraded output back into "
               "itself.\n\nStart anyway?")
                .arg(cfg.input.description()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }
    storeSettings();
    m_audio.start(cfg);
    updateButtons();
}

void MainWindow::stopAudio()
{
    m_monitor->setChecked(false);
    m_audio.stop();
    m_inMeter->reset();
    m_outMeter->reset();
    updateButtons();
}

void MainWindow::startBoth()
{
    if (m_video.state() == VideoPipeline::State::Stopped)
        startVideo();
    if (m_audio.state() == AudioPipeline::State::Stopped)
        startAudio();
}

void MainWindow::restoreAndStopAll()
{
    stopVideo();
    stopAudio();
    m_audio.setMonitoring(false, {});
    m_preview->clear(tr("All devices released"));
    addMessage(tr("Stopped. Physical and virtual devices were released."), false);
    qCInfo(lcUi) << "restore/stop: all devices released";
}

void MainWindow::setMonitoring(bool on)
{
    if (on) {
        MediaError e = m_audio.setMonitoring(true, QMediaDevices::defaultAudioOutput());
        if (e) {
            onProblem(tr("Monitor"), int(e.kind), qs(e.message), qs(e.detail));
            m_monitor->setChecked(false);
            return;
        }
        m_monitorElapsed.start();
        m_monitorTimeout.start();
    } else {
        m_audio.setMonitoring(false, {});
        m_monitorTimeout.stop();
        m_monitorLabel->clear();
    }
}

void MainWindow::onVideoSlider(int pos)
{
    const int kbps = videoBitrateScale().kbpsAt(pos);
    m_videoLabel->setText(qs(videoBitrateScale().label(kbps)));
    if (!m_populating)
        m_videoDebounce.start();
}

void MainWindow::onAudioSlider(int pos)
{
    const int kbps = audioBitrateScale().kbpsAt(pos);
    m_audioLabel->setText(qs(audioBitrateScale().label(kbps)));
    if (!m_populating)
        m_audioDebounce.start();
}

void MainWindow::onVideoAdvancedChanged()
{
    if (m_populating)
        return;
    if (m_video.state() != VideoPipeline::State::Stopped)
        m_video.setCodecConfig(videoCodecFromUi()); // restarts only the codec
}

void MainWindow::onVideoInputChanged()
{
    if (m_populating)
        return;
    if (m_video.state() != VideoPipeline::State::Stopped) {
        addMessage(tr("Video input/destination changed — restarting video."), false);
        startVideo();
    }
}

void MainWindow::onAudioAdvancedChanged()
{
    if (m_populating)
        return;
    if (m_audio.state() != AudioPipeline::State::Stopped)
        m_audio.setCodecConfig(audioCodecFromUi());
}

void MainWindow::onAudioInputChanged()
{
    if (m_populating)
        return;
    if (m_audio.state() != AudioPipeline::State::Stopped) {
        addMessage(tr("Microphone input/destination changed — restarting microphone."), false);
        startAudio();
    }
}

void MainWindow::onSleep()
{
    m_resumeVideo = m_video.state() != VideoPipeline::State::Stopped;
    m_resumeAudio = m_audio.state() != AudioPipeline::State::Stopped;
    if (m_resumeVideo || m_resumeAudio)
        addMessage(tr("System is going to sleep — releasing devices."), false);
    stopVideo();
    stopAudio();
}

void MainWindow::onResume()
{
    if (!m_resumeVideo && !m_resumeAudio)
        return;
    addMessage(tr("System resumed — restarting in 3 s."), false);
    // USB devices need a moment to re-enumerate after resume.
    QTimer::singleShot(3000, this, [this] {
        refreshCameras();
        refreshMics();
        refreshDestinations();
        if (m_resumeVideo)
            startVideo();
        if (m_resumeAudio)
            startAudio();
        m_resumeVideo = m_resumeAudio = false;
    });
}

void MainWindow::showDiagnostics()
{
    DiagnosticsDialog dlg([this] { return probe(); }, m_settings.skipStartupDiagnostics, this);
#if defined(BCS_PLATFORM_LINUX)
    dlg.setSetupAction(tr("Set up virtual camera..."), [this] { showLoopbackSetup(); });
#endif
    dlg.exec();
    m_settings.skipStartupDiagnostics = dlg.skipAtStartup();
    m_settings.save();
    refreshDestinations();
}

void MainWindow::showAbout()
{
    const FfmpegBuildInfo ff = ffmpegBuildInfo();
    QMessageBox::about(
        this, tr("About Bad Connection Studio"),
        tr("<h3>Bad Connection Studio %1</h3>"
           "<p>Degrades a live camera and microphone by encoding them at a chosen low bitrate, decoding them "
           "in-process, and publishing the result as virtual devices.</p>"
           "<p>Qt %2 &mdash; FFmpeg libavcodec %3, license: <b>%4</b>%5</p>"
           "<p>No camera or microphone content is ever stored. See LICENSES.md for bundled components.</p>")
            .arg(BCS_VERSION, qVersion(), qs(ff.avcodecVersion), qs(ff.license),
                 ff.gpl ? tr(" (GPL build)") : QString()));
}

// ------------------------------------------------------------------ feedback

void MainWindow::onProblem(const QString& source, int kind, const QString& message, const QString& detail)
{
    const auto k = ErrorKind(kind);
    QString text = QString("%1 — %2: %3").arg(source, QLatin1String(errorKindTitle(k)), message);
    const QString h = QString::fromLatin1(errorKindHint(k));
    if (!h.isEmpty())
        text += "\n" + h;
    if (!detail.isEmpty())
        text += "\n(" + detail + ")";
    addMessage(text, true);
    statusBar()->showMessage(source + ": " + message, 10000);
}

void MainWindow::addMessage(const QString& text, bool error)
{
    auto* item = new QListWidgetItem(QTime::currentTime().toString("HH:mm:ss") + "  " + text);
    if (error)
        item->setForeground(QColor(200, 60, 50));
    m_messages->insertItem(0, item);
    while (m_messages->count() > 200)
        delete m_messages->takeItem(m_messages->count() - 1);
}

void MainWindow::updateButtons()
{
    const bool v = m_video.state() != VideoPipeline::State::Stopped;
    const bool a = m_audio.state() != AudioPipeline::State::Stopped;
    m_videoBtn->setText(v ? tr("Stop video") : tr("Start video"));
    m_audioBtn->setText(a ? tr("Stop microphone") : tr("Start microphone"));
    m_bothBtn->setEnabled(!(v && a));
}

void MainWindow::updateStats()
{
    const double dt = std::max(0.05, double(m_statsClock.restart()) / 1000.0);

    // ---- video
    VideoStats vs = m_video.stats();
    const auto vstate = m_video.state();
    if (vstate != VideoPipeline::State::Stopped) {
        const double kbps = double(vs.encodedBytes - std::min(vs.encodedBytes, m_prevVideoBytes)) * 8 / 1000 / dt;
        const double fps = double(vs.output - std::min(vs.output, m_prevOutputFrames)) / dt;
        m_videoKbpsAvg = m_videoKbpsAvg <= 0 ? kbps : m_videoKbpsAvg * 0.8 + kbps * 0.2;
        m_fpsAvg = m_fpsAvg <= 0 ? fps : m_fpsAvg * 0.8 + fps * 0.2;
        m_videoActual->setText(tr("measured: %1").arg(qs(formatKbps(m_videoKbpsAvg))));
    } else {
        m_videoKbpsAvg = m_fpsAvg = 0;
        m_videoActual->setText(tr("measured: —"));
    }
    m_prevVideoBytes = vs.encodedBytes;
    m_prevOutputFrames = vs.output;

    static const char* vnames[] = {"Stopped", "Starting", "Running", "Reconnecting", "Failed"};
    QString v = "<b>" + tr("Video") + "</b>: " + vnames[int(vstate)];
    if (vstate != VideoPipeline::State::Stopped) {
        v += "<br>" + tr("Capture: %1").arg(qs(vs.captureDescription).toHtmlEscaped());
        v += "<br>" + tr("Virtual camera: %1").arg(qs(vs.outputDescription).toHtmlEscaped());
        v += "<br>" + tr("Codec: %1, target %2").arg(qs(vs.codec), qs(formatKbps(vs.bitrateKbps)));
        v += "<br>" + tr("Output: %1 fps, measured %2").arg(m_fpsAvg, 0, 'f', 1).arg(qs(formatKbps(m_videoKbpsAvg)));
        v += "<br>" + tr("Dropped (late): %1 &nbsp; skipped by encoder: %2").arg(vs.droppedLate).arg(vs.skippedByEncoder);
        v += "<br>" + tr("Latency: %1 ms (encode %2 ms, decode %3 ms)")
                          .arg(vs.latencyMs, 0, 'f', 0)
                          .arg(vs.encodeMs, 0, 'f', 1)
                          .arg(vs.decodeMs, 0, 'f', 1);
    }
    m_videoStatus->setText(v);

    // ---- audio
    AudioStats as = m_audio.stats();
    const auto astate = m_audio.state();
    static const char* anames[] = {"Stopped", "Running", "Reconnecting", "Failed"};
    QString a = "<b>" + tr("Microphone") + "</b>: " + anames[int(astate)];
    if (astate != AudioPipeline::State::Stopped) {
        m_inMeter->pushPeakDb(as.inputPeakDb);
        m_outMeter->pushPeakDb(as.outputPeakDb);
        const double dsec = as.encodedSeconds - m_prevAudioSeconds;
        double akbps = 0;
        if (dsec > 0.05 && as.encodedBytes >= m_prevAudioBytes)
            akbps = double(as.encodedBytes - m_prevAudioBytes) * 8 / 1000 / dsec;
        if (akbps > 0)
            m_audioActual->setText(tr("measured: %1").arg(akbps, 0, 'f', 1) + " kbit/s");
        a += "<br>" + tr("Capture: %1").arg(qs(as.captureDescription).toHtmlEscaped());
        a += "<br>" + tr("Virtual mic: %1").arg(qs(as.outputDescription).toHtmlEscaped());
        a += "<br>" + tr("Codec: %1, %2 Hz, %3, target %4")
                          .arg(qs(as.effective.encoder))
                          .arg(as.effective.sampleRate)
                          .arg(as.effective.channels == 1 ? tr("mono") : tr("stereo"))
                          .arg(qs(formatKbps(as.effective.bitrateKbps)));
        if (!as.adjustments.empty())
            a += "<br><i>" + qs(as.adjustments).toHtmlEscaped() + "</i>";
        a += "<br>" + tr("Latency (est.): %1 ms (codec %2 ms, output buffer %3 ms)")
                          .arg(as.latencyMs, 0, 'f', 0)
                          .arg(as.codecDelayMs, 0, 'f', 0)
                          .arg(as.outputBufferMs, 0, 'f', 0);
    } else {
        m_audioActual->setText(tr("measured: —"));
    }
    m_prevAudioBytes = as.encodedBytes;
    m_prevAudioSeconds = as.encodedSeconds;
    m_audioStatus->setText(a);

    if (m_monitorTimeout.isActive())
        m_monitorLabel->setText(tr("%1 s left").arg(int((30000 - m_monitorElapsed.elapsed()) / 1000)));
}

void MainWindow::captureScreenshot(const QString& path, int delayMs)
{
    selectData(m_cameraCombo, QString(kTestPatternCameraId));
    m_videoAdvToggle->setChecked(true);
    startBoth();
    QTimer::singleShot(delayMs, this, [this, path] {
        updateStats();
        grab().save(path);
        qCInfo(lcUi) << "screenshot saved to" << path;
        close();
        QCoreApplication::quit();
    });
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    storeSettings();
    restoreAndStopAll();
    e->accept();
}

} // namespace bcs
