cmake_minimum_required(VERSION 3.20)
project(Blueward VERSION 0.1.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
  message(FATAL_ERROR "BLUEWARD is a Windows launcher.")
endif()
if(NOT MSVC)
  message(FATAL_ERROR "BLUEWARD is built with MSVC (Visual Studio Build Tools 2022).")
endif()

enable_language(RC)

# The version lives in one place; the resource compiler gets it from here so Explorer cannot show
# a different number than the About page.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/src/core/version.h" BLUEWARD_VERSION_LINE REGEX "kBluewardVersion = ")
string(REGEX MATCH "\"([0-9]+)\\.([0-9]+)\\.([0-9]+)\"" BLUEWARD_VERSION_MATCH "${BLUEWARD_VERSION_LINE}")
if(NOT BLUEWARD_VERSION_MATCH)
  message(FATAL_ERROR "could not read kBluewardVersion from src/core/version.h")
endif()
set(BLUEWARD_VER_MAJOR "${CMAKE_MATCH_1}")
set(BLUEWARD_VER_MINOR "${CMAKE_MATCH_2}")
set(BLUEWARD_VER_PATCH "${CMAKE_MATCH_3}")

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8>
                    $<$<COMPILE_LANGUAGE:CXX>:/W4>
                    $<$<COMPILE_LANGUAGE:CXX>:/permissive->
                    $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>
                    $<$<COMPILE_LANGUAGE:CXX>:/GR->)
add_link_options($<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)

# The 720p loop is embedded as RCDATA 101. CMake copies it beside the resource script so rc.exe
# can find it by name; a missing file simply leaves the gradient background in place.
option(BLUEWARD_EMBED_VIDEO "Embed the background clip (res/bg/bg-720.mp4)" ON)
set(BLUEWARD_BG_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/res/bg/bg-720.mp4")
set(BLUEWARD_BG_COPY "${CMAKE_CURRENT_BINARY_DIR}/bg-720.mp4")
set(BLUEWARD_BG_DEFINE "")
set(BLUEWARD_BG_FLAGS "")
set(BLUEWARD_BG_DEPS "")
if(BLUEWARD_EMBED_VIDEO AND EXISTS "${BLUEWARD_BG_SOURCE}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${BLUEWARD_BG_SOURCE}" "${BLUEWARD_BG_COPY}" RESULT_VARIABLE _blueward_copy)
  if(NOT _blueward_copy EQUAL 0)
    message(FATAL_ERROR "could not copy ${BLUEWARD_BG_SOURCE}")
  endif()
  set(BLUEWARD_BG_DEFINE ";BLUEWARD_BG_720")
  set(BLUEWARD_BG_FLAGS "/I\"${CMAKE_CURRENT_BINARY_DIR}\"")
  set(BLUEWARD_BG_DEPS ";${BLUEWARD_BG_COPY}")
else()
  message(STATUS "BLUEWARD: res/bg/bg-720.mp4 not found; building without the embedded background")
endif()

set_source_files_properties(res/app.rc PROPERTIES
  COMPILE_DEFINITIONS "BLUEWARD_VER_MAJOR=${BLUEWARD_VER_MAJOR};BLUEWARD_VER_MINOR=${BLUEWARD_VER_MINOR};BLUEWARD_VER_PATCH=${BLUEWARD_VER_PATCH}${BLUEWARD_BG_DEFINE}"
  COMPILE_FLAGS "${BLUEWARD_BG_FLAGS}"
  OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/res/blueward.ico;${CMAKE_CURRENT_SOURCE_DIR}/res/app.manifest${BLUEWARD_BG_DEPS}")

add_library(blueward_core STATIC
  src/core/backup.cpp
  src/core/config.cpp
  src/core/download.cpp
  src/core/errors.cpp
  src/core/feed.cpp
  src/core/fileio.cpp
  src/core/gamepath.cpp
  src/core/http.cpp
  src/core/installer.cpp
  src/core/json.cpp
  src/core/log.cpp
  src/core/manifest.cpp
  src/core/self_update.cpp
  src/core/sha256.cpp
  src/core/text.cpp
  src/core/zip.cpp
  third_party/miniz/miniz.c)
target_include_directories(blueward_core PUBLIC src third_party/miniz)
target_link_libraries(blueward_core PUBLIC bcrypt winhttp cabinet shell32 ole32 user32)

add_executable(BLUEWARD WIN32
  src/main.cpp
  src/app/app.cpp
  src/app/single_instance.cpp
  src/app/window.cpp
  src/ui/pages.cpp
  src/ui/renderer.cpp
  src/media/video.cpp
  res/app.rc)
target_link_libraries(BLUEWARD PRIVATE blueward_core
  d3d11 dxgi d2d1 dwrite dwmapi comdlg32 windowscodecs mfplat mfreadwrite mfuuid ole32 shell32)
set_target_properties(BLUEWARD PROPERTIES OUTPUT_NAME BLUEWARD)

# The release artifact the publisher hashes: dist\BLUEWARD.exe.
add_custom_command(TARGET BLUEWARD POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/dist"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:BLUEWARD>" "${CMAKE_CURRENT_SOURCE_DIR}/dist/BLUEWARD.exe"
  COMMENT "Copying BLUEWARD.exe to dist/")

enable_testing()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/tests/paths.h.in" "${CMAKE_CURRENT_BINARY_DIR}/paths.h" @ONLY)

add_executable(blueward_tests
  tests/test_main.cpp
  tests/test_json.cpp
  tests/test_sha256.cpp
  tests/test_manifest.cpp
  tests/test_zip.cpp
  tests/test_http.cpp
  tests/test_download.cpp
  tests/test_backup.cpp
  tests/test_gamepath.cpp
  tests/test_config.cpp
  tests/test_installer.cpp
  tests/test_self_update.cpp
  tests/test_single_instance.cpp
  tests/test_pages.cpp
  tests/test_feed.cpp
  tests/test_log.cpp
  tests/test_source_scan.cpp
  src/app/single_instance.cpp
  src/ui/pages.cpp)
target_include_directories(blueward_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" tests)
target_link_libraries(blueward_tests PRIVATE blueward_core ws2_32 cabinet)
add_test(NAME core COMMAND blueward_tests)
