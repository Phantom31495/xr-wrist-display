#include "devtools.h"
#include "log.h"
#include "net.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "third_party/font8x8/font8x8_basic.h"

namespace xrwrist {
namespace devtools {
namespace {

uint64_t NowNs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

GLuint CompileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOGE("devtools: shader compile failed: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint LinkProgram(const char* vs, const char* fs) {
    GLuint v = CompileShader(GL_VERTEX_SHADER, vs);
    GLuint f = CompileShader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        LOGE("devtools: program link failed");
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

}  // namespace

// ---------------------------------------------------------------------------
// FrameDiagnostics
// ---------------------------------------------------------------------------
void FrameDiagnostics::BeginFrame() { t0ns_ = NowNs(); }

void FrameDiagnostics::EndFrame() {
    uint64_t t1 = NowNs();
    double ms = (t1 - t0ns_) / 1e6;
    times_[head_] = ms;
    head_ = (head_ + 1) % kWindow;
    if (count_ < kWindow) count_++;
    double sum = 0, worst = 0;
    for (int i = 0; i < count_; ++i) {
        sum += times_[i];
        if (times_[i] > worst) worst = times_[i];
    }
    msAvg_ = sum / count_;
    msWorst_ = worst;
    if (lastEndNs_ != 0) {
        double dt = (t1 - lastEndNs_) / 1e9;
        if (dt > 0) fps_ = fps_ * 0.9 + (1.0 / dt) * 0.1;  // EMA
    }
    lastEndNs_ = t1;
    frameIndex_++;
}

// ---------------------------------------------------------------------------
// DeviceInfo
// ---------------------------------------------------------------------------
DeviceInfo CollectDeviceInfo(XrInstance instance, XrSystemId systemId,
                             XrSession session,
                             const std::vector<const char*>& enabledExts,
                             uint32_t viewCount, int viewW, int viewH,
                             bool handTracking, bool passthrough,
                             bool handMesh, bool handAim, bool refreshRate) {
    DeviceInfo d;
    d.viewCount = viewCount;
    d.viewW = viewW;
    d.viewH = viewH;
    d.handTracking = handTracking;
    d.passthrough = passthrough;
    d.handMesh = handMesh;
    d.handAim = handAim;
    d.refreshRate = refreshRate;
    for (auto e : enabledExts) d.enabledExtensions.emplace_back(e);

    XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
    if (xrGetInstanceProperties(instance, &props) == XR_SUCCESS) {
        d.runtimeName = props.runtimeName;
        char ver[32];
        snprintf(ver, sizeof(ver), "%u.%u.%u",
                 (unsigned)XR_VERSION_MAJOR(props.runtimeVersion),
                 (unsigned)XR_VERSION_MINOR(props.runtimeVersion),
                 (unsigned)XR_VERSION_PATCH(props.runtimeVersion));
        d.runtimeVersion = ver;
    }
    (void)systemId;

    // Refresh rates offered (optional FB extension).
    if (refreshRate && session != XR_NULL_HANDLE) {
        PFN_xrEnumerateDisplayRefreshRatesFB pfnEnum = nullptr;
        xrGetInstanceProcAddr(instance, "xrEnumerateDisplayRefreshRatesFB",
                              (PFN_xrVoidFunction*)&pfnEnum);
        if (pfnEnum) {
            uint32_t n = 0;
            if (pfnEnum(session, 0, &n, nullptr) == XR_SUCCESS && n > 0 &&
                n < 16) {
                std::vector<float> rates(n);
                if (pfnEnum(session, n, &n, rates.data()) == XR_SUCCESS) {
                    d.refreshRates.assign(rates.begin(),
                                          rates.begin() + n);
                }
            }
        }
    }
    return d;
}

std::string DeviceInfo::ToString() const {
    char buf[2048];
    int o = snprintf(buf, sizeof(buf),
                     "runtime: %s %s\nviews: %u x %dx%d\nhand: %d pass: %d "
                     "mesh: %d aim: %d 120hz: %d\nrates:",
                     runtimeName.c_str(), runtimeVersion.c_str(), viewCount,
                     viewW, viewH, handTracking ? 1 : 0, passthrough ? 1 : 0,
                     handMesh ? 1 : 0, handAim ? 1 : 0, refreshRate ? 1 : 0);
    for (float r : refreshRates) {
        o += snprintf(buf + o, sizeof(buf) - (size_t)o, " %.0f", r);
    }
    o += snprintf(buf + o, sizeof(buf) - (size_t)o, "\next:");
    for (auto& e : enabledExtensions) {
        o += snprintf(buf + o, sizeof(buf) - (size_t)o, "\n  %s", e.c_str());
    }
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// InputSnapshot
// ---------------------------------------------------------------------------
InputSnapshot InputSnapshot::Capture(const XrInput& in) {
    InputSnapshot s;
    s.leftValid = in.LeftGrip().valid;
    s.rightValid = in.RightGrip().valid;
    s.aimValid = in.RightAim().valid;
    s.trigL = in.LeftTrigger();
    s.trigR = in.TriggerValue();
    s.squeezeL = in.LeftSqueeze();
    s.squeezeR = in.RightSqueeze();
    in.LeftThumbstick(s.stickLX, s.stickLY);
    in.RightThumbstick(s.stickRX, s.stickRY);
    s.x = in.XDown();
    s.y = in.YDown();
    s.a = in.ADown();
    s.b = in.BDown();
    return s;
}

void InputSnapshot::AppendTo(std::string& out) const {
    char buf[512];
    snprintf(buf, sizeof(buf),
             "L pose:%d trig:%.2f sqz:%.2f stk:%+.2f,%+.2f X:%d Y:%d\n"
             "R pose:%d trig:%.2f sqz:%.2f stk:%+.2f,%+.2f A:%d B:%d aim:%d",
             leftValid ? 1 : 0, trigL, squeezeL, stickLX, stickLY, x ? 1 : 0,
             y ? 1 : 0, rightValid ? 1 : 0, trigR, squeezeR, stickRX, stickRY,
             a ? 1 : 0, b ? 1 : 0, aimValid ? 1 : 0);
    out += buf;
}

// ---------------------------------------------------------------------------
// NetworkProbe
// ---------------------------------------------------------------------------
namespace {

sockaddr_in MakeAddr(const char* ip, int port) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, ip, &a.sin_addr);
    return a;
}

void SetNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Minimal JSON string extractor for {"ip":"..."}.
std::string ExtractIp(const char* json) {
    const char* k = strstr(json, "\"ip\"");
    if (!k) return "";
    k = strchr(k, ':');
    if (!k) return "";
    k = strchr(k, '"');
    if (!k) return "";
    const char* e = strchr(k + 1, '"');
    if (!e) return "";
    return std::string(k + 1, (size_t)(e - k - 1));
}

}  // namespace

void NetworkProbe::StartScan(int timeoutMs) {
    StopScan();
    scanDone_.store(false);
    stop_.store(false);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hosts_.clear();
    }
    thread_ = std::thread(&NetworkProbe::ScanThread, this, timeoutMs);
}

void NetworkProbe::StopScan() {
    stop_.store(true);
    if (thread_.joinable()) thread_.join();
    scanDone_.store(true);
}

std::vector<NetworkProbe::Host> NetworkProbe::TakeResults() {
    std::lock_guard<std::mutex> lock(mutex_);
    return hosts_;
}

void NetworkProbe::ScanThread(int timeoutMs) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        scanDone_.store(true);
        return;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    sockaddr_in bindAddr{};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = INADDR_ANY;
    bindAddr.sin_port = 0;
    if (bind(fd, (sockaddr*)&bindAddr, sizeof(bindAddr)) != 0) {
        close(fd);
        scanDone_.store(true);
        return;
    }
    SetNonBlocking(fd);

