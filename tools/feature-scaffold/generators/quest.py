"""Quest (C++ OpenXR) scaffold generator for the XR Wrist Display project.

Conventions followed (from quest/app/src/main/cpp/):
  - `#pragma once` headers, file-level doc comment describing the module
  - `namespace xrwrist { namespace <feature_ns> { ... } }`
  - member variables use `trailingUnderscore_` naming
  - compile-time constants: `static constexpr ... kName`
  - no magic numbers — named constants only
"""

import re


def to_snake(name: str) -> str:
    s1 = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    return re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", s1).lower()


def to_ns(name: str) -> str:
    return to_snake(name)


COUNCIL_CHECKLIST_CPP = """\
// Council checklist — all seats must sign off before this ships:
//   [ ] Engineering:    builds clean, no warnings, edge cases handled
//   [ ] Human-Centered: feels natural to the human body / eyes / hands
//   [ ] Security:       no credential/PII exposure, boundaries respected
//   [ ] UX:             polished, professional, Meta-fluent where apt
//   [ ] Platform:       Quest 2/3/3S compatible, extensions probed not assumed
//   [ ] Researcher:     docs consulted, outcome simulated, provenance noted
//   [ ] User Reviewer:  Zachery would actually use this (VETO if no)
"""

HUMAN_NOTE = """\
// Human-centered note: before adding behavior here, ask
// "what does the human body want?" — then build to that.
"""


def _class_name(name: str) -> str:
    # Ensure CamelCase class name
    name = re.sub(r"[^A-Za-z0-9]", "", name)
    return name[:1].upper() + name[1:] if name else "Feature"


# ---------------------------------------------------------------------------
# Per-type bodies
# ---------------------------------------------------------------------------

def _renderer_members(cls):
    return f"""\
public:
    bool Init();
    void Shutdown();

    // viewProj: combined view-projection. model: world transform of the panel.
    void Draw(const Mat4& viewProj, const Mat4& model);

    // Godmode hook: 0 = off, 1 = wireframe, 2 = normals.
    void SetDebugMode(int mode) {{ debugMode_ = mode; }}
    int debugMode() const {{ return debugMode_; }}

private:
    bool CompileShaders();

    static constexpr float kDefaultSizeM = 0.30f;  // panel width, meters

    GLuint prog_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLint uMvp_ = -1;
    int debugMode_ = 0;
    bool ok_ = false;"""


def _renderer_impl(cls, snake, ns):
    return f"""\
bool {cls}::Init() {{
    if (ok_) return true;
    if (!CompileShaders()) {{
        LOGE("{cls}: shader compile failed");
        return false;
    }}
    // TODO: build geometry (VAO/VBO) here. Keep buffers persistent and
    // reuse them across frames — no per-frame allocation on this path.
    ok_ = true;
    LOGI("{cls}: initialized");
    return true;
}}

void {cls}::Shutdown() {{
    if (prog_) {{ glDeleteProgram(prog_); prog_ = 0; }}
    if (vbo_) {{ glDeleteBuffers(1, &vbo_); vbo_ = 0; }}
    if (vao_) {{ glDeleteVertexArrays(1, &vao_); vao_ = 0; }}
    ok_ = false;
}}

void {cls}::Draw(const Mat4& viewProj, const Mat4& model) {{
    if (!ok_) return;
    // TODO: bind program, upload uMvp = viewProj * model, draw.
    // Human-centered: keep the panel readable at arm's length — never let
    // text drop below ~1 arcminute per pixel of angular size.
}}

bool {cls}::CompileShaders() {{
    // TODO: compile vertex/fragment shaders, cache uniform locations
    // into uMvp_. Return false (with LOGE) on any compile/link failure.
    return true;
}}"""


def _input_members(cls):
    return f"""\
public:
    // Call once per frame, AFTER the app's input poll. Reads already-polled
    // state — this class must never make OpenXR calls itself.
    void Poll(const XrInput& input);

    // TODO: replace with the real signals this feature needs.
    bool triggered() const {{ return triggered_; }}  // edge-triggered
    bool held() const {{ return held_; }}            // level

private:
    static constexpr float kTriggerThreshold = 0.5f;  // analog 0..1

    bool prevHeld_ = false;
    bool triggered_ = false;
    bool held_ = false;"""


def _input_impl(cls, snake, ns):
    return f"""\
void {cls}::Poll(const XrInput& input) {{
    // TODO: read the analog/digital state you need from `input`.
    // Example pattern (adapt to the real XrInput fields):
    //   bool now = input.trigL > kTriggerThreshold;
    //   triggered_ = now && !prevHeld_;
    //   held_ = now;
    //   prevHeld_ = now;
    (void)input;
    // Human-centered: edge-trigger on release for destructive actions so a
    // shaky hand doesn't fire twice; trigger on press for responsive ones.
}}"""


