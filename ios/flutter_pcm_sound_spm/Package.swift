// swift-tools-version: 5.9
// Swift Package Manager manifest for the plugin's Apple side.
//
// This is the whole point of the fork: flutter_pcm_sound ships CocoaPods
// only, and Flutter now warns that a plugin without SPM support "will become
// an error in a future version". The sources below are the upstream ones,
// moved into the layout Flutter's SPM integration expects.

import PackageDescription

let package = Package(
    name: "flutter_pcm_sound_spm",
    platforms: [
        .iOS("12.0"),
        .macOS("10.14"),
    ],
    products: [
        .library(name: "flutter-pcm-sound-spm", targets: ["flutter_pcm_sound_spm"])
    ],
    dependencies: [],
    targets: [
        .target(
            name: "flutter_pcm_sound_spm",
            dependencies: [],
            cSettings: [
                .headerSearchPath("include/flutter_pcm_sound_spm"),
            ]
        )
    ]
)
