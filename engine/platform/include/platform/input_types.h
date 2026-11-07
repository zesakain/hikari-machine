/// @file input_types.h
/// @brief Strongly-typed enumeration definitions for keyboard, mouse, and gamepad inputs.
///
/// Purpose:
/// Provides hardware-neutral, zero-overhead strongly-typed enums for all input devices.
/// Decouples engine and gameplay code from underlying windowing libraries (such as SDL3).
///
/// Ownership and Lifetime:
/// Value types; trivial copy and assignment with zero heap allocation.
///
/// Thread-safety:
/// Pure types; completely thread-safe.
///
/// Performance:
/// Fits in primitive register types (uint16_t / uint8_t); zero overhead.

#pragma once

#include <cstdint>

namespace eng::platform {

/// @enum KeyCode
/// @brief Standardized keyboard scan codes representing physical key locations.
///
/// Underlying values map safely to fixed-size bitsets (up to 512 keys).
enum class KeyCode : uint16_t {
    Unknown = 0,

    // Letters (A-Z)
    A = 4,
    B = 5,
    C = 6,
    D = 7,
    E = 8,
    F = 9,
    G = 10,
    H = 11,
    I = 12,
    J = 13,
    K = 14,
    L = 15,
    M = 16,
    N = 17,
    O = 18,
    P = 19,
    Q = 20,
    R = 21,
    S = 22,
    T = 23,
    U = 24,
    V = 25,
    W = 26,
    X = 27,
    Y = 28,
    Z = 29,

    // Numbers (top row 1-0)
    Num1 = 30,
    Num2 = 31,
    Num3 = 32,
    Num4 = 33,
    Num5 = 34,
    Num6 = 35,
    Num7 = 36,
    Num8 = 37,
    Num9 = 38,
    Num0 = 39,

    // Controls and whitespace
    Return = 40,
    Escape = 41,
    Backspace = 42,
    Tab = 43,
    Space = 44,

    Minus = 45,
    Equals = 46,
    LeftBracket = 47,
    RightBracket = 48,
    Backslash = 49,
    Semicolon = 51,
    Apostrophe = 52,
    Grave = 53,
    Comma = 54,
    Period = 55,
    Slash = 56,

    CapsLock = 57,

    // Function keys
    F1 = 58,
    F2 = 59,
    F3 = 60,
    F4 = 61,
    F5 = 62,
    F6 = 63,
    F7 = 64,
    F8 = 65,
    F9 = 66,
    F10 = 67,
    F11 = 68,
    F12 = 69,

    // Navigation and editing
    PrintScreen = 70,
    ScrollLock = 71,
    Pause = 72,
    Insert = 73,
    Home = 74,
    PageUp = 75,
    Delete = 76,
    End = 77,
    PageDown = 78,
    Right = 79,
    Left = 80,
    Down = 81,
    Up = 82,

    // Modifiers
    LeftCtrl = 224,
    LeftShift = 225,
    LeftAlt = 226,
    LeftGui = 227,
    RightCtrl = 228,
    RightShift = 229,
    RightAlt = 230,
    RightGui = 231,

    Count = 512
};

/// @enum MouseButton
/// @brief Physical mouse buttons.
enum class MouseButton : uint8_t { Left = 1, Middle = 2, Right = 3, X1 = 4, X2 = 5, Count = 8 };

/// @enum GamepadButton
/// @brief Standardized digital buttons for gamepad controllers.
enum class GamepadButton : uint8_t {
    South = 0, // A on Xbox, Cross on PS
    East = 1,  // B on Xbox, Circle on PS
    West = 2,  // X on Xbox, Square on PS
    North = 3, // Y on Xbox, Triangle on PS
    Back = 4,
    Guide = 5,
    Start = 6,
    LeftStick = 7,
    RightStick = 8,
    LeftBumper = 9,
    RightBumper = 10,
    DpadUp = 11,
    DpadDown = 12,
    DpadLeft = 13,
    DpadRight = 14,
    Count = 16
};

/// @enum GamepadAxis
/// @brief Standardized analog axes for gamepad controllers.
enum class GamepadAxis : uint8_t {
    LeftX = 0,
    LeftY = 1,
    RightX = 2,
    RightY = 3,
    TriggerLeft = 4,
    TriggerRight = 5,
    Count = 6
};

} // namespace eng::platform