def _network_members(cls):
    return f"""\
public:
    {cls}() = default;
    ~{cls}() {{ Stop(); }}
    {cls}(const {cls}&) = delete;
    {cls}& operator=(const {cls}&) = delete;

    // All blocking I/O happens on the worker thread — never the render
    // thread. Poll IsDone() / TakeResult() from the main thread.
    void Start();
    void Stop();
    bool IsDone() const {{ return done_.load(); }}

private:
    void WorkerThread();

    std::atomic<bool> done_{{true}};
    std::atomic<bool> stop_{{false}};
    std::thread thread_;
    std::mutex mutex_;"""


def _network_impl(cls, snake, ns):
    return f"""\
void {cls}::Start() {{
    if (!done_.load()) return;  // already running
    stop_.store(false);
    done_.store(false);
    thread_ = std::thread(&{cls}::WorkerThread, this);
}}

void {cls}::Stop() {{
    stop_.store(true);
    if (thread_.joinable()) thread_.join();
    done_.store(true);
}}

void {cls}::WorkerThread() {{
    // TODO: implement the network operation here.
    // Use poll()-bounded reads (see net.h RecvExact pattern): never block
    // forever on a dead socket. Suggested bound: 15s per operation.
    // On exit, set done_.store(true).
    done_.store(true);
}}"""


def _panel_members(cls):
    return f"""\
public:
    bool Init();
    void Shutdown();

    void SetVisible(bool v) {{ visible_ = v; }}
    bool IsVisible() const {{ return visible_; }}

    // Returns true if the touch was consumed by this panel.
    // touchPos: panel-local UV in [0,1]x[0,1].
    bool OnTouch(float u, float v);

    void Draw(const Mat4& viewProj, const Mat4& model);

private:
    static constexpr float kPanelWidthM = 0.40f;
    static constexpr float kMinTouchTargetM = 0.012f;  // ~48dp at 1m

    bool visible_ = false;
    GLuint prog_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    bool ok_ = false;"""


def _panel_impl(cls, snake, ns):
    return f"""\
bool {cls}::Init() {{
    // TODO: compile shaders, build panel geometry (rounded rect via SDF).
    ok_ = true;
    return true;
}}

void {cls}::Shutdown() {{
    if (prog_) {{ glDeleteProgram(prog_); prog_ = 0; }}
    if (vbo_) {{ glDeleteBuffers(1, &vbo_); vbo_ = 0; }}
    if (vao_) {{ glDeleteVertexArrays(1, &vao_); vao_ = 0; }}
    ok_ = false;
}}

bool {cls}::OnTouch(float u, float v) {{
    if (!visible_ || !ok_) return false;
    // TODO: hit-test against your controls. Keep every touch target >=
    // kMinTouchTargetM so fingers (not just rays) can hit them.
    // Human-centered: direct touch beats laser pointers — design for poke.
    (void)u; (void)v;
    return false;
}}

void {cls}::Draw(const Mat4& viewProj, const Mat4& model) {{
    if (!visible_ || !ok_) return;
    // TODO: draw the panel. Meta guidance: avoid pure white/black in MR;
    // prefer dark surfaces with warm accent lighting.
}}"""


def _devtool_members(cls):
    return f"""\
public:
    void BeginFrame();
    void EndFrame();  // call after the frame's xrEndFrame is submitted

    double fps() const {{ return fps_; }}
    void AppendTo(std::string& out) const;  // for the diagnostics overlay

private:
    static constexpr int kWindow = 90;

    double times_[kWindow] = {{}};
    int head_ = 0;
    int count_ = 0;
    double fps_ = 0.0;
    uint64_t t0ns_ = 0;"""


def _devtool_impl(cls, snake, ns):
    return f"""\
void {cls}::BeginFrame() {{
    // TODO: record start timestamp (clock_gettime CLOCK_MONOTONIC).
}}

void {cls}::EndFrame() {{
    // TODO: record end, push delta into the ring buffer, recompute fps_.
    // Thread-affine: only call from the render thread.
}}

void {cls}::AppendTo(std::string& out) const {{
    // TODO: format one line of diagnostics, e.g.:
    //   char buf[64];
    //   snprintf(buf, sizeof(buf), "{cls}: %.1f fps", fps_);
    //   out += buf; out += '\\n';
}}"""


