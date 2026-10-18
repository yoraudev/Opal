cmake_minimum_required(VERSION 3.28)
project(Opal VERSION 0.1.0 LANGUAGES CXX RC)

if(NOT OPAL_VERSION)
  set(OPAL_VERSION "${PROJECT_VERSION}")
endif()
string(REGEX MATCH "^([0-9]+)\.([0-9]+)\.([0-9]+)$" OPAL_VERSION_MATCH "${OPAL_VERSION}")
if(NOT OPAL_VERSION_MATCH)
  message(FATAL_ERROR "OPAL_VERSION must look like 1.2.3, got '${OPAL_VERSION}'")
endif()
set(OPAL_VERSION_MAJOR ${CMAKE_MATCH_1})
set(OPAL_VERSION_MINOR ${CMAKE_MATCH_2})
set(OPAL_VERSION_PATCH ${CMAKE_MATCH_3})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A00000C)
configure_file(src/core/Version.h.in ${CMAKE_BINARY_DIR}/generated/Version.h @ONLY)
include_directories(${CMAKE_BINARY_DIR}/generated)
add_compile_options(
  $<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/permissive->
  $<$<COMPILE_LANGUAGE:CXX>:/utf-8> $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
  $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus> $<$<COMPILE_LANGUAGE:CXX>:/MP>
  $<$<COMPILE_LANGUAGE:CXX>:/bigobj>
  $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/O2>
  $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/GL>
  $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/Gy>)
add_link_options($<$<CONFIG:Release>:/LTCG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)

add_library(opal_core STATIC
  src/core/Image.cpp
  src/render/ImageOps.cpp
  src/capture/EdgeSnap.cpp
  src/capture/CornerRadius.cpp
  src/app/Hotkey.cpp
  src/app/Ini.cpp
  src/setup/InstallRules.cpp
  src/setup/ReleaseInfo.cpp)
target_include_directories(opal_core PUBLIC src)

add_library(opal_app STATIC
  src/core/Log.cpp
  src/core/Strings.cpp
  src/app/App.cpp
  src/app/Settings.cpp
  src/app/Hotkeys.cpp
  src/app/SystemIntegration.cpp
  src/app/Tray.cpp
  src/app/Sound.cpp
  src/app/Updater.cpp
  src/render/Gpu.cpp
  src/render/D2DWindow.cpp
  src/ui/Theme.cpp
  src/ui/Icons.cpp
  src/ui/Ui.cpp
  src/capture/Monitors.cpp
  src/capture/ScreenGrab.cpp
  src/capture/WindowList.cpp
  src/capture/WindowCapture.cpp
  src/capture/ElementTree.cpp
  src/overlay/CaptureSession.cpp
  src/overlay/OverlayWindow.cpp
  src/overlay/CaptureBar.cpp
  src/overlay/Countdown.cpp
  src/output/Output.cpp
  src/thumbnail/ThumbnailWindow.cpp
  src/editor/Annotations.cpp
  src/editor/EditorWindow.cpp
  src/settingsui/SettingsWindow.cpp
  src/setup/Installer.cpp
  src/setup/SetupWindow.cpp)
target_link_libraries(opal_app PUBLIC opal_core
  d3d11 dxgi d2d1 dwrite dcomp windowscodecs dwmapi shcore uxtheme
  comctl32 shlwapi winmm ole32 oleaut32 windowsapp dxguid dbghelp wtsapi32 winhttp)

add_executable(Opal WIN32 src/main.cpp res/opal.rc)
target_link_libraries(Opal PRIVATE opal_app)
set_target_properties(Opal PROPERTIES OUTPUT_NAME "Opal")
target_link_options(Opal PRIVATE /MANIFEST:NO)

add_executable(OpalSetup WIN32 src/stub/SetupStub.cpp res/setup.rc)
target_compile_definitions(OpalSetup PRIVATE "OPAL_PAYLOAD=\"$<TARGET_FILE:Opal>\"")
target_link_options(OpalSetup PRIVATE /MANIFEST:NO)
add_dependencies(OpalSetup Opal)

add_executable(opal_preview EXCLUDE_FROM_ALL tools/preview.cpp res/opal.rc)
target_link_libraries(opal_preview PRIVATE opal_app)
target_link_options(opal_preview PRIVATE /MANIFEST:NO)

enable_testing()
add_executable(opal_tests tests/tests.cpp)
target_link_libraries(opal_tests PRIVATE opal_core)
add_test(NAME opal_tests COMMAND opal_tests)
