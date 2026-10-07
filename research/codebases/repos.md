# Open-Source Reference Codebases — Research Notes

**Researched:** 2026-10-06. No repos cloned (per task constraints); summaries from READMEs, docs, and code-review notes found via web search.

---

## 1. meta-quest/meta-openxr-sdk — native hand tracking samples (official Meta)

**URL:** https://github.com/meta-quest/meta-openxr-sdk

**Pattern demonstrated:** Native C++ OpenXR hand tracking — rendering joints, skinned mesh, capsules, and aim gestures; plus how to detect data source (camera vs controller-emulated hands).

**Key files/functions:**
- `Samples/XrSamples/XrHandsFB/` — `main.cpp`, `xr_hand_helper.h` (hand helper class encapsulating `xrCreateHandTrackerEXT` / `xrLocateHandJointsEXT`).
- `Samples/XrSamples/XrHandDataSource/` — `xr_hand_helper.h`, `main.cpp`; demonstrates `XR_EXT_hand_tracking_data_source` — passing `XrHandTrackingDataSourceEXT` arrays at tracker creation and reading `XrHandTrackingDataSourceStateEXT::dataSource` per frame.
- Study: how they toggle between mesh (`XR_FB_hand_tracking_mesh` + `xrGetHandMeshFB` for index/vertex buffers + bind-pose skeleton), capsules (`XR_FB_hand_tracking_capsules`), and raw joints in one sample, and how gesture selection is done via hand-tracked buttons.
- Sample README: https://github.com/meta-quest/meta-openxr-sdk/blob/HEAD/Samples/XrSamples/XrHandsFB/README.md

**Relevance:** closest thing to an official reference for the Quest-side hand rendering and poke input we need.

---

## 2. Unity-Technologies/mr-example-meta-openxr — spatial UI + poke interaction + video panel

**URL:** https://github.com/Unity-Technologies/mr-example-meta-openxr

**Pattern demonstrated:** Floating, world-locked VR UI panels with near (poke/direct) and far (ray) interaction; hand menu that appears when the palm faces the user; a spatial panel containing a video player the user can grab and move; "Lazy Follow" billboarding behavior.

**Key elements:**
- `Assets/Scenes/SampleScene` — preconfigured scene: Spatial UI examples (Coaching UI with Goal Manager, Hand Menu Setup with scroll menu + toggle/button/dropdown items, Spatial Panel Manipulator with the video player).
- XR Interaction Toolkit interactors: **poke, direct, ray** driven by the hand/controller input systems; "Hand Menu Follow Preset" / "Controller Follow Preset" for lazy-follow placement.
- Shows the complete interaction stack: XR Hands → hand tracking subsystem → poke interactor on UI canvases — the exact Unity-side equivalent of our `poke_ext` pose + overlap test.

**Relevance:** best reference for "how a floating panel should behave" — grab to reposition, billboard/lazy-follow, near vs far interaction modes. Directly maps to our wrist-toggleable-to-fixed quad requirement.

---

## 3. Igalia/wolvic — production OpenXR app with hand-interaction profile

**URL:** https://github.com/Igalia/wolvic

