# MediaCodec Advanced Usage + Performance Tuning (Screen-Capture Streaming) — Research Notes

**Sources:**
- http://developer.android.com/reference/android/media/MediaCodec (API reference)
- Project context: phone app captures via MediaProjection → H.264 encode → TCP 8900 to Quest (720x1280@30fps).

**Date accessed:** 2026-10-06
**Purpose:** Encoder-side performance tuning for the phone streaming pipeline; complements mediaprojection.md (setup flow).

> Summarized in my own words from official docs; class/method/key names are exact.

---

## 1. Surface input, not ByteBuffers

MediaCodec handles three data kinds: compressed data, raw audio, raw video. All three *can* go through ByteBuffers, but for raw video you should use a `Surface`: it uses native video buffers without mapping or copying them into ByteBuffers — "much more efficient." For encoders this means `COLOR_FormatSurface` as the color format and feeding the encoder from `createInputSurface()`.

## 2. Codec lifecycle (states)

Uninitialized → `configure()` → Configured → `start()` → Executing → `stop()` → Uninitialized → `release()` → Released. Executing has sub-states Flushed → Running → End-of-Stream. Errors move the codec to Error state; `reset()` returns it to Uninitialized from any state. Note: `flush()` back to Flushed is decoder-only; behavior is undefined for encoders.

## 3. VBR minimum quality floor (API 31+)

Android enforces a minimum quality floor on video encoders in **VBR mode** for resolutions above 320x240 through 1920x1080, targeting VMAF 70 ("fair"/"good"). Practical effect: low configured bitrates get silently raised; complex high-motion content gets extra bitrate. **The floor does not apply to CBR encodings**, nor outside the 320x240–1920x1080 range. Our 720x1280 VBR stream sits inside the floor's range — expect actual bitrate above the configured value on complex content; use CBR if the bandwidth budget must be hard.

## 4. Low-latency tuning levers (exact keys)

- `MediaFormat.KEY_BITRATE_MODE` → `BITRATE_MODE_CBR` / `BITRATE_MODE_VBR` / `BITRATE_MODE_CQ`: CBR for predictable LAN bandwidth, VBR for quality-per-bit.
- `MediaFormat.KEY_I_FRAME_INTERVAL`: seconds between sync frames. Shorter interval = faster recovery after packet loss, at higher bitrate cost. For lossy Wi-Fi streaming, 1–2 s is the usual trade.
- `MediaFormat.KEY_FRAME_RATE`, `KEY_BIT_RATE`, `KEY_COLOR_FORMAT` (= `COLOR_FormatSurface`).
- `MediaFormat.KEY_LOW_LATENCY` (API 30+): reduces encoder delay — relevant for interactive streaming.
- `MediaFormat.KEY_PRIORITY` = `0` (realtime) (API 23+).
- Codec selection: prefer hardware encoders via `MediaCodecList.findEncoderForFormat` / `createEncoderByType("video/avc")`; query `getCodecInfo().getCapabilitiesForType()` for supported profiles.

## 5. Output handling for streaming

Drain with `dequeueOutputBuffer` in a loop. Frames flagged `BUFFER_FLAG_CODEC_CONFIG` carry SPS/PPS — **prepend them to every IDR frame** before sending over TCP so the Quest decoder can join mid-stream. Each output buffer is one encoded frame (frame boundaries guaranteed unless `BUFFER_FLAG_PARTIAL_FRAME`).

## 6. Async vs sync

`setCallback()` for async operation is the recommended path; handle `onError` by `reset()`-and-reconfigure or full recreate. Never block the MediaProjection callback thread on encoder I/O — drain on a dedicated thread.
