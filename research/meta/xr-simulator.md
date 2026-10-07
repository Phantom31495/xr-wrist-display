# Meta XR Simulator — Research Notes

**Sources:**
- https://developers.meta.com/horizon/documentation/unity/xrsim-getting-started/ (updated Sep 3, 2026)
- https://developers.meta.com/horizon/documentation/unreal/xrsim-intro/ (overview, updated Sep 4, 2026)

**Date accessed:** 2026-10-06
**Purpose:** Headset-free testing path for the XR Wrist native OpenXR app — especially extension-probing logic.

> Summarized in my own words from official docs; UI labels and profile names are exact.

---

## 1. What it is

A lightweight OpenXR runtime that runs on the development machine and presents an **API-level model** of a Meta Quest device to the application. It ships **no OS image and no Android layer** — it does not emulate device hardware. Test and debug on the PC: launch the app from the dev machine, drive the simulated headset/inputs with keyboard, mouse, Xbox controller, or real Touch controllers, iterate without deploying.

## 2. Device profiles (one active at a time)

Meta Quest 2, Meta Quest Pro, Oculus Rift S, Meta Quest 3, Meta Quest 3S, Meta VR Glasses. The profile is captured when the app creates its OpenXR instance — **stop and restart the app after changing device** (in Unity: exit and re-enter Play mode).

The selected profile changes what the app sees:
- Field of view, recommended render resolution, refresh-rate list
- IPD range
- **Which OpenXR extensions the runtime advertises** (a feature on one device is absent on another)
- Which input options appear
- Passthrough color mode

**Key rule:** test against the device you intend to ship on. A build that runs on one profile proves nothing about another. (This is the simulator's version of our probe-never-assume principle.)

## 3. Meta VR Glasses profile (reference values)

- FOV per eye: 37° temporal / 37° nasal / 34° up / 34° down
- Refresh rates: 90 Hz and 120 Hz (default 90)
- Recommended render resolution per eye: 1680×1760
- IPD range: 0.058–0.072 m
- Passthrough color resolution: 1024×1024
- Default inputs: eye gaze + hand tracking, **no controllers** (Touch Plus optional)

## 4. Input simulation

Sources: keyboard + mouse, Xbox controller, physical Quest Touch controllers. **Inputs** panel → **Input settings** → per-side source; **Input bindings** panel shows/reassigns key mappings. **Disconnect Controllers** returns to simulated input.

## 5. Meta VR CLI

Scriptable sessions on macOS/Windows: activate the runtime, launch/quit the app, set the simulated device, read runtime logs. (Useful for CI-style smoke tests.)

## 6. Activation (Unity path)

Toolbar icon next to Play, or **Window > Meta > Meta XR Simulator > Activate**; **Deactivate** returns to headset development; **Status** checks state. The Unity Asset Store simulator package is deprecated — use the standalone app. Between runs, exit Play mode but leave the simulator open for faster restarts.

## 7. Relevance to XR Wrist

Our app is native OpenXR (not Unity), so the Unity-specific activation path doesn't apply — but the **runtime-toggle model does**: setting the simulator as the active OpenXR runtime would let us exercise our extension-probing, session-lifecycle, and input-binding code on the dev machine without the headset. Worth evaluating once v0.5.0 stabilizes; it directly tests the "works on any advertised extension set" discipline.
