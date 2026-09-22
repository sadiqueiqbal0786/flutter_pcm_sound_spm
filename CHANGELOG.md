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
