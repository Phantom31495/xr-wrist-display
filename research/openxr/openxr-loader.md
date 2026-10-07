# OpenXR Loader — Design and Integration Guide — Research Notes

**Sources:**
- https://www.khronos.org/registry/OpenXR/specs/1.0/loader.html (Loader Design Document, Khronos)

**Date accessed:** 2026-10-06
**Purpose:** Confirm our loader-init approach on Quest; document API-layer usage for debug builds.

> Summarized in my own words from the Khronos design doc; function/structure names are exact.

---

## 1. Layered architecture

Application → **Loader** → **API Layers** → **Runtime**. The loader detects, exposes, and loads runtimes and API layers, then dispatches every OpenXR command through the chain. Design goals: support multiple runtimes, support API layers, minimize memory/perf impact.

## 2. Runtimes

Each runtime controls a complete XR system. Multiple runtimes may be installed; **one is active at a time**. The loader discovers the active runtime and binds it to the `XrInstance` at `xrCreateInstance`. Only one outstanding `XrInstance` is allowed per loader.

## 3. Two ways to interface with OpenXR functions

1. **Direct linking** — link against the loader's exported core commands (`xrCreateInstance`, `xrBeginFrame`, …). Calls go through small *trampoline* functions into the per-instance dispatch table.
2. **Indirect linking (application-managed dispatch table)** — `dlsym`/`GetProcAddress` the loader for `xrGetInstanceProcAddr`, then query every other function pointer through it. Benefits: graceful failure when the loader is missing or older than expected, and one fewer trampoline hop per call.

Extension functions are **always** obtained via `xrGetInstanceProcAddr` — never assumed present.

## 4. Call chains

`xrCreateInstance` builds the chain: trampoline → per-instance dispatch table → enabled API layers (in order) → *terminator* (loader-side processing) → runtime. `xrCreateInstance`, `xrDestroyInstance`, and `xrGetInstanceProcAddr` use a "special" chain where the loader does work before/after the layers.

## 5. API layers

Optional libraries that intercept, evaluate, modify, or insert OpenXR commands between app and runtime. Enabled at `xrCreateInstance`. Cannot add new *core* commands, but may expose extension commands. Canonical uses: **validation** (dev builds), API tracing/debugging, filtering. Guidance: enable validation layers during development, **strip them for release** — the overhead disappears entirely when unloaded.

## 6. Loader library names

Windows: `openxr-loader.lib/.dll`. Linux: `libopenxr_loader.so.<major>`. (On Android/Quest the "loader" role is played by the runtime broker — which is why Meta's documented Android flow uses `xrInitializeLoaderKHR` before `xrCreateInstance`.)

## 7. Relevance to XR Wrist (already applied, now documented)

- Our `xrInitializeLoaderKHR`-via-`xrGetInstanceProcAddr` init matches the documented Android pattern — this doc is the provenance for that fix.
- Extension functions (`xrCreateHandTrackerEXT`, etc.) via `xrGetInstanceProcAddr` with null-checks = the spec-blessed probing pattern.
- Future: wire the Khronos **validation API layer** into debug builds only, to catch spec violations (like our historic xrEndFrame leak) at development time instead of in the headset.
