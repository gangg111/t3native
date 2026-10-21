// The Android platform: a NativeActivity (android_native_app_glue) with an OpenGL ES 3.0 context
// (made again when the window or the context is lost), multi-touch, gamepads and keyboards through
// AInputEvent, AAudio output (rebuilt when the device disconnects), the vibrator through JNI, and the
// converted game data read straight from the APK's assets (AAssetManager, `asset:` paths).
#ifdef __ANDROID__

#include <EGL/egl.h>
#include <aaudio/AAudio.h>
#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/native_activity.h>
#include <android/window.h>
#include <sys/stat.h>
#include <android/input.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <jni.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <time.h>
#include <vector>

#include "app/game_app.h"
#include "gd/io.h"
#include "gfx/gl.h"
#include "platform/app.h"
#include "platform/keys.h"
#include "platform/log.h"

namespace {

using namespace t3;

android_app* g_android = nullptr;
App* g_app = nullptr;
bool g_started = false;
bool g_paused = false;
EGLDisplay g_display = EGL_NO_DISPLAY;
EGLConfig g_config = nullptr;
EGLContext g_context = EGL_NO_CONTEXT;
EGLSurface g_surface = EGL_NO_SURFACE;
int g_width = 0, g_height = 0;
AAudioStream* g_stream = nullptr;
std::atomic<bool> g_audio_restart{false};

double monotonic() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// ---- assets ---------------------------------------------------------------------------------

bool asset_read(const std::string& name, gd::PBytes& out) {
    AAsset* a = AAssetManager_open(g_android->activity->assetManager, name.c_str(), AASSET_MODE_BUFFER);
    if (!a) return false;
    off64_t n = AAsset_getLength64(a);
    out.resize(static_cast<gd::Int>(n));
    bool ok = AAsset_read(a, out.data(), static_cast<size_t>(n)) == n;
    AAsset_close(a);
    return ok;
}

bool asset_exists(const std::string& name) {
    AAsset* a = AAssetManager_open(g_android->activity->assetManager, name.c_str(), AASSET_MODE_UNKNOWN);
    if (!a) return false;
    AAsset_close(a);
    return true;
}

// ---- EGL --------------------------------------------------------------------------------------

bool egl_init_display() {
    if (g_display != EGL_NO_DISPLAY) return true;
    g_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(g_display, nullptr, nullptr)) return false;
    const EGLint attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8,
                              EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE};
    EGLint count = 0;
    eglChooseConfig(g_display, attribs, &g_config, 1, &count);
    return count > 0;
}