    sockaddr_in dest = MakeAddr(kBroadcastAddr, kDiscoveryPort);
    const char* probe = "{\"discover\":\"xr-wrist\",\"v\":1}";
    auto tSend = std::chrono::steady_clock::now();
    // Send a few probes spread across the window (some stacks drop the first).
    int probesSent = 0;

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs);
    char buf[512];
    while (std::chrono::steady_clock::now() < deadline && !stop_.load()) {
        auto now = std::chrono::steady_clock::now();
        if (probesSent < 3 &&
            now - tSend > std::chrono::milliseconds(400 * probesSent)) {
            sendto(fd, probe, strlen(probe), 0, (sockaddr*)&dest,
                   sizeof(dest));
            probesSent++;
        }
        pollfd pfd{fd, POLLIN, 0};
        if (poll(&pfd, 1, 100) > 0 && (pfd.revents & POLLIN)) {
            sockaddr_in from{};
            socklen_t fromLen = sizeof(from);
            ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, 0,
                                 (sockaddr*)&from, &fromLen);
            if (n > 0) {
                buf[n] = '\0';
                std::string ip = ExtractIp(buf);
                if (!ip.empty()) {
                    char ipStr[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &from.sin_addr, ipStr, sizeof(ipStr));
                    int64_t us =
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - tSend)
                            .count();
                    std::lock_guard<std::mutex> lock(mutex_);
                    bool known = false;
                    for (auto& h : hosts_)
                        if (h.ip == ipStr) known = true;
                    if (!known) hosts_.push_back({ipStr, us});
                }
            }
        }
    }
    close(fd);
    // TCP-test the video/control ports of every responder (still off-thread
    // so the render loop never hitches).
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& h : hosts_) {
            if (stop_.load()) break;
            h.video = TestTcp(h.ip, kVideoPort, 800);
            h.control = TestTcp(h.ip, kControlPort, 800);
        }
    }
    scanDone_.store(true);
}

