// Fork of flutter_pcm_sound by Chip Weinberger.
// https://github.com/chipweinberger/flutter_pcm_sound — public domain
// (Unlicense). This file is his work, moved into the layout Flutter's Swift
// Package Manager integration expects. See NOTICE.


#if TARGET_OS_OSX
#import <FlutterMacOS/FlutterMacOS.h>
#else
#import <Flutter/Flutter.h>
#endif

#define NAMESPACE @"flutter_pcm_sound"

@interface FlutterPcmSoundPlugin : NSObject<FlutterPlugin>
@end
