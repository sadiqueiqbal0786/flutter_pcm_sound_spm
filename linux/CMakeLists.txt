cmake_minimum_required(VERSION 3.10)
set(PROJECT_NAME "flutter_pcm_sound_spm")
project(${PROJECT_NAME} LANGUAGES CXX)

set(PLUGIN_NAME "flutter_pcm_sound_spm_plugin")

# ALSA is the lowest common denominator on Linux: PulseAudio and PipeWire
# both expose an ALSA-compatible device, so one implementation covers every
# desktop. Requires libasound2-dev at build time.
find_package(PkgConfig REQUIRED)
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)

add_library(${PLUGIN_NAME} SHARED
  "flutter_pcm_sound_spm_plugin.cc"
)

apply_standard_settings(${PLUGIN_NAME})

set_target_properties(${PLUGIN_NAME} PROPERTIES CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${PLUGIN_NAME} PRIVATE flutter)
target_link_libraries(${PLUGIN_NAME} PRIVATE PkgConfig::GTK PkgConfig::ALSA)

set(flutter_pcm_sound_spm_bundled_libraries
  ""
  PARENT_SCOPE
)
