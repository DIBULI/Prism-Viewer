cmake_minimum_required(VERSION 3.20)

if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING
      "Minimum supported macOS version" FORCE)
endif()

project(prism_viewer VERSION 1.3.0 LANGUAGES CXX)

# Viewer and its pinned binary SDK are versioned independently.
set(PRISM_USB_SDK_VERSION "1.3.0")

include(GNUInstallDirs)

if(NOT (WIN32 OR APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
  message(FATAL_ERROR
    "Prism Viewer supports Windows x64, Linux x64/arm64, and macOS arm64")
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
elseif(WIN32)
  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" PRISM_SYSTEM_PROCESSOR)
  if(NOT PRISM_SYSTEM_PROCESSOR MATCHES "^(amd64|x86_64|x64)$")
    message(FATAL_ERROR
      "The bundled Windows Prism Host SDK requires an x86-64 target")
  endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" PRISM_SYSTEM_PROCESSOR)
  if(PRISM_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    set(PRISM_LINUX_RUNTIME_ARCH "linux-arm64")
  elseif(PRISM_SYSTEM_PROCESSOR MATCHES "^(amd64|x86_64|x64)$")
    set(PRISM_LINUX_RUNTIME_ARCH "linux-x64")
  else()
    message(FATAL_ERROR
      "The bundled Linux Prism Host SDK requires an x86-64 or arm64 target")
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
option(PRISM_LINK_STATIC_LINUX_SDK
  "Link the Prism Host SDK implementation statically on Linux" ON)
set(PRISM_LINUX_SHARED_SDK_RUNTIME "" CACHE FILEPATH
  "Optional Prism Host SDK shared library used when static Linux linking is disabled")
set(PRISM_USB_SDK_INCLUDE_DIR "${PRISM_USB_SDK_ROOT}/include")
if(NOT EXISTS "${PRISM_USB_SDK_INCLUDE_DIR}/prism/usb/network_runtime_api.hpp")
  message(FATAL_ERROR "Network support requires the matching Prism SDK build; set PRISM_USB_SDK_ROOT to its headers and platform runtime (network API v4).")
endif()
if(NOT EXISTS "${PRISM_USB_SDK_INCLUDE_DIR}/prism/usb/runtime_api.hpp")
  message(FATAL_ERROR
    "Bundled Prism Host SDK headers are missing under ${PRISM_USB_SDK_ROOT}")
endif()

add_library(prism-viewer-sdk INTERFACE)
target_compile_definitions(prism-viewer-sdk INTERFACE
  PRISM_REQUIRED_USB_SDK_VERSION="${PRISM_USB_SDK_VERSION}")
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
  if(PRISM_LINK_STATIC_LINUX_SDK)
    set(PRISM_LINUX_SDK_IS_STATIC TRUE)
    set(PRISM_USB_SDK_RUNTIME
        "${PRISM_USB_SDK_ROOT}/runtime/${PRISM_LINUX_RUNTIME_ARCH}/libprism_usb_sdk.a")
  else()
    set(PRISM_LINUX_SDK_IS_STATIC FALSE)
    if(PRISM_LINUX_RUNTIME_ARCH STREQUAL "linux-x64")
      set(PRISM_USB_SDK_OPENSSL_LICENSE
          "${PRISM_USB_SDK_ROOT}/runtime/linux-x64/openssl-LICENSE.txt")
    endif()
    if(PRISM_LINUX_SHARED_SDK_RUNTIME)
      get_filename_component(PRISM_USB_SDK_RUNTIME
        "${PRISM_LINUX_SHARED_SDK_RUNTIME}" ABSOLUTE)
    else()
      set(PRISM_USB_SDK_RUNTIME
          "${PRISM_USB_SDK_ROOT}/runtime/${PRISM_LINUX_RUNTIME_ARCH}/libprism_usb_sdk.so")
    endif()
  endif()
endif()
if(NOT WIN32)
  if(PRISM_LINUX_SDK_IS_STATIC)
    add_library(prism-viewer-sdk-runtime STATIC IMPORTED GLOBAL)
  else()
    add_library(prism-viewer-sdk-runtime SHARED IMPORTED GLOBAL)
  endif()
  set_target_properties(prism-viewer-sdk-runtime PROPERTIES
    IMPORTED_LOCATION "${PRISM_USB_SDK_RUNTIME}")
  target_link_libraries(prism-viewer-sdk INTERFACE
    prism-viewer-sdk-runtime)
  if(PRISM_LINUX_SDK_IS_STATIC)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
    target_link_libraries(prism-viewer-sdk INTERFACE
      PkgConfig::LIBUSB OpenSSL::Crypto)
  endif()
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
if(PRISM_USB_SDK_OPENSSL_LICENSE
    AND NOT EXISTS "${PRISM_USB_SDK_OPENSSL_LICENSE}")
  message(FATAL_ERROR
    "Bundled OpenSSL license is missing: ${PRISM_USB_SDK_OPENSSL_LICENSE}")
endif()

find_package(Qt6 QUIET COMPONENTS Widgets Sql Network)
if(Qt6_FOUND)
  set(QT_PACKAGE Qt6)
else()
  find_package(Qt5 REQUIRED COMPONENTS Widgets Sql Network)
  set(QT_PACKAGE Qt5)
endif()
find_package(Threads REQUIRED)
target_link_libraries(prism-viewer-sdk INTERFACE Threads::Threads)

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

add_library(prism-viewer-imu-traces STATIC
  src/ui/imu_trace_view.cpp
  src/ui/imu_trace_view.hpp
)
set_target_properties(prism-viewer-imu-traces PROPERTIES AUTOMOC ON)
target_include_directories(prism-viewer-imu-traces PUBLIC src)
target_compile_features(prism-viewer-imu-traces PUBLIC cxx_std_17)
target_link_libraries(prism-viewer-imu-traces PUBLIC ${QT_PACKAGE}::Widgets)

add_executable(prism-viewer
  resources/prism.qrc
  src/common/ui_text.cpp
  src/communication/device_info_compat.cpp
  src/communication/device_session.cpp
  src/communication/prism_runtime.cpp
  src/communication/rtk_corrections.cpp
  src/cors/cors_config.cpp
  src/cors/cors_session.cpp
  src/control/operation_controller.cpp
  src/dataset/dataset_browser.cpp
  src/dataset/dataset_playback.cpp
  src/dataset/dataset_playback_timing.cpp
  src/dataset/rosbag_exporter.cpp
  src/dataset/rk_dataset_client.cpp
  src/main.cpp
  src/transfer/camera_frame_assembler.cpp
  src/ui/camera_zoom_dialog.cpp
  src/ui/camera_encoding_panel.cpp
  src/ui/camera_exposure_panel.cpp
  src/ui/native_camera_control_panel.cpp
  src/ui/cors_panel.cpp
  src/ui/gnss_visualization.cpp
  src/ui/device_info_panel.cpp
  src/ui/image_view_label.cpp
  src/ui/lidar_point_cloud_widget.cpp
  src/ui/lidar_power_panel.cpp
  src/ui/main_window.cpp
  src/ui/preview_image_decoder.cpp
  src/ui/rk_dataset_dialog.cpp
  src/ui/wifi_hotspot_panel.cpp
  src/ui/zoomable_image_view.cpp
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux"
    AND PRISM_LINUX_SDK_IS_STATIC
    AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
    AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11)
  # The bundled static SDK is built with a newer GCC and references the
  # GLIBCXX_3.4.29 throw helper. Ubuntu 20.04 ships GCC 9, whose standard
  # library has the exception type but does not export that helper yet.
  target_sources(prism-viewer PRIVATE
    src/compat/libstdcxx_bad_array_new_length.cpp)
endif()
set_target_properties(prism-viewer PROPERTIES AUTORCC ON)

target_compile_features(prism-viewer PRIVATE cxx_std_17)
target_include_directories(prism-viewer PRIVATE src)
option(PRISM_BUILD_BUNDLED_ZSTD "Build pinned Zstandard for portable packages" OFF)
if(PRISM_BUILD_BUNDLED_ZSTD)
  include(FetchContent)
  set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
  set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
  set(ZSTD_USE_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(zstd
    URL https://github.com/facebook/zstd/archive/refs/tags/v1.5.7.tar.gz
    URL_HASH SHA256=37d7284556b20954e56e1ca85b80226768902e2edabd3b649e9e72c0c9012ee3
    SOURCE_SUBDIR build/cmake)
  FetchContent_MakeAvailable(zstd)
  # Build the linked static target, but do not install the dependency's SDK
  # headers, archive or CMake exports into the Viewer runtime distribution.
  set_property(DIRECTORY "${zstd_SOURCE_DIR}/build/cmake"
    PROPERTY EXCLUDE_FROM_ALL TRUE)
  set(PRISM_ZSTD_INCLUDE "${zstd_SOURCE_DIR}/lib")
  set(PRISM_ZSTD_LIBRARY libzstd_static)
else()
  find_path(PRISM_ZSTD_INCLUDE zstd.h REQUIRED)
  find_library(PRISM_ZSTD_LIBRARY NAMES zstd zstd_static REQUIRED)
endif()
target_include_directories(prism-viewer PRIVATE "${PRISM_ZSTD_INCLUDE}")
target_link_libraries(prism-viewer PRIVATE "${PRISM_ZSTD_LIBRARY}")

add_library(prism-viewer-raw-dataset STATIC
  src/dataset/raw12_dataset.cpp src/dataset/raw12_recorder.cpp src/common/ui_text.cpp)
target_include_directories(prism-viewer-raw-dataset PUBLIC src PRIVATE "${PRISM_ZSTD_INCLUDE}")
target_compile_features(prism-viewer-raw-dataset PUBLIC cxx_std_17)
target_link_libraries(prism-viewer-raw-dataset PUBLIC prism-viewer-sdk ${QT_PACKAGE}::Gui "${PRISM_ZSTD_LIBRARY}")
target_link_libraries(prism-viewer PRIVATE prism-viewer-raw-dataset)

target_link_libraries(prism-viewer PRIVATE
  prism-viewer-app-theme
  prism-viewer-imu-units
  prism-viewer-sdk
  Threads::Threads
  ${QT_PACKAGE}::Widgets
  prism-viewer-imu-traces
  ${QT_PACKAGE}::Sql
  ${QT_PACKAGE}::Network
)

# Keep dynamic SDK runtimes in the platform's loader-visible application
# directory. The Linux static archive is linked into the executable and is not
# copied into the build or release package. Prism-Viewer never compiles SDK
# sources.
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
  elseif(NOT PRISM_LINUX_SDK_IS_STATIC)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "${PRISM_USB_SDK_RUNTIME}"
              "$<TARGET_FILE_DIR:${target}>"
      VERBATIM
    )
  endif()
endfunction()
prism_copy_sdk_runtime(prism-viewer)
target_compile_definitions(prism-viewer PRIVATE
  PRISM_VIEWER_VERSION="${PROJECT_VERSION}")

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
if(APPLE)
  set(PRISM_VIEWER_LICENSE_DESTINATION "Prism-Viewer.app/Contents/Resources/licenses/PrismViewer")
elseif(WIN32)
  set(PRISM_VIEWER_LICENSE_DESTINATION "${CMAKE_INSTALL_BINDIR}/licenses/PrismViewer")
else()
  set(PRISM_VIEWER_LICENSE_DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/PrismViewer")
endif()
install(FILES LICENSE NOTICE LICENSING.md THIRD_PARTY_NOTICES.md
  DESTINATION "${PRISM_VIEWER_LICENSE_DESTINATION}")
install(DIRECTORY licenses/
  DESTINATION "${PRISM_VIEWER_LICENSE_DESTINATION}/licenses")
if(WIN32)
  install(FILES "${PRISM_USB_SDK_RUNTIME}"
    DESTINATION ${CMAKE_INSTALL_BINDIR})
elseif(NOT APPLE AND NOT PRISM_LINUX_SDK_IS_STATIC)
  install(FILES "${PRISM_USB_SDK_RUNTIME}"
    DESTINATION ${CMAKE_INSTALL_LIBDIR})
  if(PRISM_USB_SDK_OPENSSL_LICENSE)
    install(FILES "${PRISM_USB_SDK_OPENSSL_LICENSE}"
      DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/PrismUsbSdk)
  endif()
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
      ".*/ld-linux.*"
      ".*/libc[.]so.*"
      ".*/libdl[.]so.*"
      ".*/libm[.]so.*"
      ".*/libpthread[.]so.*"
      ".*/librt[.]so.*"
      ".*/libresolv[.]so.*"
      ".*/libutil[.]so.*"
  )
  install(SCRIPT "${prism_viewer_deploy_script}")
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(live-exposure-rows-test tests/live_exposure_rows_test.cpp)
  target_include_directories(live-exposure-rows-test PRIVATE src)
  target_compile_features(live-exposure-rows-test PRIVATE cxx_std_17)
  target_link_libraries(live-exposure-rows-test PRIVATE prism-viewer-sdk)
  add_test(NAME live-exposure-rows COMMAND live-exposure-rows-test)
  add_executable(imu-trace-view-test tests/imu_trace_view_test.cpp)
  target_link_libraries(imu-trace-view-test PRIVATE prism-viewer-imu-traces)
  add_test(NAME imu-trace-view COMMAND imu-trace-view-test)
  set_tests_properties(imu-trace-view PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  add_test(NAME viewer-version COMMAND prism-viewer --version)
  set_tests_properties(viewer-version PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    PASS_REGULAR_EXPRESSION "Prism Viewer ${PROJECT_VERSION}.*Prism SDK ${PRISM_USB_SDK_VERSION}")
  add_executable(gnss-visualization-test src/ui/gnss_visualization.cpp src/common/ui_text.cpp tests/gnss_visualization_test.cpp)
  target_compile_features(gnss-visualization-test PRIVATE cxx_std_17)
  target_include_directories(gnss-visualization-test PRIVATE src)
  target_link_libraries(gnss-visualization-test PRIVATE prism-viewer-sdk ${QT_PACKAGE}::Widgets)
  if(MSVC)
    target_compile_options(gnss-visualization-test PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(gnss-visualization-test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME gnss-visualization COMMAND gnss-visualization-test)
  set_tests_properties(gnss-visualization PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  add_executable(rk-dataset-test
    src/common/ui_text.cpp src/dataset/dataset_browser.cpp
    src/dataset/rk_dataset_client.cpp src/dataset/rosbag_exporter.cpp
    tests/rk_dataset_test.cpp)
  target_include_directories(rk-dataset-test PRIVATE src)
  target_compile_features(rk-dataset-test PRIVATE cxx_std_17)
  target_link_libraries(rk-dataset-test PRIVATE prism-viewer-sdk
    ${QT_PACKAGE}::Gui ${QT_PACKAGE}::Network ${QT_PACKAGE}::Sql)
  add_test(NAME rk-dataset COMMAND rk-dataset-test)
  add_executable(rk-dataset-dialog-test
    src/common/ui_text.cpp src/ui/app_theme.cpp src/ui/rk_dataset_dialog.cpp
    src/dataset/dataset_browser.cpp src/dataset/rk_dataset_client.cpp
    src/dataset/rosbag_exporter.cpp tests/rk_dataset_dialog_test.cpp)
  target_include_directories(rk-dataset-dialog-test PRIVATE src)
  target_compile_features(rk-dataset-dialog-test PRIVATE cxx_std_17)
  target_link_libraries(rk-dataset-dialog-test PRIVATE prism-viewer-sdk
    ${QT_PACKAGE}::Widgets ${QT_PACKAGE}::Network ${QT_PACKAGE}::Sql)
  add_test(NAME rk-dataset-dialog COMMAND rk-dataset-dialog-test)
  set_tests_properties(rk-dataset-dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  add_executable(sdk-runtime-test tests/sdk_runtime_test.cpp)
  target_compile_features(sdk-runtime-test PRIVATE cxx_std_17)
  target_link_libraries(sdk-runtime-test PRIVATE prism-viewer-sdk)
  if(WIN32)
    target_compile_definitions(sdk-runtime-test PRIVATE
      PRISM_TEST_SDK_RUNTIME="${PRISM_USB_SDK_RUNTIME}")
  endif()
  add_test(NAME sdk-runtime COMMAND sdk-runtime-test)
  if(UNIX)
    add_executable(aggregate-imu-test tests/aggregate_imu_test.cpp)
    add_executable(capture-credits-test tests/capture_credits_test.cpp)
    target_include_directories(capture-credits-test PRIVATE src)
    target_compile_features(capture-credits-test PRIVATE cxx_std_17)
    add_test(NAME capture-credits COMMAND capture-credits-test)
    target_include_directories(aggregate-imu-test PRIVATE src)
    target_compile_features(aggregate-imu-test PRIVATE cxx_std_17)
    target_link_libraries(aggregate-imu-test PRIVATE prism-viewer-sdk)
    add_test(NAME aggregate-imu COMMAND aggregate-imu-test)
    add_executable(bounded-audit-test tests/bounded_audit_test.cpp)
    target_include_directories(bounded-audit-test PRIVATE src "${PRISM_ZSTD_INCLUDE}")
    target_compile_features(bounded-audit-test PRIVATE cxx_std_17)
    target_link_libraries(bounded-audit-test PRIVATE prism-viewer-sdk pthread "${PRISM_ZSTD_LIBRARY}")
    add_test(NAME bounded-audit COMMAND bounded-audit-test)
    add_executable(bounded-audit-sanitized tests/bounded_audit_test.cpp)
    target_include_directories(bounded-audit-sanitized PRIVATE src "${PRISM_ZSTD_INCLUDE}")
    target_compile_features(bounded-audit-sanitized PRIVATE cxx_std_17)
    target_compile_options(bounded-audit-sanitized PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(bounded-audit-sanitized PRIVATE -fsanitize=address,undefined)
    target_link_libraries(bounded-audit-sanitized PRIVATE prism-viewer-sdk pthread "${PRISM_ZSTD_LIBRARY}")
    add_test(NAME bounded-audit-sanitized COMMAND bounded-audit-sanitized)
  endif()

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

  add_executable(cors-config-test
    src/cors/cors_config.cpp
    tests/cors_config_test.cpp
  )
  target_include_directories(cors-config-test PRIVATE src)
  target_compile_features(cors-config-test PRIVATE cxx_std_17)
  target_link_libraries(cors-config-test PRIVATE
    prism-viewer-sdk
    ${QT_PACKAGE}::Core
  )
  if(MSVC)
    target_compile_options(cors-config-test PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(cors-config-test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME cors-config COMMAND cors-config-test)

  add_executable(cors-session-test
    src/communication/prism_runtime.cpp
    src/communication/rtk_corrections.cpp
    src/cors/cors_config.cpp
    src/cors/cors_session.cpp
    tests/cors_session_test.cpp
  )
  target_include_directories(cors-session-test PRIVATE src)
  target_compile_features(cors-session-test PRIVATE cxx_std_17)
  target_link_libraries(cors-session-test PRIVATE
    prism-viewer-sdk
    ${QT_PACKAGE}::Core
    ${QT_PACKAGE}::Network
    Threads::Threads
  )
  if(MSVC)
    target_compile_options(
      cors-session-test PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(
      cors-session-test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME cors-session COMMAND cors-session-test)
  add_executable(native-cors-session-test
    src/cors/cors_config.cpp src/cors/cors_session.cpp tests/native_cors_session_test.cpp)
  target_include_directories(native-cors-session-test PRIVATE src)
  target_compile_features(native-cors-session-test PRIVATE cxx_std_17)
  target_link_libraries(native-cors-session-test PRIVATE ${QT_PACKAGE}::Core ${QT_PACKAGE}::Network prism-viewer-sdk Threads::Threads)
  add_test(NAME native-cors-session COMMAND native-cors-session-test)
  set_tests_properties(native-cors-session PROPERTIES TIMEOUT 30)

  add_executable(rtk-corrections-test
    src/communication/prism_runtime.cpp
    src/communication/rtk_corrections.cpp
    tests/rtk_corrections_test.cpp
  )
  target_include_directories(rtk-corrections-test PRIVATE src)
  target_compile_features(rtk-corrections-test PRIVATE cxx_std_17)
  target_link_libraries(rtk-corrections-test PRIVATE prism-viewer-sdk)
  if(MSVC)
    target_compile_options(
      rtk-corrections-test PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(
      rtk-corrections-test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME rtk-corrections COMMAND rtk-corrections-test)

  add_executable(device-info-compat-test
    src/communication/device_info_compat.cpp
    src/communication/prism_runtime.cpp
    tests/device_info_compat_test.cpp
  )
  target_include_directories(device-info-compat-test PRIVATE src)
  target_compile_features(device-info-compat-test PRIVATE cxx_std_17)
  target_link_libraries(device-info-compat-test PRIVATE prism-viewer-sdk)
  if(MSVC)
    target_compile_options(
      device-info-compat-test PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(
      device-info-compat-test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME device-info-compat COMMAND device-info-compat-test)

  add_executable(time-source-display-test
    src/common/ui_text.cpp
    src/communication/prism_runtime.cpp
    src/ui/device_info_panel.cpp
    tests/time_source_display_test.cpp
  )
  add_executable(recording-loss-warning-test tests/recording_loss_warning_test.cpp src/common/ui_text.cpp)
  target_include_directories(recording-loss-warning-test PRIVATE src)
  target_compile_features(recording-loss-warning-test PRIVATE cxx_std_17)
  target_link_libraries(recording-loss-warning-test PRIVATE ${QT_PACKAGE}::Widgets)
  if(MSVC)
    target_compile_options(recording-loss-warning-test PRIVATE /utf-8)
  endif()
  add_test(NAME recording-loss-warning COMMAND recording-loss-warning-test)
  if(UNIX)
    set_tests_properties(recording-loss-warning PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()
  target_include_directories(time-source-display-test PRIVATE src)
  target_compile_features(time-source-display-test PRIVATE cxx_std_17)
  if(MSVC)
    target_compile_options(time-source-display-test PRIVATE /utf-8)
  endif()
  target_link_libraries(time-source-display-test PRIVATE
    prism-viewer-sdk ${QT_PACKAGE}::Widgets)
  if(WIN32)
    prism_copy_sdk_runtime(time-source-display-test)
  endif()
  add_test(NAME time-source-display COMMAND time-source-display-test)
  if(UNIX)
    set_tests_properties(time-source-display PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()


  add_executable(imu-timestamp-policy-test
    tests/imu_timestamp_policy_test.cpp
  )
  target_include_directories(imu-timestamp-policy-test PRIVATE src)
  target_compile_features(imu-timestamp-policy-test PRIVATE cxx_std_17)
  add_test(NAME imu-timestamp-policy COMMAND imu-timestamp-policy-test)

  add_executable(recording-startup-gate-test tests/recording_startup_gate_test.cpp)
  target_include_directories(recording-startup-gate-test PRIVATE src)
  target_compile_features(recording-startup-gate-test PRIVATE cxx_std_17)
  add_test(NAME recording-startup-gate COMMAND recording-startup-gate-test)

  add_executable(recording-usb-gate-test tests/recording_usb_gate_test.cpp)
  target_include_directories(recording-usb-gate-test PRIVATE src)
  target_compile_features(recording-usb-gate-test PRIVATE cxx_std_17)
  target_link_libraries(recording-usb-gate-test PRIVATE prism-viewer-sdk)
  add_test(NAME recording-usb-gate COMMAND recording-usb-gate-test)
  add_test(NAME recording-usb-ui COMMAND prism-viewer --recording-usb-self-test)
  set_tests_properties(recording-usb-ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

  add_executable(camera-frame-assembler-test
    src/transfer/camera_frame_assembler.cpp
    tests/camera_frame_assembler_test.cpp
  )
  target_include_directories(camera-frame-assembler-test PRIVATE src)
  target_compile_features(camera-frame-assembler-test PRIVATE cxx_std_17)
  target_link_libraries(camera-frame-assembler-test PRIVATE prism-viewer-sdk)
  add_test(NAME camera-frame-assembler COMMAND camera-frame-assembler-test)
  add_test(NAME camera-transfer-ui COMMAND prism-viewer --camera-transfer-self-test)
  set_tests_properties(camera-transfer-ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_OPENGL=software" TIMEOUT 30)

  add_executable(preview-image-decoder-test
    src/ui/preview_image_decoder.cpp
    tests/preview_image_decoder_test.cpp
  )
  target_include_directories(preview-image-decoder-test PRIVATE src)
  target_compile_features(preview-image-decoder-test PRIVATE cxx_std_17)
  target_link_libraries(preview-image-decoder-test PRIVATE ${QT_PACKAGE}::Gui)
  add_test(NAME preview-image-decoder COMMAND preview-image-decoder-test)
  add_executable(camera-zoom-dialog-test
    src/common/ui_text.cpp
    src/ui/camera_zoom_dialog.cpp
    src/ui/zoomable_image_view.cpp
    tests/camera_zoom_dialog_test.cpp
  )
  target_include_directories(camera-zoom-dialog-test PRIVATE src)
  target_compile_features(camera-zoom-dialog-test PRIVATE cxx_std_17)
  target_link_libraries(camera-zoom-dialog-test PRIVATE ${QT_PACKAGE}::Widgets)
  add_test(NAME camera-zoom-dialog COMMAND camera-zoom-dialog-test)
  set_tests_properties(camera-zoom-dialog PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_OPENGL=software" TIMEOUT 30)
  if(MSVC)
    target_compile_options(camera-zoom-dialog-test PRIVATE /utf-8)
  endif()
  add_executable(profile-capture-test tests/profile_capture_test.cpp)
  add_executable(bayer-preview-test
    src/common/ui_text.cpp src/ui/camera_zoom_dialog.cpp
    src/ui/zoomable_image_view.cpp src/ui/image_view_label.cpp
    tests/bayer_preview_test.cpp)
  target_include_directories(bayer-preview-test PRIVATE src)
  target_compile_features(bayer-preview-test PRIVATE cxx_std_17)
  target_link_libraries(bayer-preview-test PRIVATE ${QT_PACKAGE}::Widgets)
  add_test(NAME bayer-preview COMMAND bayer-preview-test)
  set_tests_properties(bayer-preview PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_OPENGL=software" TIMEOUT 60)
  if(MSVC)
    target_compile_options(bayer-preview-test PRIVATE /utf-8)
  endif()
  target_include_directories(profile-capture-test PRIVATE src)
  target_link_libraries(profile-capture-test PRIVATE prism-viewer-sdk)
  target_compile_features(profile-capture-test PRIVATE cxx_std_17)
  add_test(NAME profile-capture-options COMMAND profile-capture-test)
  add_executable(independent-preview-mailbox-test tests/independent_preview_mailbox_test.cpp)
  target_include_directories(independent-preview-mailbox-test PRIVATE src)
  target_compile_features(independent-preview-mailbox-test PRIVATE cxx_std_17)
  target_link_libraries(independent-preview-mailbox-test PRIVATE Threads::Threads)
  add_test(NAME independent-preview-mailbox COMMAND independent-preview-mailbox-test)
  add_executable(raw-preview-test tests/raw_preview_test.cpp)
  target_include_directories(raw-preview-test PRIVATE src "${PRISM_ZSTD_INCLUDE}")
  target_compile_features(raw-preview-test PRIVATE cxx_std_17)
  target_link_libraries(raw-preview-test PRIVATE prism-viewer-sdk ${QT_PACKAGE}::Gui "${PRISM_ZSTD_LIBRARY}")
  add_test(NAME raw-preview COMMAND raw-preview-test)
  add_executable(raw-dataset-test tests/raw_dataset_test.cpp)
  add_executable(raw-recorder-test tests/raw_recorder_test.cpp)
  target_link_libraries(raw-recorder-test PRIVATE prism-viewer-raw-dataset)
  target_include_directories(raw-recorder-test PRIVATE "${PRISM_ZSTD_INCLUDE}")
  add_test(NAME raw-recorder COMMAND raw-recorder-test)
  set_tests_properties(raw-recorder PROPERTIES TIMEOUT 90)
  target_link_libraries(raw-dataset-test PRIVATE prism-viewer-raw-dataset)
  target_include_directories(raw-dataset-test PRIVATE "${PRISM_ZSTD_INCLUDE}")
  add_test(NAME raw-dataset COMMAND raw-dataset-test)
  add_test(NAME raw-dataset-ui COMMAND raw-dataset-test --ui $<TARGET_FILE:prism-viewer>)
  set_tests_properties(raw-dataset-ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 65)
  if(WIN32)
    set_tests_properties(preview-image-decoder PROPERTIES
      ENVIRONMENT
        "PATH=$<TARGET_FILE_DIR:${QT_PACKAGE}::Core>\;$ENV{PATH}"
    )
  endif()

  add_executable(gnss-dataset-test tests/gnss_dataset_test.cpp)
  target_include_directories(gnss-dataset-test PRIVATE src/dataset)
  target_compile_features(gnss-dataset-test PRIVATE cxx_std_17)
  target_link_libraries(gnss-dataset-test PRIVATE prism-viewer-sdk)
  add_test(NAME gnss-dataset COMMAND gnss-dataset-test)

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

  add_executable(dataset-playback-test
    src/dataset/dataset_playback.cpp
    tests/dataset_playback_test.cpp
  )
  target_include_directories(dataset-playback-test PRIVATE src)
  target_compile_features(dataset-playback-test PRIVATE cxx_std_17)
  if(MSVC)
    target_compile_options(
      dataset-playback-test PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(
      dataset-playback-test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME dataset-playback COMMAND dataset-playback-test)

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
  add_executable(gnss-bag-exporter-test tests/gnss_bag_exporter_test.cpp src/dataset/rosbag_exporter.cpp)
  target_include_directories(gnss-bag-exporter-test PRIVATE src)
  target_compile_features(gnss-bag-exporter-test PRIVATE cxx_std_17)
  target_link_libraries(gnss-bag-exporter-test PRIVATE ${QT_PACKAGE}::Sql)
  add_test(NAME gnss-bag-exporter COMMAND gnss-bag-exporter-test)
  add_executable(xt32-dataset-test tests/xt32_dataset_test.cpp
    src/dataset/dataset_playback.cpp src/dataset/dataset_browser.cpp src/common/ui_text.cpp)
  target_include_directories(xt32-dataset-test PRIVATE src)
  target_compile_features(xt32-dataset-test PRIVATE cxx_std_17)
  target_link_libraries(xt32-dataset-test PRIVATE prism-viewer-sdk ${QT_PACKAGE}::Gui ${QT_PACKAGE}::Sql)
  add_test(NAME xt32-dataset COMMAND xt32-dataset-test)

  add_executable(mid-line-dataset-test tests/mid_line_dataset_test.cpp
    src/dataset/dataset_playback.cpp src/dataset/dataset_browser.cpp src/common/ui_text.cpp)
  target_include_directories(mid-line-dataset-test PRIVATE src)
  target_compile_features(mid-line-dataset-test PRIVATE cxx_std_17)
  target_link_libraries(mid-line-dataset-test PRIVATE prism-viewer-sdk ${QT_PACKAGE}::Gui ${QT_PACKAGE}::Sql)
  add_test(NAME mid-line-dataset COMMAND mid-line-dataset-test)
  foreach(dataset_target rk-dataset-test rk-dataset-dialog-test dataset-browser-test xt32-dataset-test mid-line-dataset-test)
    target_link_libraries(${dataset_target} PRIVATE prism-viewer-raw-dataset)
  endforeach()

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
    NAME dataset-playback-ui
    COMMAND prism-viewer --dataset-playback-self-test
            "${CMAKE_CURRENT_BINARY_DIR}/test-output/dataset-recorder-full"
  )
  add_test(
    NAME main-window-layout
    COMMAND prism-viewer --window-layout-self-test
  )
  add_test(
    NAME main-window-layout-compact
    COMMAND prism-viewer --window-layout-self-test --compact-layout-test
  )
  add_test(NAME main-window-layout-zh
    COMMAND prism-viewer --window-layout-self-test)
  add_test(NAME main-window-layout-compact-zh
    COMMAND prism-viewer --window-layout-self-test --compact-layout-test)
  set_tests_properties(
    dataset-recorder-full dataset-recorder-imu-only dataset-playback-ui
    PROPERTIES RUN_SERIAL TRUE
  )
  set_tests_properties(dataset-playback-ui PROPERTIES
    DEPENDS dataset-recorder-full
  )
  if(UNIX)
    set_tests_properties(
      dataset-recorder-full dataset-recorder-imu-only dataset-playback-ui
      main-window-layout
      main-window-layout-compact
      main-window-layout-zh main-window-layout-compact-zh
      PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
  endif()

  set_property(TEST main-window-layout main-window-layout-compact
    APPEND PROPERTY ENVIRONMENT "PRISM_VIEWER_LANG=en")
  set_property(TEST main-window-layout-zh main-window-layout-compact-zh
    APPEND PROPERTY ENVIRONMENT "PRISM_VIEWER_LANG=zh")

  add_executable(lidar-power-panel-test src/common/ui_text.cpp src/ui/lidar_power_panel.cpp tests/lidar_power_panel_test.cpp)
  target_include_directories(lidar-power-panel-test PRIVATE src)
  target_compile_features(lidar-power-panel-test PRIVATE cxx_std_17)
  target_link_libraries(lidar-power-panel-test PRIVATE prism-viewer-sdk ${QT_PACKAGE}::Widgets)
  add_test(NAME lidar-power-panel COMMAND lidar-power-panel-test)
  if(UNIX)
    set_tests_properties(lidar-power-panel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
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
  add_executable(native-camera-control-test tests/native_camera_control_test.cpp)
  target_include_directories(native-camera-control-test PRIVATE src)
  target_compile_features(native-camera-control-test PRIVATE cxx_std_17)
  target_link_libraries(native-camera-control-test PRIVATE prism-viewer-sdk)
  add_test(NAME native-camera-control COMMAND native-camera-control-test)
  add_executable(native-camera-control-panel-test
    src/common/ui_text.cpp src/ui/native_camera_control_panel.cpp tests/native_camera_control_panel_test.cpp)
  target_include_directories(native-camera-control-panel-test PRIVATE src)
  target_compile_features(native-camera-control-panel-test PRIVATE cxx_std_17)
  target_link_libraries(native-camera-control-panel-test PRIVATE prism-viewer-sdk ${QT_PACKAGE}::Widgets)
  add_test(NAME native-camera-control-panel COMMAND native-camera-control-panel-test)
  if(APPLE)
    # This test exercises QMacStyle too. Qt 6.8's native group-box painter
    # requires an NSView; the offscreen platform supplies only a synthetic ID.
    set_tests_properties(native-camera-control-panel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=cocoa")
  elseif(WIN32)
    # Match the real Viewer and the other Windows widget tests. Offscreen's
    # generic font database lacks the Windows system font integration and can
    # size every glyph as a missing-glyph box instead of the installed font.
    set_tests_properties(native-camera-control-panel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
  else()
    set_tests_properties(native-camera-control-panel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()
  foreach(native_test native-camera-control-test native-camera-control-panel-test)
    if(MSVC)
      target_compile_options(${native_test} PRIVATE /UNDEBUG /W4 /utf-8)
    else()
      target_compile_options(${native_test} PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    endif()
  endforeach()
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

  add_executable(cors-panel-test
    src/communication/prism_runtime.cpp
    src/common/ui_text.cpp
    src/communication/rtk_corrections.cpp
    src/cors/cors_config.cpp
    src/cors/cors_session.cpp
    src/ui/cors_panel.cpp
    tests/cors_panel_test.cpp
  )
  target_include_directories(cors-panel-test PRIVATE src)
  target_compile_features(cors-panel-test PRIVATE cxx_std_17)
  target_link_libraries(cors-panel-test PRIVATE
    prism-viewer-sdk
    ${QT_PACKAGE}::Widgets
    ${QT_PACKAGE}::Network
    Threads::Threads
  )
  add_test(NAME cors-panel COMMAND cors-panel-test)
  if(UNIX)
    set_tests_properties(cors-panel PROPERTIES
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
