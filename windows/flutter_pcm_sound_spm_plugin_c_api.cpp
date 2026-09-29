#include "include/flutter_pcm_sound_spm/flutter_pcm_sound_spm_plugin_c_api.h"

#include <flutter/plugin_registrar_windows.h>

#include "flutter_pcm_sound_spm_plugin.h"

void FlutterPcmSoundSpmPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  flutter_pcm_sound_spm::FlutterPcmSoundSpmPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
