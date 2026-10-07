# v0.5.0 Build Notes

**Built:** 2026-10-07 ~00:38 UTC (2026-10-06 19:38 CDT)
**APK:** `quest/app/build/outputs/apk/release/app-release.apk` (5.4MB)
**Version:** 0.5.0 (versionCode 5)
**Status:** Built, verified, staged. NOT installed (Quest offline).

## What Changed from v0.4.0

### SG-1: World-Locked Engage Panel (CRITICAL)
- `UpdateDynamicZoom`: On GLANCE→EXPANDING, sets `detachTarget_=1.0f` with `engageDetached_=true`.
- On SHRINKING→GLANCE, if `engageDetached_`, returns to wrist (`detachTarget_=0.0f`).
- World-locked engaged panel scales to 1.6x (0.42m) for comfortable reading at 0.7m.
- User notification: "Display engaged — Panel moved to world-locked position".
- Addresses Meta's explicit warning: "Do NOT anchor complex menus to a moving wrist."

### SG-4: XR_EXT_hand_interaction (ROBUSTNESS)
- New actions: `poke_pose` (pose), `pinch_value` (float), both hands.
- Suggested bindings for `/interaction_profiles/ext/hand_interaction_ext`.
- `XrInput::PokePose()`, `PinchValue()`, `HandInteractionAvailable()`.
- `HandleFingerTouch`: prefers canonical poke pose; falls back to raw joints.
- Pinch (threshold 0.8, configurable) as mid-air click alternative.

### Production Config System (NEW)
- `config.h/cpp`: 22 tunable keys across Network, Display, Interaction, Avatar, Godmode, System.
- Persists to `xrwrist.cfg` in app-private storage.
- Live-apply via listeners. No hardcoded values in feature code.

### User Notifications (NEW)
- `notify.h/cpp`: Info/Warn/Error with title + recovery action.
- VR toast display (head-locked, 8s expiry).
- Integrated: network events, engage/disengage, first-run.

### First-Run Onboarding (NEW)
- Welcome message on first launch.
- Completion message after first engage.
- `FirstRunComplete` persisted in config.

### Godmode Enhancements
- Overlay shows: hand-interaction status, poke/pinch values, anchor mode, live config.
- New voice commands: "diagnostics", "godmode", "world lock".
- Command reference section.

### Voice Commands Added
- "diagnostics" / "godmode" → toggle console
- "world lock" / "lock here" → force world-lock

## Files Modified
- `xr_core.h`: Added `engageDetached_`, `notifyOverlay_`.
- `xr_core.cpp`: SG-1 logic, Config integration, notifications, voice commands, Godmode.
- `input.h`: Poke/pinch getters, hand interaction members.
- `input.cpp`: Hand interaction profile bindings, polling.
- `config.h/cpp`: NEW — production config system.
- `notify.h/cpp`: NEW — user notification system.
- `CMakeLists.txt`: Added config.cpp, notify.cpp.
- `build.gradle`: versionCode 5, versionName "0.5.0".

## Files NOT Touched (stable)
- `net.cpp`, `video.cpp` — network/video pipeline working.
- `avatar.cpp` — geometry solid, materials refined in v0.4.0.
- `voice.cpp`, `VoiceHelper.java` — working.
- `main.cpp` — stable.

## Build Process
1. cmake + NDK 27.2.12479018 → libxrwrist.so (clean, warnings only)
2. javac → d8 → classes.dex
3. aapt2 compile + link → base.apk
4. Python zip (STORED manifest/arsc/so) → unsigned.apk
5. zipalign -f 4 → aligned.apk
6. apksigner sign (xrwrist.keystore) → final.apk
7. apksigner verify → OK

## Installation Status
- **BLOCKED:** Quest offline (`localhost:5560 offline`).
- APK is staged and ready at the release path.
- Install command: `~/bin/quest.sh install -r -d -g <apk>`
- Previous install attempt failed (device went offline mid-install).

## Verification Needed (when Quest back online)
1. Install APK.
2. Launch, verify no crash.
3. Check logcat for: "hand interaction profile bound", "v0.5.0: engage -> world-lock".
4. Verify frames submitting.
5. In-headset: test engage → world-lock glide, poke touch, pinch, voice.
