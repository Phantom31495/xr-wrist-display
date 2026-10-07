# XR_EXT_hand_interaction (+ related) — Research Notes

**Source:** OpenXR Specification 1.1.58, §12.35 — https://registry.khronos.org/OpenXR/specs/1.0/html/xrspec.html#XR_EXT_hand_interaction
**Researched:** 2026-10-06
**Also consulted:** Godot PR #81533 (extension support write-up), Unity OpenXR docs "Hand Interaction Profile" + "Hand Common Poses Interaction", VIVE OpenXR hand-interaction docs, Igalia Wolvic PR #1440, Khronos "Evolution of OpenXR" (XRExpo 2026 deck).

Promoted from `XR_MSFT_hand_interaction`; now multi-vendor. **Note:** spec revision 2 (2026) adds missing `palm_ext/pose` and `grip_surface/pose` paths to `/interaction_profiles/ext/hand_interaction_ext` — request those too for a complete profile.

## What it gives

Bridges optical hand tracking into the standard **action/input** model, so hands can be handled like controllers. Two new poses + three gesture inputs, on top of the shared aim/grip poses.

### Interaction profile
Profile path: `/interaction_profiles/ext/hand_interaction_ext`
Suggest bindings via `xrSuggestInteractionProfileBindings` after `xrCreateActionSet`/`xrCreateAction`, same as controller profiles. If both a hand and a controller profile are bound, the runtime switches based on whether controllers are held; if only the hand profile is bound, runtimes should use it even when controllers are held.

### Poses (create `XR_ACTION_TYPE_POSE_INPUT` actions, bind each to `/user/hand/left|right` + subpath, then `xrCreateActionSpace` and `xrLocateSpace` per frame)

| Subpath | Pose | Use for |
|---|---|---|
| `/input/aim/pose` | aim, index-finger direction | Ray pointer for far UI |
| `/input/grip/pose` | grip, palm center | Grabbing |
| `/input/pinch_ext/pose` | between thumb + index fingertip | Precise near manipulation |
| `/input/poke_ext/pose` | index fingertip | **Direct touch / poke on UI** — put a small sphere collider on this pose and test overlap with the panel |

### Gesture inputs (bind `XR_ACTION_TYPE_FLOAT_INPUT` or `BOOLEAN_INPUT`)

- `…/input/pinch_ext/value` (+ `ready_ext` boolean: hand is tracked and shaped to pinch) — trigger "click" with the pinch pose
- `…/input/aim_activate_ext/value` (+ `ready_ext`) — index fully extended; pair with the aim pose to stabilize the ray while gesturing
- `…/input/grasp_ext/value` (+ `ready_ext`) — fist = grab
- `…/input/poke_ext/pose` — no paired gesture; the app does the touch detection itself

### Per-frame read pattern
```cpp
// pose
XrActionStatePose state = { XR_TYPE_ACTION_STATE_POSE };
XrActionStateGetInfo getInfo = { XR_TYPE_ACTION_STATE_GET_INFO, nullptr, pokeAction, subactionPath };
xrGetActionStatePose(session, &getInfo, &state);
if (state.isActive) { xrLocateSpace(pokeSpace, baseSpace, predictedTime, &location); ... }
// gesture float
XrActionStateFloat f = { XR_TYPE_ACTION_STATE_FLOAT };
xrGetActionStateFloat(session, &getInfo2, &f);   // f.currentState for pinch strength
```

## Related / adjacent extensions

- `XR_EXT_hand_joints_motion_range` — `XrHandJointsMotionRangeInfoEXT` chained into `XrHandJointsLocateInfoEXT::next`; choose unobstructed vs controller-conforming joint ranges.
- `XR_FB_hand_tracking_aim` — Meta's own gesture-state chain (`pinch`, `pinchStrength`, `isActive`) if you don't want the full action system.
- `XR_META_hand_tracking_microgestures` — pinch variants / finger-specific inputs for subtler gestures.
- Wolvic's approach (production browser, Quest): use the hand interaction profile and handle non-controller action sources in the existing action pipeline — don't assume actions only come from physical controllers.

## Project-relevant takeaways (XR Wrist Display)

- **Primary touch model for the wrist UI:** `/input/poke_ext/pose` + manual sphere-vs-panel overlap test each frame. That is the canonical "finger touch" path — no custom heuristics needed.
- **Click model:** pinch via `pinch_ext/value` (threshold ~0.7–1.0, debounced) or `aim_activate_ext` for far-mode.
- Implement the action profile even though raw joints are available — it gives pinch/aim activation logic the runtime already tuned (pinch detection quality beats hand-rolled curl math).
- Keep raw `XR_EXT_hand_tracking` joints for rendering the fingertip cursor; use the interaction profile for *intent*.
