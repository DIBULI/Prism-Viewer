cmake_minimum_required(VERSION 3.20)

project(prism_viewer VERSION 0.9.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC OFF)
set(CMAKE_AUTOUIC OFF)

set(PRISM_USB_SDK_ROOT "" CACHE PATH
    "Installed Prism Host SDK prefix containing lib/cmake/PrismUsbSdk")
if(PRISM_USB_SDK_ROOT)
  list(PREPEND CMAKE_PREFIX_PATH "${PRISM_USB_SDK_ROOT}")
endif()
if(NOT TARGET Prism::UsbSdk)
  find_package(PrismUsbSdk 0.9.0 EXACT CONFIG REQUIRED)
endif()

find_package(Qt6 QUIET COMPONENTS Widgets Charts Sql)
if(Qt6_FOUND)
  set(QT_PACKAGE Qt6)
else()
  find_package(Qt5 REQUIRED COMPONENTS Widgets Charts Sql)
  set(QT_PACKAGE Qt5)
endif()

add_executable(prism-viewer
  resources/prism.qrc
  src/common/ui_text.cpp
  src/communication/device_session.cpp
  src/control/operation_controller.cpp
  src/dataset/dataset_browser.cpp
  src/dataset/rosbag_exporter.cpp
  src/dual_imu_offset_estimator.cpp
  src/main.cpp
  src/transfer/camera_frame_assembler.cpp
  src/ui/camera_zoom_dialog.cpp
  src/ui/camera_encoding_panel.cpp
  src/ui/camera_exposure_panel.cpp
  src/ui/device_info_panel.cpp
  src/ui/lidar_point_cloud_widget.cpp
  src/ui/main_window.cpp
  src/ui/preview_image_decoder.cpp
  src/ui/wifi_hotspot_panel.cpp
  src/ui/zoomable_image_view.cpp
)
set_target_properties(prism-viewer PROPERTIES AUTORCC ON)

if(APPLE)
  set_target_properties(prism-viewer PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "Prism Viewer"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.dibuli.prism-viewer"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    OUTPUT_NAME "Prism Viewer"
  )
endif()

target_compile_features(prism-viewer PRIVATE cxx_std_17)
target_include_directories(prism-viewer PRIVATE src)

target_link_libraries(prism-viewer PRIVATE
  Prism::UsbSdk
  ${QT_PACKAGE}::Widgets
  ${QT_PACKAGE}::Charts
  ${QT_PACKAGE}::Sql
)

# Keep the SDK runtime beside development/test executables. Prism-Viewer only
# consumes the installed public SDK target; it never compiles SDK sources.
function(prism_copy_sdk_runtime target)
  if(NOT APPLE)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "$<TARGET_FILE:Prism::UsbSdk>"
              "$<TARGET_FILE_DIR:${target}>"
      VERBATIM
    )
  endif()
endfunction()
prism_copy_sdk_runtime(prism-viewer)

if(UNIX AND NOT APPLE)
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
endif()

install(TARGETS prism-viewer
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  BUNDLE DESTINATION .
)
if(WIN32)
  install(FILES "$<TARGET_FILE:Prism::UsbSdk>"
    DESTINATION ${CMAKE_INSTALL_BINDIR})
elseif(UNIX AND NOT APPLE)
  install(FILES "$<TARGET_FILE:Prism::UsbSdk>"
    DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
if(UNIX AND NOT APPLE)
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

include(CTest)
if(BUILD_TESTING)
  add_executable(imu-timestamp-policy-test
    tests/imu_timestamp_policy_test.cpp
  )
  target_include_directories(imu-timestamp-policy-test PRIVATE src)
  target_compile_features(imu-timestamp-policy-test PRIVATE cxx_std_17)
  add_test(NAME imu-timestamp-policy COMMAND imu-timestamp-policy-test)

  add_executable(dual-imu-offset-estimator-test
    src/dual_imu_offset_estimator.cpp
    tests/dual_imu_offset_estimator_test.cpp
  )
  target_include_directories(dual-imu-offset-estimator-test PRIVATE src)
  target_compile_features(dual-imu-offset-estimator-test PRIVATE cxx_std_17)
  add_test(NAME dual-imu-offset-estimator COMMAND dual-imu-offset-estimator-test)

  add_executable(camera-frame-assembler-test
    src/transfer/camera_frame_assembler.cpp
    tests/camera_frame_assembler_test.cpp
  )
  target_include_directories(camera-frame-assembler-test PRIVATE src)
  target_compile_features(camera-frame-assembler-test PRIVATE cxx_std_17)
  target_link_libraries(camera-frame-assembler-test PRIVATE Prism::UsbSdk)
  prism_copy_sdk_runtime(camera-frame-assembler-test)
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

  add_executable(camera-encoding-panel-test
    src/common/ui_text.cpp
    src/ui/camera_encoding_panel.cpp
    tests/camera_encoding_panel_test.cpp
  )
  target_include_directories(camera-encoding-panel-test PRIVATE src)
  target_compile_features(camera-encoding-panel-test PRIVATE cxx_std_17)
  target_link_libraries(camera-encoding-panel-test PRIVATE
    Prism::UsbSdk
    ${QT_PACKAGE}::Widgets
  )
  prism_copy_sdk_runtime(camera-encoding-panel-test)
  add_test(NAME camera-encoding-panel COMMAND camera-encoding-panel-test)
  if(UNIX)
    set_tests_properties(camera-encoding-panel PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
  endif()

endif()
