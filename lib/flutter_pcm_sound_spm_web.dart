// Web implementation for flutter_pcm_sound_spm.
//
// The original package (Chip Weinberger's flutter_pcm_sound) has no web
// support; this is new work in the fork. It answers the same
// `flutter_pcm_sound/methods` channel as the Android/iOS/macOS plugins, so
// the Dart API in `flutter_pcm_sound_spm.dart` is unchanged on web.
//
// How it plays audio
// ------------------
// Web Audio has no "push samples into a queue" primitive. What it does have
// is a clock: `AudioContext.currentTime`. So each `feed()` converts the
// 16-bit samples to float, wraps them in an `AudioBuffer`, and schedules that
// buffer to start exactly where the previous one ends. Keeping a running
// `_nextStartTime` is what makes consecutive feeds play gaplessly.
//
// "Remaining frames" — which the whole feed-callback contract is built on —
// is then just how far that schedule runs past the clock:
//
//     remaining = (_nextStartTime - currentTime) * sampleRate
//
// An AudioWorklet would be the other way to do this. It was not used
// deliberately: a worklet needs a separate JS file loaded by URL, which a pub
// package cannot ship without asking every host app to copy an asset into its
// web directory. Scheduled buffers need nothing but Dart.

import 'dart:async';
import 'dart:js_interop';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter_web_plugins/flutter_web_plugins.dart';
import 'package:web/web.dart' as web;

class FlutterPcmSoundWeb {
  static const String _channelName = 'flutter_pcm_sound/methods';

  /// How often the scheduled-ahead amount is compared against the feed
  /// threshold. Web Audio gives no "buffer ran low" event, so this is polled.
  /// 20ms is well under a typical threshold and costs almost nothing.
  static const Duration _pollInterval = Duration(milliseconds: 20);

  late final MethodChannel _channel;

  web.AudioContext? _context;
  web.GainNode? _gain;

  int _sampleRate = 44100;
  int _channelCount = 1;
  int _feedThreshold = 0;
  int _logLevel = 2; // LogLevel.standard

  /// Where, on the context clock, the next fed buffer should start.
  double _nextStartTime = 0;

  Timer? _poll;

  /// The callback fires at most once per [feed] for each of the two events
  /// the API documents — dropping below the threshold, and draining to zero.
  /// These latches are what enforce "once", and [feed] clears them.
  bool _firedLowBuffer = false;
  bool _firedZero = false;

  /// Sources that have been scheduled but have not finished. Kept so
  /// [_release] can stop audio immediately rather than letting queued buffers
  /// play on after the app asked for silence.
  final List<web.AudioBufferSourceNode> _scheduled = [];

  static void registerWith(Registrar registrar) {
    final instance = FlutterPcmSoundWeb();
    instance._channel = MethodChannel(
      _channelName,
      const StandardMethodCodec(),
      registrar,
    );
    instance._channel.setMethodCallHandler(instance._handle);
  }

  Future<dynamic> _handle(MethodCall call) async {
    switch (call.method) {
      case 'setLogLevel':
        _logLevel = (call.arguments['log_level'] as num).toInt();
        return null;

      case 'setup':
        final args = call.arguments as Map;
        _sampleRate = (args['sample_rate'] as num).toInt();
        _channelCount = (args['num_channels'] as num).toInt();
        // ios_audio_category / ios_allow_background_audio are accepted and
        // ignored — they are iOS session settings with no web equivalent.
        _setup();
        return null;

      case 'feed':
        final bytes = call.arguments['buffer'] as Uint8List;
        _feed(bytes);
        return null;

      case 'setFeedThreshold':
        _feedThreshold = (call.arguments['feed_threshold'] as num).toInt();
        return null;

      case 'release':
        await _release();
        return null;

      default:
        throw PlatformException(
          code: 'Unimplemented',
          details: '$_channelName on web has no "${call.method}"',
        );
    }
  }

  void _setup() {
    _stopScheduled();
    _context?.close();

    final ctx = web.AudioContext(
      web.AudioContextOptions(sampleRate: _sampleRate.toDouble()),
    );
    final gain = ctx.createGain();
    gain.connect(ctx.destination);

    _context = ctx;
    _gain = gain;
    _nextStartTime = 0;
    _firedLowBuffer = false;
    _firedZero = false;

    _poll?.cancel();
    _poll = Timer.periodic(_pollInterval, (_) => _checkFeed());

    _log('setup ${_sampleRate}Hz x$_channelCount');
  }

