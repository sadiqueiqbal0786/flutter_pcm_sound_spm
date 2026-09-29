// Plays a C major scale by streaming 16-bit PCM to the speakers.
//
// The shape worth copying is the feed loop: you never push the whole sound at
// once. You hand the plugin a chunk, it plays it, and when its queue runs low
// it calls you back for more. That is what makes this usable for live audio —
// speech synthesis, a voice call, anything generated as it plays.

import 'package:flutter/material.dart';
import 'package:flutter_pcm_sound_spm/flutter_pcm_sound_spm.dart';

void main() => runApp(const ExampleApp());

class ExampleApp extends StatelessWidget {
  const ExampleApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'flutter_pcm_sound_spm',
      theme: ThemeData(colorSchemeSeed: Colors.indigo),
      home: const ScalePage(),
    );
  }
}

class ScalePage extends StatefulWidget {
  const ScalePage({super.key});

  @override
  State<ScalePage> createState() => _ScalePageState();
}

class _ScalePageState extends State<ScalePage> {
  static const int _sampleRate = 44100;

  /// Ask for more audio once fewer than this many frames are still queued.
  /// Lower is tighter (less latency, more callbacks); too low and the queue
  /// empties between callbacks and you hear gaps.
  static const int _feedThreshold = 8000;

  final MajorScale _scale = MajorScale(
    sampleRate: _sampleRate,
    noteDuration: 0.20,
  );

  bool _playing = false;
  int _remaining = 0;

  @override
  void initState() {
    super.initState();
    _init();
  }

  Future<void> _init() async {
    FlutterPcmSound.setLogLevel(LogLevel.error);
    await FlutterPcmSound.setup(sampleRate: _sampleRate, channelCount: 1);
    await FlutterPcmSound.setFeedThreshold(_feedThreshold);
    FlutterPcmSound.setFeedCallback(_onFeed);
  }

  /// Called by the plugin when its queue runs low — the only place audio is
  /// produced. [remainingFrames] is what is still queued.
  void _onFeed(int remainingFrames) {
    if (!mounted) return;
    setState(() => _remaining = remainingFrames);
    if (!_playing) return;
    FlutterPcmSound.feed(PcmArrayInt16.fromList(_scale.generate(periods: 20)));
  }

  void _togglePlay() {
    setState(() => _playing = !_playing);
    if (_playing) {
      // start() invokes the feed callback if playback is not already running,
      // which primes the queue. Without it nothing would ever ask for audio.
      FlutterPcmSound.start();
    }
  }

  @override
  void dispose() {
    FlutterPcmSound.setFeedCallback(null);
    FlutterPcmSound.release();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('flutter_pcm_sound_spm')),
      body: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Text(
              _playing ? 'Playing' : 'Stopped',
              style: Theme.of(context).textTheme.headlineSmall,
            ),
            const SizedBox(height: 8),
            Text('$_remaining frames queued'),
            const SizedBox(height: 24),
            FilledButton.icon(
              onPressed: _togglePlay,
              icon: Icon(_playing ? Icons.stop : Icons.play_arrow),
              label: Text(_playing ? 'Stop' : 'Play C major scale'),
            ),
            const Padding(
              padding: EdgeInsets.all(24),
              child: Text(
                'On web, browsers block audio until the page has been '
                'interacted with — the button press is that interaction.',
                textAlign: TextAlign.center,
              ),
            ),
          ],
        ),
      ),
    );
  }
}
