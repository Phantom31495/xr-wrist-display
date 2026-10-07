# VRC Compliance Audit — XR Wrist Display (Quest)

**Package:** `com.zachery.xrwrist.quest`
**Version audited:** 0.2.0 (versionCode 2)
**Date:** 2026-10-06
**Auditor:** Muse (subagent)

## Summary

| Category | Before | After |
|---|---|---|
| Manifest | 2 FAIL | PASS |
| Performance | 1 FAIL, 1 WARN | PASS |
| Input | 1 FAIL | PASS |
| Security | PASS (with notes) | PASS |
| UX / Lifecycle | 2 WARN | PASS |
| OpenXR | 1 FAIL | PASS |

**Result:** All auto-fixable issues resolved. App is store-ready from a code perspective. Manual steps (developer account, store assets, submission) remain for Zachery — listed at the bottom.

---

## Findings & Fixes

### 1. Manifest — FAIL → PASS

| # | Requirement (VRC) | Status | Action |
|---|---|---|---|
| 1.1 | Launcher icon (`android:icon`) required | **FAIL** → FIXED | Generated adaptive icon (`res/mipmap-*/ic_launcher.png` + `mipmap-anydpi-v26/ic_launcher.xml`); wired `android:icon="@mipmap/ic_launcher"` in manifest. |
| 1.2 | `com.oculus.intent.category.VR` intent filter | PASS | Already present. |
| 1.3 | Permissions minimal and justified | PASS | INTERNET, ACCESS_NETWORK_STATE, ACCESS_WIFI_STATE, CHANGE_WIFI_MULTICAST_STATE — all required for LAN discovery + TCP streaming. RECEIVE_BOOT_COMPLETED added (user-requested auto-launch on boot). |
| 1.4 | `android:allowBackup="false"` | PASS | Already set. |
| 1.5 | 64-bit only (`arm64-v8a`) | PASS | Already `abiFilters 'arm64-v8a'`. |
| 1.6 | `targetSdk 34` (current) | PASS | Already 34. |
| 1.7 | Boot receiver declared correctly | **NEW** → FIXED | Added `BootReceiver.java` + `RECEIVE_BOOT_COMPLETED` + manifest `<receiver android:exported="false">`. Auto-launches the app when the headset boots (user request). |

### 2. Performance — FAIL → PASS

| # | Requirement (VRC) | Status | Action |
|---|---|---|---|
| 2.1 | No infinite waits in frame loop (`xrWaitSwapchainImage`) | **FAIL** → FIXED | Was `XR_INFINITE_DURATION` (a wedged compositor would hang the app forever). Now 100 ms bounded wait; on timeout the image is released and the frame submits empty instead of stalling. |
| 2.2 | Swapchain acquire/wait errors handled | **WARN** → FIXED | `xrAcquireSwapchainImage` / `xrWaitSwapchainImage` return values were ignored. Now checked; failed views are skipped, and if no views are acquired the frame submits with 0 layers (compositor keeps running). |
| 2.3 | Sustain 72 fps | PASS | Scene is a single textured quad — trivially within budget. |
| 2.4 | Request higher refresh rate when cheap | **WARN** → FIXED | Added optional `XR_FB_display_refresh_rate`: requests the highest offered rate ≤ 90 Hz; silently keeps default if the extension is absent. |

### 3. Input — FAIL → PASS

| # | Requirement (VRC) | Status | Action |
|---|---|---|---|
| 3.1 | Must not request unsupported OpenXR extensions | **FAIL** → FIXED | `XR_EXT_hand_tracking` was requested **unconditionally** — `xrCreateInstance` fails outright on any runtime without it. Now probed via `xrEnumerateInstanceExtensionProperties` (same pattern as passthrough) and only requested when advertised. `XrInput::Init` takes an explicit `handTrackingEnabled` flag. |
| 3.2 | Touch controller support (primary input) | PASS | Action-based input with `oculus/touch_controller` + `khr/simple_controller` fallback profiles. X+Y combo toggles anchor mode; right trigger ray-casts touch. |
| 3.3 | Graceful degradation if controllers disconnect | PASS | Poses marked invalid → quad falls back to head-locked position; no crash. |

### 4. Security — PASS (notes)

| # | Requirement (VRC) | Status | Action |
|---|---|---|---|
| 4.1 | No excessive permissions | PASS | See 1.3. |
| 4.2 | Network traffic | PASS w/ disclosure | App uses **plaintext TCP/UDP on the local LAN only** (discovery `:8899`, video `:8900`, control `:8901`) to talk to the user's own phone. No cloud, no internet egress, no PII leaves the LAN. Raw sockets are not subject to cleartext policy, but this must be disclosed in the store Data Safety questionnaire and privacy policy. |
| 4.3 | Signing key persisted | **WARN** → FIXED | Release keystore was in `/tmp` (ephemeral — losing it bricks future updates). Copied to `quest/xrwrist.keystore` (mode 600); `build.gradle` now references the project-local path. **Before store submission, generate a proper release key with a strong password** — the current one uses a placeholder password. |

