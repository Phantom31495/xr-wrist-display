# XR Wrist Display — Build Prompt for AI

Copy this entire prompt into your AI app to build the XR Wrist Display system.

---

## Project Overview

**XR Wrist Display** is a system that streams a phone's live screen to a VR headset (Meta Quest 3), displaying it as a wrist-anchored smartwatch with touch control back to the phone.

**Components:**
1. **Phone App** (`com.zachery.xrwrist.phone`) — Kotlin, Android. Captures screen via MediaProjection, encodes H.264, streams over TCP. Receives touch commands.
2. **Quest App** (`com.zachery.xrwrist.quest`) — C++ OpenXR, native. Receives H.264 video, renders on wrist-anchored quad. Sends touch input back.

**Wire Protocol:**
- UDP 8899: Discovery (broadcast)
- TCP 8900: Video (H.264 Annex B, 4-byte big-endian length prefix per NAL)
- TCP 8901: Control (newline-delimited JSON: `{"type":"touch","x":0.5,"y":0.5,"action":"down"}`)

## Phone App Architecture

**Key files:**
- `MainActivity.kt` — UI, Start/Stop streaming buttons, Dev Options entry
- `StreamService.kt` — Foreground service, MediaProjection, H.264 encoder, TCP servers
- `TouchService.kt` — Accessibility service, injects touch events from Quest
- `DevOptionsActivity.kt` — Developer settings (bitrate, resolution, IP override)
- `ConsoleActivity.kt` — In-app command console
- `GodmodeActivity.kt` — Hidden advanced features

**Critical implementation details:**
- MUST call `MediaProjection.registerCallback(handler, Handler(Looper.getMainLooper()))` BEFORE `createVirtualDisplay()`. Null handler crashes on Android 14.
- Video: 720x1280 @ 30fps, H.264, ~4 Mbps (configurable).
- Foreground service with `mediaProjection` type (Android 14+ requirement).
- APK packaging: `AndroidManifest.xml` and `resources.arsc` MUST be STORED (not DEFLATED) or Samsung Android 14+ fails to install.

## Quest App Architecture

**Key files:**
- `xr_core.h/cpp` — OpenXR instance, session, frame loop, swapchains
- `avatar.h/cpp` — Procedural dual-hand avatar (forearm, palm, articulated fingers)
- `video.h/cpp` — H.264 decoder (MediaCodec via JNI), texture upload
- `network.h/cpp` — UDP discovery, TCP video/control clients
- `input.h/cpp` — Controller and hand tracking input
- `config.h/cpp` — Production config system (22 tunables, persisted)
- `notify.h/cpp` — User notification system
- `devtools.h/cpp` — Diagnostics overlay, Godmode

**Critical implementation details:**
- EVERY `xrBeginFrame` MUST be paired with `xrEndFrame`, even on error paths. Leaked frame = permanent loading screen.
- Probe extensions, never assume: `XR_FB_passthrough`, `XR_FB_display_refresh_rate`, hand tracking.
- `xrWaitSwapchainImage` with bounded timeout (100ms), not `XR_INFINITE_DURATION`.
- Discovery: broadcast to 255.255.255.255:8899 (works on any LAN).
- Controller input: XR_EXT_hand_interaction for poke/pinch, fallback to controller ray.

**Features (v0.5.0):**
- World-locked engage panel (gaze dwell → panel glides from wrist to world-locked)
- Dynamic sizing (1.0x → 2.4x on gaze dwell)
- Direct finger touch (hand tracking poke)
- Auto wrist→fixed transition (4s raised wrist → 600ms glide)
- Voice control (hold Y, 8 commands)
- Dual-hand avatar with articulated fingers
- Godmode diagnostics (B button)

## Build Instructions

**Phone (manual, Gradle broken):**
```bash
kotlinc <sources> -d classes/
d8 --lib android.jar classes/ --output dex/
aapt2 compile ... && aapt2 link ...
# Zip with AndroidManifest.xml STORED
zipalign -f 4 app.apk aligned.apk
apksigner sign --ks keystore.jks aligned.apk
```

**Quest (manual):**
```bash
cmake -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-34 ...
cmake --build . 
# Then aapt2/d8/zipalign/apksigner as above
```

## Current State (2026-10-06)

- Quest v0.5.0: Built, staged, awaiting Quest online for install.
- Phone v1.2: Installed on device, includes dev console.
- Research: 21 files (Meta, Android, OpenXR docs).
- Sub-goals: World-locked panel (done in v0.5.0), phone install (done), in-headset verification (pending), robust touch (planned).

## Build This

Using the architecture above, build a complete, production-ready implementation of the XR Wrist Display system. Follow these standards:
- State-of-the-art code, no prototypes.
- Human-centered design: "What does the human body want?"
- Professional UI polish.
- No hardcoded values — everything configurable.
- Human-readable errors with recovery actions.
- Hidden Godmode with advanced features.

Start with the component I specify, or ask which to build first.
