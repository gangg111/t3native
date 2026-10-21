// The Windows development host: a window with a GL 4.3 core context (ARB_ES3_compatibility runs
// the renderer's `#version 300 es` shaders), the keyboard and mouse (as touch), XInput pads,
// waveOut audio, and the remake's capture options so frames can be compared with Godot's.
//
//   t3host [--data DIR] [--user DIR] [--size WxH] [--screenshot FILE.png --frames N] [--fixed-step]
//          [--pads FILE]   (the remake's pad script: `<steps> <pad1 hex> [<pad2 hex>]` per line)
//          [--lose-context-at N]   (the GL context destroyed and made again before frame N)
#ifndef __ANDROID__

#include <windows.h>
#include <mmsystem.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "gfx/gl.h"
#include "gfx/image.h"
#include "gfx/texture.h"
#include "platform/app.h"
#include "platform/host/host_options.h"
#include "platform/keys.h"
#include "platform/log.h"

namespace t3 {
App* make_app(const HostOptions& options);
}

namespace {

using namespace t3;

HostOptions g_options;
App* g_app = nullptr;
HWND g_hwnd = nullptr;
HDC g_dc = nullptr;
int g_width = 1280, g_height = 720;
bool g_running = true;
bool g_mouse_down = false;
HMODULE g_gl32 = nullptr;
std::chrono::steady_clock::time_point g_start = std::chrono::steady_clock::now();

void* gl_proc(const char* name) {
    void* p = reinterpret_cast<void*>(wglGetProcAddress(name));
    if (p == nullptr || p == reinterpret_cast<void*>(1) || p == reinterpret_cast<void*>(2) || p == reinterpret_cast<void*>(3) ||
        p == reinterpret_cast<void*>(-1))
        p = reinterpret_cast<void*>(GetProcAddress(g_gl32, name));
    return p;
}

LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CLOSE: g_running = false; return 0;
        case WM_SIZE:
            g_width = LOWORD(lp);
            g_height = HIWORD(lp);
            if (g_app && g_width > 0 && g_height > 0) g_app->resize(g_width, g_height, SafeArea{});
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (g_app && !(lp & (1 << 30))) g_app->key(static_cast<int>(t3::Keys::from_windows_vk(static_cast<int>(wp))), true);
            return 0;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (g_app) g_app->key(static_cast<int>(t3::Keys::from_windows_vk(static_cast<int>(wp))), false);
            return 0;
        case WM_LBUTTONDOWN:
            SetCapture(h);
            g_mouse_down = true;
            if (g_app) g_app->pointer(PointerEvent::DOWN, Pointer{0, float(short(LOWORD(lp))), float(short(HIWORD(lp)))});
            return 0;
        case WM_MOUSEMOVE:
            if (g_mouse_down && g_app) g_app->pointer(PointerEvent::MOVE, Pointer{0, float(short(LOWORD(lp))), float(short(HIWORD(lp)))});
            return 0;
        case WM_LBUTTONUP:
            ReleaseCapture();
            g_mouse_down = false;
            if (g_app) g_app->pointer(PointerEvent::UP, Pointer{0, float(short(LOWORD(lp))), float(short(HIWORD(lp)))});
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

using CreateAttribs = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
CreateAttribs g_create = nullptr;
HGLRC g_context = nullptr;
const int CONTEXT_ATTRIBS[] = {0x2091, 4, 0x2092, 3, 0x9126, 0x00000001, 0};   // MAJOR 4, MINOR 3, CORE profile

void swap_interval() {
    using SwapInterval = BOOL(WINAPI*)(int);
    if (auto swap = reinterpret_cast<SwapInterval>(reinterpret_cast<void*>(wglGetProcAddress("wglSwapIntervalEXT")))) swap(g_options.screenshot.empty() ? 1 : 0);
}

bool create_window() {
    WNDCLASSW wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"t3host";
    RegisterClassW(&wc);
    RECT r{0, 0, g_width, g_height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindowW(L"t3host", L"Tekken 3 (t3native host)", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_hwnd) return false;
    g_dc = GetDC(g_hwnd);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    SetPixelFormat(g_dc, ChoosePixelFormat(g_dc, &pfd), &pfd);
    // A legacy context to reach wglCreateContextAttribsARB, then the 4.3 core context.
    HGLRC legacy = wglCreateContext(g_dc);
    wglMakeCurrent(g_dc, legacy);
    g_create = reinterpret_cast<CreateAttribs>(reinterpret_cast<void*>(wglGetProcAddress("wglCreateContextAttribsARB")));
    if (!g_create) return false;
    g_context = g_create(g_dc, nullptr, CONTEXT_ATTRIBS);
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(legacy);
    if (!g_context) return false;
    wglMakeCurrent(g_dc, g_context);
    swap_interval();
    return true;
}

// `--lose-context-at`: what Android does when EGL_CONTEXT_LOST comes back. The context is deleted
// with every GL object in it, a new one is made current (its object names start over, so an old
// name may now belong to a new object) and the app makes its objects again.
bool lose_context() {
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(g_context);
    g_context = g_create(g_dc, nullptr, CONTEXT_ATTRIBS);
    if (!g_context || !wglMakeCurrent(g_dc, g_context)) return false;
    swap_interval();
    const char* missing = nullptr;
    if (!t3::gl::load(gl_proc, &missing)) return false;   // the entry points of the new context
    Log::info("host: GL context destroyed and made again");
    g_app->graphics_lost();
    return true;
}

// XInput, loaded at run time (no import library needed).
struct XPad {
    WORD buttons;
    BYTE lt, rt;
    SHORT lx, ly, rx, ry;
};
struct XState {
    DWORD packet;
    XPad pad;
};
using XGetState = DWORD(WINAPI*)(DWORD, XState*);
XGetState g_xinput = nullptr;
WORD g_xbuttons[4] = {};
bool g_xconnected[4] = {};
int g_xpolls = 0;

// The 4 XInput slots each frame; a slot without a pad only once a second (XInputGetState on an
// empty slot is slow). A slot that starts or stops answering is a connection change.
void poll_pads() {
    if (!g_xinput) return;
    static const std::pair<WORD, PadButton> MAP[] = {
        {0x1000, PadButton::A}, {0x2000, PadButton::B}, {0x4000, PadButton::X}, {0x8000, PadButton::Y},
        {0x0100, PadButton::L1}, {0x0200, PadButton::R1}, {0x0020, PadButton::SELECT}, {0x0010, PadButton::START},
        {0x0001, PadButton::UP}, {0x0002, PadButton::DOWN}, {0x0004, PadButton::LEFT}, {0x0008, PadButton::RIGHT},
        {0x0040, PadButton::L3}, {0x0080, PadButton::R3}};
    bool probe_empty = g_xpolls++ % 60 == 0;
    for (DWORD i = 0; i < 4; ++i) {
        if (!g_xconnected[i] && !probe_empty) continue;
        XState s{};
        bool connected = g_xinput(i, &s) == 0;
        if (connected != g_xconnected[i]) {
            g_xconnected[i] = connected;
            g_xbuttons[i] = 0;   // the router forgets the pad's buttons and axes with the change
            g_app->pad_connected(static_cast<int>(i), connected);
        }
        if (!connected) continue;
        WORD b = s.pad.buttons;
        if (s.pad.lt > 128) b |= 0x0400;   // L2 / R2 as pseudo bits
        if (s.pad.rt > 128) b |= 0x0800;
        WORD changed = static_cast<WORD>(b ^ g_xbuttons[i]);
        for (auto [bit, button] : MAP)
            if (changed & bit) g_app->pad(static_cast<int>(i), button, (b & bit) != 0);
        if (changed & 0x0400) g_app->pad(static_cast<int>(i), PadButton::L2, (b & 0x0400) != 0);
        if (changed & 0x0800) g_app->pad(static_cast<int>(i), PadButton::R2, (b & 0x0800) != 0);
        g_xbuttons[i] = b;
        g_app->pad_axis(static_cast<int>(i), 0, s.pad.lx / 32767.0f);
        g_app->pad_axis(static_cast<int>(i), 1, -s.pad.ly / 32767.0f);
    }
}

// waveOut: 48 kHz stereo 16-bit, four buffers of 512 frames filled from App::audio.
constexpr int AUDIO_RATE = 48000, AUDIO_FRAMES = 512, AUDIO_BUFFERS = 4;
std::atomic<bool> g_audio_run{false};
std::thread g_audio_thread;

void audio_loop() {
    HWAVEOUT out = nullptr;
    WAVEFORMATEX fmt{WAVE_FORMAT_PCM, 2, AUDIO_RATE, AUDIO_RATE * 4, 4, 16, 0};
    HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (waveOutOpen(&out, WAVE_MAPPER, &fmt, reinterpret_cast<DWORD_PTR>(done), 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        Log::warning("host: no audio device");
        return;
    }
    std::vector<std::vector<int16_t>> data(AUDIO_BUFFERS, std::vector<int16_t>(AUDIO_FRAMES * 2));
    std::vector<WAVEHDR> hdr(AUDIO_BUFFERS);
    std::vector<float> mix(AUDIO_FRAMES * 2);
    for (int k = 0; k < AUDIO_BUFFERS; ++k) {
        hdr[k] = WAVEHDR{reinterpret_cast<LPSTR>(data[k].data()), AUDIO_FRAMES * 4, 0, 0, 0, 0, nullptr, 0};
        waveOutPrepareHeader(out, &hdr[k], sizeof(WAVEHDR));
        hdr[k].dwFlags |= WHDR_DONE;
    }
    while (g_audio_run) {
        for (int k = 0; k < AUDIO_BUFFERS; ++k) {
            if (!(hdr[k].dwFlags & WHDR_DONE)) continue;
            g_app->audio(mix.data(), AUDIO_FRAMES, AUDIO_RATE);
            for (int i = 0; i < AUDIO_FRAMES * 2; ++i) {
                float v = mix[i] < -1 ? -1 : (mix[i] > 1 ? 1 : mix[i]);
                data[k][i] = static_cast<int16_t>(v * 32767);
            }
            hdr[k].dwFlags &= ~WHDR_DONE;
            waveOutWrite(out, &hdr[k], sizeof(WAVEHDR));
        }
        WaitForSingleObject(done, 20);
    }
    waveOutReset(out);
    for (auto& h : hdr) waveOutUnprepareHeader(out, &h, sizeof(WAVEHDR));
    waveOutClose(out);
    CloseHandle(done);
}

// --audio-out: 16-bit stereo PCM at AUDIO_RATE.
bool save_wav(const std::string& path, const std::vector<int16_t>& samples) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    uint32_t bytes = static_cast<uint32_t>(samples.size() * 2);
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + bytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);
    u16(2);
    u32(AUDIO_RATE);
    u32(AUDIO_RATE * 4);
    u16(4);
    u16(16);
    std::fwrite("data", 1, 4, f);
    u32(bytes);
    std::fwrite(samples.data(), 2, samples.size(), f);
    return std::fclose(f) == 0;
}

bool save_screenshot(const std::string& path) {
    gfx::Image im;
    im.width = g_width;
    im.height = g_height;
    im.rgba.resize(static_cast<size_t>(g_width) * g_height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, g_width, g_height, GL_RGBA, GL_UNSIGNED_BYTE, im.rgba.data());
    // GL rows run bottom to top.
    std::vector<uint8_t> row(static_cast<size_t>(g_width) * 4);
    for (int y = 0; y < g_height / 2; ++y) {
        uint8_t* a = im.pixel(0, y);
        uint8_t* b = im.pixel(0, g_height - 1 - y);
        std::copy(a, a + row.size(), row.begin());
        std::copy(b, b + row.size(), a);
        std::copy(row.begin(), row.end(), b);
    }
    for (size_t i = 3; i < im.rgba.size(); i += 4) im.rgba[i] = 255;
    return gfx::save_png(im, path);
}

}  // namespace

