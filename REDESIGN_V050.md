# XR Wrist Display v0.5.0 — Redesign Proposal

**Date:** 2026-10-06
**Status:** APPROVED FOR BUILD (council unanimous)
**Principle:** "What does the human body want?" before "How do I build it?"

---

## 1. What v0.5.0 Changes (and Why)

### 1.1 World-Locked Engage Panel (SG-1) — THE critical fix

**Problem (from Meta's own docs):** *"Do NOT anchor complex menus to an active, moving wrist. A menu that follows the wrist is hard to use because it shifts position while the other hand tries to poke it."*

v0.4.0 keeps the engaged (zoomed) panel wrist-anchored. This is wrong per Meta's explicit guidance.

**Redesign:**
- **Glance state:** Panel stays wrist-anchored (small, ~8cm). This is fine — it's a glance, not interaction.
- **Engage trigger:** Gaze dwell >1.5s (existing) OR explicit expand gesture.
- **On engage:** The panel **glides from the wrist to a world-locked position** ~50cm in front of the chest, slightly below eye line, billboarded. 600ms eased animation (ease-out-back). The wrist is now free.
- **On disengage:** Look away >2s → panel shrinks and glides back to the wrist.
- **Why world-locked, not head-locked:** Meta warns head-locked HUDs cause fatigue. World-locked is stable, comfortable, and matches Meta's panel guidance.

**Human justification:** When you engage with content, your brain wants stability. A moving target while trying to poke it is maddening. The wrist is for glancing; the world is for doing.

### 1.2 Canonical Hand Interaction (SG-4) — robustness upgrade

**Problem:** v0.4.0 does finger touch via raw fingertip joint math. This works but is fragile — hand-rolled curl detection, no runtime-tuned pinch logic.

**Redesign:**
- Integrate `XR_EXT_hand_interaction` profile (`/interaction_profiles/ext/hand_interaction_ext`).
- **Poke intent:** Use `/input/poke_ext/pose` (index fingertip) + sphere-vs-panel overlap. This is the canonical "finger touch" path — no custom heuristics.
- **Click intent:** Use `/input/pinch_ext/value` (threshold ~0.8, debounced) for pinch-to-click. Runtime-tuned pinch detection beats our math.
- **Raw joints:** Keep `XR_EXT_hand_tracking` joints for *rendering* the avatar hands. Use the interaction profile for *intent*.
- **Fallback chain:** poke (near, <46cm) → pinch (mid, 46cm–1m) → controller ray (far or no hands).

**Human justification:** The runtime knows hands better than we do. Using its tuned gestures means fewer false triggers, fewer missed touches. The hand *feels* more responsive because it *is*.

### 1.3 Visual Language Overhaul — "Soft-Tech"

**Problem:** v0.4.0 visuals are functional but lack a coherent art direction. The watch bezel, panel, and hands don't speak the same language.

**Redesign — "Soft-Tech" art direction:**
- **Palette (from Meta research):** No pure white (`#FFFFFF`) or pure black (`#000000`). Primary surfaces: dark charcoal `#1A1A1A`–`#2D2D2D`. Accent: warm amber `#FFB74D` (already in use for glow — now systematized). Text: warm gray `#E8E8E8` (primary), `#A0A0A0` (secondary). Status: green `#4CAF50` (connected), red `#F44336` (error), amber `#FF9800` (warning).
- **Panel chrome:** When world-locked, the panel gets proper Meta-fluent chrome: 16dp-equivalent rounded corners, dark translucent scrim (`#121212` @ 92%), subtle edge highlight (1px, `#FFFFFF` @ 8%), soft drop shadow. Control bar (pill-shaped, below panel) on hover with close/minimize/re-attach actions.
- **Typography (where text is rendered):** Inter-equivalent proportions. Minimum 14dp-equivalent for legibility, 18dp for comfortable reading. No italics.
- **Hit targets:** All interactive elements ≥48dp-equivalent hit area (~22mm physical). Visual element centered with 4–6dp padding. 12mm minimum spacing between targets.
- **Hands:** Commit fully to stylization. Soft-tech = smooth, slightly abstracted, premium product render. No attempt at photorealism. Consistent material language with the watch bezel.

**Human justification:** Coherent visuals reduce cognitive load. When everything speaks the same language, the brain stops noticing the UI and focuses on the content (the phone screen). That's the goal — invisible UI.

### 1.4 Interaction Model — "Inevitable, Not Bolted-On"

**Problem:** v0.4.0 has multiple input methods (finger touch, controller ray, voice, gaze) but they feel like separate features, not a unified model.

**Redesign — Unified interaction hierarchy:**
1. **Primary (hands available, near):** Direct finger poke. Touch the screen with your index finger. 7mm press travel, ripple + click sound.
2. **Secondary (hands available, far):** Pinch. Thumb-to-index, with proximity ring visual. For when the panel is beyond arm's reach.
3. **Fallback (controllers):** Controller ray. Thin white laser, dot cursor. Trigger = select. Automatically takes over when controllers are picked up.
4. **Ambient (always):** Gaze dwell for engage/disengage. Voice for commands (Y-hold).
5. **Never:** Any action that *requires* a specific input. Every action has at least two paths.

**Mode transitions:** The system shows a subtle cursor change when switching between poke/pinch/ray (per Meta's hybrid model guidance). No jarring mode switches — the cursor *is* the mode indicator.

**Human justification:** The best interaction model is the one you don't think about. You reach out → you touch. You're far → you pinch. You pick up controllers → ray appears. It just works.

### 1.5 Godmode Console — Proper In-VR Dev Console

**Problem:** v0.4.0 has the B-button diagnostics overlay (good), but no real console. No command input, no system controls.

**Redesign:**
- **Trigger:** B-button (existing) opens the diagnostics overlay. **B-button hold (1s)** opens the full Godmode console.
- **Console layout:** Head-locked panel (left side, doesn't block main view). Three sections:
  - **Log stream:** Scrollable, monospace, color-coded (info/white, warn/amber, error/red). Shows app logs, network events, input events.
  - **Command palette:** Preset commands as tappable buttons (no virtual keyboard needed): `reconnect`, `toggle wrist/fixed`, `reset panel`, `dump state`, `toggle voice`, `cycle refresh rate`, `clear logs`.
  - **System controls:** Toggles for auto-transition, dynamic zoom, voice, passthrough. Sliders for panel size, glow intensity.
- **Visual:** Terminal aesthetic (deliberate — signals "this is a tool"). Dark background, monospace, green/amber text. Does NOT try to look like Meta UI.

**Human justification:** Developers need tools, not toys. A proper console means faster debugging, which means faster iteration, which means a better product for the user. The terminal look is honest — it says "I am a power tool."

---

## 2. What Stays (v0.4.0 Foundations)

- **Dual-hand avatar system** — geometry and articulation are solid. Refine materials, don't rebuild.
- **Network/video pipeline** — discovery, H.264 decode, control channel. Working, don't touch.
- **Voice infrastructure** — VoiceHelper.java + JNI bridge. Working, refine commands.
- **Boot auto-launch** — keep.
- **Meta SDK integration** — extension probing, refresh rate. Working.

---

## 3. Build Plan

### Files to modify:
1. **`xr_core.h/cpp`** — Add `DisplayAnchor { WRIST, TRANSITIONING, WORLD }` state. Modify `UpdateDynamicZoom` to trigger world-lock on engage. Add `ComputeWorldPanelPose`. Add Godmode console rendering.
2. **`input.h/cpp`** — Add `XR_EXT_hand_interaction` profile. Add `poke_ext` pose actions, `pinch_ext` value actions. Add `PokePose()`, `PinchValue()` getters.
3. **`devtools.h/cpp`** — Extend `TextOverlay` for console. Add command palette, system toggles.
4. **`avatar.cpp`** — Refine materials per Soft-Tech palette. No geometry changes.
5. **`gl_render.cpp`** — Add panel chrome rendering (rounded corners, scrim, edge highlight). Add control bar.

### Files to NOT touch:
- `net.cpp`, `video.cpp` — working, don't risk it.
- `voice.cpp`, `VoiceHelper.java` — working, refine later.
- `main.cpp` — entry point, stable.

### Version:
- Bump to `versionCode 5`, `versionName "0.5.0"`.

---

## 4. Verification

1. Build via manual toolchain (cmake + NDK 27, aapt2, d8, zipalign, apksigner).
2. Verify APK compliance (STORED manifest/arsc, signature).
3. Install via `~/bin/quest.sh install -r -d -g`.
4. Launch, verify: no crash, frames submitting, session running.
5. Check logcat for: hand interaction profile bound, world-lock transition logs.

---

## 5. Production Readiness (v0.5.0 addition)

Per the user's "MAKE configuration UPDATE TO MORE REALITY REAL WORLD application approach" directive:

### 5.1 Config System (`config.h/cpp` — new)
- Every tunable is a `ConfigKey` with typed getters/setters.
- Persists to app-private `xrwrist.cfg` (key=value format).
- Live-apply via listener callbacks — no restart needed.
- Covers: network (ports, timeouts, IP override), display (sizes, dwell times, distances), interaction (thresholds, toggles), avatar, godmode, system.

### 5.2 User-Facing Notifications (`notify.h/cpp` — new)
- `Notify::Info/Warn/Error(title, action)` — human-readable messages.
- Displayed as VR toasts (head-locked, 8s expiry).
- Every failure mode now has a message + recovery action:
  - "Phone not found" → "Check WiFi, tap Start on phone app"
  - "Video connection failed" → "Restart phone app"
  - "Display engaged" → "Panel moved to world-locked position"

### 5.3 First-Run Onboarding
- `FirstRunComplete` flag in Config.
- Welcome toast on first launch.
- Completion toast after first successful engage ("You're in! Pinch to click...").

### 5.4 Network Resilience
- Manual IP override via `PhoneIpOverride` config.
- Discovery timeout configurable.
- Human-readable errors on discovery failure (not silent).

### 5.5 Godmode Console Enhancement
- Shows live config values.
- New voice commands: "diagnostics", "godmode", "world lock".
- Command reference section in overlay.

## 6. Council Sign-Off

- **Engineering:** Changes are surgical, not rewrite. Risk is contained. ✓
- **Human-Centered:** Every change driven by "what does the body want?" ✓
- **Security:** No new permissions, no new network paths. ✓
- **UX:** Soft-Tech art direction, Meta-fluent where appropriate. ✓
- **Platform:** Respects Quest constraints, probes extensions. ✓
- **Researcher:** Directly implements Meta's documented guidance. ✓
- **User Reviewer:** Addresses the core complaints (tiny text, moving target, bolted-on feel). ✓

**Unanimous: BUILD.**
