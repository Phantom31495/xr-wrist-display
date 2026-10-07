# XR_EXT_hand_tracking — Research Notes

**Source:** OpenXR Specification 1.1.58, §12.37 — https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html#XR_EXT_hand_tracking
**Researched:** 2026-10-06 (spec release 1.1.58, dated 2026-04-30 in the spec preamble)
**Man pages:** xrCreateHandTrackerEXT, XrSystemHandTrackingPropertiesEXT, XrHandEXT, XrHandJointLocationEXT (registry.khronos.org/OpenXR/specs/1.0/man/html/…)
**Tutorial reference:** Khronos official OpenXR tutorial, Chapter 5 — https://openxr-tutorial.com/linux/opengl/5-extensions.html

Multi-vendor EXT extension (registered #52, revision 4). Supported on Quest (requires hand tracking enabled in the headset's system settings). Gives per-joint hand poses; mesh/aim/capsule variants come from companion FB extensions below.

## Actionable call sequence

1. **Enable at instance creation:** pass `"XR_EXT_hand_tracking"` in `XrInstanceCreateInfo::enabledExtensionNames`.
2. **Load function pointers** after `xrCreateInstance` (portable route):
   ```cpp
   PFN_xrCreateHandTrackerEXT xrCreateHandTrackerEXT = nullptr;
   PFN_xrDestroyHandTrackerEXT xrDestroyHandTrackerEXT = nullptr;
   PFN_xrLocateHandJointsEXT xrLocateHandJointsEXT = nullptr;
   xrGetInstanceProcAddr(instance, "xrCreateHandTrackerEXT", (PFN_xrVoidFunction*)&xrCreateHandTrackerEXT);
   xrGetInstanceProcAddr(instance, "xrDestroyHandTrackerEXT", (PFN_xrVoidFunction*)&xrDestroyHandTrackerEXT);
   xrGetInstanceProcAddr(instance, "xrLocateHandJointsEXT", (PFN_xrVoidFunction*)&xrLocateHandJointsEXT);
   ```
   If hand tracking is unsupported/disabled these come back `nullptr` — probe, never assume.
3. **Probe capability:** chain `XrSystemHandTrackingPropertiesEXT` (type `XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT`) into `XrSystemProperties::next` before calling `xrGetSystemProperties`. If `supportsHandTracking == XR_FALSE`, do not create a tracker (`xrCreateHandTrackerEXT` will return `XR_ERROR_FEATURE_UNSUPPORTED`).
4. **Create one tracker per hand** (session-lifetime object, destroy with `xrDestroyHandTrackerEXT` when the session ends):
   ```cpp
   XrHandTrackerCreateInfoEXT ci = { XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT };
   ci.hand = XR_HAND_LEFT_EXT;            // enum XrHandEXT: LEFT=1, RIGHT=2
   ci.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;  // the standard 26-joint set
   xrCreateHandTrackerEXT(session, &ci, &handTracker);
   ```
5. **Locate joints once per frame** (per hand):
   ```cpp
   XrHandJointLocationEXT joints[XR_HAND_JOINT_COUNT_EXT];  // 26 entries
   XrHandJointsLocateInfoEXT locateInfo = { XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT };
   locateInfo.baseSpace = localSpace;              // e.g. XR_REFERENCE_SPACE_TYPE_LOCAL
   locateInfo.time = frameState.predictedDisplayTime;
   XrHandJointLocationsEXT locations = { XR_TYPE_HAND_JOINT_LOCATIONS_EXT };
   locations.jointCount = XR_HAND_JOINT_COUNT_EXT;
   locations.jointLocations = joints;
   xrLocateHandJointsEXT(handTracker, &locateInfo, &locations);
   ```
   Also check `locations.isActive` — when false, the hand is not currently tracked.

## Key structs / enums

- `XrHandJointLocationEXT { XrSpaceLocationFlags locationFlags; XrPosef pose; float radius; }` — position+orientation+joint radius per joint.
  - **Always gate on `locationFlags`:** use a joint only if `XR_SPACE_LOCATION_POSITION_VALID_BIT` (and ideally `XR_SPACE_LOCATION_POSITION_TRACKED_BIT` / `ORIENTATION_VALID_BIT`) are set. If position is invalid, `radius` is undefined.
- `XrHandJointEXT` enum — 26 joints in the default set: `XR_HAND_JOINT_PALM_EXT`, `XR_HAND_JOINT_WRIST_EXT`, then per finger (THUMB, INDEX, MIDDLE, RING, LITTLE): `METACARPAL_*`, `PHALANX_PROXIMAL_*`, `PHALANX_INTERMEDIATE_*`, `PHALANX_DISTAL_*`, `TIP_*`. Index `[XR_HAND_JOINT_COUNT_EXT] = 26` arrays directly.
- `XrHandJointsLocateInfoEXT::next` chaining — optional extra data in one call:
  - `XrHandJointVelocitiesEXT` (per-joint linear/angular velocity)
  - `XrHandJointsMotionRangeInfoEXT` — request `XR_HAND_JOINTS_MOTION_RANGE_UNOBSTRUCTED_EXT` (bare hands) vs `XR_HAND_JOINTS_MOTION_RANGE_CONFORMING_TO_CONTROLLER_EXT` (joints conforming to held controller)
  - `XrHandTrackingAimStateFB` (`XR_FB_hand_tracking_aim`) — gesture states: `pinch`/`pinchStrength`, `isActive`
  - `XrHandTrackingCapsulesFB` (`XR_FB_hand_tracking_capsules`) — list of capsules approximating hand volume
  - `XrHandTrackingMeshFB` (`XR_FB_hand_tracking_mesh`) — skinned hand mesh + bind-pose skeleton driven by the located joints (use `xrGetHandMeshFB` to fetch index/vertex buffers); render with the joint poses as bones
  - `XrHandTrackingScaleFB` — per-joint scale
  - `XrHandTrackingDataSourceStateEXT` (`XR_EXT_hand_tracking_data_source`) — tells you whether the current pose came from cameras (`UNOBSTRUCTED`) or controller emulation (`CONTROLLER`) — useful for detecting "holding controllers vs bare hands"
  - `XrHandTrackingUnextrapolatedPosesMETA` — raw capture timestamp (`captureTime`) for latency measurement

## Project-relevant takeaways (XR Wrist Display)

- Direct touch on the wrist UI = read index-fingertip joint (`XR_HAND_JOINT_TIP_INDEX_EXT`) + `radius` each frame; compare its position against the quad's plane in world space. This is the "poke" without any extra extension.
- For click semantics, pair with `XR_FB_hand_tracking_aim` pinch state, or use `XR_EXT_hand_interaction`'s poke/aim/pinch poses (see `xr-ext-hand-interaction.md`).
- Render hands as simple geometry first (tutorial renders a cuboid per joint); upgrade to the FB skinned mesh later.
- Poll once per frame after `xrWaitFrame`, using `predictedDisplayTime` for `locateInfo.time`.
