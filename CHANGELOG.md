## 1.2.0

* **Windows and Linux support**, completing coverage of every Flutter
  platform.
  * **Windows** uses XAudio2. A source voice already is this plugin's
    abstraction — submit buffers, get told when each finishes — so the queue
    and its "running low" signal come from the API rather than being
    hand-rolled on top of WASAPI. Needs no redistributable: XAudio2 2.9 ships
    in Windows 10, which is Flutter's minimum.
  * **Linux** uses ALSA. PulseAudio and PipeWire both expose an
    ALSA-compatible device, so one implementation covers every desktop.
    **Requires `libasound2-dev` at build time** (`sudo apt install
    libasound2-dev`).
  * Both match the Android callback contract exactly, including the
    generation counters that make each event fire at most once per `feed()`.
  * The feed callback is marshalled onto the platform thread on both — a
    message-only window on Windows, `g_idle_add` on Linux. Flutter method
    channels are not thread-safe, and calling one from an audio thread fails
    intermittently rather than obviously.
  * ALSA underruns (`-EPIPE`) are recovered rather than reported: they are
    expected whenever the audio stream pauses.
* The example app now runs on all six platforms.

## 1.1.0

* **Web support.** New platform implementation backed by the Web Audio API,
  answering the same method channel as the native plugins — the Dart API is
  identical on web, so nothing in your code changes.
  * Feeds are scheduled on the `AudioContext` clock so consecutive buffers
    play gaplessly; "remaining frames" is how far that schedule runs ahead of
    the clock.
  * The feed callback keeps its contract: one low-buffer event and one
    drained-to-zero event per `feed()`.
  * Written with `package:web` and `dart:js_interop`, so it compiles to
    WebAssembly as well as JavaScript.
  * Browsers block audio until the page has been interacted with; the context
    is resumed on each feed, so playback begins at the first user gesture.
* Added an example app (plays a C major scale) that runs on all four
  platforms.
* `PcmArrayInt16`'s `[]` and `[]=` now declare their return types.
* Shortened the package description to fit pub.dev's 180-character limit.
* Minimum SDK is now Dart 3.3 / Flutter 3.19, which `package:web` requires.

## 1.0.0

First release, forked from `flutter_pcm_sound` 3.3.3.

* Swift Package Manager support on iOS and macOS. CocoaPods still works, so a
  project can move either way.
* Android compiles against Flutter's configured SDK instead of a pinned 33
  (the fix on upstream's `master` that was never released).
* Android `minSdk` 21 and Java 17.
* Android package renamed to `com.sadiqueiqbal.flutter_pcm_sound_spm` so it
  cannot collide with the original.
* Dart API unchanged from 3.3.3.
