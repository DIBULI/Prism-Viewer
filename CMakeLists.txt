cmake_minimum_required(VERSION 3.20)

if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING
      "Minimum supported macOS version" FORCE)
endif()

project(prism_viewer VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)

if(NOT (WIN32 OR APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
  message(FATAL_ERROR
    "Prism Viewer supports Windows x64, Linux x64, and macOS arm64")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "The bundled Prism Host SDK requires a 64-bit build")
endif()
if(APPLE)
  if(CMAKE_OSX_ARCHITECTURES)
    set(PRISM_SYSTEM_PROCESSOR "${CMAKE_OSX_ARCHITECTURES}")
  else()
    set(PRISM_SYSTEM_PROCESSOR "${CMAKE_SYSTEM_PROCESSOR}")
  endif()
  string(TOLOWER "${PRISM_SYSTEM_PROCESSOR}" PRISM_SYSTEM_PROCESSOR)
  if(NOT PRISM_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    message(FATAL_ERROR
      "The bundled macOS Prism Host SDK requires an Apple Silicon arm64 target")
  endif()
else()
  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" PRISM_SYSTEM_PROCESSOR)
  if(NOT PRISM_SYSTEM_PROCESSOR MATCHES "^(amd64|x86_64|x64)$")
    message(FATAL_ERROR
      "The bundled Windows/Linux Prism Host SDK requires an x86-64 target")
  endif()
endif()
unset(PRISM_SYSTEM_PROCESSOR)
if(WIN32 AND NOT MSVC)
  message(FATAL_ERROR
    "Prism Viewer runtime loading requires the MSVC 14.x C++ ABI")
endif()
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY
      "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC OFF)
set(CMAKE_AUTOUIC OFF)

set(PRISM_USB_SDK_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/third_party/Prism-SDK"
    CACHE PATH "Prism Host SDK Git submodule root")
set(PRISM_USB_SDK_INCLUDE_DIR "${PRISM_USB_SDK_ROOT}/include")
if(NOT EXISTS "${PRISM_USB_SDK_INCLUDE_DIR}/prism/usb/runtime_api.hpp")
  message(FATAL_ERROR
    "Bundled Prism Host SDK headers are missing under ${PRISM_USB_SDK_ROOT}")
endif()

add_library(prism-viewer-sdk INTERFACE)
target_compile_definitions(prism-viewer-sdk INTERFACE
  PRISM_REQUIRED_USB_SDK_VERSION="${PROJECT_VERSION}")
target_include_directories(prism-viewer-sdk INTERFACE
  "${PRISM_USB_SDK_INCLUDE_DIR}")
if(WIN32)
  set(PRISM_USB_SDK_RUNTIME
      "${PRISM_USB_SDK_ROOT}/runtime/windows-x64/prism_usb_sdk.dll")
elseif(APPLE)
  set(PRISM_USB_SDK_RUNTIME
      "${PRISM_USB_SDK_ROOT}/runtime/macos-arm64/libprism_usb_sdk.dylib")
  set(PRISM_USB_SDK_RUNTIME_DEPENDENCIES
      "${PRISM_USB_SDK_ROOT}/runtime/macos-arm64/libusb-1.0.0.dylib")
  set(PRISM_USB_SDK_LIBUSB_LICENSE
      "${PRISM_USB_SDK_ROOT}/runtime/macos-arm64/libusb-COPYING.txt")
else()
  set(PRISM_USB_SDK_RUNTIME
      "${PRISM_USB_SDK_ROOT}/runtime/linux-x64/libprism_usb_sdk.so")
endif()
if(NOT WIN32)
  add_library(prism-viewer-sdk-runtime SHARED IMPORTED GLOBAL)
  set_target_properties(prism-viewer-sdk-runtime PROPERTIES
    IMPORTED_LOCATION "${PRISM_USB_SDK_RUNTIME}")
  target_link_libraries(prism-viewer-sdk INTERFACE
    prism-viewer-sdk-runtime)
endif()
set(PRISM_USB_SDK_RUNTIME_FILES
    "${PRISM_USB_SDK_RUNTIME}" ${PRISM_USB_SDK_RUNTIME_DEPENDENCIES})
foreach(PRISM_USB_SDK_RUNTIME_FILE IN LISTS PRISM_USB_SDK_RUNTIME_FILES)
  if(NOT EXISTS "${PRISM_USB_SDK_RUNTIME_FILE}")
    message(FATAL_ERROR
      "Bundled Prism Host SDK runtime is missing: ${PRISM_USB_SDK_RUNTIME_FILE}")
  endif()
endforeach()
unset(PRISM_USB_SDK_RUNTIME_FILE)
if(APPLE AND NOT EXISTS "${PRISM_USB_SDK_LIBUSB_LICENSE}")
  message(FATAL_ERROR
    "Bundled libusb license is missing: ${PRISM_USB_SDK_LIBUSB_LICENSE}")
endif()

find_package(Qt6 QUIET COMPONENTS Widgets Charts Sql)
if(Qt6_FOUND)
  set(QT_PACKAGE Qt6)
else()
  find_package(Qt5 REQUIRED COMPONENTS Widgets Charts Sql)
  set(QT_PACKAGE Qt5)
endif()

option(PRISM_DEPLOY_RUNTIME_DEPENDENCIES
  "Install redistributable runtime dependencies for a release package" OFF)
if(PRISM_DEPLOY_RUNTIME_DEPENDENCIES)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
      "Automatic runtime dependency deployment is enabled only for Linux")
  endif()
  if(NOT QT_PACKAGE STREQUAL "Qt6" OR Qt6_VERSION VERSION_LESS "6.5")
    message(FATAL_ERROR
      "Runtime dependency deployment requires Qt 6.5 or newer")
  endif()
endif()

add_library(prism-viewer-app-theme STATIC
  src/ui/app_theme.cpp
)
target_include_directories(prism-viewer-app-theme PUBLIC src)
target_compile_features(prism-viewer-app-theme PUBLIC cxx_std_17)
target_link_libraries(prism-viewer-app-theme PUBLIC ${QT_PACKAGE}::Widgets)

add_library(prism-viewer-imu-units STATIC
  src/imu_units.cpp
)
target_include_directories(prism-viewer-imu-units PUBLIC src)
target_compile_features(prism-viewer-imu-units PUBLIC cxx_std_17)

add_executable(prism-viewer
  resources/prism.qrc
  src/common/ui_text.cpp
  src/communication/device_session.cpp
  src/communication/prism_runtime.cpp
  src/control/operation_controller.cpp
  src/dataset/dataset_browser.cpp
  src/dataset/dataset_playback_timing.cpp
  src/dataset/rosbag_exporter.cpp
  src/main.cpp
  src/transfer/camera_frame_assembler.cpp
  src/ui/camera_zoom_dialog.cpp
  src/ui/camera_encoding_panel.cpp
  src/ui/camera_exposure_panel.cpp
  src/ui/device_info_panel.cpp
  src/ui/image_view_label.cpp
  src/ui/lidar_point_cloud_widget.cpp
  src/ui/main_window.cpp
  src/ui/preview_image_decoder.cpp
  src/ui/wifi_hotspot_panel.cpp
  src/ui/zoomable_image_view.cpp
)
set_target_properties(prism-viewer PROPERTIES AUTORCC ON)

target_compile_features(prism-viewer PRIVATE cxx_std_17)
target_include_directories(prism-viewer PRIVATE src)

target_link_libraries(prism-viewer PRIVATE
  prism-viewer-app-theme
  prism-viewer-imu-units
  prism-viewer-sdk
  ${QT_PACKAGE}::Widgets
  ${QT_PACKAGE}::Charts
  ${QT_PACKAGE}::Sql
)

# Keep the bundled SDK runtime in the platform's loader-visible application
# directory. On Windows no import library is searched or linked. Prism-Viewer
# never compiles SDK sources.
function(prism_copy_sdk_runtime target)
  if(APPLE)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory
              "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Frameworks"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              ${PRISM_USB_SDK_RUNTIME_FILES}
              "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Frameworks"
      COMMAND "${CMAKE_COMMAND}" -E make_directory
              "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources/licenses"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "${PRISM_USB_SDK_LIBUSB_LICENSE}"
              "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources/licenses"
      VERBATIM
    )
  else()
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "${PRISM_USB_SDK_RUNTIME}"
              "$<TARGET_FILE_DIR:${target}>"
      VERBATIM
    )
  endif()