def _settings_members(cls):
    return f"""\
public:
    // TODO: add your settings fields here with sane defaults.
    // Example:
    // float panelScale = 1.0f;   // 0.5 .. 2.5
    // bool enabled = true;

    static {cls} Defaults();

    std::string ToString() const;  // for logging / Godmode display
}};"""


def _settings_impl(cls, snake, ns):
    return f"""{cls} {cls}::Defaults() {{
    {cls} s;
    // Defaults are already set by in-class initializers above.
    return s;
}}

std::string {cls}::ToString() const {{
    // TODO: serialize fields for the diagnostics overlay.
    return "{cls}{{}}";
}}"""


TYPE_INFO = {
    "renderer": ("Renderer", _renderer_members, _renderer_impl,
                 "OpenGL ES panel/object renderer drawn every frame.",
                 ["GLES3/gl3.h", "openxr/openxr.h", "gl_render.h (Mat4)"]),
    "input": ("Input Handler", _input_members, _input_impl,
              "Per-frame consumer of already-polled controller state.",
              ["openxr/openxr.h", "input.h (XrInput)"]),
    "network": ("Network Module", _network_members, _network_impl,
                "Background-thread network worker (never blocks render).",
                ["<atomic>", "<thread>", "<mutex>", "<string>", "net.h"]),
    "panel": ("UI Panel", _panel_members, _panel_impl,
              "Touch-able world-space panel with Meta-fluent styling.",
              ["GLES3/gl3.h", "gl_render.h (Mat4)"]),
    "devtool": ("DevTool", _devtool_members, _devtool_impl,
                "Diagnostics component for the Godmode overlay.",
                ["<string>"]),
    "settings": ("Settings", _settings_members, _settings_impl,
                 "Plain settings struct with defaults and ToString().",
                 ["<string>"]),
}


def generate_quest(name: str, ftype: str, description: str):
    """Return a list of {path, language, content} dicts for a Quest feature."""
    cls = _class_name(name)
    snake = to_snake(cls)
    ns = to_ns(cls)
    var = snake + "_"

    info = TYPE_INFO.get(ftype, TYPE_INFO["devtool"])
    type_label, members_fn, impl_fn, type_desc, includes = info

    desc = description.strip() or f"{cls}: {type_desc}"

    header = f"""\
#pragma once
// {cls} — {desc}
//
// {type_desc}
// Threading: see per-method notes. When in doubt, render thread only.

{COUNCIL_CHECKLIST_CPP}
{HUMAN_NOTE}
"""
    seen = set()
    for inc in ["<cstdint>", "<string>"] + includes:
        if inc not in seen:
            seen.add(inc)
            header += f'#include {inc}\n'
    header += f"""
namespace xrwrist {{
namespace {ns} {{

class {cls} {{
{members_fn(cls)}
}};

}}  // namespace {ns}
}}  // namespace xrwrist
"""

    impl = f"""\
#include "{snake}.h"

#include "log.h"

namespace xrwrist {{
namespace {ns} {{

{impl_fn(cls, snake, ns)}

}}  // namespace {ns}
}}  // namespace xrwrist
"""

    integration = f"""\
# Integrating {cls} into the Quest app

## 1. `quest/app/src/main/cpp/CMakeLists.txt`
Add the new translation unit to the source list:

```cmake
    {snake}.cpp
```

## 2. `quest/app/src/main/cpp/xr_core.h`
Include the header (near the other includes):

```cpp
#include "{snake}.h"
```

Add a member to the `XrCore` class (private section, near `avatar_`):

```cpp
    xrwrist::{ns}::{cls} {var};
```

## 3. `quest/app/src/main/cpp/xr_core.cpp`
Initialize during startup (check the return, fail loudly):

```cpp
    if (!{var}.Init()) {{
        LOGE("{cls}: Init failed");
        return false;  // or handle gracefully per your init flow
    }}
```

Drive it per frame (find the main frame loop):

```cpp
    {var}.Draw(viewProj, model);   // renderer / panel types
    // — or —
    {var}.Poll(xrInput_);          // input-handler types
```

Shut down cleanly:

```cpp
    {var}.Shutdown();
```

## 4. Godmode wiring (optional)
Expose `SetDebugMode()` / stats through the diagnostics overlay
(`devtools::TextOverlay`) so the feature is inspectable in-headset.
"""

    return [
        {"path": f"quest/app/src/main/cpp/{snake}.h",
         "language": "cpp", "content": header},
        {"path": f"quest/app/src/main/cpp/{snake}.cpp",
         "language": "cpp", "content": impl},
        {"path": f"quest/INTEGRATION_{snake}.md",
         "language": "markdown", "content": integration},
    ]
