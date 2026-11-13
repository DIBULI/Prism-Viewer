# CI supplies one identity to every platform, independently of container Git access.
set(PRISM_VIEWER_REVISION "" CACHE STRING "Source commit embedded in Viewer")
set(PRISM_VIEWER_BUILD_EPOCH "" CACHE STRING "Source commit epoch embedded in Viewer")
option(PRISM_REQUIRE_RELEASE_IDENTITY "Reject unidentified release builds" OFF)
if(PRISM_VIEWER_REVISION STREQUAL "" AND PRISM_VIEWER_BUILD_EPOCH STREQUAL "")
  execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE PRISM_VIEWER_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  execute_process(COMMAND git show -s --format=%ct HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE PRISM_VIEWER_BUILD_EPOCH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(PRISM_VIEWER_REVISION STREQUAL "" OR PRISM_VIEWER_BUILD_EPOCH STREQUAL "")
    if(PRISM_REQUIRE_RELEASE_IDENTITY)
      message(FATAL_ERROR "Release build requires an explicit source revision and commit epoch")
    endif()
    set(PRISM_VIEWER_REVISION "source-build")
    set(PRISM_VIEWER_BUILD_EPOCH 0)
    message(WARNING "Source identity unavailable; this build must not be published")
    return()
  endif()
endif()
string(LENGTH "${PRISM_VIEWER_REVISION}" _prism_revision_length)
if(NOT _prism_revision_length EQUAL 40 OR
   NOT PRISM_VIEWER_REVISION MATCHES "^[0-9a-f]+$" OR
   NOT PRISM_VIEWER_BUILD_EPOCH MATCHES "^[1-9][0-9]*$" OR
   PRISM_VIEWER_BUILD_EPOCH GREATER 9007199254740991)
  message(FATAL_ERROR "Invalid Viewer source revision or commit epoch")
endif()