bool egl_make_context() {
    const EGLint ctx[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    g_context = eglCreateContext(g_display, g_config, EGL_NO_CONTEXT, ctx);
    return g_context != EGL_NO_CONTEXT;
}

// The window appeared: a surface on it (and the context, the first time or after a loss).
bool g_had_context = false;   // a context was made before (a new one means every GL object is gone)

bool egl_attach(ANativeWindow* window) {
    if (!egl_init_display()) return false;
    bool made = false;
    if (g_context == EGL_NO_CONTEXT) {
        if (!egl_make_context()) return false;
        made = true;
    }
    g_surface = eglCreateWindowSurface(g_display, g_config, window, nullptr);
    if (g_surface == EGL_NO_SURFACE) return false;
    if (!eglMakeCurrent(g_display, g_surface, g_surface, g_context)) {
        if (eglGetError() == EGL_CONTEXT_LOST) {
            eglDestroyContext(g_display, g_context);
            if (!egl_make_context() || !eglMakeCurrent(g_display, g_surface, g_surface, g_context)) return false;
            made = true;
        } else {
            return false;
        }
    }
    // Every GL object is gone with a context made again (lost here, or at a swap in the main loop):
    // the app makes its objects again.
    if (made && g_had_context && g_started) g_app->graphics_lost();
    if (made) g_had_context = true;
    eglSwapInterval(g_display, 1);
    eglQuerySurface(g_display, g_surface, EGL_WIDTH, &g_width);
    eglQuerySurface(g_display, g_surface, EGL_HEIGHT, &g_height);
    return true;
}

void egl_detach() {
    if (g_surface != EGL_NO_SURFACE) {
        eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(g_display, g_surface);
        g_surface = EGL_NO_SURFACE;
    }
}

// ---- audio ------------------------------------------------------------------------------------

aaudio_data_callback_result_t audio_data(AAudioStream* stream, void*, void* data, int32_t frames) {
    // No locks, allocations or I/O here (the real-time thread): the app mixes into the buffer.
    if (g_app && g_started) g_app->audio(static_cast<float*>(data), frames, AAudioStream_getSampleRate(stream));
    else std::fill(static_cast<float*>(data), static_cast<float*>(data) + frames * 2, 0.0f);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void audio_error(AAudioStream*, void*, aaudio_result_t error) {
    // A disconnected device (headphones out, Bluetooth gone): the main loop opens a new stream.
    if (error == AAUDIO_ERROR_DISCONNECTED) g_audio_restart = true;
}

void audio_open() {
    AAudioStreamBuilder* b = nullptr;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return;
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setDataCallback(b, audio_data, nullptr);
    AAudioStreamBuilder_setErrorCallback(b, audio_error, nullptr);
    if (AAudioStreamBuilder_openStream(b, &g_stream) == AAUDIO_OK) AAudioStream_requestStart(g_stream);
    else g_stream = nullptr;
    AAudioStreamBuilder_delete(b);
}

void audio_close() {
    if (g_stream) {
        AAudioStream_requestStop(g_stream);
        AAudioStream_close(g_stream);
        g_stream = nullptr;
    }
}

// ---- input ------------------------------------------------------------------------------------

PadButton pad_button(int32_t key, bool* ok) {
    *ok = true;
    switch (key) {
        case AKEYCODE_BUTTON_A: return PadButton::A;
        case AKEYCODE_BUTTON_B: return PadButton::B;
        case AKEYCODE_BUTTON_X: return PadButton::X;
        case AKEYCODE_BUTTON_Y: return PadButton::Y;
        case AKEYCODE_BUTTON_L1: return PadButton::L1;
        case AKEYCODE_BUTTON_R1: return PadButton::R1;
        case AKEYCODE_BUTTON_L2: return PadButton::L2;
        case AKEYCODE_BUTTON_R2: return PadButton::R2;
        case AKEYCODE_BUTTON_SELECT: return PadButton::SELECT;
        case AKEYCODE_BUTTON_START: return PadButton::START;
        case AKEYCODE_BUTTON_THUMBL: return PadButton::L3;
        case AKEYCODE_BUTTON_THUMBR: return PadButton::R3;
        case AKEYCODE_DPAD_UP: return PadButton::UP;
        case AKEYCODE_DPAD_DOWN: return PadButton::DOWN;
        case AKEYCODE_DPAD_LEFT: return PadButton::LEFT;
        case AKEYCODE_DPAD_RIGHT: return PadButton::RIGHT;
        default: *ok = false; return PadButton::A;
    }
}

bool is_pad_source(int32_t source) {
    return (source & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD || (source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK;
}

// The d-pad as a hat (HAT_X / HAT_Y) on many pads: pressed directions as buttons.
float g_hat[64][2] = {};

int32_t on_input(android_app*, AInputEvent* e) {
    if (!g_app || !g_started) return 0;
    int32_t type = AInputEvent_getType(e);
    int32_t source = AInputEvent_getSource(e);
    int device = AInputEvent_getDeviceId(e);
    if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t key = AKeyEvent_getKeyCode(e);
        bool down = AKeyEvent_getAction(e) == AKEY_EVENT_ACTION_DOWN;
        // Native: a pad's keys stay the game's. Some Xbox pad firmwares send their View button as
        // Back: from a pad it is SELECT, as the button where a DualSense has Create.
        if (key == AKEYCODE_BACK && is_pad_source(source)) key = AKEYCODE_BUTTON_SELECT;
        if (key == AKEYCODE_BACK || key == AKEYCODE_BUTTON_MODE) {
            // The system's Back (button or gesture) is the game's Escape; the app does not close on it.
            // So is a pad's PS / Xbox button, where the system lets the app have it.
            if (down && AKeyEvent_getRepeatCount(e) == 0) g_app->back();
            return 1;
        }
        bool ok = false;
        PadButton b = pad_button(key, &ok);
        if (ok && (is_pad_source(source) || key >= AKEYCODE_BUTTON_A)) {
            if (AKeyEvent_getRepeatCount(e) == 0 || !down) g_app->pad(device, b, down);
            return 1;
        }
        gd::Int godot_key = Keys::from_android_keycode(key);
        if (godot_key != 0) {
            // A held key's repeats are dropped, as on the host (the remake ignores InputEventKey echoes).
            if (!down || AKeyEvent_getRepeatCount(e) == 0) g_app->key(static_cast<int>(godot_key), down);
            return 1;
        }
        return 0;
    }
    if (type != AINPUT_EVENT_TYPE_MOTION) return 0;
    if (is_pad_source(source)) {
        g_app->pad_axis(device, 0, AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_X, 0));
        g_app->pad_axis(device, 1, AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_Y, 0));
        float lt = std::max(AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_LTRIGGER, 0), AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_BRAKE, 0));
        float rt = std::max(AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_RTRIGGER, 0), AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_GAS, 0));
        g_app->pad_axis(device, 4, lt);
        g_app->pad_axis(device, 5, rt);
        float hx = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_HAT_X, 0);
        float hy = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_HAT_Y, 0);
        float* h = g_hat[device & 63];
        auto hat = [&](float before, float now, PadButton neg, PadButton pos) {
            if ((before < -0.5f) != (now < -0.5f)) g_app->pad(device, neg, now < -0.5f);
            if ((before > 0.5f) != (now > 0.5f)) g_app->pad(device, pos, now > 0.5f);
        };
        hat(h[0], hx, PadButton::LEFT, PadButton::RIGHT);
        hat(h[1], hy, PadButton::UP, PadButton::DOWN);
        h[0] = hx;
        h[1] = hy;
        return 1;
    }
    // Native: fingers on the screen only; a DualSense's touchpad (a mouse or touchpad source) is not
    // the touch controls'.
    if ((source & AINPUT_SOURCE_TOUCHSCREEN) != AINPUT_SOURCE_TOUCHSCREEN) return 0;
    int32_t action = AMotionEvent_getAction(e);
    int32_t kind = action & AMOTION_EVENT_ACTION_MASK;
    size_t index = static_cast<size_t>((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    auto at = [&](size_t i) {
        return Pointer{AMotionEvent_getPointerId(e, i), AMotionEvent_getX(e, i), AMotionEvent_getY(e, i)};
    };
    switch (kind) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN: g_app->pointer(PointerEvent::DOWN, at(index)); break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP: g_app->pointer(PointerEvent::UP, at(index)); break;
        case AMOTION_EVENT_ACTION_CANCEL:
            for (size_t i = 0; i < AMotionEvent_getPointerCount(e); ++i) g_app->pointer(PointerEvent::CANCEL, at(i));
            break;
        case AMOTION_EVENT_ACTION_MOVE:
            for (size_t i = 0; i < AMotionEvent_getPointerCount(e); ++i) g_app->pointer(PointerEvent::MOVE, at(i));
            break;
        default: return 0;
    }
    return 1;
}

