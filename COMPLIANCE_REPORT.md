# XR Wrist Display — Compliance & Rebuild Report
**Date:** 2026-10-06  
**Standard:** Android 15 (API 35) / Google Play target-35 / Samsung / Meta Quest

## Rebuilt APKs (both PASS all checks)

### Phone — com.zachery.xrwrist.phone v1.1 (versionCode 2)
- **File:** `phone/app/build/outputs/apk/debug/app-debug-v2-manual.apk` (730 KB)
- **targetSdk 35** ✅ (meets Play requirement)
- **minSdk 29, compileSdk 35**
- Signed (v1+v2+v3), not debuggable
- All components declare `android:exported`
- `POST_NOTIFICATIONS` + `allowBackup=false`
- `FOREGROUND_SERVICE_MEDIA_PROJECTION` declared (Android 14+ requirement)
- **AndroidManifest.xml STORED** (Samsung-safe — fixes the original parse error)

### Quest — com.zachery.xrwrist.quest v0.3.0 (versionCode 3)
- **File:** `quest/app/build/outputs/apk/debug/app-debug-v2-manual.apk`
- **targetSdk 35** ✅
- **minSdk 29, compileSdk 35**
- Native ABIs: **arm64-v8a** ✅ (libxrwrist.so + libopenxr_loader.so)
- Signed (v1+v2+v3), not debuggable
- `android.hardware.vr.headtracking` declared ✅
- `com.oculus.intent.category.VR` intent category ✅
- Passthrough feature declared (optional) ✅
- **AndroidManifest.xml STORED** (Samsung-safe)

## Build method
Gradle daemon is broken in this VM (client/daemon socket protocol mismatch —
"Unexpected type tag 109" — affects Gradle 8.7 and 8.10.2). APKs were built
manually with:
- `kotlinc 2.1.20` (phone) / `javac` (quest) + `d8` for dex
- `aapt2` (build-tools 35.0.0) for resources + manifest
- `cmake 3.x` + NDK 27.2.12479018 for Quest native lib (arm64-v8a, API 29+)
- `zipalign` + `apksigner` (debug key for these builds)

Scripts:
- `forge/scripts/manual-build-apk.sh` (phone/Kotlin)
- `forge/scripts/manual-build-quest.sh` (quest/native)

## Tooling installed
- JDK 17.0.20.1 (Adoptium) at `~/jdk`
- Gradle 8.10.2 at `~/gradle/gradle-8.10.2` (daemon broken, kept for future)
- Android SDK: platforms android-34 + android-35, build-tools 34.0.0 + 35.0.0
- NDK 27.2.12479018, CMake (system)
- kotlin-compiler 2.1.20 at `~/dl/kotlinc`

## Proxy notes (see TOOLS.md)
- curl fails on dl.google.com (502/empty); use Python urllib instead.
- /tmp is a 512MB tmpfs — download large files to ~/dl, not /tmp.