**Pattern demonstrated:** A shipping browser app whose 3D UI panels are driven through the OpenXR action system, including the `XR_EXT_hand_interaction` profile (PR #1440 "Add support for hand interaction profile").

**Key lessons (from the PR discussion):**
- Hand tracking was previously disconnected from the action model — gestures had to be vendor-hacked or inferred from joint positions. With the hand interaction profile, the same action pipeline (poses + pinch/aim/grasp) serves hands and controllers.
- Their integration work: don't assume action events come only from physical controllers; retrieve hand joint data also when the hand interaction profile (not just bare-hand mode) is active.
- Study: search the tree for `hand_interaction_ext` and the OpenXR device layer's hand-joint/poke handling.

**Relevance:** proof that hand-interaction profile is the right abstraction for UI (vs raw joints), and a real-world example of poke-style UI input.

---

## 4. maxnau89/mupen64plus-ae-quest — native OpenXR quad layer for a flat screen

**URL:** https://github.com/maxnau89/mupen64plus-ae-quest

**Pattern demonstrated:** A native OpenXR Android app that renders its 2D content on an **`XrCompositionLayerQuad`** placed in the room ("a real OpenXR session with a quad layer for the game, so the picture is sharp and stays where you put it while you move your head").

**Key pattern to study:**
- A second swapchain feeding the quad layer; fill `XrCompositionLayerQuad { type, subImage (swapchain + imageRect), pose (world-locked), size, eyeVisibility }` and submit in the `xrEndFrame` layers array **before** the projection layer.
- World-locked pose: layer space from `XR_REFERENCE_SPACE_TYPE_LOCAL` (or stage) + fixed pose, so the panel stays put while the head moves.
- Layer ordering: quad before projection = UI under the 3D scene is wrong; quad after projection = panel drawn over the scene (with alpha blend flags for cutouts).
- Build is a NativeActivity + CMake + OpenXR loader, same shape as our Quest app — including the manifest entries (`IMMERSIVE_HMD`, `vr.headtracking`).

**Relevance:** the exact mechanism for our wrist/fixed video quad — a compositor-level quad is sharper (no double resampling) and cheaper than drawing the quad inside the projection layer's eye buffers. Corroborated by wesjones15/shadows-of-doubt-vr's write-up: acquire/wait a swapchain image, blit the panel texture into it, write the `XrCompositionLayerQuad`, add to `xrEndFrame` layers (https://github.com/wesjones15/shadows-of-doubt-vr/blob/HEAD/docs/postfx_immune_ui.md).

---

## 5. neonatural/xr-3d-viewer — hardware-decoded video → OpenXR surface (native Android)

**URL:** https://github.com/neonatural/xr-3d-viewer

**Pattern demonstrated:** Live video texture path on Quest without pixel copies: Android **Media3/ExoPlayer** decodes on the hardware decoder and renders directly into the XR renderer's input `Surface` (`XrRenderer.getInputSurface()`), keeping the frame path entirely GPU-side.

**Key elements:**
- `VideoXrActivity` — document picker → Media3 pipeline → surface handoff; first frame into the XR surface ~564 ms after activity start on Quest 3.
- `ARCHITECTURE_NOTES.md` — documents the sizing decisions that matter for us: swapchain sized to the decoded frame (they bumped the static-image canvas from 1280×720 to 2048×1152, i.e. 2560×960-ish sources decoded at full resolution), bitmap longest-side clamped to 3072, DIBR/video depth thread for stereo.
- Media3 pinned to 1.8.1 for API-21 compat (their app supports API 21; ours targets higher, so current Media3 is fine).

**Relevance:** our phone stream is H.264 Annex B — on the Quest side this repo shows the cleanest pattern: `MediaCodec` (or ExoPlayer for files) → `Surface`/`SurfaceTexture` → GL external texture (`GL_TEXTURE_EXTERNAL_OES`) → blit into the quad layer's swapchain image each frame. No `glReadPixels`, no CPU round-trip.

---

## Cross-repo patterns (summary)

1. **Probe, never assume** — every repo gates on `xrEnumerateInstanceExtensionProperties` / capability structs; our `Probe extensions` lesson aligns.
2. **Two-layer frame model** — world in `XrCompositionLayerProjection`, UI/screen in `XrCompositionLayerQuad` submitted to the same `xrEndFrame`. This is the canonical way to do a wrist/fixed display.
3. **Hands via actions, not joints, for intent** — `XR_EXT_hand_interaction` profile (poke pose + pinch value) is the production pattern (Wolvic, Unity XRI); raw joints are for *rendering* (Meta samples, tutorial cuboids → FB skinned mesh).
4. **GPU-only video path** — decoder Surface → external OES texture → quad swapchain; sizing the swapchain to the decoded frame is the detail that decides sharpness.
5. **Lifecycle discipline** — all of these manage session-lifetime objects (trackers, passthrough, swapchains) explicitly with create/destroy symmetry, and every `xrBeginFrame` is paired with `xrEndFrame` — matching our own OpenXR lesson.
