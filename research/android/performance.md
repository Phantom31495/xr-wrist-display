# Android Performance: JankStats, FrameMetrics, Baseline Profiles — Research Notes

**Sources:**
- https://developer.android.com/topic/performance/jankstats?authuser=1 (JankStats library)
- http://developer.android.com/topic/performance/baselineprofiles/overview (Baseline Profiles overview)

**Date accessed:** 2026-10-06
**Purpose:** Frame-quality instrumentation and startup optimization for the phone app (dev console, Godmode diagnostics).

> Summarized in my own words from official docs; class/method names and dependency coordinates are exact.

---

## 1. JankStats (androidx.metrics:metrics-performance)

Tracks per-frame jank, per `Window`. Builds on the platform `FrameMetrics` API (API 24+) and adds two things FrameMetrics lacks: **jank heuristics** (decides what counts as jank) and **UI state context** (what the app was doing when jank occurred).

### Usage (exact API)

```kotlin
implementation "androidx.metrics:metrics-performance:1.0.0"
jankStats = JankStats.createAndTrack(window, jankFrameListener)
// onResume:  jankStats.isTrackingEnabled = true
// onPause:   jankStats.isTrackingEnabled = false
```

The `OnFrameListener` receives a `FrameData` per frame: `frameStartNanos`, `frameDurationUiNanos`, `isJank`; on API 31+ also `frameDurationCpuNanos` and `frameOverrunNanos` (via `FrameDataApi24`/`FrameDataApi31`). **The FrameData object is reused every frame — copy out what you need inside the callback**, and return quickly (the callback runs on the FrameMetrics thread on API 24+).

### UI state

Populate context through `PerformanceMetricsState` (not JankStats directly):

```kotlin
val holder = PerformanceMetricsState.getHolderForHierarchy(binding.root)
holder.state?.putState("Activity", javaClass.simpleName)   // key/value pairs
holder.state?.putSingleFrameState("Scroll", "Fling")        // auto-removed after one frame
```

For aggregation across sessions use `JankStatsAggregator` and `issueJankReport()` (e.g., on pause).

## 2. Baseline Profiles

Ship a `baseline-prof.txt` with the app; ART AOT-compiles the listed code paths at install time → **~30% faster code execution from the very first launch** (startup, navigation, scroll), without waiting for JIT/cloud profiles. Also reduces runtime jank on hot paths.

### How they work

Human-readable rules → compiled to `assets/dexopt/baseline.prof` in the APK/AAB → Play ships it with the APK → ART pre-compiles at install. Cooperates with Cloud Profiles (which fine-tune later from real usage).

### Generation

Use the Macrobenchmark library (`androidx.benchmark:benchmark-macro-junit4`, `BaselineProfileRule.collect`) to exercise startup + critical user journeys (scroll, navigation). **The profile-generation build must NOT be obfuscated** (`isMinifyEnabled = false`); the release build IS obfuscated — R8 rewrites the rules to match. Minimum toolchain: AGP 8.0, benchmark-macro-junit4 1.5.0, profileinstaller 1.4.1.

### Startup Profiles (AGP 8.2+)

A related artifact (`startup-prof.txt`) that optimizes DEX *layout* at compile time for faster startup — roughly an additional ~15% startup win on top of baseline profiles.

## 3. Relevance to XR Wrist phone app

- Add `androidx.metrics` + JankStats to the dev-console/Godmode build to quantify UI jank in the streaming controls — cheap, production-safe.
- Generate a Baseline Profile covering app start → Start Streaming tap → streaming steady-state; the phone app is small, so the win is mostly first-launch responsiveness.
- (Quest app is native C++ — these Android-Jetpack tools don't apply there; the equivalent discipline is our bounded frame-time logging.)
