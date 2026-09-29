#include "flutter_pcm_sound_spm_plugin.h"

#include <flutter/method_result_functions.h>

#include <algorithm>
#include <string>

namespace flutter_pcm_sound_spm {

namespace {
constexpr char kChannelName[] = "flutter_pcm_sound/methods";
constexpr wchar_t kWindowClass[] = L"FlutterPcmSoundSpmMessageWindow";
constexpr UINT kFeedCallbackMessage = WM_APP + 0x51;
constexpr int kBitsPerSample = 16;

template <typename T>
bool GetArg(const flutter::EncodableMap& map, const char* key, T* out) {
  auto it = map.find(flutter::EncodableValue(key));
  if (it == map.end()) return false;
  if (const auto* value = std::get_if<T>(&it->second)) {
    *out = *value;
    return true;
  }
  return false;
}
}  // namespace

void FlutterPcmSoundSpmPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows* registrar) {
  auto plugin = std::make_unique<FlutterPcmSoundSpmPlugin>();

  plugin->channel_ =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(), kChannelName,
          &flutter::StandardMethodCodec::GetInstance());

  auto* raw = plugin.get();
  plugin->channel_->SetMethodCallHandler(
      [raw](const auto& call, auto result) {
        raw->HandleMethodCall(call, std::move(result));
      });

  registrar->AddPlugin(std::move(plugin));
}

FlutterPcmSoundSpmPlugin::FlutterPcmSoundSpmPlugin() {
  // The message-only window must be created on the platform thread, which is
  // where the plugin is constructed. Its only job is to carry the feed
  // callback back from the audio thread.
  WNDCLASSW wc = {};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = kWindowClass;
  RegisterClassW(&wc);

  message_window_ =
      CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                      nullptr, GetModuleHandle(nullptr), this);
  if (message_window_) {
    SetWindowLongPtr(message_window_, GWLP_USERDATA,
                     reinterpret_cast<LONG_PTR>(this));
  }
}

FlutterPcmSoundSpmPlugin::~FlutterPcmSoundSpmPlugin() {
  Release();
  if (message_window_) {
    DestroyWindow(message_window_);
    message_window_ = nullptr;
  }
  if (com_initialized_) {
    CoUninitialize();
    com_initialized_ = false;
  }
}

LRESULT CALLBACK FlutterPcmSoundSpmPlugin::WndProc(HWND hwnd, UINT message,
                                                   WPARAM wparam,
                                                   LPARAM lparam) {
  if (message == kFeedCallbackMessage) {
    auto* plugin = reinterpret_cast<FlutterPcmSoundSpmPlugin*>(
        GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (plugin) plugin->DeliverFeedCallback();
    return 0;
  }
  return DefWindowProc(hwnd, message, wparam, lparam);
}

void FlutterPcmSoundSpmPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue>& call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  const std::string& method = call.method_name();

  if (method == "setLogLevel") {
    result->Success(flutter::EncodableValue(true));
    return;
  }

  const auto* args = std::get_if<flutter::EncodableMap>(call.arguments());

  if (method == "setup") {
    int sample_rate = 44100;
    int num_channels = 1;
    if (args) {
      GetArg(*args, "sample_rate", &sample_rate);
      GetArg(*args, "num_channels", &num_channels);
    }
    std::string error;
    if (!Setup(sample_rate, num_channels, &error)) {
      result->Error("XAudio2Error", error);
      return;
    }
    result->Success();
    return;
  }

  if (method == "feed") {
    if (!did_setup_) {
      result->Error("NotSetup", "feed() called before setup()");
      return;
    }
    std::vector<uint8_t> bytes;
    if (args) GetArg(*args, "buffer", &bytes);
    Feed(bytes);
    result->Success();
    return;
  }

  if (method == "setFeedThreshold") {
    int threshold = 0;
    if (args) GetArg(*args, "feed_threshold", &threshold);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      feed_threshold_ = threshold;
    }
    result->Success();
    return;
  }

  if (method == "release") {
    Release();
    result->Success();
    return;
  }

  result->NotImplemented();
}

