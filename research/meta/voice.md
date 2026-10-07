# Voice SDK / Voice Interaction — Research Notes

**Source URLs:**
- Voice (design overview): https://developers.meta.com/horizon/resources/voice-sdk-design-overview
- Voice Best Practices: https://developers.meta.com/horizon/design/voice-best-practices/
- Voice SDK Overview (Unreal): https://developers.meta.com/horizon/documentation/unreal/vsdk-overview/
- Audio (includes system voice commands + mic permission guidance): https://developers.meta.com/horizon/essentials/horizon-os-audio/

**Date accessed:** 2026-10-06

## What exists
- **Voice SDK** (Unity + Unreal; production for Quest) powered by **Wit.ai** NLU (free, no ML expertise required): voice commands, dictation (real-time speech-to-text), text-to-speech, live understanding (real-time NLU), custom wake/activation methods. High-quality English plus early preview of 12 other Quest device languages.
- **System voice commands** exist at the OS level (e.g., open Universal Menu); in-app voice is separate.
- **Native audio path:** microphone access requires **`android.permission.RECORD_AUDIO`**; users must be clearly informed when recording is active via a visible indicator.

## Design principles (actionable)
1. **Transparent activation:** the user must always know when the mic is listening — use earcons (short audio cues) and conversational/visual cues on activation.
2. **Visible mic indicator:** a persistent, legible recording indicator while capturing audio (Meta uses a recording status role; treat it like a system-level persistent indicator, e.g. red/recording dot + state label).
3. **Privacy controls:** opt-in for voice experiences; explain how audio data is used and protected; provide consent revocation and data deletion.
4. **Wake word:** Meta's documented system example is **"Hey Meta"** (the current documented wake phrase in the voice design glossary). Custom in-app wake/activation methods are supported by the Voice SDK, but keep a visible activation cue.
5. **Cognitive load:** limit what the user must remember; use breadcrumbing ("You're in X. Say 'go back' to return"); announce menu options before expecting commands; support "What's next? / Go back" navigation primitives.
6. **Confirmations:** confirm understanding after a command executes (brief spoken or visual confirmation); disambiguate with follow-up options instead of guessing.
7. **Error handling & repair:** acknowledge failure, explain it, offer alternatives/retry path — prioritize user understanding and resolution over silent retries.
8. **Contextual awareness:** adapt to visual + audio-only contexts; voice composes with other modalities (e.g., point at an object and say "delete" — voice as *selection*, gaze/hand ray as *targeting*).
9. **Onboarding:** teach the interaction model up front (NUX); users don't discover voice capabilities on their own.

## Limitations to design around
- **Noise:** performance degrades in noisy environments; mitigate by adjusting mic sensitivity to ambient noise and by cueing the user (visual/audio) to move somewhere quieter when high noise is detected.
- **Latency:** real-time recognition trades speed vs accuracy; keep the interaction responsive and show intermediate state (e.g., live transcription) to mask latency.
- **Accent/dialect/speech diversity:** recognition is weakest for underrepresented accents and users with speech differences — always provide alternative input methods; never make voice the only path.
- **Barge-in:** support user interruption of spoken prompts/responses (lets users correct errors quickly).
- **Don't over-trust TTS:** generated speech may mispronounce words and sound flat — keep spoken output short, clear, and paired with on-screen text.

## Summary for v0.4.0
Voice is a **secondary, complementary modality** (never the only input path). If implemented: visible recording indicator + activation earcon at all times; opt-in with privacy disclosure; "Hey Meta"-style wake or an explicit on-screen/voice activation with clear cue; brief confirmations after commands; live transcription during dictation; barge-in support; short TTS paired with text; noise detection cue; and graceful fallback to poke/pinch/controller when recognition fails.
