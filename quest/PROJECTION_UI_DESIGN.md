# XR Wrist — Phone Screen Projection UI (v0.6.2)

## Design philosophy

"What does the human body want?" A projected phone screen in VR should feel
like a premium physical device that respects your attention: there when you
look, invisible when you don't. Every element is glanceable, high-contrast,
and plain-language (v0.6.1 readability standard).

## 1. Projection frame: accent ring + shadow + status dot

**Problem:** The video quad floated bare in space — no visual weight, no
indication it was a "device," no stream health signal.

**Solution** (`ProjectionUI::DrawFrame`):
- **Soft drop shadow** — a dark rounded rect 7% larger than the panel,
  offset 6mm behind it, 42% alpha. Gives the projection physical depth
  without expensive blur.
- **Warm-amber accent ring** — a 2.8mm rounded-rect outline hugging the
  glass edge (Soft-Tech trim). States:
  - Streaming: steady 85% amber
  - Connected but no frames yet: gentle breathing pulse (sine 3Hz)
  - Idle (no stream): faint 22% trim
- **Status dot** — 9mm circle, top-right corner of the frame:
  - Soft green: streaming ≥ 20 fps
  - Amber: degraded (< 20 fps) or connecting
  - Dim gray: idle
  - (Lost connections get a full human-readable toast, not just a dot.)

**Why:** The ring gives the product a visual identity (amber = XR Wrist).
The dot is glanceable health without demanding attention.

## 2. Screen controls overlay (auto-hiding)

**Problem:** Brightness/zoom/orientation needed the full menu — too heavy
for a quick tweak.

**Solution** (`ProjectionUI` control bar):
- **Reveal:** gaze at the display for 0.8s (shorter than the 1.5s engage
  dwell, so it appears before the panel expands).
- **Layout:** 3 buttons floating just below the display, tracking its pose:
  `Dimmer | Brighter | Lock` (Lock toggles to `Unlock`).
- **Auto-hide:** 3s after gaze leaves, with a 0.25s fade.
- **Input:** controller ray + trigger, or hand pinch — same pattern as the
  in-VR menu. While the pointer is on the bar, phone-touch is suppressed
  (same `ConsumesInput()` contract as the menu).
- **Brightness** (0.3–1.5, persisted): applied in the glass shader via a
  new `uBrightness` uniform, and to the fallback billboard quad's tint.

**Why:** Like a real watch — controls appear when you look, vanish when
you don't. No mode to enter, no menu to open.

## 3. Projection modes

| Mode | Size | Distance | Use |
|------|------|----------|-----|
| Wrist | current | wrist | glanceable (default) |
| Expanded | 0.84m wide | 0.9m, billboarded | reading, detail work |
| Theater | 1.40m wide | 2.2m, billboarded | media, video |

- **Transitions:** 0.6s smoothstep lerp from the current pose — no popping.
  The "from" state is captured on mode change; targets track the head
  gently while active.
- **Switching:** in-VR menu → `Projection: Wrist/Expanded/Theater` row,
  or voice: "theater mode" / "expanded view" / "cinema" / "wrist mode".
- **Persistence:** mode survives restarts (config).

**Why:** A wrist screen is great for glances but terrible for movies.
Theater mode answers "what does the body want" for media: lean back,
big screen, no neck strain.

## 4. Touch feedback

- The existing touch ripple is now **warm-amber** (was white) — consistent
  with the accent ring.
- New **persistent touch dot**: an 11mm amber dot at the exact touch UV
  while a finger is down (the ripple alone only fired on tap-down).
- Together: you always know where your finger lands.

## 5. Status indicators

- **Frame dot** (above) for at-a-glance health.
- **Tech panel** gains: `stream %.0f fps`, `projection <mode>`,
  `brightness <%>` — words, not codes.
- **Connection-drop watchdog:** if the stream dies unexpectedly (not via
  user Stop), a human-readable toast appears: "Connection lost / Check
  WiFi, then tap Start on the phone app." User-initiated stops still say
  "Stream stopped / Menu > Stream to restart."

## Implementation notes

- New files: `projection.h` / `projection.cpp` (~700 lines).
- Modified: `xr_core.{h,cpp}` (integration), `avatar.{h,cpp}` (brightness
  uniform, amber ripple), `video.{h,cpp}` (frame counter),
  `config.{h,cpp}` (3 new keys + fixed missing KeyName cases for the
  v0.6.0 passthrough/environment keys), `menu.{h,cpp}` (Projection row),
  `CMakeLists.txt`.
- Video fps measured from `SurfaceTextureHelper` frame counter (1s window).
- Orientation lock freezes the panel's world pose; size still follows the
  projection mode. Changing projection mode does not clear the lock.
- Brightness default 1.0, range 0.3–1.5, persisted per device.

## Verification checklist (needs human in-headset)

- [ ] Ring visible around the display in all modes; amber, not distracting
- [ ] Shadow gives depth without looking dirty
- [ ] Status dot green while streaming, gray when idle
- [ ] Gaze 0.8s reveals control bar; 3s look-away hides it
- [ ] Dimmer/Brighter visibly change video brightness; Lock freezes pose
- [ ] Theater mode: "theater mode" voice → smooth 0.6s glide to 1.4m panel
- [ ] Touch dot tracks finger; ripple is amber
- [ ] Kill phone stream → "Connection lost" toast with recovery action
- [ ] 1-hour stability run
