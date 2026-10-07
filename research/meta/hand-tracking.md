# Hand Tracking Design Guidelines — Research Notes

**Source URLs:**
- Hands UI best practices: https://developers.meta.com/horizon/design/hands-ui-best-practices/
- Gesture Design: https://developers.meta.com/horizon/design/hands-gesture-design/
- Wrist buttons (system): https://developers.meta.com/horizon/design/wrist-buttons/
- Limitations & Mitigations: https://developers.meta.com/horizon/design/hands-limitations-mitigations/
- Input/Interactions Setup (Unity): https://developers.meta.com/horizon/documentation/unity/unity-handtracking-interactions/
- VRC.Quest.Input.8 (system gesture reservation): https://developers.meta.com/horizon/resources/vrc-quest-input-8/

**Date accessed:** 2026-10-06

## Actionable design tokens & thresholds

### Target sizing (hands UI best practices — Updated Sep 24, 2026)
- **Minimum hit target: 48dp** at panel scale — this is the *hit target*, not the visual button. Render a smaller visual element centered inside it with **4–6dp padding** so selection is forgiving. At the recommended touch distance, 48dp ≈ **~22mm** of physical target.
- **Minimum angular collider size: 2.5°–3°** for both direct touch and ray casting. Below this, targets become unreliable under hand tremor and tracking variance.
- **Minimum spacing between adjacent interactables: 12mm** — prevents accidental selection from hand tremor or brief tracking gaps (the "Midas Touch" effect).
- **Direct touch (poke):** position UI **42–46cm** from the user to encourage touch — within comfortable arm extension without leaning forward.
- **Indirect (ray/gaze):** comfortable from **~0.8m to 3m**. Never place ray-targeted UI closer than **0.8m** (users will expect to touch it). Avoid the dead zone **0.5m–0.8m** — go under ~46cm (touch) or 1m+ (ray).
- Panels should be kept **level** with the user's space (no arbitrary tilt). Pitch/yaw should track the user to stay legible; **constrain roll**.
- Frequent-use elements go in the **bottom half** of the panel — sustained reaching/pinching with arms elevated causes shoulder/arm fatigue; lower placement keeps the head neutral. Let users reposition UI to their own comfortable reach.
- When a panel moves along the z-axis, **maintain angular size**, not physical size — legibility/targetability stays constant.

### Poke/touch specifics
- **Button travel depth: 7mm** (default) — a short physical press reads as pushing into a real surface.
- **Touch limiting:** virtual hand must not pass completely through a 2D panel — apply touch limiting so the hand collides; phasing through breaks solidity illusion.
- **Index finger only** for direct touch on UI panels — other fingers cause accidental activations.
- **No progressive disclosure on hover** — layout must not shift as the finger approaches; options moving under the finger = unintended presses.
- Scroll: **index-finger swipe** directly on panel content (poke through, then drag up/down) for direct UI; **microgesture thumb swipe** for indirect UI in addition to pinch-and-drag. **No traditional 2D scroll bars** (pinch-and-drag on a thin bar fails for most users). Content must be visibly clipped at the edge as the scroll affordance.

### Pinch gestures
- **Pinch = select** (stand-in for clicks across UIs). Index-thumb pinch has the highest precision; accuracy degrades for middle/ring/pinkie fingers.
- **Pinch-and-hold** = manipulation (grab bounding-box handles for move/rotate/scale).
- Microgestures: **thumb swipes** for navigation (D-pad-like turn/step/teleport in games; scrolling in UI).
- Gesture design criteria: **Reliability, Comfort, Intuition**. Recognizers should be lenient with limited recognition windows; account for intention→execution latency; avoid rapid-transition ambiguities; limit the gesture count (too many = misrecognition); gate with proximity/hover to prevent accidental activation; prefer poses facing the cameras (avoid self-occlusion); teach gestures with visual instructions, not verbal ones.
- Reserved: the **system gesture** (dominant hand palm-up + index pinch → Universal Menu) is reserved by the OS; apps must not bind anything to it and must ignore other gesture events while it is in progress.

### Hand visuals
- Only use **high-confidence, visible** hand pose data for rendering/interaction (`IsTracked` + `HandConfidence == High`).
- Scale rendered hands with the runtime **HandScale** property (relative to the default 1.0 hand model).
- For ray interactions from hands, use the **system pointer pose** (`XR_FB_hand_tracking_aim` `aimPose` in OpenXR; `HandPointerPose` in ISDK) — it sits near the wrist root, not at the fingertip mesh. Show the teardrop pincher mesh between index and thumb as the visual affordance.
- Avatars/hands: avoid exaggerated stylization conflicts; keep the wrist-button area unobstructed if system wrist buttons are in play (constant size, fades to zero opacity after timeout without interaction).

### Wrist anchoring — critical for XR Wrist Display
- **Do NOT anchor complex menus to an active, moving wrist.** "A menu that follows the wrist is hard to use because it shifts position while the other hand tries to poke it. The wrist also moves naturally during gameplay, which causes accidental triggers or missed inputs."
- Spawning a menu *from* the wrist (e.g., on palm-up glance) is acceptable **only if the menu is static once it appears** and sits in world space, not following the wrist. Keep wrist-spawned menus simple; primary/complex actions belong on world-space panels.

### Feedback (hands have no haptics — audiovisual is mandatory)
- **Hover:** clear hover state before commit.
- **Pressed:** every interactive element changes visually on press — color shift, depression (7mm travel), glow, or all three.
- **Audio:** pair every successful selection with a short, distinct sound — the primary confirmation the action registered.
- Subtle feedback designs will frustrate users — this is not optional.

### Hybrid interaction model (relevant to wrist UI)
- Use hybrid: **direct touch when the hand is near the panel, ray casting when far**. On transition, visibly change the cursor to a raycast reticle (mode indicator) and keep hover/reticle feedback consistent. Test the handoff zone explicitly.
- Don't run gaze and ray casting as simultaneous targeting modes — ray is the fallback when eye tracking is unavailable.
- Input handoff: picking up a controller disables hand tracking for that hand (device takes precedence); putting controllers down switches back automatically after a short delay.

## Summary for v0.4.0
Wrist overlay panel must be **world/fixed-locked after appearing** — never live-follow the wrist. Touch targets ≥48dp hit (~22mm physical) with 4–6dp visual padding, 12mm spacing; 7mm press travel; index-finger poke only; touch limiting on; mandatory hover/press states + short confirmation sound per action; thumb-swipe or index-swipe for scrolling (no scrollbars); frequent controls in the lower half of the panel; hybrid poke-near/ray-far with visible reticle change; reserve palm-up index pinch for the system; only high-confidence hand data drives visuals.
