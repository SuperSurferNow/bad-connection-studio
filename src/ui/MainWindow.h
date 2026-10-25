#pragma once

#include "app/Settings.h"
#include "backend/BackendRegistry.h"
#include "core/AudioPipeline.h"
#include "core/CameraEnumerator.h"
#include "core/VideoPipeline.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QTimer>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFileSystemWatcher;
class QLabel;
class QListWidget;
class QMediaDevices;
class QPushButton;
class QSlider;
class QSpinBox;
class QToolButton;

namespace bcs {

class LevelMeter;
class PowerMonitor;
class VideoPreview;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Launch-time capability check; shows Diagnostics if something is missing.
    void runStartupCheck();

    // Documentation/CI helper: start the test pattern, wait, save a PNG of
    // the window, quit. Uses no camera content.
    void captureScreenshot(const QString& path, int delayMs);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    // construction
    QWidget* buildDevicesBox();
    QWidget* buildVideoBox();
    QWidget* buildAudioBox();
    QWidget* buildButtons();
    QWidget* buildRightPane();
    void buildMenus();
    void connectSignals();
    void applySettingsToUi();

    // device lists
    void refreshCameras();
    void refreshMics();
    void refreshDestinations();
    void scheduleRescan();
    CapabilityReport probe();

    // config from UI
    VideoCodecConfig videoCodecFromUi() const;
    AudioCodecConfig audioCodecFromUi() const;
    VideoPipelineConfig videoConfigFromUi();
    AudioPipelineConfig audioConfigFromUi();
    BackendOptions backendOptions() const;
    void storeSettings();

    // actions
    void startVideo();
    void stopVideo();
    void startAudio();
    void stopAudio();
    void startBoth();
    void restoreAndStopAll();
    void setMonitoring(bool on);
    void onVideoSlider(int pos);
    void onAudioSlider(int pos);
    void onVideoAdvancedChanged();
    void onAudioAdvancedChanged();
    void onVideoInputChanged();
    void onAudioInputChanged();
    void onSleep();
    void onResume();
    void showDiagnostics();
    void showAbout();

    // feedback
    void onProblem(const QString& source, int kind, const QString& message, const QString& detail);
    void addMessage(const QString& text, bool error);
    void updateStats();
    void updateButtons();
    QImage placeholderImage(int w, int h) const;

    AppSettings m_settings;
    VideoPipeline m_video;
    AudioPipeline m_audio;
    std::vector<CameraDevice> m_cameras;
    bool m_resumeVideo = false;
    bool m_resumeAudio = false;
    bool m_populating = false;

    // devices
    QComboBox* m_cameraCombo = nullptr;
    QComboBox* m_micCombo = nullptr;
    QComboBox* m_vcamCombo = nullptr;
    QComboBox* m_vmicCombo = nullptr;
    QCheckBox* m_showVirtualInputs = nullptr;
    QCheckBox* m_showAllOutputs = nullptr;

    // video
    QSlider* m_videoSlider = nullptr;
    QLabel* m_videoLabel = nullptr;
    QLabel* m_videoActual = nullptr;
    QToolButton* m_videoAdvToggle = nullptr;
    QWidget* m_videoAdv = nullptr;
    QComboBox* m_videoCodec = nullptr;
    QComboBox* m_outputRes = nullptr;
    QComboBox* m_captureRes = nullptr;
    QComboBox* m_captureFormat = nullptr;
    QSpinBox* m_fps = nullptr;
    QDoubleSpinBox* m_keyframe = nullptr;
    QComboBox* m_preset = nullptr;
    QCheckBox* m_lowLatency = nullptr;
    QCheckBox* m_frameDrop = nullptr;
    QCheckBox* m_yuyv = nullptr;

    // audio
    QSlider* m_audioSlider = nullptr;
    QLabel* m_audioLabel = nullptr;
    QLabel* m_audioActual = nullptr;
    QToolButton* m_audioAdvToggle = nullptr;
    QWidget* m_audioAdv = nullptr;
    QComboBox* m_audioCodec = nullptr;
    QComboBox* m_channels = nullptr;
    QComboBox* m_sampleRate = nullptr;
    QCheckBox* m_speechMode = nullptr;

    // controls
    QPushButton* m_videoBtn = nullptr;
    QPushButton* m_audioBtn = nullptr;
    QPushButton* m_bothBtn = nullptr;
    QPushButton* m_restoreBtn = nullptr;

    // right pane
    VideoPreview* m_preview = nullptr;
    LevelMeter* m_inMeter = nullptr;
    LevelMeter* m_outMeter = nullptr;
    QCheckBox* m_monitor = nullptr;
    QLabel* m_monitorLabel = nullptr;
    QLabel* m_videoStatus = nullptr;
    QLabel* m_audioStatus = nullptr;
    QListWidget* m_messages = nullptr;
    QLabel* m_backendLabel = nullptr;

    QTimer m_statsTimer;
    QTimer m_videoDebounce;
    QTimer m_audioDebounce;
    QTimer m_rescanDebounce;
    QTimer m_monitorTimeout;
    QElapsedTimer m_monitorElapsed;
    QElapsedTimer m_statsClock;
    uint64_t m_prevVideoBytes = 0;
    uint64_t m_prevOutputFrames = 0;
    uint64_t m_prevAudioBytes = 0;
    double m_prevAudioSeconds = 0;
    double m_videoKbpsAvg = 0;
    double m_fpsAvg = 0;

    QMediaDevices* m_mediaDevices = nullptr;
    QFileSystemWatcher* m_devWatcher = nullptr;
    PowerMonitor* m_power = nullptr;
};

} // namespace bcs
