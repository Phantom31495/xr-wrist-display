# Meta Quest VRC Guidelines + Submission — Research Notes

**Sources:**
- https://developers.meta.com/vr/resources/publish-quest-req/ (VRC checklist, updated Aug 19, 2026)
- https://developers.meta.com/horizon/resources/publish-mobile-manifest/ (release manifest spec, updated Aug 31, 2026)
- https://developers.meta.com/horizon/resources/publish-upload-overview/ (upload requirements, updated Mar 26, 2026)

**Date accessed:** 2026-10-06
**Purpose:** Store-submission readiness for the XR Wrist Quest app; deltas vs. our existing VRC_AUDIT.md.

> Summarized in my own words from official docs; VRC IDs and requirement phrasing are exact. ✓ = required, + = recommended.

---

## 1. How to read the VRCs

Meta's framing: the checklist is a means, not the end — "your focus should be on the user experience of your app and not just to check off a box." The **test plans** are the exact criteria used in technical review (note: Meta flags the published test plans as slightly out of date — treat the VRC page itself as source of truth). Commonly-failed VRCs have dedicated guidance ("Passing the Most Commonly Failed VRCs").

## 2. Packaging (all ✓)

- **VRC.Quest.Packaging.1** — manifest must conform to the release build manifest spec.
- **VRC.Quest.Packaging.2** — sign with APK Signature Scheme v2.
- **VRC.Quest.Packaging.3** — no Android features unsupported on Quest.
- **VRC.Quest.Packaging.4** — supported SDK and engine version.
- **VRC.Quest.Packaging.5** — APK < 1 GB; OBB < 4 GB.
- **VRC.Quest.Packaging.6** — 64-bit binaries only.

Release manifest spec (Quest family): recommended minSdk 32, targetSdk 34, compileSdk 34. **Apps created since March 1, 2026 must set targetSdkVersion to 34.** (We target 34 ✓.)

## 3. Performance

- **VRC.Quest.Performance.1** ✓ — run at the specified refresh rates (72 Hz Quest 2 / 90 Hz Quest 3 baseline).
- **VRC.Quest.Performance.3** ✓ — head-tracked graphics in-headset within **4 seconds of launch**, or a loading indicator in VR. (This is the exact VRC our xrEndFrame render fix addressed.)
- **VRC.Quest.Performance.4** + — ≥85% render scaling for most of the experience.

## 4. Functional (✓ unless marked)

- **Functional.1** — install and run without crashes, freezes, extended unresponsive states.
- **Functional.2** — pause when Horizon OS requests pause.
- **Functional.3** — never leave the user stuck; reviewers play through ~45 minutes.
- **Functional.4** — don't lose user data.
- **Functional.5** — respond to positional *and* orientation tracking.
- **Functional.9** — local-tracking-space apps must offer forward-orientation reset.
- **Functional.10** + — head-locked menus are uncomfortable; avoid. (Note: our B-button diagnostics overlay is head-locked — acceptable as a hidden dev tool, must never be product UI.)
- **Functional.12** — full functionality for multiple entitled users on one headset.
- **Functional.14** — passthrough-launch apps must show passthrough loading screens.

## 5. Input

- **VRC.Quest.Input.4** ✓ — **focus-aware**: continue rendering when focus is lost, hide hands/controllers, ignore all input. (Required — verify our session-focus handling.)
- **Input.5** + — hand-tracking apps: hands rendered correctly positioned/oriented and animating properly.
- **Input.7** ✓ — respect controller↔hand input switching.
- **Input.8** ✓ — system gesture reserved; must not trigger in-app actions.
- **Input.1** + — menus on the menu button (gamepad or left Touch).
- **Input.2** + — grip (not trigger) for picking up objects.
- **Input.3** + — virtual hands/controllers align with real counterparts.

## 6. Security, assets, privacy, streaming

- **Security.2** ✓ — minimum permissions only; unsupported permissions rejected.
- **Security.1** + — platform entitlement check within 10 s of launch.
- **Assets** — transparent-background logo ✓; representative screenshots ✓; cover art rules.
- **Privacy.1–5** ✓ — privacy policy URL; what is collected, how it's used, how to request deletion; data-protection checks. (Our LAN-only plaintext networking goes in this disclosure.)
- **Streaming.1** + — graceful handling of connectivity issues. **Streaming.2** ✓ — stream only from a local PC the customer has physical access to unless Meta approves. (Our phone→Quest LAN stream is local-device streaming — compliant posture, disclose it.)
- **Accessibility.9** + — sitting/standing apps should offer fixed-position interaction. (Direct justification for our fixed mode.)

## 7. Submission mechanics

Need: Meta account + dev team, app page in the Developer Dashboard. The upload validator rejects bad packaging automatically. **Locked submissions cannot be updated after approval** — submit only when confident. Size caps: APK 1 GB, expansion 4 GB.

## 8. Deltas vs. our VRC_AUDIT.md (2026-10-06)

Already covered there: signature scheme, manifest conformance, 64-bit, permissions. **New action items from this read:** (a) verify focus-loss behavior per Input.4; (b) add forward-orientation reset (Functional.9); (c) keep diagnostics overlay out of product UI (Functional.10); (d) draft the privacy-policy disclosure for LAN plaintext streaming now, not at submission time.