NetworkProbe::TcpTest NetworkProbe::TestTcp(const std::string& ip, int port,
                                            int timeoutMs) {
    TcpTest t;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        t.error = "socket failed";
        return t;
    }
    SetNonBlocking(fd);
    sockaddr_in addr = MakeAddr(ip.c_str(), port);
    uint64_t t0 = NowNs();
    int rc = connect(fd, (sockaddr*)&addr, sizeof(addr));
    if (rc != 0 && errno != EINPROGRESS) {
        t.error = strerror(errno);
        close(fd);
        return t;
    }
    pollfd pfd{fd, POLLOUT, 0};
    int pr = poll(&pfd, 1, timeoutMs);
    if (pr > 0) {
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
        if (err == 0) {
            t.ok = true;
            t.connectUs = (int64_t)(NowNs() - t0) / 1000;
        } else {
            t.error = strerror(err);
        }
    } else if (pr == 0) {
        t.error = "timeout";
    } else {
        t.error = strerror(errno);
    }
    close(fd);
    return t;
}

// ---------------------------------------------------------------------------
// TextOverlay
// ---------------------------------------------------------------------------
namespace {

const char* kTextVert = R"(
#version 300 es
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUv;
uniform mat4 uMvp;
out vec2 vUv;
void main() {
    vUv = aUv;
    gl_Position = uMvp * vec4(aPos, 0.0, 1.0);
}
)";
const char* kTextFrag = R"(
#version 300 es
precision mediump float;
in vec2 vUv;
uniform sampler2D uTex;
out vec4 fragColor;
void main() {
    float a = texture(uTex, vUv).r;
    if (a < 0.02) discard;
    fragColor = vec4(0.85, 1.0, 0.9, a);  // soft green-white glyphs
}
)";
const char* kBgVert = R"(
#version 300 es
layout(location=0) in vec2 aPos;
uniform mat4 uMvp;
void main() { gl_Position = uMvp * vec4(aPos, 0.0, 1.0); }
)";
const char* kBgFrag = R"(
#version 300 es
precision mediump float;
uniform vec4 uColor;
out vec4 fragColor;
void main() { fragColor = uColor; }
)";

}  // namespace

bool TextOverlay::Init() {
    if (!BuildAtlas()) return false;
    prog_ = LinkProgram(kTextVert, kTextFrag);
    bgProg_ = LinkProgram(kBgVert, kBgFrag);
    if (!prog_ || !bgProg_) return false;
    uMvp_ = glGetUniformLocation(prog_, "uMvp");
    uTex_ = glGetUniformLocation(prog_, "uTex");
    uBgMvp_ = glGetUniformLocation(bgProg_, "uMvp");
    uBgColor_ = glGetUniformLocation(bgProg_, "uColor");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    // Max quads: lines * cols; 6 verts * (2 pos + 2 uv) floats each.
    glBufferData(GL_ARRAY_BUFFER,
                 (size_t)kMaxLines * kMaxCols * 6 * 4 * sizeof(float), nullptr,
                 GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void*)(2 * sizeof(float)));
    glBindVertexArray(0);
    // Persistent unit quad [0,1]x[0,1] for the background plate.
    {
        float q[] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
        glGenVertexArrays(1, &bgVao_);
        glGenBuffers(1, &bgVbo_);
        glBindVertexArray(bgVao_);
        glBindBuffer(GL_ARRAY_BUFFER, bgVbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glBindVertexArray(0);
    }
    ok_ = true;
    LOGI("devtools: text overlay init ok");
    return true;
}

void TextOverlay::Shutdown() {
    if (bgVbo_) glDeleteBuffers(1, &bgVbo_);
    if (bgVao_) glDeleteVertexArrays(1, &bgVao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (prog_) glDeleteProgram(prog_);
    if (bgProg_) glDeleteProgram(bgProg_);
    if (atlasTex_) glDeleteTextures(1, &atlasTex_);
    vao_ = vbo_ = prog_ = bgProg_ = atlasTex_ = 0;
    bgVao_ = bgVbo_ = 0;
    ok_ = false;
}

bool TextOverlay::BuildAtlas() {
    // 16x8 grid of 8x8 glyphs -> 128x64 R8 texture. font8x8 rows are bytes,
    // LSB = leftmost pixel.
    static uint8_t px[64 * 128];
    memset(px, 0, sizeof(px));
    for (int c = 0; c < 128; ++c) {
        int gx = (c % 16) * 8;
        int gy = (c / 16) * 8;
        for (int y = 0; y < 8; ++y) {
            uint8_t row = font8x8_basic[c][y];
            for (int x = 0; x < 8; ++x) {
                if (row & (1u << x)) px[(gy + y) * 128 + gx + x] = 255;
            }
        }
    }
    glGenTextures(1, &atlasTex_);
    glBindTexture(GL_TEXTURE_2D, atlasTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 128, 64, 0, GL_RED,
                 GL_UNSIGNED_BYTE, px);
    glBindTexture(GL_TEXTURE_2D, 0);
    return atlasTex_ != 0;
}

void TextOverlay::Clear() {
    text_.clear();
    dirty_ = true;
}

void TextOverlay::Printf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    // Split into lines, cap total, clamp width.
    const char* p = buf;
    int lines = 0;
    for (char c : text_)
        if (c == '\n') lines++;
    while (*p && lines < kMaxLines) {
        const char* e = strchr(p, '\n');
        std::string line(p, e ? (size_t)(e - p) : strlen(p));
        if ((int)line.size() > kMaxCols) line.resize(kMaxCols);
        if (!text_.empty()) text_ += '\n';
        text_ += line;
        lines++;
        if (!e) break;
        p = e + 1;
    }
    dirty_ = true;
}

