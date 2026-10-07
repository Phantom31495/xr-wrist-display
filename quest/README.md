# XR Wrist Display — Quest 3 Client (Track 2)

Native OpenXR VR app for Quest 3. Connects to the phone app (Track 1) over
WiFi, receives the H.264 phone-screen stream, and renders it on a
wrist-anchored quad in VR with full touch control.

- **Package:** `com.zachery.xrwrist.quest`
- **Language:** C++17, native OpenXR 1.1 (Khronos loader), OpenGL ES 3.0
- **Build:** Gradle + CMake + NDK (arm64-v8a)

## Features

- **Wrist-anchored display:** quad follows your left wrist via hand tracking
  (falls back to left controller grip pose). Press **X+Y together** on the
  left controller to toggle between wrist-anchored and fixed head-locked
  modes (fixed mode tints the display slightly blue).
- **Live phone screen:** H.264 decoded via MediaCodec, rendered on the quad.
- **Touch control:** point the right controller ray at the display, pull the
  **trigger** to touch down/up, move while holding for drag.
- **Passthrough:** your room stays visible behind the display.
- **Phone screen off:** sends `screen_off` to the phone when streaming starts,
  `screen_on` on exit.

## Wire protocol (shared with Track 1 phone app)

| Channel | Details |
|---|---|
| Discovery | UDP broadcast to `192.168.1.255:8899`; phone replies `{"ip":"…","v":1,"name":"ziggy"}`. Fallback IP: `192.168.1.172` |
| Video | TCP `phone:8900`. Client sends `{"hello":"xr-wrist","v":1}\n`, server replies `{"width":720,"height":1280,"fps":30}\n`. Then loop: 4-byte big-endian length + H.264 Annex-B NAL units |
| Control | TCP `phone:8901`. Newline-delimited JSON: `{"t":"touch","a":"down\|move\|up","x":0-1,"y":0-1}`, `{"t":"cmd","c":"screen_off\|screen_on"}` |

## Build

Prerequisites (one-time):
- JDK 17, Android SDK (platform-34, build-tools), NDK r27, CMake 3.22+
- This project expects `ANDROID_HOME=~/android` and `JAVA_HOME=~/jdk`

```bash
cd ~/workspace/works/projects/xr-wrist-display/quest
export ANDROID_HOME=~/android JAVA_HOME=~/jdk
./gradlew assembleDebug
# APK: app/build/outputs/apk/debug/app-debug.apk
```

The OpenXR loader AAR (`org.khronos.openxr:openxr_loader_for_android:1.1.63`)
is pulled from Maven Central; headers + link-time `.so` are vendored under
`app/src/main/cpp/third_party/openxr/`.

## Sideload

```bash
~/bin/quest.sh install -r app/build/outputs/apk/debug/app-debug.apk
```

Then launch **XR Wrist Display** from the Quest library (Unknown Sources).
Make sure the phone app (Track 1) is running first — the Quest connects to it
on launch.

## Controls

| Input | Action |
|---|---|
| Right trigger (on the quad) | Touch down / up / drag |
| Left X + Y together | Toggle wrist-anchored ↔ fixed position |
| Right controller ray | Points at the display to aim touches |

## Project layout

```
app/src/main/cpp/
├── main.cpp        # android_main, app lifecycle
├── xr_core.h/.cpp  # OpenXR instance/session/swapchains/passthrough/frame loop
├── gl_render.h/.cpp# EGL helpers, math, textured-quad renderer
├── video.h/.cpp    # TCP video client, MediaCodec decoder, SurfaceTexture JNI
├── net.h/.cpp      # UDP discovery, TCP client, control channel
├── input.h/.cpp    # OpenXR actions, hand tracking, ray↔quad math
└── third_party/openxr/  # Khronos headers + loader .so (vendored)
```

## Notes / limitations (v1)

- No on-device UI beyond the quad tint for mode; check `adb logcat -s XRWrist`
  for status.
- If the phone isn't found, the app still runs (quad shows black) — start the
  phone app and restart.
- Hand tracking must be enabled in Quest settings for wrist mode; otherwise it
  uses the left controller grip pose.
