# Android TV and Google TV

The same app as on webOS (`src/`), with `src/platform/android/`, built with `SB_ANDROID=1`. Android TV 9 (API 28)
and later: the TVs and boxes of 2020 on; Google TV first (Chromecast with Google TV, Google TV sets).

## Build

    ./fetch-deps.sh                                   # SDL2, mbedTLS and curl sources into deps/, SDL's Java activity into the app
    ../../tools/build-engine.sh android-arm64         # the torrent engine (libsbengine.so) into build/android/<abi>
    ../../tools/build-engine.sh android-armv7
    ./gradlew assembleRelease                         # NDK 29, CMake 3.31.6, JDK 17+

Release signing: set `ORIGIN_SIGNING` to a properties file (`storeFile`, `storePassword`, `keyAlias`, `keyPassword`)
kept outside the repository; every release must be signed with the same key, or Android refuses the update.
`tools/release.sh` builds `dist/app.stremioorigin.tv_<version>.apk` when it is set; the in-app updater looks for
that name in the GitHub releases, checks its SHA-256 and opens Android's installer.

`assets/` goes into the APK with `assets.lst`, unpacked to the app's private storage on the first start of each version.
The built-in keys come from the same private file as the webOS build (`tools/sbkeys.py`).

## How it works

- `OriginActivity` (SDL's activity): full screen, landscape, in the Android TV launcher (banner) and the usual one.
  It reads the remote's keys before SDL and sends them to the app as its own codes (`nativeKey`):
  D-pad, OK, Back; Play/Pause, Play, Pause, Stop, Rewind, Fast forward (and Previous / Next); Menu or red: Settings;
  Search or green: Search; yellow and blue; Channel up / down; Captions: the subtitle list; Audio track: the audio
  list; Info: the controls. Volume, Home and the Assistant stay Android's.
- `OriginVideo`: Media3 ExoPlayer on a SurfaceView under SDL's see-through GL surface. Audio tracks listed and chosen by
  the app (its language rules, as on webOS); Dolby Digital (Plus, Atmos), DTS and TrueHD passed to the TV or receiver
  when they take them; the display follows the film's frame rate when Android's "Match content frame rate" is on;
  Fill and Zoom by a picture larger than the screen. Subtitles are the app's own (ExoPlayer's text tracks are off).
- `OriginBridge`: the update (download, SHA-256, the installer; the first time, Android asks to allow installs from
  the app), a page in the TV's browser, a clean exit.
- Not on Android TV: the webOS TV's own picture and sound settings (Picture and sound, the upscale button), the DTS
  and Dolby Vision converter and torrents on a USB drive.
