# Horizon OS Design Guidelines — Research Notes

**Source URLs:**
- Components: https://developers.meta.com/horizon/design/components/
- Panels: https://developers.meta.com/horizon/design/panels/
- Typography: https://developers.meta.com/horizon/design/styles_typography/
- Fonts and Icons: https://developers.meta.com/horizon/design/fonts-icons/
- Android design requirements: https://developers.meta.com/horizon/documentation/android-apps/design-requirements/
- (Supplement) Meta Wearables semantic color tokens: https://wearables.developer.meta.com/docs/develop/webapps/design/foundations/theme/colors/
- (Supplement) Meta Wearables corner radius scale: https://wearables.developer.meta.com/docs/develop/webapps/design/foundations/theme/corner-radius/

**Date accessed:** 2026-10-06

> Note: Meta publishes Horizon OS design guidance as named semantic roles/styles rather than a single public hex palette. The exact hex list lives in the Meta Horizon OS UI Set (Figma / Interaction SDK for Unity). The concrete values below are everything the official docs state numerically; for exact panel hexes, pull the UI Set Figma kit.

## Actionable design tokens

### Color
- **Contrast:** minimum **4.5:1** for regular text (WCAG AA), **3:1** for headlines and non-text elements (button backgrounds). (REQUIREMENT)
- **No pure white/black:** `#FFFFFF` and `#000000` create harsh contrast in mixed reality — avoid both.
- **Light backgrounds cap:** keep light backgrounds at **`#DADADA` or darker** to reduce eye strain.
- **Test on device:** Meta VR color space makes colors look more saturated than on phone/desktop — always verify on the headset.
- **Semantic usage:** use green for positive, red for negative, orange for warnings; never use color alone to convey meaning — pair with icon/shape/label.
- **Default text palette:** grays; "Primary Text" for main content, "Secondary Text" for supplementary content.
- **Supplement (Wearables UI toolkit token system — additive-light additive display):** semantic tokens like `--uit-color-text-primary`, `--uit-color-background-window/surface/elevation1..3`, `--uit-color-interactive-stroke-*`, `--uit-color-persistent-positive/warning/negative/info`, plus 15 named accents (red, orange, yellow, olive, green, shamrock, teal, cyan, blue, indigo, purple, violet, pink, rose, gray). Secondary foregrounds over materials need a `lighten` blend mode to stay legible. Useful as a token-naming model; the Quest UI Set equivalent lives in the Figma kit.

### Typography
- **Typeface:** **Inter** is the available/developer-recommended typeface on Meta VR devices. **Optimistic Display** is used by Meta on-device and in the companion app.
- **Min sizes (REQUIREMENT):** minimum **14px** for legibility; **18px+** for comfortable reading.
- **UI Set type scale (size/line-height, dp):**
  - Headline 1: 32/36 — title of the surface/content
  - Headline 2: 24/28 — title of a smaller surface/content
  - Headline 3: 20/24 — section header, dialog/nav title
  - Body 1: 14/20 — primary body copy
  - Body 2: 11/16 — supplementary info, validation messages
- **Weights:** Strong (Black/Bold/Medium) for titles/labels; Regular for body/captions. Avoid overusing Strong. **No italics** (render poorly in immersive).
- Sans-serif with high x-height and large counters; don't use Light/Thin without increasing size.

### Spacing & hit targets
- **Minimum hit target: 48dp × 48dp** (REQUIREMENT); **60dp × 60dp** for primary controls.
- **Hit slop:** add invisible hit slop when a visual asset is smaller than the minimum (e.g. 12dp all around); leave room for it — overlapping hit areas are a defect.
- **Spacing between interactive elements:** minimum **8–12dp**; extra padding between groups; margin around window edges.

### Panels
- **Default panel: 1024dp × 640dp. Minimum panel: 384dp × 500dp.**
- Panels display content only — no UI chrome inside; content fills edge to edge.
- Title/close/minimize/theater actions live in a **pill-shaped Control Bar** that appears on hover just below the panel's bottom-center edge.
- Panel configs: **single** (grab edges/Control Bar to move, corners to resize), **hinged** (2–3 panels joined at a hinge, moved via a small white manipulation handle), **theater** (maximizes, darkens the surrounding environment for visual priority; toggled via expand button in the Control Bar).
- For XR Wrist Display: keep the video panel in single-panel form, world/fixed-anchored, sized near the default aspect (~1.6:1), never below the 384×500 minimum.

### Icons
- **24dp** default system icon; **12/16dp** for status icons; 48dp only sparingly (spot illustrations).
- Grid: build on a **192×192 px** source grid for a **24×24 dp** display grid; live content inside a **20×20 area with 2dp padding**.
- **Filled icons** in fully immersive experiences; outlined for mobile/web; never mix in one environment.
- Angles in 45° increments; keyline shapes: square, horizontal/vertical rectangles, circle.
- Status indication via **30% opacity** on the relevant icon part (not the whole icon).

### Corners (supplement — Wearables semantic scale)
Nine semantic steps: xxsmall → xsmall → small → medium (ordinary content surfaces) → large (panels/cards) → xlarge → 2xlarge → 3xlarge → full (pills/circles). Pick by surface weight; exact pixel values resolve from the theme.

## Other requirements relevant to the build
- **No system back button** across input modalities — provide in-app back navigation.
- **Hover/focus states are required** on all targets and inputs.
- Focus indicators: ≥2px thick, high contrast, visually distinct from hover/selected.

## Summary for v0.4.0
Use Inter; Body 1 ≥14dp (prefer 18dp); hit targets ≥48dp (60dp primary) with 12dp slop; panel sizing defaults 1024×640 dp, min 384×500; dark translucent backgrounds over bright passthrough, never pure #FFF/#000, light surfaces ≤ #DADADA; semantic colors only for status (green/red/orange + icon); filled 24dp icons; required hover/focus/pressed states.