void TextOverlay::RebuildMesh() {
    // Panel-local pixel units: cols*8 wide, rows*8 tall, y-up, row 0 at top.
    std::vector<std::string> lines;
    {
        const char* p = text_.c_str();
        while (*p) {
            const char* e = strchr(p, '\n');
            lines.emplace_back(p, e ? (size_t)(e - p) : strlen(p));
            if (!e) break;
            p = e + 1;
        }
    }
    int rows = (int)lines.size();
    int cols = 0;
    for (auto& l : lines) cols = std::max(cols, (int)l.size());
    if (rows == 0 || cols == 0) {
        quadCount_ = 0;
        dirty_ = false;
        return;
    }
    float H = (float)(rows * 8);
    std::vector<float> v;
    v.reserve((size_t)rows * cols * 4 * 4);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < (int)lines[r].size(); ++c) {
            unsigned char ch = (unsigned char)lines[r][c];
            if (ch < 32 || ch > 126) ch = '?';
            float x0 = (float)(c * 8), x1 = x0 + 8;
            float y1 = H - (float)(r * 8), y0 = y1 - 8;
            float u0 = (float)((ch % 16) * 8) / 128.0f;
            float u1 = u0 + 8.0f / 128.0f;
            // Atlas rows are top-down in memory; GL textures are bottom-up.
            float v1 = 1.0f - (float)((ch / 16) * 8) / 64.0f;
            float v0 = v1 - 8.0f / 64.0f;
            // Two triangles: (x0,y0)(x1,y0)(x1,y1) / (x0,y0)(x1,y1)(x0,y1)
            float q[] = {x0, y0, u0, v0, x1, y0, u1, v0, x1, y1, u1, v1,
                         x0, y0, u0, v0, x1, y1, u1, v1, x0, y1, u0, v1};
            v.insert(v.end(), q, q + 24);
        }
    }
    quadCount_ = (int)(v.size() / 24);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, v.size() * sizeof(float), v.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    dirty_ = false;
}

void TextOverlay::Draw(const Mat4& viewProj, const Mat4& panelModel,
                       float panelW) {
    if (!ok_ || text_.empty()) return;
    if (dirty_) RebuildMesh();
    if (quadCount_ == 0) return;

    // Panel pixel extents of the current content.
    int rows = 0, cols = 0;
    {
        const char* p = text_.c_str();
        while (*p) {
            const char* e = strchr(p, '\n');
            int len = (int)(e ? (size_t)(e - p) : strlen(p));
            cols = std::max(cols, len);
            rows++;
            if (!e) break;
            p = e + 1;
        }
    }
    float s = panelW / (float)(cols * 8);
    Mat4 scale = Mat4::Identity();
    scale.m[0] = s;
    scale.m[5] = s;
    Mat4 mvp = viewProj * panelModel * scale;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    // Background plate: unit quad scaled to the content pixel extents.
    {
        Mat4 bgScale = Mat4::Identity();
        bgScale.m[0] = (float)(cols * 8);
        bgScale.m[5] = (float)(rows * 8);
        Mat4 bgMvp = mvp * bgScale;
        glUseProgram(bgProg_);
        glUniformMatrix4fv(uBgMvp_, 1, GL_FALSE, bgMvp.m);
        glUniform4f(uBgColor_, 0.02f, 0.03f, 0.04f, 0.72f);
        glBindVertexArray(bgVao_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }

    // Glyphs.
    glUseProgram(prog_);
    glUniformMatrix4fv(uMvp_, 1, GL_FALSE, mvp.m);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlasTex_);
    glUniform1i(uTex_, 0);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, quadCount_ * 6);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glDisable(GL_BLEND);
}

}  // namespace devtools
}  // namespace xrwrist
