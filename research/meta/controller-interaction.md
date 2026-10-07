# Controller Interaction Patterns — Research Notes

**Source URLs:**
- Ray casting (design): https://developers.meta.com/vr/design/raycasting_usage/
- Multimodality: https://developers.meta.com/horizon/design/interactions-multimodality/
- ISDK Ray Interaction: https://developers.meta.com/horizon/documentation/unity/unity-isdk-ray-interaction/
- Gear VR controller best practices (legacy blog, still-quoted patterns): https://developers.meta.com/horizon/blog/gear-vr-controller-best-practices
- Easy controller selection (blog): https://developers.meta.com/horizon/blog/easy-controller-selection/

**Date accessed:** 2026-10-06

## Actionable patterns & thresholds

### Ray casting model
- **Ray origin:** for controllers, render the controller in the scene and draw a **laser from the tip** — either extending fully to the UI or fading after a few inches. Render a **cursor at the ray–UI intersection point** (dot or ring shape).
- **Cursor semantics:** the cursor shows the exact hit point — a discrete shape (dot/ring) or a contextual hover effect on the target itself.
- **Use the system arm model** to position the rendered controller (matches Home/Universal Menu and user expectations). If you don't show a controller but drive a cursor from controller rotation, the **raycast origin must be the camera position**, not the controller.
- **Multi-depth aiming:** for 3D scenes with targets at varying depths, leave the **full laser visible at all times** so users can resolve aim ambiguity (a cursor alone can appear behind objects and confuse depth).
- For hands: use the system pointer pose (`HandPointerPose` / `XR_FB_hand_tracking_aim` `aimPose`) for ray origin — the origin is near the wrist root; the visual affordance is the teardrop pincher mesh between index and thumb.

### Targeting × selection matrix
| Targeting | Selection | Notes |
|---|---|---|
| Hand ray | Index-thumb **pinch** | Primary indirect method |
| Controller ray | **Trigger button** | Primary indirect method |
| Gaze | Hand pinch or voice | Eye-tracking devices only; system renders hover on the app's behalf (app gets selection events, not gaze hover) |
| Head ray (center of FOV) | HMD **volume buttons** | Fallback when no other input; also a dev/debug tool |

- When a user picks up controllers, the system **automatically switches to standard ray casting** (device input takes precedence over hands).
- Manipulation at distance: **pinch-and-hold / trigger-hold** on bounding-box handles for move/rotate/scale; "distance grab" pulls the object to the hand for close inspection.

### Hover / selection semantics
- Hover states are **required** on all targets (Android design requirements): users aim first, then commit. Hover feedback must be visible and distinct from focus and selected states.
- Selection semantics: touchpad/controller swipe scrolls a page in the **same direction as the swipe** (swipe up moves the page up). Both **trigger and touchpad click** should select unless one is reserved for another action.
- Focus indicators (accessibility): at least **2px thick**, high contrast, consistent style, never just a color change — combine with borders/outlines.

### Multimodality rules (Updated Sep 14, 2026)
- Horizon OS **orchestrates modality transitions in real time**; interaction types (ray casting, touch, grab) stay consistent regardless of active modality.
- **Controllers and hands are primary inputs** — the system prioritizes them.
- **Picking up a device = intent:** hand tracking for that hand is disabled automatically; concurrent hand+controller tracking is supported (one hand each). Putting the controller down switches back to hand tracking after a short re-acquisition delay.
- **Direct touch is not a 'mode' — it is always available.** Avoid artificial near/far distinctions; the system shows/hides affordances by hand distance.
- Do **not** run gaze and ray casting simultaneously — ray is the fallback when eye tracking is unavailable.
- Bluetooth mouse/keyboard/gamepad connect → their pointer/input appears and conflicting modalities (rays) deactivate automatically.

### Scroll & selection niceties
- Pages: swiping the touchpad moves the page in the swipe's direction (reference: Home implementation).
- Allow selection via **either trigger or touchpad click** unless reserved.
- Cursor behavior on panels: consistent cursor, reticle, and hover styling so users always know which mode (direct/indirect) is active.

## Summary for v0.4.0
Implement controller rays as tip-origin lasers with a visible dot/ring cursor at the panel intersection; ray origin = camera position when no controller mesh is rendered; full laser when aiming at varied depths; trigger = select, trigger-hold = manipulate; mandatory distinct hover/pressed/selected states (≥2px focus outlines, not color-only); treat direct poke as always-on and layer the controller ray on top; picking up a controller must immediately take over from hands for that hand.
