# XR_KHR_composition_layer_depth — Research Notes

**Sources:**
- https://registry.khronos.org/OpenXR/specs/1.0/man/html/XrCompositionLayerDepthInfoKHR.html (spec man page)
- OpenXR spec § composition layers (via registry)

**Date accessed:** 2026-10-06
**Purpose:** Depth-layer submission for correct occlusion (avatar hands vs. passthrough/scene); future enhancement candidate.

> Summarized in my own words from the Khronos spec; struct/field names are exact.

---

## 1. What it does

Lets the app submit a **depth buffer alongside the color layer** so the compositor can resolve occlusion per-pixel (e.g., virtual hands correctly occluded by real-world geometry in passthrough, or virtual objects occluding each other across layers).

## 2. Struct (exact)

```c
// Provided by XR_KHR_composition_layer_depth
typedef struct XrCompositionLayerDepthInfoKHR {
    XrStructureType     type;       // XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR
    const void*         next;
    XrSwapchainSubImage subImage;   // the depth image + rect + array index
    float               minDepth;   // window-space depth range, like glDepthRange
    float               maxDepth;   // (requirement: maxDepth >= minDepth)
    float               nearZ;      // positive distance in meters for minDepth
    float               farZ;       // positive distance in meters for maxDepth
} XrCompositionLayerDepthInfoKHR;
```

Chain it into `XrCompositionLayerProjectionView::next` at `xrEndFrame` time.

## 3. Depth mapping notes (from spec)

- `minDepth`/`maxDepth` are akin to `glDepthRange` — the mapping from normalized device coordinates into window space.
- Reversed depth (closer = larger window depth) is expressed with `nearZ > farZ`.
- `nearZ`/`farZ` are positive distances in **meters**; both may be infinite; they must not be equal.
- The spec defines the homogeneous transform from view-space z to window-space depth explicitly (matrix in terms of min/maxDepth, near/far).

## 4. Negotiation caveats (practical, from community + spec)

- The runtime is **not required** to advertise a depth-capable swapchain format — `xrEnumerateSwapchainFormats` may return none. You must enumerate and pick (e.g., `GL_DEPTH_COMPONENT24` / `GL_DEPTH_COMPONENT32F` on GLES), and **fall back to color-only submission for any frame where negotiation fails**. Never let a depth experiment break the proven color path.
- Probe the extension string like any other: never assume.

## 5. Relevance to XR Wrist

Our quad layers are currently color-only. Depth submission is the correct long-term fix for avatar-hand occlusion against passthrough background and for the watch face vs. hand self-occlusion. Not required for v0.5.0 — file under future enhancement, with the per-frame color-only fallback as the safety pattern.