// ---- pad connections --------------------------------------------------------------------------

// AInputQueue reports no connections: the game thread asks android.view.InputDevice for the
// devices about once a second (getDeviceIds, getSources) and reports the pads that came or went
// (Input.joy_connection_changed). The thread is attached to the VM once and stays attached (the
// vibrator and the safe area then find it attached); the class and method ids are kept.
JNIEnv* g_env = nullptr;
jclass g_input_device = nullptr;   // a global reference
jmethodID g_get_ids = nullptr, g_get_device = nullptr, g_get_sources = nullptr;
std::vector<int> g_pads;           // the pad devices reported connected
double g_next_pad_poll = 0;

JNIEnv* game_thread_env() {
    if (g_env) return g_env;
    JavaVM* vm = g_android->activity->vm;
    if (vm->GetEnv(reinterpret_cast<void**>(&g_env), JNI_VERSION_1_6) != JNI_OK &&
        vm->AttachCurrentThread(&g_env, nullptr) != JNI_OK) {
        g_env = nullptr;
    }
    return g_env;
}

void poll_pad_devices(double now) {
    if (now < g_next_pad_poll) return;
    g_next_pad_poll = now + 1.0;
    JNIEnv* env = game_thread_env();
    if (!env) return;
    if (!g_input_device) {
        jclass c = env->FindClass("android/view/InputDevice");
        if (!c || env->ExceptionCheck()) {
            env->ExceptionClear();
            return;
        }
        g_input_device = static_cast<jclass>(env->NewGlobalRef(c));
        env->DeleteLocalRef(c);
        g_get_ids = env->GetStaticMethodID(g_input_device, "getDeviceIds", "()[I");
        g_get_device = env->GetStaticMethodID(g_input_device, "getDevice", "(I)Landroid/view/InputDevice;");
        g_get_sources = env->GetMethodID(g_input_device, "getSources", "()I");
        if (env->ExceptionCheck() || !g_get_ids || !g_get_device || !g_get_sources) {
            env->ExceptionClear();
            env->DeleteGlobalRef(g_input_device);
            g_input_device = nullptr;
            g_next_pad_poll = 1e300;   // never again: no connections are reported
            Log::warning("android: no android.view.InputDevice methods, pad connections not reported");
            return;
        }
    }
    auto ids = static_cast<jintArray>(env->CallStaticObjectMethod(g_input_device, g_get_ids));
    if (env->ExceptionCheck() || !ids) {
        env->ExceptionClear();
        return;
    }
    std::vector<jint> all(static_cast<size_t>(env->GetArrayLength(ids)));
    env->GetIntArrayRegion(ids, 0, static_cast<jsize>(all.size()), all.data());
    env->DeleteLocalRef(ids);
    std::vector<int> pads;
    for (jint id : all) {
        jobject device = env->CallStaticObjectMethod(g_input_device, g_get_device, id);
        if (env->ExceptionCheck() || !device) {
            env->ExceptionClear();
            continue;
        }
        jint sources = env->CallIntMethod(device, g_get_sources);
        if (env->ExceptionCheck()) env->ExceptionClear(), sources = 0;
        env->DeleteLocalRef(device);
        if (is_pad_source(sources)) pads.push_back(static_cast<int>(id));
    }
    for (int id : g_pads) {
        if (std::find(pads.begin(), pads.end(), id) != pads.end()) continue;
        g_hat[id & 63][0] = g_hat[id & 63][1] = 0;
        g_app->pad_connected(id, false);
    }
    for (int id : pads) {
        if (std::find(g_pads.begin(), g_pads.end(), id) == g_pads.end()) g_app->pad_connected(id, true);
    }
    g_pads = pads;
}

