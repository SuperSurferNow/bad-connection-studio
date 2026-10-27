# ------------------------------------------------------------------ bcs_core
# Codec round-trip, pipelines, capture. Shared by every platform.
add_library(bcs_core STATIC
    core/Errors.h core/Errors.cpp
    core/FfmpegUtil.h core/FfmpegUtil.cpp
    core/BoundedQueue.h
    core/PcmRingBuffer.h core/PcmRingBuffer.cpp
    core/QualityMapping.h core/QualityMapping.cpp
    core/VideoCodecRoundTrip.h core/VideoCodecRoundTrip.cpp
    core/AudioCodecRoundTrip.h core/AudioCodecRoundTrip.cpp
    core/VideoCapture.h core/VideoCapture.cpp
    core/VideoPipeline.h core/VideoPipeline.cpp
    core/AudioPipeline.h core/AudioPipeline.cpp
    core/AudioSinkWorker.h core/AudioSinkWorker.cpp
    core/CameraEnumerator.h core/CameraEnumerator.cpp
    core/SelfTest.h core/SelfTest.cpp
    core/SyntheticSource.h core/SyntheticSource.cpp
    core/LoopClip.h core/LoopClip.cpp
    backend/VirtualCameraBackend.h
    backend/VirtualMicBackend.h
    backend/BackendRegistry.h backend/BackendRegistry.cpp
    backend/common/AudioDeviceMicBackend.h backend/common/AudioDeviceMicBackend.cpp
    backend/common/WindowOutputBackend.h backend/common/WindowOutputBackend.cpp
)
target_include_directories(bcs_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bcs_core PUBLIC Qt6::Core Qt6::Gui Qt6::Multimedia PkgConfig::FFMPEG)
target_compile_definitions(bcs_core PUBLIC
    BCS_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(bcs_core PRIVATE /W4)
else()
    target_compile_options(bcs_core PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

if(BCS_LINUX)
    target_sources(bcs_core PRIVATE
        backend/linux/V4L2Devices.h backend/linux/V4L2Devices.cpp
        backend/linux/V4L2LoopbackCamera.h backend/linux/V4L2LoopbackCamera.cpp
        backend/linux/LoopbackSetup.h backend/linux/LoopbackSetup.cpp)
    target_compile_definitions(bcs_core PUBLIC BCS_PLATFORM_LINUX=1)
    if(BCS_WITH_PIPEWIRE)
        target_sources(bcs_core PRIVATE
            backend/linux/PipeWireApi.h backend/linux/PipeWireApi.cpp
            backend/linux/PipeWireMicBackend.h backend/linux/PipeWireMicBackend.cpp)
        # Headers only: libpipewire is dlopen()ed at runtime.
        target_include_directories(bcs_core PRIVATE ${PIPEWIRE_HEADERS_INCLUDE_DIRS})
        target_compile_options(bcs_core PRIVATE ${PIPEWIRE_HEADERS_CFLAGS_OTHER})
        target_compile_definitions(bcs_core PUBLIC BCS_WITH_PIPEWIRE=1)
        target_link_libraries(bcs_core PRIVATE ${CMAKE_DL_LIBS})
    endif()
elseif(BCS_WINDOWS)
    target_sources(bcs_core PRIVATE
        backend/windows/SoftcamCameraBackend.h backend/windows/SoftcamCameraBackend.cpp)
    target_compile_definitions(bcs_core PUBLIC BCS_PLATFORM_WINDOWS=1 NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_link_libraries(bcs_core PRIVATE advapi32)
endif()

# ------------------------------------------------------------------ app
qt_add_executable(bad-connection-studio WIN32
    main.cpp
    app/Logging.h app/Logging.cpp
    app/Settings.h app/Settings.cpp
    app/PowerMonitor.h app/PowerMonitor.cpp
    ui/MainWindow.h ui/MainWindow.cpp
    ui/VideoPreview.h ui/VideoPreview.cpp
    ui/LevelMeter.h ui/LevelMeter.cpp
    ui/DiagnosticsDialog.h ui/DiagnosticsDialog.cpp
    ui/Diagnostics.h ui/Diagnostics.cpp
)
qt_add_resources(bad-connection-studio "resources"
    PREFIX "/"
    BASE ${PROJECT_SOURCE_DIR}/resources
    FILES ${PROJECT_SOURCE_DIR}/resources/bad-connection-studio.svg)

target_link_libraries(bad-connection-studio PRIVATE bcs_core Qt6::Widgets)
if(TARGET Qt6::DBus)
    target_link_libraries(bad-connection-studio PRIVATE Qt6::DBus)
    target_compile_definitions(bad-connection-studio PRIVATE BCS_HAVE_DBUS=1)
endif()
if(BCS_WINDOWS)
    target_sources(bad-connection-studio PRIVATE ${PROJECT_SOURCE_DIR}/packaging/windows/app.rc)
endif()
if(BCS_LINUX)
    target_sources(bad-connection-studio PRIVATE ui/LoopbackSetupDialog.h ui/LoopbackSetupDialog.cpp)
    # The root helper is embedded and piped to pkexec; never installed as a file.
    qt_add_resources(bad-connection-studio "linux_helper"
        PREFIX "/linux"
        BASE ${PROJECT_SOURCE_DIR}/packaging/linux
        FILES ${PROJECT_SOURCE_DIR}/packaging/linux/bcs-loopback-helper.sh)
endif()

if(BCS_WINDOWS)
    # MSYS2/MinGW: FFmpeg, codec and runtime DLLs live next to the compiler.
    get_filename_component(BCS_TOOLCHAIN_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    # Qt libraries + plugins via windeployqt, and every non-system DLL the
    # executable needs (FFmpeg, codec libraries, MinGW runtime).
    # RUNTIME_DEPENDENCIES must directly follow the target list.
    install(TARGETS bad-connection-studio
        RUNTIME_DEPENDENCIES
            PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*" "^Qt6.*"
            POST_EXCLUDE_REGEXES ".*[Ss]ystem32/.*\\.dll"
            DIRECTORIES "${BCS_TOOLCHAIN_BIN}" ${BCS_RUNTIME_DLL_DIRS}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    qt_generate_deploy_app_script(TARGET bad-connection-studio
        OUTPUT_SCRIPT bcs_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${bcs_deploy_script})
else()
    install(TARGETS bad-connection-studio
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
