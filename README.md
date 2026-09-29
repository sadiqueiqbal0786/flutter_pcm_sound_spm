# flutter_pcm_sound_spm

Send real-time PCM audio (16-bit integer) to the device speakers.

> **This is not original work.** It is a fork of
> [`flutter_pcm_sound`](https://pub.dev/packages/flutter_pcm_sound) by
> **Chip Weinberger**, adding **Swift Package Manager support** on iOS and
> macOS. The audio code is his. See [NOTICE](NOTICE) for exactly what is his
> and what this fork changed.

## Why this exists

Flutter builds now warn:

> The following plugins do not support Swift Package Manager for ios:
> `flutter_pcm_sound`. This will become an error in a future version of Flutter.

The upstream package is CocoaPods-only. SPM support has been
[an open request since May 2026](https://github.com/chipweinberger/flutter_pcm_sound/issues/50)
with no work on it, and the last release was October 2025. This fork does that
migration so the plugin keeps building, and so a project whose only remaining
pod was this one can drop CocoaPods entirely.

**Upstream is being retired.** The SPM change was offered back as
[PR #54](https://github.com/chipweinberger/flutter_pcm_sound/pull/54); on
2026-09-29 Chip replied that the package "will become archived soon" and
recommended forking. So this is not a temporary divergence waiting to be
merged — upstream will not be adopting SPM, and this fork exists because of
that, with his recommendation.

It also carries a fix that exists on upstream's `master` but has never been
released: the Android module pinned `compileSdkVersion 33`, which breaks
against newer toolchains, and now follows Flutter's configured SDK.

## Differences from the original

| | `flutter_pcm_sound` | this fork |
|---|---|---|
| iOS / macOS build | CocoaPods only | **Swift Package Manager** *and* CocoaPods |
| Web | not supported | **supported** (Web Audio, wasm-compatible) |
| Android `compileSdk` | pinned to 33 | follows `flutter.compileSdkVersion` |
| Android `minSdk` | 19 | 21 |
| Java | 1.8 | 17 |
| Android package | `com.lib.flutter_pcm_sound` | `com.sadiqueiqbal.flutter_pcm_sound_spm` |

**The Dart API is unchanged** — same class names, same methods, same method
channel. Migration is one line in `pubspec.yaml` and one import.

## Usage

```yaml
dependencies:
  flutter_pcm_sound_spm: ^1.1.0
```

```dart
import 'package:flutter_pcm_sound_spm/flutter_pcm_sound_spm.dart';

await FlutterPcmSound.setup(sampleRate: 24000, channelCount: 1);
await FlutterPcmSound.setFeedThreshold(2400);
FlutterPcmSound.setFeedCallback((remaining) => feedMore());
FlutterPcmSound.feed(PcmArrayInt16.fromList(samples));
await FlutterPcmSound.release();
```

Do not depend on this and `flutter_pcm_sound` at the same time: they share a
method channel name, and only one of them can own it.

See [`example/`](example) for a runnable app — it plays a C major scale and
shows the feed loop, which is the part worth copying.

### On web

The Dart API is the same, with two behaviours worth knowing:

* **Audio will not start until the user has interacted with the page.** Every
  browser blocks it. Trigger `setup`/`feed` from a tap or click, not on load.
* `iosAudioCategory` and `iosAllowBackgroundAudio` are accepted and ignored —
  they are iOS audio-session settings with no web equivalent.

## Credit and licence

The audio implementation is Chip Weinberger's, unchanged apart from being
moved into the layout Flutter's SPM integration expects. The original is
released into the public domain under [the Unlicense](LICENSE), which is what
makes this fork possible; this fork keeps that licence.

The web implementation is the one part that is not his: upstream has no web
support, and that code is new in this fork.
