# XR_FB_passthrough — Research Notes

**Source:** Meta "Implement Passthrough" (native Android) — https://developers.meta.com/horizon/documentation/native/android/mobile-passthrough/ (crawled 2026-10-06)
**Researched:** 2026-10-06
**Spec:** OpenXR Specification 1.1.58, §12.109 (XR_FB_passthrough, revision 5).
**Man page:** xrCreatePassthroughFB (registry.khronos.org/OpenXR/specs/1.0/man/html/xrCreatePassthroughFB.html)

The app never sees camera pixels: passthrough is rendered by a dedicated system service into a special layer the XR compositor swaps in. The app creates the feature + layers, starts/pauses them, and orders them in the `xrEndFrame` layer list.

## Actionable call sequence

1. **Enable at instance creation:** `"XR_FB_passthrough"` in `enabledExtensionNames`. Probe support — on Quest, the runtime must also advertise the capability:
   ```cpp
   XrSystemPassthroughPropertiesFB ptProps = { XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES_FB };
   systemProps.next = &ptProps;  // chained into XrSystemProperties for xrGetSystemProperties
   // ptProps.supportsPassthrough (bit) / capability flags: PASSTHROUGH / COLOR / LAYER_DEPTH
   ```
   Flags: `XR_PASSTHROUGH_CAPABILITY_BIT_FB`, `XR_PASSTHROUGH_CAPABILITY_COLOR_BIT_FB`, `XR_PASSTHROUGH_CAPABILITY_LAYER_DEPTH_BIT_FB`.
2. **Create the passthrough feature** (one per session; creating a second returns `XR_ERROR_FEATURE_ALREADY_CREATED_PASSTHROUGH_FB`):
   ```cpp
   XrPassthroughCreateInfoFB ci = { XR_TYPE_PASSTHROUGH_CREATE_INFO_FB };
   ci.flags = 0;
   xrCreatePassthroughFB(session, &ci, &passthrough);   // errors: XR_ERROR_NOT_PERMITTED_PASSTHROUGH_FB, XR_ERROR_UNKNOWN_PASSTHROUGH_FB
   xrPassthroughStartFB(passthrough);   // or xrPassthroughPauseFB()
   ```
   Load all of these via `xrGetInstanceProcAddr` like the hand-tracking pointers.
3. **Create a passthrough layer** (max 3 at a time):
   ```cpp
   XrPassthroughLayerCreateInfoFB lci = { XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB };
   lci.passthrough = passthrough;
   lci.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;  // standard full reconstruction
   lci.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;     // omit to create paused, then xrPassthroughLayerResumeFB(layer)
   xrCreatePassthroughLayerFB(session, &lci, &passthroughLayer);
   ```
   Other purposes: `XR_PASSTHROUGH_LAYER_PURPOSE_TRACKED_KEYBOARD_HANDS_FB`, `XR_PASSTHROUGH_LAYER_PURPOSE_PROJECTED_FB` (surface-projected).
4. **Submit the layer every frame** in `xrEndFrame`'s layer array — position in the array is the compositing order:
   ```cpp
   XrCompositionLayerPassthroughFB ptLayer = { XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB };
   ptLayer.flags = 0;
   ptLayer.space = XR_NULL_HANDLE;     // optional; may be left null
   ptLayer.layerHandle = passthroughLayer;
   // layers[0] = &ptLayer (underlay → virtual content draws over reality)
   // or place AFTER the projection layer for passthrough-over-VR
   ```
5. **Style** (optional): `xrPassthroughLayerSetStyleFB(layer, &XrPassthroughStyleFB{textureOpacityFactor, edgeColor})` for opacity/color/edge effects; mono color-map structs (`XrPassthroughColorMapMonoToRgbaFB`) for posterize/grayscale looks.
6. **Events:** watch for `XrEventDataPassthroughStateChangedFB` — states include `XR_PASSTHROUGH_STATE_CHANGED_REINIT_REQUIRED_ERROR_BIT_FB` (must destroy/recreate) and `RESTORED_ERROR_BIT_FB` (recovered). On Meta runtimes also `XR_META_passthrough_layer_resumed_event` — useful to hide the black flicker: only reveal the MR scene once the layer is fully initialized (a few hundred ms after start), e.g. behind a fade quad.
7. **Teardown:** `xrPassthroughLayerPauseFB` → `xrDestroyPassthroughLayerFB` → `xrPassthroughStopFB` → `xrDestroyPassthroughFB`.

## Notes / gotchas

- Quest manifest: the app needs the `com.oculus.feature.PASSTHROUGH` feature declaration (our compliance rebuild already handles this; do not request unconditionally — probe).
- `xrEndFrame` must still be called with 0 layers on frames where locate fails; a passthrough layer counts as a layer — keep submitting it every frame while the feature is running.
- Enabling passthrough is asynchronous (camera spin-up takes a few hundred ms) — design transitions to tolerate it.

## Project-relevant takeaways (XR Wrist Display)

- For a wrist-mounted display, the intended composition is: **passthrough as underlay (layer 0)** + **projection layer with alpha** (so the room shows through around the wrist quad) — same pattern as the `ue-openxr-passthrough` approach, or simply make the projection layer fully opaque if the app is VR-only. The compliance decision "passthrough optional, probe not assume" stands.
- The wrist quad itself is rendered in the projection layer (or as a separate `XrCompositionLayerQuad` — see codebase notes); passthrough stays a pure background layer.
