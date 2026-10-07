# XR Wrist Display — Phone Streamer

Native Android app that captures the phone screen and streams it to a Quest 3
headset over WiFi, with touch input injection from VR.

- **Package:** `com.zachery.xrwrist.phone`
- **minSdk:** 29 (Android 10) · **targetSdk:** 34 (Android 14)
- **Language:** Kotlin · **Build:** manual (kotlinc + d8 + aapt2; Gradle files included for IDE use)

## Wire Protocol (v1)

| Channel | Port | Direction | Format |
|---|---|---|---|
| Discovery | UDP 8899 | Phone listens | Respond to any packet with `{"ip":"<ip>","v":1,"name":"ziggy"}` |
| Video | TCP 8900 | Phone → Quest | Client sends `{"hello":"xr-wrist","v":1}\n`; server replies `{"width":720,"height":1280,"fps":30}\n`; then raw H.264 Annex B NAL units, each prefixed with 4-byte big-endian length |
| Control | TCP 8901 | Quest → Phone | Newline-delimited JSON: `{"t":"touch","a":"down\|move\|up","x":0..1,"y":0..1}` or `{"t":"cmd","c":"screen_off\|screen_on"}` |

## Setup on the Phone

1. Install `app/build/outputs/apk/debug/app-debug.apk` (enable "Install unknown apps" if prompted).
2. Open **XR Wrist Phone**.
3. Tap **Enable Touch Service** → turn on **XR Wrist Touch** in Accessibility settings.
   (Required for VR touch control to work.)
4. Tap **Start Streaming** → accept the screen-capture prompt.
5. The Quest app connects automatically via discovery (or manual IP).

## Architecture

- `MainActivity.kt` — launcher UI, requests MediaProjection permission, starts/stops service.
- `StreamService.kt` — foreground service (mediaProjection type). Creates VirtualDisplay
  fed into the encoder, hosts the network servers.
- `VideoEncoder.kt` — MediaCodec H.264 encoder (720×1280, 30fps, 2Mbps),
  Surface input, emits Annex B NAL units.
- `NetworkServer.kt` — UDP discovery responder, TCP video server (hello handshake
  + length-prefixed NAL broadcast), TCP control server (JSON touch/commands).
- `TouchService.kt` — AccessibilityService that injects gestures via
  `dispatchGesture()` and handles power-button commands.

## Notes

- Video is 720p portrait; the Quest client scales to the wrist quad.
- Touch coordinates are normalized (0.0–1.0) relative to the phone screen.
- `screen_off`/`screen_on` currently opens the power dialog via the
  accessibility service; a DeviceAdmin implementation can hard-toggle later.
- The encoder requests a keyframe every 2 seconds (I-frame interval).
- If the Quest disconnects, the phone keeps streaming (reconnect anytime).

## Building

Manual build (used for the shipped APK):
```bash
# 1. Compile Kotlin
kotlinc app/src/main/java/com/zachery/xrwrist/phone/*.kt \
  -cp $ANDROID_HOME/platforms/android-34/android.jar \
  -d /tmp/xrbuild/classes

# 2. DEX with Kotlin stdlib
d8 --lib $ANDROID_HOME/platforms/android-34/android.jar \
  --output /tmp/xrbuild/dex \
  /tmp/xrbuild/classes/com/zachery/xrwrist/phone/*.class \
  $KOTLIN_HOME/lib/kotlin-stdlib.jar

# 3. Resources
aapt2 compile --dir app/src/main/res -o /tmp/xrbuild/res/
aapt2 link -o /tmp/xrbuild/app-unsigned.apk \
  -I $ANDROID_HOME/platforms/android-34/android.jar \
  --manifest app/src/main/AndroidManifest.xml \
  /tmp/xrbuild/res/*.flat \
  --min-sdk-version 29 --target-sdk-version 34

# 4. Add DEX, align, sign
zip -j /tmp/xrbuild/app-unsigned.apk /tmp/xrbuild/dex/classes.dex
zipalign -f 4 /tmp/xrbuild/app-unsigned.apk /tmp/xrbuild/app-aligned.apk
apksigner sign --ks debug.keystore --out app-debug.apk app-aligned.apk
```

Gradle files (`build.gradle`, `settings.gradle`) are included for Android Studio
import, but the sandboxed build environment could not resolve Gradle plugin
dependencies through the proxy — the manual toolchain above was used instead.
