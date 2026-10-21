#pragma once
// What a platform (Android NativeActivity, the Windows development host) runs: the app gets the
// surface size, its frames, the pointer/touch and key/pad input, and the lifecycle; the platform
// owns the window, the GL context, the audio device and the clock.
#include <cstdint>
#include <string>

namespace t3 {

struct Pointer {
    int id = 0;                // stable while the finger (or mouse button) is down
    float x = 0, y = 0;        // surface pixels
};

enum class PointerEvent { DOWN, MOVE, UP, CANCEL };

// Pad buttons as the platforms report them, before the app maps them to PadState bits.
enum class PadButton { A, B, X, Y, L1, R1, L2, R2, SELECT, START, UP, DOWN, LEFT, RIGHT, L3, R3, COUNT };

struct SafeArea {
    int left = 0, top = 0, right = 0, bottom = 0;   // insets in surface pixels (cutouts, system bars)
};

class App {
public:
    virtual ~App() = default;
    // GL is current; false stops the platform (the message is logged).
    virtual bool start(const std::string& data_root, const std::string& user_dir) = 0;
    // The GL context was lost (Android): every GL object must be made again before the next frame.
    virtual void graphics_lost() {}
    virtual void resize(int width, int height, const SafeArea& safe) = 0;
    // One display frame; `now` in seconds of a monotonic clock.
    virtual void frame(double now) = 0;
    virtual void pointer(PointerEvent kind, const Pointer& p) = 0;
    virtual void key(int key_code, bool down) = 0;            // platform key codes (host: Windows VK_*)
    virtual void pad(int device, PadButton button, bool down) = 0;
    virtual void pad_axis(int device, int axis, float value) = 0;   // 0/1 left stick x/y, -1 ... 1
    // A pad came or went (Input.joy_connection_changed). The platforms report the pads present at
    // start as connections too, and may repeat a report (the app counts changes only).
    virtual void pad_connected(int device, bool connected) { (void)device, (void)connected; }
    virtual void back() = 0;                                  // Android back / Escape
    virtual void paused(bool is_paused) = 0;                  // sent to the background / back
    // Audio: fill `frames` stereo float frames at `rate` Hz (called from the audio thread).
    virtual void audio(float* out, int frames, int rate) = 0;
    virtual void stop() = 0;
};

// The platform's side the app may call.
namespace Platform {
void vibrate(int milliseconds, int strength);   // strength 1 ... 255
// A pad's own motors (weak / strong 0 ... 255) for `milliseconds`; all 0 stops them. Nothing on a
// pad without motors.
void pad_rumble(int device, int weak, int strong, int milliseconds);
// A performance log line (PerfLog), written out on another thread: Android adds the phone's
// temperature and appends it to t3native_perf_<time>.txt in Downloads; the host to perf.txt in the
// user folder.
void perf_line(const std::string& line);
double now();
// A line of text typed by the player (Android: a dialog with the system keyboard), asked with the
// text so far; false when there is no text input. The answer arrives later through `poll_text`:
// TEXT_NONE while waiting, TEXT_ENTERED with `text`, TEXT_CANCELLED.
enum TextAnswer { TEXT_NONE, TEXT_ENTERED, TEXT_CANCELLED };
bool ask_text(const std::string& title, const std::string& initial);
TextAnswer poll_text(std::string& text);
// Native: NETWORK MODE. The device's name as the other player sees it ("GALAXY Z FOLD7"); whether
// the Wi-Fi lets broadcasts in (Android drops them unless the app holds a multicast lock: a host
// must hear the searches); the system's Wi-Fi panel over the game (Android 10+ lets no app turn
// Wi-Fi on itself; older versions turn it on).
std::string device_name();
void network_lock(bool on);
void open_wifi_panel();
}  // namespace Platform

}  // namespace t3