// ---- lifecycle --------------------------------------------------------------------------------

// The display cutout's safe insets (DisplayCutout.getSafeInset*, API 28+) through JNI: the window
// is laid out into the short edges' cutouts (the theme's windowLayoutInDisplayCutoutMode), so the
// game's UI keeps out of them. Zero before API 28 or while the view has no insets yet.
SafeArea safe_area(android_app* app) {
    SafeArea area;
    if (AConfiguration_getSdkVersion(app->config) < 28) return area;
    JavaVM* vm = app->activity->vm;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return area;
        attached = true;
    }
    auto call = [&](jobject o, const char* cls, const char* name, const char* sig) -> jobject {
        if (!o) return nullptr;
        jclass c = env->FindClass(cls);
        jmethodID m = c ? env->GetMethodID(c, name, sig) : nullptr;
        jobject r = m ? env->CallObjectMethod(o, m) : nullptr;
        if (env->ExceptionCheck()) env->ExceptionClear(), r = nullptr;
        return r;
    };
    jobject window = call(app->activity->clazz, "android/app/Activity", "getWindow", "()Landroid/view/Window;");
    jobject decor = call(window, "android/view/Window", "getDecorView", "()Landroid/view/View;");
    jobject insets = call(decor, "android/view/View", "getRootWindowInsets", "()Landroid/view/WindowInsets;");
    jobject cutout = call(insets, "android/view/WindowInsets", "getDisplayCutout", "()Landroid/view/DisplayCutout;");
    if (cutout) {
        jclass c = env->FindClass("android/view/DisplayCutout");
        auto inset = [&](const char* name) {
            jmethodID m = env->GetMethodID(c, name, "()I");
            int v = m ? env->CallIntMethod(cutout, m) : 0;
            if (env->ExceptionCheck()) env->ExceptionClear(), v = 0;
            return v;
        };
        area.left = inset("getSafeInsetLeft");
        area.top = inset("getSafeInsetTop");
        area.right = inset("getSafeInsetRight");
        area.bottom = inset("getSafeInsetBottom");
    }
    if (attached) vm->DetachCurrentThread();
    return area;
}