namespace t3::Platform {
void vibrate(int milliseconds, int strength) { Log::debug("vibrate " + std::to_string(milliseconds) + " ms, " + std::to_string(strength)); }
void perf_line(const std::string& line) {
    std::string path = g_options.user_dir + "/perf.txt";
    if (FILE* f = std::fopen(path.c_str(), "a")) {
        std::fprintf(f, "%s\n", line.c_str());
        std::fclose(f);
    }
}
void pad_rumble(int device, int weak, int strong, int milliseconds) {
    Log::debug("pad " + std::to_string(device) + " rumble " + std::to_string(weak) + "/" + std::to_string(strong) + ", " +
               std::to_string(milliseconds) + " ms");
}
double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count(); }
// The host has no dialog: `--text-answer TEXT` answers every question with TEXT (development).
namespace {
bool g_text_asked = false;
}
bool ask_text(const std::string& title, const std::string& initial) {
    Log::info("ask_text " + title + " (" + initial + ")" + (g_options.text_answer.empty() ? ": no --text-answer" : ""));
    g_text_asked = !g_options.text_answer.empty();
    return g_text_asked;
}
TextAnswer poll_text(std::string& text) {
    if (!g_text_asked) return TEXT_NONE;
    g_text_asked = false;
    text = g_options.text_answer;
    return TEXT_ENTERED;
}
}  // namespace t3::Platform