endfunction()
prism_copy_sdk_runtime(prism-viewer)

if(APPLE)
  set_target_properties(prism-viewer PROPERTIES
    BUILD_RPATH "@executable_path/../Frameworks"
    INSTALL_RPATH "@executable_path/../Frameworks"
    INSTALL_RPATH_USE_LINK_PATH FALSE
  )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set_target_properties(prism-viewer PROPERTIES
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
  )
endif()

if(MSVC)
  target_compile_options(prism-viewer PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(prism-viewer PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
  target_sources(prism-viewer PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/branding/prism-viewer.rc"
  )
  set_property(SOURCE
    "${CMAKE_CURRENT_LIST_DIR}/branding/prism-viewer.rc"
    APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/branding/prism-viewer.ico"
  )
  set_target_properties(prism-viewer PROPERTIES WIN32_EXECUTABLE TRUE)
elseif(APPLE)
  set(PRISM_MACOS_APP_ICON
      "${CMAKE_CURRENT_LIST_DIR}/branding/prism-viewer.icns")
  target_sources(prism-viewer PRIVATE "${PRISM_MACOS_APP_ICON}")
  set_source_files_properties("${PRISM_MACOS_APP_ICON}" PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
  )
  set_target_properties(prism-viewer PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "Prism Viewer"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.dibuli.prism-viewer"
    MACOSX_BUNDLE_ICON_FILE "prism-viewer.icns"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    OUTPUT_NAME "Prism-Viewer"
  )
endif()

install(TARGETS prism-viewer
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  BUNDLE DESTINATION .
)
if(WIN32)
  install(FILES "${PRISM_USB_SDK_RUNTIME}"
    DESTINATION ${CMAKE_INSTALL_BINDIR})
elseif(NOT APPLE)
  install(FILES "${PRISM_USB_SDK_RUNTIME}"
    DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  install(FILES packaging/prism-viewer.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
  )
  install(FILES branding/prism-mark-256.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
    RENAME prism-viewer.png
  )
  install(FILES branding/prism-mark.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME prism-viewer.svg
  )
endif()

# Qt's deployment API collects the linked Qt modules, the required Qt plugins,
# and recursively linked non-Qt libraries at install time. The Linux release
# intentionally keeps only the kernel/glibc ABI as a host requirement; all
# other resolved user-space libraries (including the SDK's OpenSSL and libusb
# dependencies) are copied into the package.
if(PRISM_DEPLOY_RUNTIME_DEPENDENCIES)
  qt_generate_deploy_app_script(
    TARGET prism-viewer
    OUTPUT_SCRIPT prism_viewer_deploy_script
    NO_TRANSLATIONS
    POST_EXCLUDE_REGEXES
      ".*/ld-linux[^/]*\\.so(\\..*)?$"
      ".*/lib(c|dl|m|pthread|rt|resolv|util)\\.so(\\..*)?$"
  )
  install(SCRIPT "${prism_viewer_deploy_script}")
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(app-theme-test
    tests/app_theme_test.cpp
  )
  target_compile_features(app-theme-test PRIVATE cxx_std_17)
  target_link_libraries(app-theme-test PRIVATE prism-viewer-app-theme)
  add_test(NAME app-theme COMMAND app-theme-test)
  if(UNIX)
    set_tests_properties(app-theme PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
  endif()

  add_executable(imu-units-test
    tests/imu_units_test.cpp
  )
  target_compile_features(imu-units-test PRIVATE cxx_std_17)
  target_link_libraries(imu-units-test PRIVATE prism-viewer-imu-units)
  add_test(NAME imu-units COMMAND imu-units-test)

  add_executable(imu-timestamp-policy-test
    tests/imu_timestamp_policy_test.cpp
  )
  target_include_directories(imu-timestamp-policy-test PRIVATE src)
  target_compile_features(imu-timestamp-policy-test PRIVATE cxx_std_17)
  add_test(NAME imu-timestamp-policy COMMAND imu-timestamp-policy-test)

  add_executable(camera-frame-assembler-test
    src/transfer/camera_frame_assembler.cpp
    tests/camera_frame_assembler_test.cpp
  )
  target_include_directories(camera-frame-assembler-test PRIVATE src)
  target_compile_features(camera-frame-assembler-test PRIVATE cxx_std_17)
  target_link_libraries(camera-frame-assembler-test PRIVATE prism-viewer-sdk)
  add_test(NAME camera-frame-assembler COMMAND camera-frame-assembler-test)

  add_executable(preview-image-decoder-test
    src/ui/preview_image_decoder.cpp
    tests/preview_image_decoder_test.cpp
  )
  target_include_directories(preview-image-decoder-test PRIVATE src)
  target_compile_features(preview-image-decoder-test PRIVATE cxx_std_17)
  target_link_libraries(preview-image-decoder-test PRIVATE ${QT_PACKAGE}::Gui)
  add_test(NAME preview-image-decoder COMMAND preview-image-decoder-test)
  if(WIN32)
    set_tests_properties(preview-image-decoder PROPERTIES
      ENVIRONMENT
        "PATH=$<TARGET_FILE_DIR:${QT_PACKAGE}::Core>\;$ENV{PATH}"
    )
  endif()

  add_executable(dataset-browser-test
    src/common/ui_text.cpp
    src/dataset/dataset_browser.cpp
    tests/dataset_browser_test.cpp
  )
  target_include_directories(dataset-browser-test PRIVATE src)
  target_compile_features(dataset-browser-test PRIVATE cxx_std_17)
  target_link_libraries(dataset-browser-test PRIVATE
    prism-viewer-sdk
    ${QT_PACKAGE}::Gui
  )
  if(MSVC)
    target_compile_options(
      dataset-browser-test PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(
      dataset-browser-test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME dataset-browser COMMAND dataset-browser-test)

  add_executable(dataset-playback-timing-test
    src/dataset/dataset_playback_timing.cpp
    tests/dataset_playback_timing_test.cpp
  )
  target_include_directories(dataset-playback-timing-test PRIVATE src)
  target_compile_features(dataset-playback-timing-test PRIVATE cxx_std_17)
  if(MSVC)
    target_compile_options(
      dataset-playback-timing-test PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(
      dataset-playback-timing-test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME dataset-playback-timing COMMAND dataset-playback-timing-test)

  add_executable(rosbag-exporter-test
    src/dataset/rosbag_exporter.cpp
    tests/rosbag_exporter_test.cpp
  )
  target_include_directories(rosbag-exporter-test PRIVATE src)
  target_compile_features(rosbag-exporter-test PRIVATE cxx_std_17)
  target_link_libraries(rosbag-exporter-test PRIVATE ${QT_PACKAGE}::Sql)
  if(MSVC)
    target_compile_options(
      rosbag-exporter-test PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(
      rosbag-exporter-test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME rosbag-exporter COMMAND rosbag-exporter-test)

  add_test(
    NAME dataset-recorder-full
    COMMAND prism-viewer --dataset-recorder-self-test
            "${CMAKE_CURRENT_BINARY_DIR}/test-output/dataset-recorder-full"
  )
  add_test(
    NAME dataset-recorder-imu-only
    COMMAND prism-viewer --imu-only-recorder-self-test
            "${CMAKE_CURRENT_BINARY_DIR}/test-output/dataset-recorder-imu-only"
  )
  add_test(
    NAME main-window-layout
    COMMAND prism-viewer --window-layout-self-test
  )
  set_tests_properties(
    dataset-recorder-full dataset-recorder-imu-only
    PROPERTIES RUN_SERIAL TRUE
  )
  if(UNIX)
    set_tests_properties(
      dataset-recorder-full dataset-recorder-imu-only main-window-layout
      PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
  endif()

  add_executable(camera-encoding-panel-test
    src/common/ui_text.cpp
    src/ui/camera_encoding_panel.cpp
    tests/camera_encoding_panel_test.cpp
  )
  target_include_directories(camera-encoding-panel-test PRIVATE src)
  target_compile_features(camera-encoding-panel-test PRIVATE cxx_std_17)
  target_link_libraries(camera-encoding-panel-test PRIVATE
    prism-viewer-sdk
    ${QT_PACKAGE}::Widgets
  )
  add_test(NAME camera-encoding-panel COMMAND camera-encoding-panel-test)
  if(UNIX)
    set_tests_properties(camera-encoding-panel PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
  endif()

  add_executable(camera-exposure-panel-test
    src/common/ui_text.cpp
    src/ui/camera_exposure_panel.cpp
    tests/camera_exposure_panel_test.cpp
  )
  target_include_directories(camera-exposure-panel-test PRIVATE src)
  target_compile_features(camera-exposure-panel-test PRIVATE cxx_std_17)
  target_link_libraries(camera-exposure-panel-test PRIVATE
    prism-viewer-sdk
    ${QT_PACKAGE}::Widgets
  )
  add_test(NAME camera-exposure-panel COMMAND camera-exposure-panel-test)
  if(UNIX)
    set_tests_properties(camera-exposure-panel PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
  endif()

  add_executable(image-view-label-test
    src/ui/image_view_label.cpp
    tests/image_view_label_test.cpp
  )
  target_include_directories(image-view-label-test PRIVATE src)
  target_compile_features(image-view-label-test PRIVATE cxx_std_17)
  target_link_libraries(image-view-label-test PRIVATE
    ${QT_PACKAGE}::Widgets
  )
  add_test(NAME image-view-label COMMAND image-view-label-test)
  if(UNIX)
    set_tests_properties(image-view-label PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
  endif()

  add_executable(lidar-point-cloud-widget-test
    src/ui/lidar_point_cloud_widget.cpp
    tests/lidar_point_cloud_widget_test.cpp
  )
  target_include_directories(lidar-point-cloud-widget-test PRIVATE src)
  target_compile_features(lidar-point-cloud-widget-test PRIVATE cxx_std_17)
  target_link_libraries(lidar-point-cloud-widget-test PRIVATE
    prism-viewer-sdk
    ${QT_PACKAGE}::Widgets
  )
  add_test(NAME lidar-point-cloud-widget COMMAND lidar-point-cloud-widget-test)
  if(UNIX)
    set_tests_properties(lidar-point-cloud-widget PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
  endif()

  if(WIN32)
    add_executable(windows-runtime-loader-test
      src/communication/prism_runtime.cpp
      tests/windows_runtime_loader_test.cpp
    )
    target_include_directories(windows-runtime-loader-test PRIVATE src)
    target_compile_features(
      windows-runtime-loader-test PRIVATE cxx_std_17)
    target_link_libraries(
      windows-runtime-loader-test PRIVATE prism-viewer-sdk)
    if(MSVC)
      target_compile_options(
        windows-runtime-loader-test PRIVATE /W4 /permissive- /utf-8)
    endif()
    prism_copy_sdk_runtime(windows-runtime-loader-test)
    add_test(
      NAME windows-runtime-loader
      COMMAND windows-runtime-loader-test
    )
  endif()

endif()
