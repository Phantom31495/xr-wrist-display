# XR Wrist Display — Design Ratings

**Date:** 2026-10-06
**Purpose:** Rate every design idea on (1) how natural it feels to a human user, and (2) how visually identical it is to Meta's Horizon OS UI system. Be honest: divergence from Meta is flagged as good or bad explicitly.

**Scales:**
- **User Compatibility (UC) 1–10:** 10 = feels invisible/natural, 1 = confusing or painful.
- **Meta UI Similarity (MS) 1–10:** 10 = indistinguishable from native Horizon OS, 1 = completely foreign.

---

## Summary Table

| # | Idea | UC | MS | Verdict |
|---|------|----|----|---------|
| 1 | Wrist-anchored smartwatch display | 7 | 3 | KEEP |
| 2 | Dynamic sizing (glance → expand) | 9 | 5 | KEEP |
| 3 | Direct finger touch (hand tracking) | 9 | 7 | KEEP |
| 4 | Auto wrist→fixed transition | 8 | 2 | REFINE |
| 5 | Stylized hands (vs realistic) | 8 | 4 | KEEP |
| 6 | Controller ray interaction | 6 | 10 | KEEP (fallback) |
| 7 | Fixed-position panel (Meta-style) | 8 | 9 | KEEP |
| 8 | Pinch gesture (Meta hand tracking) | 7 | 10 | KEEP |
| 9 | Voice control | 8 | 8 | REFINE |
| 10 | Godmode diagnostics overlay | 5 | 3 | KEEP (hidden) |

---

## 1. Wrist-Anchored Smartwatch Display

**Design:** Live phone video rendered on a watch-face quad mounted on the left wrist of the avatar, framed by a dark-metal bezel with a crown detail. Face normal blends head-facing (70%) with wrist-up orientation, like glancing at a real watch. Toggled to fixed mode via X+Y.

**User Compatibility: 7/10** — The watch metaphor is universally understood (look at wrist = check info), but a wrist-sized phone screen makes text and touch targets uncomfortably small for anything beyond a glance.

**Meta UI Similarity: 3/10** — Horizon OS has no wrist-anchored UI surface at all; its panels are world-locked or head-locked. This is our product's signature divergence from Meta — intentional, since Meta doesn't do wrist displays.

**Recommendation: KEEP.** This is the product identity. The low Meta-similarity is a feature, not a bug — but it must be paired with idea #2 (dynamic sizing) to fix the readability problem.

---

## 2. Dynamic Sizing (Glanceable → Expanded)

**Design:** The display has two states. *Glance mode:* a compact watch face (~8cm) showing notifications, time, and status. *Engage mode:* triggered by sustained gaze (>1.5s) or an explicit expand gesture, the panel smoothly scales up to ~40cm at comfortable reading distance. Content reflows; touch targets grow to ≥1cm.

**User Compatibility: 9/10** — Mirrors exactly how humans use real watches: glance for awareness, raise-and-focus for interaction. Eliminates the #1 complaint about idea #1 (tiny text) without abandoning the wrist metaphor.

**Meta UI Similarity: 5/10** — Meta's panels are fixed-size; Horizon OS notifications expand on selection but don't have a gaze-driven two-stage sizing model. The *visual style* of the expanded panel can match Meta (rounded dark panel), but the *behavior* is novel.

**Recommendation: KEEP.** This is the highest-leverage UX improvement available. Design the expand animation to feel physical (ease-out-back, subtle scale bounce) so it reads as "the watch opening up" rather than a UI mode switch.

---

## 3. Direct Finger Touch (Hand Tracking)

**Design:** With Meta hand tracking active, the user's real index finger becomes the cursor. Reaching out and physically touching the virtual screen registers a tap at the contact point, with a subtle ripple/highlight at the touch location and a soft audio click. No ray, no pinch — just touch.

**User Compatibility: 9/10** — The most natural interaction possible: it reuses a lifetime of touchscreen muscle memory. A child who has never used VR understands "touch the screen."

**Meta UI Similarity: 7/10** — Horizon OS supports direct poke/touch on UI elements in hand-tracking mode (e.g., pressing virtual buttons), so the *interaction* is Meta-native. Our *application* of it to a phone-mirroring surface is novel, but the feel matches Meta's direct-manipulation elements.

