# Presence Platform (Passthrough / Scene / Spatial Anchors) — Research Notes

**Sources:**
- https://developers.meta.com/horizon/blog/building-mixed-reality-MR-meta-quest-3-connect-developers-presence-platform (Quest 3 MR overview)

**Date accessed:** 2026-10-06
**Purpose:** Presence Platform capability inventory; future hooks for world-locked panels (SG-1), occlusion, and room-aware placement.

> Summarized in my own words from official docs; capability names are exact.

---

## 1. What Presence Platform is

A collection of ML/AI-powered capabilities for building mixed reality that blends virtual content with the physical world, without exposing raw headset sensor imagery. Core capabilities:

- **Passthrough:** real-time, perceptually comfortable 3D visualization of the physical world. (Quest 3: full color, ~10x the pixels of Quest 2, AI depth engine.)
- **Scene:** environment-aware experiences — the app understands the user's physical surroundings (planes, volumes, semantic labels) and objects can interact with them.
- **Spatial Anchors:** world-locked frames of reference for pinning virtual content to physical locations, persisted on-device across sessions.
- **Shared Spatial Anchors:** the same world-locked frame shared across users in the same physical space (local multiplayer).
- **Depth API** (experimental, v57+, Quest 3): real-time per-eye, per-frame environment depth estimates — enables dynamic occlusion, depth-based effects (fog), gameplay use.
- **Mesh API** (v57): scene mesh for realistic physical interactions (characters navigating the room, transforming spaces).
- **Space Setup:** automated room-layout detection so apps react to physical spaces with less backend work.
- Also in the family: Movement SDK (body tracking), Hand Tracking, Interaction SDK, Voice SDK.

## 2. Tooling

- **MR Utility Kit** (Unity/Unreal): simplifies Scene integration; ships pre-captured rooms for headset-free testing.
- **Phanto** (open source, Unity): reference app for Scene Mesh / Scene Model / Scene API — placement, collision, navigation.
- **Discover** (open source, Unity): showcases Scene, Spatial Anchors, Shared Spatial Anchors, Passthrough.
- **Meta XR Simulator** + **Building Blocks**: integrate and test without donning/doffing the headset.
- **Immersive Web Emulator** (WebXR, Chromium): 6DOF emulation, controller emulation, emulated hand tracking, hit-testing, persistent anchors.

## 3. Design signals

- Meta's retention story: apps adding Passthrough saw meaningful engagement lifts (example cited: +31% 7-day DAU for a mindfulness app).
- Seamless Home→app passthrough transitions are a platform direction — no black screens or loading hitches when entering MR.

## 4. Relevance to XR Wrist

- Our passthrough usage is currently ambient (optional background). The **Depth API** is the future path to correct occlusion between our avatar hands and the real world.
- **Spatial Anchors** are the natural persistence mechanism if we ever let users pin the "fixed" panel to a real-world spot (SG-1 world-locked panel, phase 2).
- **Scene** semantics could keep the fixed panel off the user's desk/floor — room-aware placement.
- OpenXR-level equivalents to watch: `XR_FB_scene`, `XR_FB_spatial_entity`, `XR_FB_passthrough` (already probed).
