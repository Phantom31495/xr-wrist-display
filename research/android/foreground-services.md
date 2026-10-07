# Foreground Service Guidelines — Research Notes for XR Wrist Display Phone App

**Sources:**
- https://developer.android.com/develop/background-work/services/foreground-services (overview, last updated 2026-10-01 UTC)
- https://developer.android.com/develop/background-work/services/fg-service-types (all FGS types, incl. mediaProjection)
- https://developer.android.com/about/versions/14/changes/fgs-types-required (Android 14 type requirements)

**Date accessed:** 2026-10-06
**Purpose:** Exact manifest/permission/runtime rules for the phone app's `mediaProjection` foreground service (screen capture → encode → stream to Quest).

> Summarized in my own words from official docs; API signatures, manifest attributes, and permission strings are exact.

---

## 1. Core concept

Foreground services perform user-noticeable async work and **must show a status-bar notification** so the user knows the app is running and consuming resources. Only use one when the work is user-noticeable even when the user isn't interacting with the app. (Screen streaming to the Quest qualifies.)

## 2. Manifest declaration (exact)

Every foreground service must declare `android:foregroundServiceType` (mandatory since Android 14; on Android 15+ an untyped service throws `MissingForegroundServiceTypeException` at `startForeground()`).

For XR Wrist screen capture:

```xml
<uses-permission android:name="android.permission.FOREGROUND_SERVICE" />
<uses-permission android:name="android.permission.FOREGROUND_SERVICE_MEDIA_PROJECTION" />

<service
    android:name=".ScreenCaptureService"
    android:foregroundServiceType="mediaProjection"
    android:exported="false" />
```

- The `FOREGROUND_SERVICE_MEDIA_PROJECTION` permission is a **normal permission** — granted by default, user cannot revoke it. (All per-type `FOREGROUND_SERVICE_*` permissions are normal/install-time.)
- Multiple types can be combined (`"mediaProjection|microphone"`), but then the service must satisfy the runtime prerequisites of **all** declared types.

## 3. Starting the service (exact runtime call)

Best practice per docs: use `ServiceCompat.startForeground()` (androidx-core 1.12+) and pass the type as a bitwise int:

```kotlin
ServiceCompat.startForeground(
    0, // notificationId
    notification,
    ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION
)
```

Equivalent framework call: `startForeground(id, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION)`. If you omit the type at runtime, it defaults to the manifest declaration; if the manifest has none, the system throws `MissingForegroundServiceTypeException`.

## 4. Version-by-version behavior rules

### Android 8 (API 26) — baseline (unchanged context)
- Start via `Context.startForegroundService()`; the service **must call `startForeground()` within ~5 seconds**, or the system kills it (ANR: `ForegroundServiceDidNotStartInTimeException`).

### Android 12 (API 31) — background-start restrictions
- Apps **cannot start foreground services from the background** (e.g. from a `BOOT_COMPLETED` receiver or while cached) except via narrow exemptions (user-initiated action, widget/button tap, notification action, etc.). Violation → `ForegroundServiceStartNotAllowedException`.
- **The service must call `startForeground()` within ~10 seconds** of being started. (Docs cite 10s as the Android 12 requirement.)
- Exact exemptions ("background start allowed" cases) are enumerated in the official guide; for XR Wrist the stream is always started by a user tap, so this is satisfied.

### Android 13 (API 33)
- `POST_NOTIFICATIONS` **runtime permission** is required before the service's notification can be shown. The notification must clearly describe the user-visible work (e.g. "Streaming screen to Quest").
- The service notification must be posted with a proper channel (API 26+ channels).

### Android 14 (API 34) — mandatory types
- `android:foregroundServiceType` is **required** in the manifest; missing type → `MissingForegroundServiceTypeException` on `startForeground()`.
- System **runtime checks**: it verifies the app meets each type's prerequisites *before* `startForeground()` — permissions must be requested and granted first (order: consent/permissions → start service → `startForeground()`).
- `mediaProjection` prerequisites: `createScreenCaptureIntent()` shown and user-granted **before** starting the service; `getMediaProjection()` **after** the service exists.
- Users can now **manually stop foreground services** from the system UI (Foreground Service Task Manager) at any time → expect `onDestroy()`/projection `onStop()` and handle gracefully.

### Android 15 (API 35)
- Further enforcement hardening; untyped services started on 35+ throw `MissingForegroundServiceTypeException`.

## 5. Notification requirements

- A persistent notification is mandatory; on API 33+ the app needs `POST_NOTIFICATIONS` granted at runtime, otherwise the notification is suppressed (service still runs, but the user can't see/manage it — bad).
- Notification should describe the user-visible work and ideally offer a Stop action (also mitigates users killing it via the system task manager).
- Test custom notification content views under both light and dark themes (from dark-theme guidance: don't assume a light background or hardcode text colors).

## 6. Stopping

- Call `stopForeground()` + `stopSelf()` when streaming ends or reaches a failure state. For `mediaProjection`, also call `MediaProjection.stop()` and release the `VirtualDisplay`/`Surface`/encoder in the same teardown path.
- `onTimeout(int, int)` exists on API 34+ for time-limited types — not applicable to `mediaProjection` (no system timeout), but be aware it exists for `shortService`/`mediaProcessing`.

## 7. Concise action list

1. Manifest: `FOREGROUND_SERVICE` + `FOREGROUND_SERVICE_MEDIA_PROJECTION`; service declared with `android:foregroundServiceType="mediaProjection"`, `android:exported="false"`.
2. Runtime start: `ServiceCompat.startForeground(id, notification, FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION)` — call within 10s of service start (Android 12+).
3. Start the service only from a user-initiated action (tap "Start Streaming") — background starts throw `ForegroundServiceStartNotAllowedException` on Android 12+.
4. Request `POST_NOTIFICATIONS` at runtime (Android 13+); build the notification on a proper channel with clear text ("Streaming screen to Quest") + a Stop action.
5. Respect the ordering: consent → start FGS → `startForeground(type)` → `getMediaProjection()` → `registerCallback` → `createVirtualDisplay`.
6. Handle external stop: user task-manager kill / system `onStop()` → release everything, update Godmode console status.
7. Keep targetSdk 35 compliant: typed service, normal per-type permission declared, notification posted — the project's compliance rebuild already aligns with this.