**Recommendation: KEEP.** Make this the primary input method whenever hand tracking is available. The tactile expectation is so strong that the visual touch feedback (ripple + click) must be immediate (<50ms) or the illusion breaks.

---

## 4. Auto Wrist→Fixed Transition

**Design:** The system monitors wrist elevation and dwell time. If the wrist stays raised in reading posture for >4 seconds, the display detaches from the wrist with a gentle 600ms eased animation and settles into a comfortable fixed position ~50cm in front of the chest. Lowering the arm and raising it again re-attaches to the wrist. No button press, no menu — posture is the input.

**User Compatibility: 8/10** — Directly solves wrist fatigue, the silent killer of wrist-anchored UI. The "it just knows" feeling is magical when the animation is smooth. Docked 2 points because auto-moving UI can startle if the trigger threshold is wrong or the animation is abrupt — this lives or dies on tuning.

**Meta UI Similarity: 2/10** — Nothing in Horizon OS behaves this way. Meta's panels are explicitly placed (grab-and-move) or fixed; there is no posture-driven auto-relocation. This is a genuine innovation, but it means users can't lean on Meta-learned expectations.

**Recommendation: REFINE.** Ship it, but make the trigger conservative (longer dwell, clear visual "about to detach" pre-animation like a subtle glow) and add a settings toggle. Consider a brief onboarding moment: the first time it triggers, show a one-line hint ("Display moved to a comfortable spot — raise your wrist to bring it back").

---

## 5. Stylized Hands (vs Realistic)

**Design:** The avatar hands use clean, slightly abstracted geometry — smooth proportions, soft skin shading with wrap-lighting, no pores/wrinkles/nails-detail. The aesthetic is "premium product render," not "photograph." Consistent across both hands, matching the watch bezel's material language.

**User Compatibility: 8/10** — Sidesteps the uncanny valley entirely. The brain files stylized hands under "VR hands" and stops scrutinizing; near-realistic hands invite constant subconscious comparison to real hands and always lose.

**Meta UI Similarity: 4/10** — Meta's own hand-tracked hand meshes aim for semi-realistic (anatomically proportioned, natural skin tones). Our stylized direction visibly diverges. This is *good* divergence: Meta's realism target is constrained by their platform needs; we can choose the aesthetic that feels best.

**Recommendation: KEEP.** Commit to the stylization fully — half-stylized (realistic shape, flat shading) is the worst of both worlds. A defined art direction ("soft-tech") across hands, watch, and UI chrome will read as intentional design, not a rendering limitation.

---

## 6. Controller Ray Interaction (Meta's Standard)

**Design:** Thin white laser ray from the controller with a small dot cursor at the hit point, exactly per Meta's interaction SDK. Trigger pull = select. Hover states highlight interactive elements. Used when hand tracking is unavailable or the user prefers controllers.