  void _feed(Uint8List bytes) {
    final ctx = _context;
    final gain = _gain;
    if (ctx == null || gain == null) {
      _log('feed before setup — ignored', error: true);
      return;
    }

    // Browsers start an AudioContext suspended until a user gesture. Resuming
    // on every feed is cheap when already running, and means playback starts
    // as soon as the page has had any interaction rather than staying
    // silently dead.
    if (ctx.state == 'suspended') {
      ctx.resume();
    }

    // A new feed re-arms both events, matching the native contract: "once
    // means once per feed()".
    _firedLowBuffer = false;
    _firedZero = false;

    final frames = bytes.lengthInBytes ~/ 2 ~/ _channelCount;
    if (frames == 0) return;

    final buffer = ctx.createBuffer(
      _channelCount,
      frames,
      _sampleRate.toDouble(),
    );

    // Int16 little-endian, interleaved by channel, to float [-1, 1] per
    // channel. Dividing by 32768 rather than 32767 keeps the full negative
    // range representable without clipping at -1.
    final view = ByteData.sublistView(bytes);
    for (var ch = 0; ch < _channelCount; ch++) {
      final channel = Float32List(frames);
      for (var i = 0; i < frames; i++) {
        final sample =
            view.getInt16((i * _channelCount + ch) * 2, Endian.little);
        channel[i] = sample / 32768.0;
      }
      buffer.copyToChannel(channel.toJS, ch);
    }

    final source = ctx.createBufferSource();
    source.buffer = buffer;
    source.connect(gain);

    // Never schedule in the past: if the queue has drained, the clock has
    // moved past _nextStartTime and starting "then" would play immediately
    // AND leave the running total wrong.
    final now = ctx.currentTime.toDouble();
    final startAt = _nextStartTime > now ? _nextStartTime : now;
    source.start(startAt);

    _scheduled.add(source);
    source.onended = (web.Event _) {
      _scheduled.remove(source);
    }.toJS;

    _nextStartTime = startAt + (frames / _sampleRate);

    _log('feed $frames frames, ${_remainingFrames()} queued');
  }

  /// Frames scheduled but not yet played.
  int _remainingFrames() {
    final ctx = _context;
    if (ctx == null) return 0;
    final ahead = _nextStartTime - ctx.currentTime.toDouble();
    if (ahead <= 0) return 0;
    return (ahead * _sampleRate).round();
  }

  void _checkFeed() {
    if (_context == null) return;
    final remaining = _remainingFrames();

    if (remaining == 0) {
      if (!_firedZero) {
        _firedZero = true;
        // Draining past the threshold implies the low-buffer event too;
        // firing both for one drain would call the app back twice.
        _firedLowBuffer = true;
        _emitFeed(0);
      }
      return;
    }

    if (remaining < _feedThreshold && !_firedLowBuffer) {
      _firedLowBuffer = true;
      _emitFeed(remaining);
    }
  }

  void _emitFeed(int remainingFrames) {
    _channel.invokeMethod('OnFeedSamples', {
      'remaining_frames': remainingFrames,
    });
  }

  void _stopScheduled() {
    for (final source in List<web.AudioBufferSourceNode>.of(_scheduled)) {
      try {
        source.stop();
      } catch (_) {
        // stop() throws if the source never started; nothing to undo.
      }
    }
    _scheduled.clear();
  }

  Future<void> _release() async {
    _poll?.cancel();
    _poll = null;
    _stopScheduled();
    final ctx = _context;
    _context = null;
    _gain = null;
    _nextStartTime = 0;
    if (ctx != null) {
      await ctx.close().toDart;
    }
    _log('released');
  }

  void _log(String message, {bool error = false}) {
    // Mirrors the Dart side's levels: 0 none, 1 error, 2 standard, 3 verbose.
    if (_logLevel >= (error ? 1 : 2)) {
      debugPrint('[PCM/web] $message');
    }
  }
}
