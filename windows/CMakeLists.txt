cmake_minimum_required(VERSION 3.14)
set(PROJECT_NAME "flutter_pcm_sound_spm")
project(${PROJECT_NAME} LANGUAGES CXX)

# This value is used when generating builds using this plugin, so it must
# not be changed.
set(PLUGIN_NAME "${PROJECT_NAME}_plugin")

add_library(${PLUGIN_NAME} SHARED
  "flutter_pcm_sound_spm_plugin.cpp"
  "flutter_pcm_sound_spm_plugin_c_api.cpp"
)

apply_standard_settings(${PLUGIN_NAME})

set_target_properties(${PLUGIN_NAME} PROPERTIES CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")

# xaudio2_9redist is NOT needed: XAudio2 2.9 ships in Windows 10 and later,
# which is also Flutter's minimum for Windows.
target_link_libraries(${PLUGIN_NAME} PRIVATE flutter flutter_wrapper_plugin xaudio2)

set(flutter_pcm_sound_spm_bundled_libraries
  ""
  PARENT_SCOPE
)
