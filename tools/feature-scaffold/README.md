# Feature Scaffold

Developer tool for the XR Wrist Display project. Generates production-ready
boilerplate for new features through a web UI.

## Run

```bash
cd ~/workspace/works/projects/xr-wrist-display/tools/feature-scaffold
./run.sh
```

Then open **http://127.0.0.1:8890** in a browser.

## What it generates

**Quest target (C++ OpenXR)** — header + implementation + integration notes:
- `<feature>.h` / `<feature>.cpp` under `quest/app/src/main/cpp/`
- Follows project conventions: `namespace xrwrist::<ns>`, `#pragma once`,
  `trailingUnderscore_` members, `static constexpr kConstants`, no magic numbers
- `INTEGRATION_<feature>.md` with exact snippets for `CMakeLists.txt`,
  `xr_core.h`, `xr_core.cpp`, and Godmode wiring

**Phone target (Kotlin)** — class / Activity / Service + manifest notes:
- `<Feature>.kt` (or `<Feature>Activity.kt` / `<Feature>Service.kt`) under
  `phone/app/src/main/java/com/zachery/xrwrist/phone/`
- Material Design 3 programmatic Views (matches DevOptionsActivity style)
- Foreground-service pattern for network modules (API 26+ safe)

**Feature types:** Renderer · Input Handler · Network Module · UI Panel ·
DevTool · Settings

## Standards baked into every file

- Council checklist comment (all 7 seats must sign off before shipping)
- Human-centered note ("what does the human body want?")
- TODO markers where the developer fills in logic
- Threading notes (never block the render thread / main thread)

## Safety

- **Download .zip** — no filesystem writes, review before using
- **Copy to project** — writes only to allow-listed source directories
  (`quest/app/src/main/cpp/`, `phone/.../xrwrist/phone/`), never overwrites
  existing files

## Test

```bash
# generate a sample without the UI:
python3 -c "
from generators.quest import generate_quest
from generators.phone import generate_phone
for f in generate_quest('TestFeature', 'devtool', 'sample'):
    print(f['path'], len(f['content']), 'chars')
for f in generate_phone('TestFeature', 'devtool', 'sample'):
    print(f['path'], len(f['content']), 'chars')
"
```
