#pragma once
// Godot's Key codes (physical keycodes), the codes the remake's settings file stores the keyboard
// layouts in; each platform translates its own key codes to these.
#include "gd/gd.h"

namespace t3::Keys {

constexpr gd::Int SPECIAL = 1 << 22;
constexpr gd::Int ESCAPE = SPECIAL | 0x01, TAB = SPECIAL | 0x02, BACKSPACE = SPECIAL | 0x04, ENTER = SPECIAL | 0x05,
                  KP_ENTER = SPECIAL | 0x06, LEFT = SPECIAL | 0x0F, UP = SPECIAL | 0x10, RIGHT = SPECIAL | 0x11,
                  DOWN = SPECIAL | 0x12, SHIFT = SPECIAL | 0x15, CTRL = SPECIAL | 0x16, ALT = SPECIAL | 0x18,
                  F1 = SPECIAL | 0x1C, F3 = SPECIAL | 0x1E, F11 = SPECIAL | 0x26;
constexpr gd::Int SPACE = 32, SEMICOLON = 59;
constexpr gd::Int letter(char c) { return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c; }   // KEY_A ... KEY_Z, KEY_0 ... KEY_9

// The Windows virtual-key code's Godot key (0: none).
inline gd::Int from_windows_vk(int vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return vk;
    if (vk >= 0x70 && vk <= 0x7B) return F1 + (vk - 0x70);   // VK_F1 ... VK_F12
    switch (vk) {
        case 0x1B: return ESCAPE;
        case 0x09: return TAB;
        case 0x08: return BACKSPACE;
        case 0x0D: return ENTER;
        case 0x25: return LEFT;
        case 0x26: return UP;
        case 0x27: return RIGHT;
        case 0x28: return DOWN;
        case 0x10: return SHIFT;
        case 0x11: return CTRL;
        case 0x12: return ALT;
        case 0x20: return SPACE;
        case 0xBA: return SEMICOLON;   // VK_OEM_1
        default: return 0;
    }
}

// The Android AKEYCODE_* code's Godot key (0: none).
inline gd::Int from_android_keycode(int k) {
    if (k >= 29 && k <= 54) return 'A' + (k - 29);   // AKEYCODE_A ... AKEYCODE_Z
    if (k >= 7 && k <= 16) return '0' + (k - 7);     // AKEYCODE_0 ... AKEYCODE_9
    if (k >= 131 && k <= 142) return F1 + (k - 131); // AKEYCODE_F1 ... AKEYCODE_F12
    switch (k) {
        case 111: return ESCAPE;
        case 61: return TAB;
        case 67: return BACKSPACE;
        case 66: return ENTER;
        case 160: return KP_ENTER;
        case 21: return LEFT;
        case 19: return UP;
        case 22: return RIGHT;
        case 20: return DOWN;
        case 59: case 60: return SHIFT;
        case 113: case 114: return CTRL;
        case 57: case 58: return ALT;
        case 62: return SPACE;
        case 74: return SEMICOLON;
        default: return 0;
    }
}

}  // namespace t3::Keys