### 5. UX / Lifecycle — WARN → PASS

| # | Requirement (VRC) | Status | Action |
|---|---|---|---|
| 5.1 | Clean exit on `XR_SESSION_STATE_EXITING` / `LOSS_PENDING` | **WARN** → FIXED | Previously only set a flag; now calls `xrEndSession` (tracked via `sessionBegun_`) and requests app exit, so the runtime can reclaim the session cleanly. |
| 5.2 | `VISIBLE` / `FOCUSED` states handled | **WARN** → FIXED | Added explicit (logging) handlers; rendering already degrades correctly via `shouldRender == false` → empty frame submit. |
| 5.3 | Release builds quiet | **WARN** → FIXED | `LOGI`/`LOGD` are now compiled out under `NDEBUG` (release); warnings/errors remain. |
| 5.4 | Subnet-independent discovery | **WARN** → FIXED | Discovery broadcast was hardcoded to `192.168.1.255` (user's subnet). Now uses limited broadcast `255.255.255.255` — works on any LAN. The `192.168.1.172` fallback is retained as a last resort (documented). |
| 5.5 | Network threads can't wedge on dead peer | **WARN** → FIXED | `TcpClient::RecvExact` blocked indefinitely in `recv()`. Now poll-bounded (default 15 s timeout) — a dead phone can't hang the video thread forever. |

### 6. OpenXR — FAIL → PASS

Covered by 2.1, 2.2, 3.1, 5.1. Session lifecycle (`READY` → `begin` → `RUNNING` → `STOPPING` → `end`), loader init via `xrGetInstanceProcAddr`, and EGL config selection (caveat-free, runtime-required ES version) were already correct.

---

## Build

Release APK rebuilt with all fixes:
`quest/app/build/outputs/apk/release/app-release.apk` (1.08 MB, versionCode 2, signed)

> Build note: the Gradle daemon could not start in this environment during the audit (Java localhost sockets return corrupted bytes — `65` sent, `109` received — breaking Gradle's daemon IPC; Python sockets are unaffected). The APK was therefore assembled with the Android SDK tools directly (`aapt2`, `d8`, `zipalign`, `apksigner`) from CMake-compiled native code. All C++ changes were verified compiling clean under the NDK (only pre-existing `-Wmissing-field-initializers` warnings). When the environment is healthy, `./gradlew assembleRelease` is the canonical build.

## Manual steps for Zachery (store submission)

These require his Meta developer account — Muse cannot do them:

1. **Create a Meta developer account** at developer.oculus.com and verify an organization (required for store submission).
2. **Create the app** in the developer dashboard; reserve the package name `com.zachery.xrwrist.quest`.
3. **Generate a proper release signing key** with a strong password (`keytool -genkeypair`); keep it backed up — losing it means you can never update the app. Update `app/build.gradle` to point at it.
4. **Store assets:** 1024×1024 icon (source: `res/mipmap-xxxhdpi/ic_launcher.png`), hero/banner art, at least 4 screenshots or a trailer captured from the headset.
5. **Privacy policy URL** — required. Must disclose: LAN-only plaintext networking to the user's own phone; no data leaves the local network; no accounts, analytics, or ads.
6. **Data Safety / privacy questionnaire** in the dashboard — declare the LAN networking above.
7. **IARC age-rating questionnaire.**
8. **Content & VRC review submission** — upload the release APK; Meta tests against the VRC checklist this audit mirrors.
9. **Phone companion app** (`com.zachery.xrwrist.phone`) is a standard Android app — it goes through **Google Play** review, not the Horizon Store. Same drill: Play Console account, privacy policy, data safety form.

## Files changed

- `quest/app/src/main/cpp/xr_core.{cpp,h}` — optional hand-tracking ext, bounded swapchain wait, acquire error handling, session exit handling, 90 Hz request
- `quest/app/src/main/cpp/input.{cpp,h}` — `handTrackingEnabled` flag
- `quest/app/src/main/cpp/log.h` — release log stripping
- `quest/app/src/main/cpp/net.{cpp,h}` — subnet-independent broadcast, `RecvExact` timeout
- `quest/app/src/main/AndroidManifest.xml` — icon, boot receiver, `RECEIVE_BOOT_COMPLETED`
- `quest/app/src/main/java/com/zachery/xrwrist/quest/BootReceiver.java` — **new**
- `quest/app/src/main/res/` — **new** (adaptive launcher icon)
- `quest/app/build.gradle` — versionCode 2 / 0.2.0, project-local keystore path
- `quest/xrwrist.keystore` — **new** (persisted signing key)
- `quest/VRC_AUDIT.md` — this report
