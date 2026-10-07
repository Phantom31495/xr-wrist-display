# MediaProjection Best Practices — Research Notes for XR Wrist Display Phone App

**Sources:**
- https://developer.android.com/reference/android/media/projection/MediaProjection (API reference, last updated 2026-08-03 UTC)
- https://developer.android.com/reference/android/media/projection/MediaProjectionManager (API reference, last updated 2026-08-03 UTC)
- https://developer.android.com/develop/background-work/services/fg-service-types (foreground service type: mediaProjection)
- https://developer.android.com/about/versions/14/changes/fgs-types-required (Android 14 FGS type requirements)

**Date accessed:** 2026-10-06
**Purpose:** Exact API ordering and rules for the phone app's screen-capture service (MediaProjection → H.264 → TCP 8900).

> Summarized in my own words from official docs; API signatures and manifest/permission strings are exact.

---

## 1. Official setup flow (exact order — do not reorder)

From the `MediaProjectionManager` class docs, the canonical flow is:

1. **Declare** a foreground service with type `mediaProjection` in `AndroidManifest.xml`.
2. `mediaProjectionManager.createScreenCaptureIntent()` → pass the returned `Intent` to `Activity.startActivityForResult()` (or the Activity Result API equivalent). This shows the system consent dialog; the user must grant it.
   - Overload `createScreenCaptureIntent(MediaProjectionConfig)`: use `MediaProjectionConfig.createConfigForUserChoice()` (same as no-arg) or `createConfigForDefaultDisplay()` to restrict to the default display.
3. In `onActivityResult`, **start the foreground service** with type `ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION`.
4. Call `mediaProjectionManager.getMediaProjection(resultCode, resultData)` to get the `MediaProjection` token.
5. `mediaProjection.registerCallback(callback, handler)` — **required before** `createVirtualDisplay()` when targetSdk is U (API 34)+; the docs advise registering before `createVirtualDisplay` regardless so no state-change notifications are missed. Implement `Callback.onStop()` to release `VirtualDisplay`, `Surface`, and update UI — onStop fires when the system/user stops the session (user revokes via system UI, screen locks, or another projection starts).
6. `mediaProjection.createVirtualDisplay(name, width, height, dpi, flags, surface, callback, handler)`.

### Android 14+ (targetSdk 34) hard rules

- The user **must grant the screen-capture consent before** the app starts the `mediaProjection` foreground service. (`getMediaProjection` docs: "the user must have granted the app with the permission to start a projection, before the app starts a foreground service with the type `FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION`".)
- The foreground service **must be started before** calling `getMediaProjection()`, or a `SecurityException` is thrown (unless the caller is a privileged app).
- `createVirtualDisplay()` throws `IllegalStateException` if targetSdk is U+ and **no `MediaProjection.Callback` is registered**. (This was the project's "Must register a callback before starting capture" crash — docs confirm: pass a real `Handler`, not null, and register before `createVirtualDisplay`.)
- `getMediaProjection()` throws `SecurityException` on API 29+ if not invoked from a foreground service with type `FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION` (unless privileged).
- `createVirtualDisplay()` throws `SecurityException` if: the projection was already `stop()`ped; `onStop()` was already received; a recording was taken without calling `stop()` (targetSdk U+); or `getMediaProjection` was invoked more than once for the same instance (targetSdk U+). On lower targetSdk the same cases silently require the user to re-grant consent instead of throwing.

### Android 10+ (targetSdk 29+) rule

- `getMediaProjection` and the capture session (`createVirtualDisplay`) **must run inside a foreground service** with `android:foregroundServiceType="mediaProjection"` declared on the `<service>` element. (This is targetSdk-Q+ behavior.)

## 2. `createVirtualDisplay` parameters (exact signature)

```java
public VirtualDisplay createVirtualDisplay(
    String name,      // non-empty, non-null
    int width,        // > 0 px
    int height,       // > 0 px
    int dpi,          // > 0
    int flags,        // DisplayManager flags; VIRTUAL_DISPLAY_FLAG_PRESENTATION always enabled
    Surface surface,  // surface to render into, or null initially
    VirtualDisplay.Callback callback,  // or null
    Handler handler)  // callback handler, or null for calling thread's main looper
```

Flags notes: `VIRTUAL_DISPLAY_FLAG_OWN_CONTENT_ONLY`, `VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR`, `VIRTUAL_DISPLAY_FLAG_PUBLIC` may be overridden depending on how the consent-holder handles user consent.

**Actionable for XR Wrist:** width/height = phone screen resolution (or scaled), dpi = phone density, flags = `DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR` for mirroring, surface = the H.264 encoder input surface.

## 3. Manifest entries (exact)

```xml
<uses-permission android:name="android.permission.FOREGROUND_SERVICE" />
<uses-permission android:name="android.permission.FOREGROUND_SERVICE_MEDIA_PROJECTION" />

<service
    android:name=".ScreenCaptureService"
    android:foregroundServiceType="mediaProjection"
    android:exported="false" />
```

And at runtime, call `startForeground(id, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION)` (or `ServiceCompat.startForeground(...)` with the type from androidx-core 1.12+).

## 4. Runtime prerequisites (official)

For the `mediaProjection` FGS type: call `createScreenCaptureIntent()` **before** starting the foreground service — this shows the permission notification to the user; the user must grant before the service is created. After the service is created, call `getMediaProjection()`.

## 5. Lifecycle / teardown

- `MediaProjection.stop()` ends the projection.
- `Callback.onStop()` must clean up: `VirtualDisplay.release()`, `Surface.release()`, stop encoder/network threads, and update any UI/status (notification, Godmode console) to reflect the stopped state.
- The session can be killed externally at any time: user stops via system UI, screen lock, or a second projection starting. Always handle `onStop` — never assume you control the lifecycle.
- The consent dialog cannot be bypassed and (since Android 10) cannot be "remembered"; each process/session needs a fresh grant. The result Intent can be cached per process lifetime to avoid re-prompting within one process run.

## 6. Concise action list

1. Manifest: `FOREGROUND_SERVICE` + `FOREGROUND_SERVICE_MEDIA_PROJECTION` permissions; `<service android:foregroundServiceType="mediaProjection" android:exported="false">`.
2. Consent flow: `createScreenCaptureIntent()` → `startActivityForResult` → on `RESULT_OK`, start the FGS **first**, then `getMediaProjection(resultCode, data)`.
3. In the service's `onCreate`/`onStartCommand`: `startForeground(id, notification, FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION)`; only after that, call `getMediaProjection`.
4. Before `createVirtualDisplay`: `registerCallback(projectionCallback, handler)` with a **real Handler** (never null — null caused the Android 14 `IllegalStateException` on Ziggy).
5. `createVirtualDisplay(name, w, h, dpi, VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR, encoderSurface, vdCallback, handler)`.
6. Implement `MediaProjection.Callback.onStop()` → release VirtualDisplay + Surface + encoder + sockets; update notification and dev-console status.
7. Guard against `SecurityException`/`IllegalStateException` around `getMediaProjection`/`createVirtualDisplay` (wrong order, double-use of intent data, revoked consent) and surface clear errors in the Godmode console.
8. Note: requesting `SYSTEM_ALERT_WINDOW` (API 30+) auto-grants it until the projection stops — useful for overlay controls on top of the captured screen.
