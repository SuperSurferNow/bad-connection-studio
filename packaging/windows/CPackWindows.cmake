# Windows installer for the APPLICATION ONLY (Qt + FFmpeg runtime bundled).
# It never installs or registers a virtual camera/microphone component; see
# packaging/windows/virtual-camera/ for the separate, signed component.
set(CPACK_PACKAGE_NAME "Bad Connection Studio")
set(CPACK_PACKAGE_VENDOR "Bad Connection Studio contributors")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "BadConnectionStudio-${PROJECT_VERSION}-windows-x64-setup")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Bad Connection Studio")
set(CPACK_PACKAGE_EXECUTABLES "bad-connection-studio" "Bad Connection Studio")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/packaging/windows/installer-notice.txt")
set(CPACK_GENERATOR "NSIS")
set(CPACK_NSIS_DISPLAY_NAME "Bad Connection Studio")
set(CPACK_NSIS_PACKAGE_NAME "Bad Connection Studio")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_MODIFY_PATH OFF)
set(CPACK_NSIS_EXECUTABLES_DIRECTORY "bin")
set(CPACK_NSIS_MUI_FINISHPAGE_RUN "bin\\\\bad-connection-studio.exe")
set(CPACK_NSIS_MENU_LINKS "share/doc/BadConnectionStudio/WINDOWS_SETUP.md" "Virtual device setup")
# Per-user data (settings, logs) is left in place on uninstall; it contains
# no media, only preferences and text logs.
include(CPack)