bool FlutterPcmSoundSpmPlugin::Setup(int sample_rate, int num_channels,
                                     std::string* error) {
  Release();

  if (!com_initialized_) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // RPC_E_CHANGED_MODE means COM is already up in another mode, which is
    // fine — XAudio2 works either way. Only a real failure aborts.
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
      *error = "CoInitializeEx failed";
      return false;
    }
    com_initialized_ = (hr != RPC_E_CHANGED_MODE);
  }

  if (FAILED(XAudio2Create(&xaudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
    *error = "XAudio2Create failed";
    return false;
  }
  if (FAILED(xaudio2_->CreateMasteringVoice(&mastering_voice_))) {
    *error = "CreateMasteringVoice failed";
    Release();
    return false;
  }

  WAVEFORMATEX format = {};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = static_cast<WORD>(num_channels);
  format.nSamplesPerSec = static_cast<DWORD>(sample_rate);
  format.wBitsPerSample = kBitsPerSample;
  format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
  format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

  if (FAILED(xaudio2_->CreateSourceVoice(&source_voice_, &format, 0,
                                         XAUDIO2_DEFAULT_FREQ_RATIO,
                                         &voice_callback_))) {
    *error = "CreateSourceVoice failed";
    Release();
    return false;
  }

  if (FAILED(source_voice_->Start(0))) {
    *error = "SourceVoice Start failed";
    Release();
    return false;
  }

  num_channels_ = num_channels;
  did_setup_ = true;

  std::lock_guard<std::mutex> lock(mutex_);
  queued_frames_ = 0;
  total_feeds_ = 0;
  last_low_buffer_feed_ = -1;
  last_zero_feed_ = -1;
  return true;
}

void FlutterPcmSoundSpmPlugin::Feed(const std::vector<uint8_t>& bytes) {
  const int bytes_per_frame = 2 * num_channels_;
  if (bytes.size() < static_cast<size_t>(bytes_per_frame)) {
    // Still counts as a feed: it re-arms both events, exactly as an empty
    // feed does on the other platforms.
    std::lock_guard<std::mutex> lock(mutex_);
    ++total_feeds_;
    return;
  }

  // Trim any trailing partial frame rather than handing XAudio2 a buffer
  // whose length is not a whole number of frames.
  const size_t usable = (bytes.size() / bytes_per_frame) * bytes_per_frame;

  XAUDIO2_BUFFER buffer = {};
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queued_buffers_.emplace_back(bytes.begin(), bytes.begin() + usable);
    queued_frames_ += static_cast<int64_t>(usable / bytes_per_frame);
    ++total_feeds_;

    buffer.AudioBytes = static_cast<UINT32>(usable);
    buffer.pAudioData = queued_buffers_.back().data();
  }

  if (source_voice_) source_voice_->SubmitSourceBuffer(&buffer);
}

void FlutterPcmSoundSpmPlugin::VoiceCallback::OnBufferEnd(void*) noexcept {
  plugin_->OnBufferFinished();
}

void FlutterPcmSoundSpmPlugin::OnBufferFinished() {
  bool should_notify = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queued_buffers_.empty()) return;

    const int bytes_per_frame = 2 * num_channels_;
    queued_frames_ -= static_cast<int64_t>(queued_buffers_.front().size() /
                                           bytes_per_frame);
    if (queued_frames_ < 0) queued_frames_ = 0;
    queued_buffers_.pop_front();

    // Same rule as Android: each event fires at most once per feed, and the
    // two are collapsed into a single callback when they coincide.
    const bool low_buffer = queued_frames_ <= feed_threshold_ &&
                            last_low_buffer_feed_ != total_feeds_;
    const bool zero = queued_frames_ == 0 && last_zero_feed_ != total_feeds_;
    if (low_buffer) last_low_buffer_feed_ = total_feeds_;
    if (zero) last_zero_feed_ = total_feeds_;
    should_notify = low_buffer || zero;
  }

  if (should_notify) PostFeedCallback();
}

void FlutterPcmSoundSpmPlugin::PostFeedCallback() {
  if (message_window_) {
    PostMessage(message_window_, kFeedCallbackMessage, 0, 0);
  }
}

void FlutterPcmSoundSpmPlugin::DeliverFeedCallback() {
  int64_t remaining;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    remaining = queued_frames_;
  }
  if (!channel_) return;
  channel_->InvokeMethod(
      "OnFeedSamples",
      std::make_unique<flutter::EncodableValue>(flutter::EncodableMap{
          {flutter::EncodableValue("remaining_frames"),
           flutter::EncodableValue(remaining)},
      }));
}

void FlutterPcmSoundSpmPlugin::Release() {
  if (source_voice_) {
    source_voice_->Stop(0);
    source_voice_->FlushSourceBuffers();
    source_voice_->DestroyVoice();
    source_voice_ = nullptr;
  }
  if (mastering_voice_) {
    mastering_voice_->DestroyVoice();
    mastering_voice_ = nullptr;
  }
  if (xaudio2_) {
    xaudio2_->Release();
    xaudio2_ = nullptr;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  queued_buffers_.clear();
  queued_frames_ = 0;
  did_setup_ = false;
}

}  // namespace flutter_pcm_sound_spm