void on_command(android_app* app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window && !egl_attach(app->window)) {
                Log::error("android: no EGL surface (error " + std::to_string(eglGetError()) + ")");
            } else if (app->window) {
                if (!g_started) {
                    // The log also goes to Android/data/<package>/files/t3native.log (readable over
                    // USB or with a file manager, no adb needed).
                    if (app->activity->externalDataPath) {
                        std::string dir = app->activity->externalDataPath;
                        mkdir(dir.c_str(), 0770);
                        Log::to_file(dir + "/t3native.log");
                    }
                    Log::info(std::string("GL ") + reinterpret_cast<const char*>(glGetString(GL_VERSION)) + ", " +
                              reinterpret_cast<const char*>(glGetString(GL_RENDERER)) + ", " + std::to_string(g_width) +
                              "x" + std::to_string(g_height));
                    gd::set_asset_hooks(gd::AssetHooks{asset_read, asset_exists});
                    std::string user = app->activity->internalDataPath ? app->activity->internalDataPath : "";
                    g_started = g_app->start(gd::ASSET_PREFIX + "imported", user);
                    Log::info(g_started ? "android: game started" : "android: the game did not start");
                }
                g_app->resize(g_width, g_height, safe_area(app));
            }
            break;
        case APP_CMD_TERM_WINDOW: egl_detach(); break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
        case APP_CMD_CONTENT_RECT_CHANGED:
            if (g_surface != EGL_NO_SURFACE) {
                eglQuerySurface(g_display, g_surface, EGL_WIDTH, &g_width);
                eglQuerySurface(g_display, g_surface, EGL_HEIGHT, &g_height);
                if (g_started) g_app->resize(g_width, g_height, safe_area(app));
            }
            break;
        case APP_CMD_RESUME:
            g_paused = false;
            audio_open();
            if (g_started) g_app->paused(false);
            break;
        case APP_CMD_PAUSE:
            g_paused = true;
            audio_close();
            if (g_started) g_app->paused(true);
            break;
        case APP_CMD_DESTROY:
            if (g_started) g_app->stop();
            break;
        default: break;
    }
}

}  // namespace

