#include "include/flutter_pcm_sound_spm/flutter_pcm_sound_spm_plugin.h"

#include <alsa/asoundlib.h>
#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

// ALSA implementation, shaped to match the Android plugin exactly: a
// dedicated playback thread takes buffers off a queue and writes them with a
// blocking snd_pcm_writei, then decides whether the feed callback is due.
//
// Why ALSA rather than PulseAudio or PipeWire: ALSA is the one API present on
// every Linux with working audio, and both of the others expose an
// ALSA-compatible device. One implementation covers every desktop, and the
// only cost is needing libasound2-dev at build time.

namespace {

constexpr char kChannelName[] = "flutter_pcm_sound/methods";

// How much audio ALSA buffers before it blocks us. Large enough to survive a
// scheduling hiccup, small enough that release() does not sit through a long
// tail of already-buffered sound.
constexpr unsigned int kLatencyMicros = 200000;  // 200ms

struct PluginState {
  FlMethodChannel* channel = nullptr;

  snd_pcm_t* pcm = nullptr;
  int num_channels = 1;
  bool did_setup = false;

  std::thread playback_thread;
  std::atomic<bool> should_stop{false};

  std::mutex mutex;
  std::condition_variable cv;
  std::deque<std::vector<uint8_t>> queue;
  int64_t queued_frames = 0;
  int64_t feed_threshold = 8000;

  // Generation counters. Each event fires at most once per feed(), which is
  // what "once per feed" in the Dart API means.
  int64_t total_feeds = 0;
  int64_t last_low_buffer_feed = -1;
  int64_t last_zero_feed = -1;
};

// Carries the callback from the playback thread to the GTK main loop.
// fl_method_channel_invoke_method must not be called off the main thread.
struct FeedCallbackData {
  FlMethodChannel* channel;
  int64_t remaining_frames;
};

gboolean DeliverFeedCallback(gpointer user_data) {
  auto* data = static_cast<FeedCallbackData*>(user_data);
  if (data->channel != nullptr) {
    g_autoptr(FlValue) args = fl_value_new_map();
    fl_value_set_string_take(args, "remaining_frames",
                             fl_value_new_int(data->remaining_frames));
    fl_method_channel_invoke_method(data->channel, "OnFeedSamples", args,
                                    nullptr, nullptr, nullptr);
  }
  delete data;
  return G_SOURCE_REMOVE;
}

void PostFeedCallback(PluginState* state, int64_t remaining_frames) {
  auto* data = new FeedCallbackData{state->channel, remaining_frames};
  g_idle_add(DeliverFeedCallback, data);
}

void PlaybackLoop(PluginState* state) {
  const int bytes_per_frame = 2 * state->num_channels;

  while (!state->should_stop.load()) {
    std::vector<uint8_t> chunk;
    {
      std::unique_lock<std::mutex> lock(state->mutex);
      state->cv.wait(lock, [state] {
        return !state->queue.empty() || state->should_stop.load();
      });
      if (state->should_stop.load()) break;
      chunk = std::move(state->queue.front());
      state->queue.pop_front();
    }

    const snd_pcm_uframes_t frames = chunk.size() / bytes_per_frame;
    const uint8_t* cursor = chunk.data();
    snd_pcm_uframes_t remaining = frames;

    while (remaining > 0 && !state->should_stop.load()) {
      snd_pcm_sframes_t written = snd_pcm_writei(state->pcm, cursor, remaining);
      if (written == -EPIPE) {
        // Underrun: the queue went empty for long enough that the device ran
        // dry. Recover and carry on — this is expected during a pause in the
        // audio stream, not an error worth surfacing.
        snd_pcm_prepare(state->pcm);
        continue;
      }
      if (written == -ESTRPIPE) {
        while (snd_pcm_resume(state->pcm) == -EAGAIN) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        snd_pcm_prepare(state->pcm);
        continue;
      }
      if (written < 0) break;  // unrecoverable; drop this chunk

      cursor += written * bytes_per_frame;
      remaining -= written;
    }

    bool notify = false;
    int64_t remaining_frames = 0;
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->queued_frames -= static_cast<int64_t>(frames);
      if (state->queued_frames < 0) state->queued_frames = 0;
      remaining_frames = state->queued_frames;

      const bool low_buffer = remaining_frames <= state->feed_threshold &&
                              state->last_low_buffer_feed != state->total_feeds;
      const bool zero = remaining_frames == 0 &&
                        state->last_zero_feed != state->total_feeds;
      if (low_buffer) state->last_low_buffer_feed = state->total_feeds;
      if (zero) state->last_zero_feed = state->total_feeds;
      notify = low_buffer || zero;
    }

    if (notify) PostFeedCallback(state, remaining_frames);
  }
}

