# Interaction SDK Overview + Hand Tracking API — Research Notes

**Sources:**
- https://developers.meta.com/horizon/documentation/spatial-sdk/spatial-sdk-isdk-overview/ (updated Apr 2, 2026)
- https://developers.meta.com/horizon/documentation/unity/unity-handtracking-overview/ (updated Sep 14, 2026)

**Date accessed:** 2026-10-06
**Purpose:** Interaction primitives (poke, pinch, ray, grab) as design reference for the XR Wrist native app's direct-touch work; hand-tracking feature inventory.

> Summarized in my own words from official docs; component/class names are exact.

---

## 1. What Interaction SDK is

A device-agnostic input layer that converts raw controller and hand inputs into generic pointer events. Since v0.8.0 it is the default input system, replacing the legacy `InputSystem`.

| | Interaction SDK (default) | InputSystem (legacy) |
|---|---|---|
| Device support | Hands + controllers, unified API | Controller-centric |
| Interaction model | Ray, pinch, **direct touch** | Ray only |
| Panel touch | Direct touch with **collision prevention** | Ray-based only |
| Grabbables | Grab, move, rotate, scale | Limited |

It is enabled automatically when you register `VRFeature`. Opting out is launch-only (not runtime): set `inputSystemType = VrInputSystemType.SIMPLE_CONTROLLER` on the `VRFeature` in `registerFeatures`. The lower-level `Controller` component API works with both systems for raw button states and poses.

## 2. Per-frame tick (conceptual pipeline)

Each frame `IsdkSystem` ticks the native library:
1. Read raw input from OpenXR input devices (hands and controllers).
2. Read Interaction SDK component changes from the ECS data model (add/update/delete interactables).
3. Physics hit-test between input devices and interactables.
4. Conflict resolution (e.g., two close interactables — which is grabbed?).
5. Apply positional updates to interactable entities (grab transforms).
6. Apply **touch limiting** to device visuals (hand/controller meshes are prevented from penetrating panels — this is what makes poke feel natural).
7. Broadcast pointer events: raw via `onPointerEvent`, backwards-compatible via `onInput`.

## 3. Components (exact names)

`IsdkCurvedPanel`, `IsdkPanelDimensions`, `IsdkPanelGrabHandle`, `IsdkGrabbable`, `IsdkGrabConstraints`, `IsdkBoxCollider`, `IsdkSphereCollider`. With the exception of `IsdkGrabConstraints`, these are added automatically by ISDK systems.

## 4. Caveats that matter

- Curved panels cannot be grabbed and transformed.
- Entities with `Grabbable`/`IsdkGrabbable` stop receiving `onClick` unless they also carry `IsdkPanelDimensions` — listen for button presses in `onInput` instead.
- Interaction SDK can only be disabled at app launch, never conditionally at runtime.

## 5. Hand tracking feature inventory (Unity overview)

**Tracking:** Hand Tracking, Fast Motion Mode (60 Hz, fitness/rhythm), Wide Motion Mode (plausible poses outside FOV), Multimodal (hands + controllers simultaneously), Capsense (logical hand poses while holding controllers), OpenXR Hand Skeleton.

**Poses & gestures:** Pose Detection, Pose Recording, Gesture Detection (sequences of states), Microgestures (thumb tap/swipe on the side of the index finger).

**Interactions:** Poke (direct touch on surfaces), Grab, Hand Grab (physics-less, hand-specific), Distance Grab, Ray Grab, Custom Grab Poses, Throw, Raycast, 2D Widget Interaction.

**Visuals:** Custom hand models can replace the default Interaction SDK hands.

**Data rule:** hand size/pose data may be used only to enable hand tracking in-app — expressly forbidden for any other purpose.

**Meta's guidance:** hand tracking complements controllers; it is not meant to replace them where precision matters. The recommended integration path is the Interaction SDK; custom interactions without it are "a significant challenge" and complicate store approval.

## 6. Relevance to XR Wrist (native OpenXR app)

We are native OpenXR, not Unity/Spatial SDK, so ISDK itself doesn't apply — but the primitives map directly onto our work:
- **Poke** = our v0.4.0 direct finger touch. Meta's collision-prevention (touch limiting) is the reference behavior for our glass ripple/contact model.
- **Custom hand models** = precedent for our procedural avatar hands; Meta explicitly supports replacing hands.
- **Pinch + ray** remain the distance fallback, matching Meta's own hierarchy (touch near, ray far).
