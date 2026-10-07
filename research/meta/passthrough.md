# Passthrough Design Best Practices — Research Notes

**Source URLs:**
- Key considerations (MR design guideline): https://developers.meta.com/horizon/design/mr-design-guideline/
- Passthrough overview (essentials): https://developers.meta.com/vr/essentials/horizon-os-passthrough/
- Passthrough API overview (Unreal): https://developers.meta.com/horizon/documentation/unreal/unreal-passthrough-overview/

**Date accessed:** 2026-10-06

## Actionable guidance

### Placement & anchoring (from MR design guideline, Updated Oct 7, 2025)
- **Default object placement: ~1 meter from the user, slightly below their line of sight.** Avoid objects suddenly appearing too close or too large at launch — use fade-in for large objects and visual/audio cues when placing on real surfaces (tables, walls).
- **World-locked over head-locked:** "Avoid locking HUD style content to the user's head movements" — head-locked HUDs tire users quickly and reduce usability. Anchor information to space, or use **smoothed follow** (loosely follow the user with smoothing animation) instead.
- **Billboarding for readability:** make text labels and key UI always face the user; use axis constraints to keep it natural.
- **Angular scaling:** for info that must stay readable at any distance (dialogs, wayfinding, labels), scale with viewing angle so apparent size is constant; clamp with min/max size limits; animate size changes.
- When a modal/overlay appears over virtual content, **dim or hide the virtual content** so depth is unambiguous.
- For the XR Wrist Display: the wrist panel should be world/fixed-anchored (toggleable), never head-locked. Use smooth-follow only as a transitional assist, then re-lock.

### Occlusion & blending
- Use **environment depth** so real objects occlude virtual ones — without it, virtual content floats in front of everything and reads as wrong.
- Blend modes: **alpha blend** for opaque virtual objects (standard compositing); **additive blend** for holographic-style content (adds light over the passthrough feed).
- Styling: passthrough layers support edge highlighting, color adjustments, contrast/posterize effects — useful to separate the video panel from a busy real background.
- Lighting consistency: virtual objects look believable when lit to match the room; consider ambient light estimation.

### Visual contrast on variable backgrounds
- **Readability is the hard problem:** "Dark text on a transparent panel may be invisible against a dark wall." UI must survive arbitrary real-world backgrounds.
- Consequence for panels over passthrough: prefer **dark translucent scrim/backing** behind text and controls; avoid transparent panels for text-heavy content; test against bright and dark rooms.
- Don't require users to read fine text through passthrough or make precise real-world movements based on the passthrough view (comfort limitation).

### Safety & spatial rules
- **Never teleport the user** in a passthrough/MR experience — it breaks their sense of real location. Never place virtual content behind walls/physical objects users might walk into.
- Boundary system: stationary (seated/standing) vs roomscale (traced play area); approaching the boundary edge auto-reveals passthrough. Apps can query boundary dimensions to adapt layout.
- Transition between immersive and passthrough modes with **fade animations** to avoid disorientation.
- During loading: showing passthrough on loading screens keeps transitions seamless.
- Privacy: default passthrough needs no special permissions (app never sees raw camera frames; the OS composites the feed). Raw camera access requires explicit permissions and policy compliance — and user expectations are "view-only," so communicate any processing beyond that.

### Feedback & input in MR
- No tactile feedback exists — **visual cues for hover and pressed states are crucial**; pair press with compressing movement + highlight + audio feedback.
- Support both direct (touch) and indirect (hand ray) interactions; avoid forcing users to bend down; provide a way to summon/recenter content.
- Content placement should keep important info **within the field of view** without forcing head turns; rest-state interaction should allow the user to stay comfortable.

### Interaction distances recap (passthrough contexts)
- Touch-intended UI: 42–46cm; ray-intended UI: 0.8–3m; avoid 0.5–0.8m dead zone; frequent controls in lower half of panel.

## Summary for v0.4.0
Wrist overlay defaults to a **world/fixed-anchored** quad at ~0.8–1.5m, slightly below eye line, billboarded — never head-locked. Video panel gets a **dark translucent scrim** (light surfaces capped at #DADADA; no pure #FFF/#000) so the phone stream stays readable against arbitrary rooms. Use fade transitions when toggling wrist↔fixed or showing/hiding; keep controls in the lower half of the panel; mandatory hover/press states + audio confirmations; keep default passthrough (no raw camera access) for privacy.