void StopPlayback(PluginState* state) {
  state->should_stop.store(true);
  state->cv.notify_all();
  if (state->playback_thread.joinable()) state->playback_thread.join();

  if (state->pcm != nullptr) {
    snd_pcm_drop(state->pcm);
    snd_pcm_close(state->pcm);
    state->pcm = nullptr;
  }

  std::lock_guard<std::mutex> lock(state->mutex);
  state->queue.clear();
  state->queued_frames = 0;
  state->did_setup = false;
}

bool Setup(PluginState* state, int sample_rate, int num_channels,
           const char** error) {
  StopPlayback(state);
  state->should_stop.store(false);

  int rc = snd_pcm_open(&state->pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
  if (rc < 0) {
    *error = snd_strerror(rc);
    state->pcm = nullptr;
    return false;
  }

  rc = snd_pcm_set_params(state->pcm, SND_PCM_FORMAT_S16_LE,
                          SND_PCM_ACCESS_RW_INTERLEAVED, num_channels,
                          sample_rate, 1 /* allow resampling */,
                          kLatencyMicros);
  if (rc < 0) {
    *error = snd_strerror(rc);
    snd_pcm_close(state->pcm);
    state->pcm = nullptr;
    return false;
  }

  state->num_channels = num_channels;
  state->did_setup = true;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->queued_frames = 0;
    state->total_feeds = 0;
    state->last_low_buffer_feed = -1;
    state->last_zero_feed = -1;
  }

  state->playback_thread = std::thread(PlaybackLoop, state);
  return true;
}

int64_t GetIntArg(FlValue* args, const char* key, int64_t fallback) {
  if (args == nullptr || fl_value_get_type(args) != FL_VALUE_TYPE_MAP) {
    return fallback;
  }
  FlValue* value = fl_value_lookup_string(args, key);
  if (value == nullptr || fl_value_get_type(value) != FL_VALUE_TYPE_INT) {
    return fallback;
  }
  return fl_value_get_int(value);
}

}  // namespace

struct _FlutterPcmSoundSpmPlugin {
  GObject parent_instance;
  PluginState* state;
};

G_DEFINE_TYPE(FlutterPcmSoundSpmPlugin, flutter_pcm_sound_spm_plugin,
              g_object_get_type())

