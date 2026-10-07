# XR Wrist Display v0.6.0 — VR Environment + In-VR Menu

## What was built

A comfortable VR environment and a floating in-VR menu system for the native
C++ OpenXR Quest app. No Unity/Unreal. Follows existing code patterns.

## VR Environment (`environment.h` / `environment.cpp`)

A warm, professional space — not a void. Three procedural draw calls, no
textures:

| Element | Description |
|---------|-------------|
| **Sky dome** | 40m inverted sphere, vertical gradient: warm amber horizon → soft slate mid → deep blue-charcoal zenith. Subtle warm "sun" glow for depth cueing. Follows head translation (never reachable). |
| **Ground grid** | 32m disc at y=0. Concentric 1m rings + 24 radial spokes in warm amber, fading exponentially with distance. Soft platform glow under the user. |
| **Ambient motes** | 140 slow-drifting points (warm white, alpha ~0.3) for depth perception. Drift computed in the vertex shader from a time uniform — zero CPU per frame. |

The environment is **skipped in passthrough mode** (the real world is the
backdrop) and when `EnvironmentEnabled` is false.

### Config tunables (new)

| Key | Type | Default | Effect |
|-----|------|---------|--------|
| `PassthroughEnabled` | bool | true | Mixed-reality passthrough on/off |
| `EnvironmentEnabled` | bool | true | VR room backdrop on/off |

## In-VR Menu (`menu.h` / `menu.cpp`)

### Opening / closing
- **Quick Y-tap** (< 0.45s) toggles the menu.
- **Y-hold** (≥ 0.8s) remains voice push-to-talk — unchanged.

### Layout (human-centered)
- Floats **0.55m** ahead, **6cm below** eye line, billboarded to face the user.
- Panel: 0.36m wide, height adapts to page. Buttons 0.33m × 0.044m — roughly
  **6× the 48dp-equivalent minimum** at this distance.
- Soft-Tech styling: charcoal plates, warm amber hover border, rounded
  corners. Open animation: 0.18s ease-out scale 0.92 → 1.0.

### Pages

**Main**
1. `Stream: Start/Stop` — dynamic label; stops/starts the phone link.
   Stop bumps a network generation counter so a discovery thread still in
   flight aborts instead of connecting late.
2. `Settings` → Settings page
3. `Environment: VR Room/Passthrough` — dynamic label; toggles MR mode live.
4. `Diagnostics` — toggles the Godmode overlay (same as B button).
5. `About` → About page

**Settings**
- `Display size: ±` — EngagedSizeM, 0.30–0.60m in 0.02 steps
- `Gaze dwell ±` — GazeDwellSec, 0.5–3.0s in 0.25 steps
- `Voice: On/Off`, `Auto-transition: On/Off`, `Avatar hands: On/Off`
- `< Back`

**About**
- Version, "Native OpenXR client", project URL, `< Back`

### Input
| Modality | Hover | Select |
|----------|-------|--------|
| Controller | Right-hand ray + amber aim line | Trigger pull (edge > 0.55) |
| Hand tracking | Poke fingertip proximity (XR_EXT_hand_interaction, fallback to index tip) | Pinch (edge > 0.8, either hand) |

While the menu is open **and** the pointer is on it, `ConsumesInput()` returns
true and the app suppresses phone-bound touches — a trigger pull on a menu
button never also taps the phone screen.

### Implementation notes
- Text uses the existing `devtools::TextOverlay` (8x8 font). Two minimal,
  backward-compatible additions: `SetBackgroundColor()` and
  `SetBackgroundEnabled()` — the menu draws its own rounded plates, so row
  labels render with transparent backgrounds.
- Button plates use an SDF rounded-rect shader with border support.
- Dynamic labels refresh after every activation.

## XrApp integration (`xr_core.h` / `xr_core.cpp`)

- `Init()`: `env_.Init()`, `menu_.Init()` with action lambdas
  (stream toggle, diagnostics toggle, passthrough toggle, version string).
- `RenderLayer()`: passthrough composition layer is now gated on
  `PassthroughEnabled`; environment draws after clear when passthrough is
  off; `menu_.Update()` runs every frame; `menu_.Draw()` renders last (on top).
- New methods: `StopNetwork()`, `IsStreaming()`, `ToggleDiagnostics()`
  (shared by the B button and the menu).
- `StartNetwork()` captures a generation counter; the thread aborts if
  `StopNetwork()` bumped it mid-discovery.
- `Shutdown()`: `menu_.Shutdown()`, `env_.Shutdown()`.

## Build

`CMakeLists.txt` adds `environment.cpp` and `menu.cpp`.

**Native lib verified:** cmake + NDK 27, arm64-v8a, Release — zero warnings,
`libxrwrist.so` (5.4MB) with all new symbols exported.

**Full APK** (`quest/app/build/outputs/apk/release/app-release-v060.apk`,
7.0MB): assembled by swapping the new `libxrwrist.so` into the v0.5.0 APK
(all other entries byte-preserved: manifest STORED, resources.arsc
STORED+aligned), then `zipalign -f 4` + `apksigner` with
`quest/xrwrist.keystore`. `apksigner verify` passes.

**Install:** `adb install -r -d -g app-release-v060.apk`
(same versionCode 0 as the on-device v0.5.0 build, so `-r` reinstalls cleanly;
source manifest is bumped to versionCode 6 / v0.6.0 for the next full rebuild).

## Verification status

- [x] Native lib compiles clean (zero warnings)
- [x] APK assembles, aligns, signs, verifies
- [ ] On-device: menu open/close, hover, selection (needs human in-headset)
- [ ] On-device: environment visuals, passthrough toggle (needs human in-headset)
- [ ] 1-hour stability with menu usage

## Files changed

| File | Change |
|------|--------|
| `quest/app/src/main/cpp/environment.h` | New |
| `quest/app/src/main/cpp/environment.cpp` | New |
| `quest/app/src/main/cpp/menu.h` | New |
| `quest/app/src/main/cpp/menu.cpp` | New |
| `quest/app/src/main/cpp/config.h` | +2 keys |
| `quest/app/src/main/cpp/config.cpp` | +2 defaults |
| `quest/app/src/main/cpp/devtools.h` | TextOverlay bg color/enable |
| `quest/app/src/main/cpp/devtools.cpp` | Honor bg color/enable in Draw |
| `quest/app/src/main/cpp/xr_core.h` | Includes, members, method decls |
| `quest/app/src/main/cpp/xr_core.cpp` | Init/Shutdown/RenderLayer/network/menu wiring |
| `quest/app/src/main/cpp/CMakeLists.txt` | +2 sources |
| `quest/app/src/main/AndroidManifest.xml` | versionCode 6 / v0.6.0 |
| `quest/MENU_ENV_DESIGN.md` | This doc |