**User Compatibility: 6/10** — Serviceable and familiar to existing VR users, but "pointing a laser" is an abstraction layer between intent and action. New users find it less intuitive than direct touch (idea #3); it also keeps one hand occupied as a pointer rather than free.

**Meta UI Similarity: 10/10** — This *is* Meta's standard. Same ray thickness, same cursor, same hover/selection semantics as Horizon OS system UI.

**Recommendation: KEEP (as fallback).** Never the primary when hand tracking is available, but essential for compatibility: some users disable hand tracking, some environments break it (low light), and Meta's own UI uses it everywhere, so users arrive pre-trained.

---

## 7. Fixed-Position Panel (Meta-Style)

**Design:** When detached from the wrist, the display becomes a floating rounded-rectangle panel: dark translucent background (#121212 @ 92%), 16dp-equivalent corner radius, subtle edge highlight, soft drop shadow. Sized ~40×70cm at 50cm distance. Matches Horizon OS panel chrome so closely it could sit next to Quick Settings without visual clash.

**User Compatibility: 8/10** — Zero fatigue, large readable surface, familiar "floating window" mental model from every VR platform. Loses 2 points only because it abandons the wrist metaphor that makes the product distinctive — it's the comfortable shoe, not the exciting one.

**Meta UI Similarity: 9/10** — Deliberately modeled on Horizon OS system panels (Quick Settings, App Library cards). Rounded dark floating surfaces are Meta's core visual language; we speak it fluently here.

**Recommendation: KEEP.** This is the "resting state" of the product — where the display lives 80% of the time during real use. Its Meta-fidelity is a strength: when the panel looks native, the *content* (your phone) gets all the attention, which is correct.

---

## 8. Pinch Gesture (Meta Hand Tracking)

**Design:** Thumb-to-index pinch to select, exactly as defined in Meta's hand-tracking interaction model: a small visual indicator (ring that closes) appears between thumb and index as they approach, selection fires at the pinch threshold, with a subtle scale-pulse on the target. Works on both the wrist display and the fixed panel.

**User Compatibility: 7/10** — Natural enough once learned, and Meta has trained a large user base on it. Loses points because mid-air pinch lacks tactile confirmation — users sometimes aren't sure the pinch "took," especially at arm's length where finger separation is hard to judge visually.

**Recommendation: KEEP.** It's the hand-tracking complement to the controller ray: when the user isn't close enough for direct touch (idea #3), pinch is the next-most-natural option. Pair every pinch with immediate visual+audio confirmation to compensate for the missing tactile feedback.

**Meta UI Similarity: 10/10** — Identical to Meta's documented pinch interaction, including the proximity ring visual.

---

## 9. Voice Control

**Design:** Wake word + command grammar for display control: "Wrist display on/off," "Expand," "Move here" (with gaze or pinch targeting), "Go home" (return phone to home screen). On-device or phone-relayed speech recognition; visual confirmation via a brief mic-indicator pill near the display, Meta-style.

**User Compatibility: 8/10** — Hands-free control is ideal when hands are busy or fatigued — the exact moments wrist UI is hardest to operate. Loses points for social friction (talking to your headset in public) and recognition failures breaking flow.

**Meta UI Similarity: 8/10** — Conceptually identical to "Hey Meta" voice commands; the mic-indicator pill mirrors Meta's voice UI patterns. Docked 2 points because we must use a *different* wake word to avoid colliding with Meta's own assistant — the UX pattern matches, the trigger phrase deliberately doesn't.

**Recommendation: REFINE.** Pick a wake word that can't be confused ("Display" or "Wrist"), keep the command set tiny (≤8 commands), and always show what was heard. Voice should be a convenience layer, never the only path for any action.

---

## 10. Godmode Diagnostics Overlay

**Design:** Toggled by the B button: a head-locked monospace overlay showing FPS, frame ms (avg/worst), session state, video/phone connection status, full input state (buttons, triggers, sticks), avatar draw calls/triangles, and LAN scan + TCP probe results. Dark terminal aesthetic, single draw call, dirty-flagged text rebuilds.

**User Compatibility: 5/10** — This is a developer instrument, not a user feature. Hidden behind an undiscoverable button combo, so it never harms normal UX — but if a regular user stumbles into it, it's incomprehensible. Rated on its own terms: as a *tool*, it's excellent; as *product UI*, it's intentionally absent.

**Meta UI Similarity: 3/10** — Meta exposes nothing like this to users. The closest native analog is the developer-mode OVR Metrics overlay, which is similarly terminal-styled and similarly hidden. Our overlay actually resembles *that* more than any consumer Horizon OS surface.

**Recommendation: KEEP (hidden).** It stays behind the B button, never advertised in normal onboarding. Its value is in development and power-user debugging — exactly where it is. Do not "Meta-ify" its visuals; the terminal aesthetic correctly signals "this is a tool, not product."

---

## Cross-Cutting Recommendations

### Where we should match Meta (familiarity wins)
- **Fixed panel chrome** (#7): look native so content shines.
- **Ray + pinch mechanics** (#6, #8): users arrive pre-trained; don't reinvent.
- **Voice indicator patterns** (#9): borrow the pill, change the wake word.

### Where we should diverge (our product wins)
- **Wrist anchoring** (#1): nobody else does this; it's the identity.
- **Posture-driven auto-transition** (#4): genuine innovation, no Meta equivalent.
- **Stylized hands** (#5): better than Meta's realism target for our use case.
- **Dynamic sizing** (#2): extends the wrist metaphor instead of abandoning it.

### Priority build order (human-impact per effort)
1. **#2 Dynamic sizing** — fixes the biggest UX flaw (readability) with moderate effort.
2. **#3 Direct finger touch** — highest naturalness payoff; hand-tracking APIs already integrated.
3. **#4 Auto transition** — needs tuning time; start conservative.
4. **#9 Voice** — small command set, big convenience win.
5. **#5 Stylization pass** — art-direction commitment across existing geometry.