namespace t3::Platform {

// android.os.Vibrator through JNI (the activity's VIBRATOR_SERVICE): one-shot pulses.
void vibrate(int milliseconds, int strength) {
    if (!g_android || milliseconds <= 0) return;
    JavaVM* vm = g_android->activity->vm;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        attached = true;
    }
    jobject activity = g_android->activity->clazz;
    jclass context = env->FindClass("android/content/Context");
    jfieldID field = env->GetStaticFieldID(context, "VIBRATOR_SERVICE", "Ljava/lang/String;");
    jobject name = env->GetStaticObjectField(context, field);
    jmethodID get_service = env->GetMethodID(context, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    jobject vibrator = env->CallObjectMethod(activity, get_service, name);
    if (vibrator) {
        jclass effect = env->FindClass("android/os/VibrationEffect");
        jmethodID one_shot = env->GetStaticMethodID(effect, "createOneShot", "(JI)Landroid/os/VibrationEffect;");
        jobject fx = env->CallStaticObjectMethod(effect, one_shot, static_cast<jlong>(milliseconds),
                                                 static_cast<jint>(strength < 1 ? 1 : (strength > 255 ? 255 : strength)));
        jclass vib = env->GetObjectClass(vibrator);
        jmethodID run = env->GetMethodID(vib, "vibrate", "(Landroid/os/VibrationEffect;)V");
        env->CallVoidMethod(vibrator, run, fx);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (attached) vm->DetachCurrentThread();
}

double now() { return monotonic(); }

// GameActivity.askText / pollText (the dialog runs on the UI thread; the answer waits in the
// activity until the game thread takes it). The activity's own class: FindClass on this thread
// would not see the app's classes.
namespace {
bool g_text_asked = false;

jmethodID activity_method(JNIEnv* env, const char* name, const char* sig) {
    jclass c = env->GetObjectClass(g_android->activity->clazz);
    jmethodID m = c ? env->GetMethodID(c, name, sig) : nullptr;
    if (env->ExceptionCheck()) env->ExceptionClear(), m = nullptr;
    return m;
}
}  // namespace

bool ask_text(const std::string& title, const std::string& initial) {
    JNIEnv* env = g_android ? game_thread_env() : nullptr;
    jmethodID m = env ? activity_method(env, "askText", "(Ljava/lang/String;Ljava/lang/String;)V") : nullptr;
    if (!m) return false;
    jstring t = env->NewStringUTF(title.c_str()), s = env->NewStringUTF(initial.c_str());
    env->CallVoidMethod(g_android->activity->clazz, m, t, s);
    env->DeleteLocalRef(t);
    env->DeleteLocalRef(s);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    g_text_asked = true;
    return true;
}

// GameActivity.padRumble: the pad's own vibrators (android.view.InputDevice), the game thread's.
void pad_rumble(int device, int weak, int strong, int milliseconds) {
    JNIEnv* env = g_android ? game_thread_env() : nullptr;
    static jmethodID m = nullptr;
    if (env && !m) m = activity_method(env, "padRumble", "(IIII)V");
    if (!m) return;
    env->CallVoidMethod(g_android->activity->clazz, m, device, weak, strong, milliseconds);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

// GameActivity.perfLine: the activity's writer thread adds the temperature and appends the line.
void perf_line(const std::string& line) {
    JNIEnv* env = g_android ? game_thread_env() : nullptr;
    static jmethodID m = nullptr;
    if (env && !m) m = activity_method(env, "perfLine", "(Ljava/lang/String;)V");
    if (!m) return;
    jstring s = env->NewStringUTF(line.c_str());
    env->CallVoidMethod(g_android->activity->clazz, m, s);
    env->DeleteLocalRef(s);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

// pollText: null while the dialog is open, "1" + the text after OK, "0" after Cancel.
TextAnswer poll_text(std::string& text) {
    if (!g_text_asked) return TEXT_NONE;
    JNIEnv* env = game_thread_env();
    jmethodID m = env ? activity_method(env, "pollText", "()Ljava/lang/String;") : nullptr;
    if (!m) return TEXT_NONE;
    auto answer = static_cast<jstring>(env->CallObjectMethod(g_android->activity->clazz, m));
    if (env->ExceptionCheck()) env->ExceptionClear(), answer = nullptr;
    if (!answer) return TEXT_NONE;
    const char* chars = env->GetStringUTFChars(answer, nullptr);
    std::string got = chars ? chars : "";
    if (chars) env->ReleaseStringUTFChars(answer, chars);
    env->DeleteLocalRef(answer);
    g_text_asked = false;
    if (got.empty() || got[0] != '1') return TEXT_CANCELLED;
    text = got.substr(1);
    return TEXT_ENTERED;
}

}  // namespace t3::Platform

void android_main(android_app* app) {
    g_android = app;
    app->onAppCmd = on_command;
    // The screen stays on while the game is in front (no input during a movie or a demonstration).
    ANativeActivity_setWindowFlags(app->activity, AWINDOW_FLAG_KEEP_SCREEN_ON, 0);
    app->onInputEvent = on_input;
    t3::GameOptions options;
    g_app = new t3::GameApp(options);
    while (true) {
        int events = 0;
        android_poll_source* source = nullptr;
        // Animate while there is a surface and the app is in front; otherwise wait for events.
        int timeout = (g_surface != EGL_NO_SURFACE && !g_paused) ? 0 : -1;
        while (ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) {
                audio_close();
                egl_detach();
                if (g_env) app->activity->vm->DetachCurrentThread();
                return;
            }
            timeout = (g_surface != EGL_NO_SURFACE && !g_paused) ? 0 : -1;
        }
        if (g_audio_restart.exchange(false) && !g_paused) {
            audio_close();
            audio_open();
        }
        if (g_started && g_surface != EGL_NO_SURFACE && !g_paused) {
            poll_pad_devices(monotonic());
            g_app->frame(monotonic());
            if (!eglSwapBuffers(g_display, g_surface) && eglGetError() == EGL_CONTEXT_LOST) {
                egl_detach();
                eglDestroyContext(g_display, g_context);
                g_context = EGL_NO_CONTEXT;
                if (app->window) egl_attach(app->window);
            }
        }
    }
}

#endif
