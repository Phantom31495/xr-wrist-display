# XR_FB_display_refresh_rate — Research Notes

**Source:** https://registry.khronos.org/OpenXR/specs/1.0/man/html/XR_FB_display_refresh_rate.html (extension page)
**Researched:** 2026-10-06
**Man pages:** xrEnumerateDisplayRefreshRatesFB, xrGetDisplayRefreshRateFB, xrRequestDisplayRefreshRateFB (registry.khronos.org/OpenXR/specs/1.1/man/html/…)
**Extension:** registered #102, revision 1. Requires OpenXR 1.0+. Supported by Quest runtimes.

Lets an app discover the session's supported refresh rates, read the current one, request a preferred one, and receive events when the system changes it.

## Actionable call sequence

1. **Enable at instance creation:** `"XR_FB_display_refresh_rate"` in `enabledExtensionNames`. (Probe first via `xrEnumerateInstanceExtensionProperties`; never assume.)
2. **Enumerate supported rates** (session must exist):
   ```cpp
   uint32_t count = 0;
   xrEnumerateDisplayRefreshRatesFB(session, 0, &count, nullptr);
   std::vector<float> rates(count);
   xrEnumerateDisplayRefreshRatesFB(session, count, &count, rates.data());
   ```
   - Rates come sorted **lowest → highest**.
   - The runtime must return **identical contents for the lifetime of the session**.
   - Quest 3 typically reports `{72.0f, 90.0f, 120.0f}`.
3. **Read the current rate:**
   ```cpp
   float current = 0.0f;
   xrGetDisplayRefreshRateFB(session, &current);
   ```
4. **Request a preferred rate:**
   ```cpp
   xrRequestDisplayRefreshRateFB(session, 90.0f);
   ```
   - The requested value **must** be `0.0f` (no preference) or one of the enumerated rates, otherwise the runtime returns `XR_ERROR_DISPLAY_REFRESH_RATE_UNSUPPORTED_FB`.
   - This is **only a request** — the system is not guaranteed to switch.
5. **Handle change events** in the event poll loop:
   ```cpp
   XrEventDataDisplayRefreshRateChangedFB ev = { XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB };
   // after xrPollEvent reads it:
   // ev.fromDisplayRefreshRate -> ev.toDisplayRefreshRate
   ```
   Use this to re-sync frame pacing / swapchain timing when the system changes the rate out from under you.

## Struct / enum summary

- `XrEventDataDisplayRefreshRateChangedFB { type, next, float fromDisplayRefreshRate, float toDisplayRefreshRate }`
- New error code: `XR_ERROR_DISPLAY_REFRESH_RATE_UNSUPPORTED_FB`
- No new structs for the enumerate/request path — plain `float` arrays.

## Project-relevant takeaways (XR Wrist Display)

- After session start, enumerate and request a rate appropriate to load (72 Hz saves battery/heat for a streaming app; 120 Hz lowers motion-to-photon latency but costs GPU headroom). Default could be: start at 72 Hz; if CPU/GPU headroom allows, request 90 Hz.
- Always implement the `XrEventDataDisplayRefreshRateChangedFB` handler — the OS can change the rate (e.g., power saving), and the frame pacing math must follow it.
- Pair with `XR_EXT_performance_settings` (if needed later) rather than fighting the compositor.