static void HandleMethodCall(FlutterPcmSoundSpmPlugin* self,
                             FlMethodCall* method_call) {
  g_autoptr(FlMethodResponse) response = nullptr;
  const gchar* method = fl_method_call_get_name(method_call);
  FlValue* args = fl_method_call_get_args(method_call);
  PluginState* state = self->state;

  if (strcmp(method, "setLogLevel") == 0) {
    g_autoptr(FlValue) ok = fl_value_new_bool(TRUE);
    response = FL_METHOD_RESPONSE(fl_method_success_response_new(ok));

  } else if (strcmp(method, "setup") == 0) {
    const int sample_rate =
        static_cast<int>(GetIntArg(args, "sample_rate", 44100));
    const int num_channels =
        static_cast<int>(GetIntArg(args, "num_channels", 1));
    const char* error = nullptr;
    if (!Setup(state, sample_rate, num_channels, &error)) {
      response = FL_METHOD_RESPONSE(fl_method_error_response_new(
          "AlsaError", error != nullptr ? error : "setup failed", nullptr));
    } else {
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
    }

  } else if (strcmp(method, "feed") == 0) {
    if (!state->did_setup) {
      response = FL_METHOD_RESPONSE(fl_method_error_response_new(
          "NotSetup", "feed() called before setup()", nullptr));
    } else {
      FlValue* buffer =
          args != nullptr ? fl_value_lookup_string(args, "buffer") : nullptr;
      const int bytes_per_frame = 2 * state->num_channels;
      size_t length = 0;
      const uint8_t* data = nullptr;
      if (buffer != nullptr &&
          fl_value_get_type(buffer) == FL_VALUE_TYPE_UINT8_LIST) {
        length = fl_value_get_length(buffer);
        data = fl_value_get_uint8_list(buffer);
      }
      // Trim a trailing partial frame rather than writing a fraction of one.
      const size_t usable = (length / bytes_per_frame) * bytes_per_frame;
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        // Every feed re-arms both events, including an empty one.
        ++state->total_feeds;
        if (usable > 0) {
          state->queue.emplace_back(data, data + usable);
          state->queued_frames += static_cast<int64_t>(usable / bytes_per_frame);
        }
      }
      if (usable > 0) state->cv.notify_one();
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
    }

  } else if (strcmp(method, "setFeedThreshold") == 0) {
    const int64_t threshold = GetIntArg(args, "feed_threshold", 0);
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->feed_threshold = threshold;
    }
    response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));

  } else if (strcmp(method, "release") == 0) {
    StopPlayback(state);
    response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));

  } else {
    response = FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
  }

  fl_method_call_respond(method_call, response, nullptr);
}

static void flutter_pcm_sound_spm_plugin_dispose(GObject* object) {
  FlutterPcmSoundSpmPlugin* self = FLUTTER_PCM_SOUND_SPM_PLUGIN(object);
  if (self->state != nullptr) {
    StopPlayback(self->state);
    if (self->state->channel != nullptr) {
      g_object_unref(self->state->channel);
      self->state->channel = nullptr;
    }
    delete self->state;
    self->state = nullptr;
  }
  G_OBJECT_CLASS(flutter_pcm_sound_spm_plugin_parent_class)->dispose(object);
}

static void flutter_pcm_sound_spm_plugin_class_init(
    FlutterPcmSoundSpmPluginClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = flutter_pcm_sound_spm_plugin_dispose;
}

static void flutter_pcm_sound_spm_plugin_init(FlutterPcmSoundSpmPlugin* self) {
  self->state = new PluginState();
}

static void method_call_cb(FlMethodChannel* channel, FlMethodCall* method_call,
                           gpointer user_data) {
  FlutterPcmSoundSpmPlugin* plugin = FLUTTER_PCM_SOUND_SPM_PLUGIN(user_data);
  HandleMethodCall(plugin, method_call);
}

void flutter_pcm_sound_spm_plugin_register_with_registrar(
    FlPluginRegistrar* registrar) {
  FlutterPcmSoundSpmPlugin* plugin = FLUTTER_PCM_SOUND_SPM_PLUGIN(
      g_object_new(flutter_pcm_sound_spm_plugin_get_type(), nullptr));

  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
  FlMethodChannel* channel = fl_method_channel_new(
      fl_plugin_registrar_get_messenger(registrar), kChannelName,
      FL_METHOD_CODEC(codec));

  // Held (not autoptr) because the playback thread posts callbacks onto it
  // for as long as the plugin lives; released in dispose.
  plugin->state->channel = FL_METHOD_CHANNEL(g_object_ref(channel));

  fl_method_channel_set_method_call_handler(
      channel, method_call_cb, g_object_ref(plugin), g_object_unref);

  g_object_unref(plugin);
  g_object_unref(channel);
}
