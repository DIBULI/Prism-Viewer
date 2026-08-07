cmake_minimum_required(VERSION 3.20)

project(prism_viewer VERSION 0.8.0 LANGUAGES CXX)

set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC OFF)
set(CMAKE_AUTOUIC OFF)

if(NOT TARGET prism_usb_sdk)
  set(PRISM_USB_SDK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../usb-sdk"
                   "${CMAKE_CURRENT_BINARY_DIR}/usb-sdk")
endif()

find_package(Qt6 QUIET COMPONENTS Widgets Charts)
if(Qt6_FOUND)
  set(QT_PACKAGE Qt6)
else()
  find_package(Qt5 REQUIRED COMPONENTS Widgets Charts)
  set(QT_PACKAGE Qt5)
endif()

add_executable(prism-viewer
  resources/prism.qrc
  src/common/ui_text.cpp
  src/communication/device_session.cpp
  src/control/operation_controller.cpp
  src/dataset/dataset_browser.cpp
  src/dual_imu_offset_estimator.cpp
  src/main.cpp
  src/transfer/camera_frame_assembler.cpp
  src/ui/camera_zoom_dialog.cpp
  src/ui/camera_exposure_panel.cpp
  src/ui/device_info_panel.cpp
  src/ui/main_window.cpp
  src/ui/preview_image_decoder.cpp
  src/ui/wifi_hotspot_panel.cpp
  src/ui/zoomable_image_view.cpp
)
set_target_properties(prism-viewer PROPERTIES AUTORCC ON)

target_compile_features(prism-viewer PRIVATE cxx_std_17)
target_include_directories(prism-viewer PRIVATE src)

target_link_libraries(prism-viewer PRIVATE
  prism_usb_sdk
  ${QT_PACKAGE}::Widgets
  ${QT_PACKAGE}::Charts
)

if(MSVC)
  target_compile_options(prism-viewer PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(prism-viewer PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
  target_sources(prism-viewer PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../branding/prism-viewer.rc"
  )
  set_property(SOURCE
    "${CMAKE_CURRENT_LIST_DIR}/../branding/prism-viewer.rc"
    APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/../branding/prism-viewer.ico"
  )
  set_target_properties(prism-viewer PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

include(GNUInstallDirs)
install(TARGETS prism-viewer
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(UNIX AND NOT APPLE)
  install(FILES packaging/prism-viewer.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
  )
  install(FILES ../branding/prism-mark-256.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
    RENAME prism-viewer.png
  )
  install(FILES ../branding/prism-mark.svg
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
  target_link_libraries(camera-frame-assembler-test PRIVATE prism_usb_sdk)
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

endif()
