# XR Wrist Display

Stream your phone's live screen to a Meta Quest 3 as a wrist-anchored smartwatch with touch control.

## Components

- **Phone App** (`phone/`) — Kotlin, Android. MediaProjection screen capture, H.264 encoding, TCP streaming.
- **Quest App** (`quest/`) — C++ OpenXR, native. Wrist-anchored video panel, hand tracking, touch input.

## Protocol

- UDP 8899: Discovery
- TCP 8900: Video (H.264 Annex B)
- TCP 8901: Control (JSON)

## Status

- Quest v0.5.0: World-locked panel, dynamic sizing, hand tracking, voice control
- Phone v1.2: MediaProjection streaming, dev console

See `XRWIRST_BUILD_PROMPT.md` for the full build prompt.