int main(int argc, char** argv) {
    g_options = t3::HostOptions::parse(argc, argv);
    g_width = g_options.width;
    g_height = g_options.height;
    if (!create_window()) {
        std::fprintf(stderr, "cannot create a GL 4.3 window\n");
        return 1;
    }
    g_gl32 = LoadLibraryW(L"opengl32.dll");
    const char* missing = nullptr;
    if (!t3::gl::load(gl_proc, &missing)) {
        std::fprintf(stderr, "GL entry point missing: %s\n", missing ? missing : "?");
        return 1;
    }
    t3::Log::info(std::string("GL ") + reinterpret_cast<const char*>(glGetString(GL_VERSION)) + ", " +
                  reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    if (HMODULE x = LoadLibraryW(L"xinput1_4.dll")) g_xinput = reinterpret_cast<XGetState>(reinterpret_cast<void*>(GetProcAddress(x, "XInputGetState")));
    g_app = t3::make_app(g_options);
    if (!g_app->start(g_options.data_root, g_options.user_dir)) return 1;
    g_app->resize(g_width, g_height, t3::SafeArea{});
    // Shown even when capturing: a hidden window's default framebuffer has undefined pixels.
    ShowWindow(g_hwnd, SW_SHOW);
    g_audio_run = g_options.screenshot.empty();
    if (g_audio_run) g_audio_thread = std::thread(audio_loop);
    int frame = 0;
    // --audio-out: the frames' sound pulled on this thread, exactly a frame's worth after each one.
    std::vector<int16_t> recorded;
    std::vector<float> frame_mix(AUDIO_RATE / 60 * 2);
    while (g_running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        poll_pads();
        if (frame == g_options.lose_context_at && !lose_context()) {
            std::fprintf(stderr, "cannot make the GL context again\n");
            return 1;
        }
        double now = g_options.fixed_step ? frame / 60.0 : t3::Platform::now();
        g_app->frame(now);
        frame += 1;
        if (!g_options.audio_out.empty() && !g_audio_run) {
            g_app->audio(frame_mix.data(), AUDIO_RATE / 60, AUDIO_RATE);
            for (float v : frame_mix) recorded.push_back(static_cast<int16_t>((v < -1 ? -1 : (v > 1 ? 1 : v)) * 32767));
        }
        if (!g_options.screenshot.empty() && frame >= g_options.frames) {
            if (!g_options.audio_out.empty()) {
                bool wrote = save_wav(g_options.audio_out, recorded);
                t3::Log::info((wrote ? "audio " : "cannot write ") + g_options.audio_out);
            }
            if (g_options.lose_context_at >= 0) {
                t3::Log::info("host: stale texture binds after the context loss: " + std::to_string(t3::gfx::Texture::stale_binds()));
            }
            bool ok = save_screenshot(g_options.screenshot);
            t3::Log::info((ok ? "screenshot " : "cannot write ") + g_options.screenshot);
            break;
        }
        SwapBuffers(g_dc);
    }
    g_audio_run = false;
    if (g_audio_thread.joinable()) g_audio_thread.join();
    g_app->stop();
    delete g_app;
    return 0;
}

#endif
