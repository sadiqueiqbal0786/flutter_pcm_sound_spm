#ifndef FLUTTER_PLUGIN_FLUTTER_PCM_SOUND_SPM_PLUGIN_H_
#define FLUTTER_PLUGIN_FLUTTER_PCM_SOUND_SPM_PLUGIN_H_

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>
#include <windows.h>
#include <xaudio2.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace flutter_pcm_sound_spm {

// Plays 16-bit PCM through XAudio2.
//
// Why XAudio2 rather than WASAPI: a source voice already *is* the abstraction
// this plugin exposes. You submit buffers, it plays them in order, and it
// tells you when each one is done — which is exactly `feed`, exactly gapless
// playback, and exactly the signal needed to decide "the queue is running
// low". WASAPI would mean writing that ring buffer and its event loop by hand
// for no gain here.
class FlutterPcmSoundSpmPlugin : public flutter::Plugin {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrarWindows* registrar);

  FlutterPcmSoundSpmPlugin();
  virtual ~FlutterPcmSoundSpmPlugin();

  FlutterPcmSoundSpmPlugin(const FlutterPcmSoundSpmPlugin&) = delete;
  FlutterPcmSoundSpmPlugin& operator=(const FlutterPcmSoundSpmPlugin&) = delete;

 private:
  // XAudio2 calls these back on its own audio thread. Nothing here may touch
  // the method channel directly — see PostFeedCallback.
  class VoiceCallback : public IXAudio2VoiceCallback {
   public:
    explicit VoiceCallback(FlutterPcmSoundSpmPlugin* plugin) : plugin_(plugin) {}
    void STDMETHODCALLTYPE OnBufferEnd(void* context) noexcept override;

    void STDMETHODCALLTYPE OnStreamEnd() noexcept override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() noexcept override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) noexcept override {}
    void STDMETHODCALLTYPE OnBufferStart(void*) noexcept override {}
    void STDMETHODCALLTYPE OnLoopEnd(void*) noexcept override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) noexcept override {}

   private:
    FlutterPcmSoundSpmPlugin* plugin_;
  };

  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue>& call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  bool Setup(int sample_rate, int num_channels, std::string* error);
  void Feed(const std::vector<uint8_t>& bytes);
  void Release();

  // Called from the XAudio2 thread when a buffer finishes.
  void OnBufferFinished();

  // Hops from the audio thread to the platform thread. Flutter's method
  // channels are NOT thread-safe: invoking one from the XAudio2 thread is a
  // crash that only shows up under load. A message-only window gives us a
  // queue onto the thread that owns the engine.
  void PostFeedCallback();
  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  void DeliverFeedCallback();

  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel_;

  IXAudio2* xaudio2_ = nullptr;
  IXAudio2MasteringVoice* mastering_voice_ = nullptr;
  IXAudio2SourceVoice* source_voice_ = nullptr;
  VoiceCallback voice_callback_{this};

  HWND message_window_ = nullptr;

  int num_channels_ = 1;
  bool did_setup_ = false;
  bool com_initialized_ = false;

  // Buffers handed to XAudio2. It does NOT copy them, so each must stay
  // alive and at a stable address until its OnBufferEnd. A deque never moves
  // existing elements, and a source voice completes buffers in submission
  // order, so front() is always the one that just finished.
  std::mutex mutex_;
  std::deque<std::vector<uint8_t>> queued_buffers_;
  int64_t queued_frames_ = 0;
  int64_t feed_threshold_ = 8000;

  // Generation counters, matching the Android implementation: each event may
  // fire at most once per feed(). Comparing against total_feeds_ is what
  // enforces "once".
  int64_t total_feeds_ = 0;
  int64_t last_low_buffer_feed_ = -1;
  int64_t last_zero_feed_ = -1;
};

}  // namespace flutter_pcm_sound_spm

#endif  // FLUTTER_PLUGIN_FLUTTER_PCM_SOUND_SPM_PLUGIN_H_
