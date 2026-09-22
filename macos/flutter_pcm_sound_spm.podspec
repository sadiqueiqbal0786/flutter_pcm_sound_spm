#
# Kept so the plugin still works in projects that have not moved to Swift
# Package Manager. It builds the same sources Package.swift does.
#
Pod::Spec.new do |s|
  s.name             = 'flutter_pcm_sound_spm'
  s.version          = '0.0.1'
  s.summary          = 'Send real-time PCM audio (16-bit integer) to your device speakers.'
  s.description      = 'Swift Package Manager compatible fork of flutter_pcm_sound.'
  s.homepage         = 'https://github.com/sadiqueiqbal0786/flutter_pcm_sound_spm'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'Sadique Iqbal' => 'sadiqueiqbal.si@gmail.com' }
  s.source           = { :path => '.' }
  s.source_files     = 'flutter_pcm_sound_spm/Sources/flutter_pcm_sound_spm/**/*.{h,m}'
  s.public_header_files = 'flutter_pcm_sound_spm/Sources/flutter_pcm_sound_spm/include/**/*.h'
  s.dependency 'FlutterMacOS'
  s.platform = :osx, '10.14'
  s.framework = 'CoreAudio'
  s.pod_target_xcconfig = { 'DEFINES_MODULE' => 'YES' }
end
