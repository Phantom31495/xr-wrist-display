# scrcpy Integration — Phone App v1.4

**Date:** 2026-10-06
**Source:** Genymobile/scrcpy (server source, master branch)
**Files studied:** `video/SurfaceEncoder.java`, `device/Streamer.java`,
`control/ControlMessage.java`, `control/PositionMapper.java`

## What makes scrcpy fast

scrcpy's low latency (~35-70ms glass-to-glass on USB, ~100-150ms on WiFi)
comes from a stack of deliberate choices, not one trick:

1. **Encoder runs at realtime priority** (`KEY_PRIORITY=0`) — the encoder
   thread is never starved by background work.
2. **Frames are emitted ASAP** (`KEY_LATENCY=1`) — the codec outputs a
   frame as soon as one is queued, instead of buffering for B-frame
   lookahead (H.264 baseline has no B-frames anyway, so this is pure win).
3. **Blocking output dequeue** — `dequeueOutputBuffer(..., -1)` sleeps
   until a frame is ready. No polling loop, no 10ms quantization delay.
4. **Long I-frame interval (10s)** — keyframes are 5-10x larger than
   P-frames; fewer of them means steadier bitrate and no periodic
   latency spikes. Keyframes are requested on demand instead.
5. **TCP_NODELAY on all sockets** — Nagle's algorithm is disabled so
   small packets (NAL headers, control messages) go out immediately.
6. **Surface input** — zero-copy from the compositor to the encoder.

## Techniques applied to XR Wrist

### VideoEncoder.kt (rewritten encoder config)

| Setting | Before (v1.3) | After (v1.4, scrcpy) | Why |
|---|---|---|---|
| `KEY_FRAME_RATE` | 30 (target fps) | 60 (formality) | scrcpy: the declared rate doesn't control the real rate; the encoder is variable-rate. 60 satisfies the API requirement. |
| `KEY_I_FRAME_INTERVAL` | 2s | 10s | Fewer keyframes = lower average bitrate, no periodic 200ms+ spikes when a 100KB keyframe hits the wire. |
| `KEY_REPEAT_PREVIOUS_FRAME_AFTER` | 33ms (1s/fps) | 100ms | Less redundant re-encoding of static frames. |
| `KEY_PRIORITY` | not set | 0 (realtime, API 23+) | Encoder thread gets CPU priority. **Major latency win under load.** |
| `KEY_LATENCY` | not set | 1 (API 26+) | Emit each frame immediately. **Single biggest latency reduction.** |
| `KEY_COLOR_RANGE` | not set | LIMITED (API 24+) | Correct color reproduction. |
| `max-fps-to-encoder` | not set | target fps | Private AOSP key scrcpy uses to cap the real encode rate. |
| Dequeue timeout | 10ms (polling) | -1 (blocking) | No busy-wait; frame handed off the instant it's ready. |
| Codec-config packets | forwarded as video | skipped (logged) | SPS/PPS are in the keyframe Annex B stream our decoder already handles. |
| Error handling | log and continue | consecutive-error counter, 50ms backoff, fail after 3 | scrcpy's `MAX_CONSECUTIVE_ERRORS` pattern: transient glitches recover, real failures surface fast. |
| Encode thread | plain Thread | `Looper.prepare()` | scrcpy's Meizu deadlock workaround. |

**Expected impact:** Glass-to-glass latency should drop by roughly one
frame interval (33ms at 30fps) from `KEY_LATENCY=1` alone, plus the
removal of up-to-10ms polling quantization, plus steadier frame pacing
under CPU contention from realtime priority. Bitrate should drop
~10-15% from the longer I-frame interval at the same quality.

### NetworkServer.kt (socket tuning)

- `TCP_NODELAY=true` on every accepted video and control socket
  (scrcpy does this on all its sockets). NAL length headers and small
  JSON control messages are no longer delayed up to 200ms by Nagle's
  coalescing timer.
- Send buffer raised to ≥256KB so the encoder thread never blocks on
  a slow reader; dead clients are still reaped by the existing
  dead-client detection in `sendVideoNal`.

**Expected impact:** Touch-to-photon round trip improves most on the
control path — a tap from the Quest previously could sit in the Nagle
buffer for up to 200ms before the phone even saw it.

### TouchService.kt (smooth gestures)

- Rewrote touch injection around **chained stroke slices** with
  `willContinue=true`, mirroring scrcpy's treatment of
  down→move*→up as one continuous gesture.
- Each slice is 16ms (one frame at 60fps); moves chain from the last
  injected point so the system renders smooth motion instead of a
  stutter of independent 50ms taps.
- A `move` without a preceding `down` starts a new gesture instead of
  being dropped; cancellation resets the gesture state machine.

**Expected impact:** Dragging and scrolling from the Quest feel
continuous instead of steppy. Tap latency unchanged (already minimal).

## What we deliberately did NOT take from scrcpy

- **Binary control protocol** — scrcpy's control channel is a compact
  binary format. Ours is newline-delimited JSON. JSON is working,
  human-debuggable, and our Quest client already parses it. The
  per-message overhead (~40 bytes) is negligible next to video.
  Not worth the rewrite.
- **`InputManager.injectInputEvent`** — scrcpy injects input events
  directly, which requires `INJECT_EVENTS` (ADB/root). We correctly
  use the AccessibilityService gesture API, which works on stock
  devices with user consent. Different constraint, correct choice.
- **12-byte frame headers with PTS/flags** — scrcpy's packet framing
  carries presentation timestamps and keyframe flags. Our 4-byte
  length prefix is simpler and our Quest decoder doesn't need PTS
  (it renders on arrival). Keep it simple.
- **Downsize-on-error fallback** — scrcpy retries encoding at
  progressively smaller resolutions (2560→800) when the encoder
  fails. Valuable, but our fixed 720x1280 pipeline hasn't shown
  encoder failures. Added the consecutive-error guard instead;
  full fallback is future work if we support dynamic resolution.

## Verification

- [x] Phone APK v1.4 (versionCode 5) builds clean: 777KB, zipaligned,
      signed, `apksigner verify` passes
- [x] Manifest/arsc STORED per Samsung requirements
- [x] Kotlin metadata warnings are pre-existing (kotlinc 2.1 vs D8),
      unrelated to these changes
- [ ] On-device latency measurement (needs phone + Quest streaming;
      both were intermittently offline during this task)
- [ ] 1-hour stability run with new encoder settings

## How to measure the improvement

1. Install v1.4 on the phone, start streaming.
2. In the Quest, open the Tech info panel (B button) — it shows
   `video 720x1280 ready` and frame stats.
3. Compare glass-to-photon: film the phone and the Quest display
   with a 240fps camera while tapping; count frames between tap
   visual and Quest visual. Expect ~2-4 frames improvement
   (~8-16ms at 240fps) from `KEY_LATENCY=1` + `TCP_NODELAY`.
4. Bitrate: the Godmode bitrate graph (phone) should show a lower
   floor between keyframes and smaller periodic spikes.
