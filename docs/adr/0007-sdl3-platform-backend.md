# ADR-0007: Adoption of SDL3 for Platform Abstraction

- **Status**: Accepted
- **Date**: 2026-11-07
- **Authors**: zesakain

## Context

A fast-paced action 3D game engine requires robust, low-latency operating system abstractions covering:
- Window lifecycle, resizing, and fullscreen switching.
- High-frequency keyboard and raw mouse relative motion.
- Comprehensive gamepad and controller support (analog triggers, sticks, haptics, hot-plugging).
- Display mode enumeration and platform event polling.

Per ADR-0006, any third-party dependency requires a formal Architecture Decision Record validating necessity, memory characteristics, exception/RTTI compatibility, and license compliance before integration.

## Decision

Adopt **SDL3** (Simple DirectMedia Layer 3, version >= 3.2.0) as the backend implementation for `engine/platform`.

Enforce the following architectural rules for the integration:
1. **Strict Encapsulation**: SDL3 headers (`<SDL3/SDL.h>`) must reside exclusively within private implementation files (`engine/platform/src/`). Public engine-facing headers (`engine/platform/include/platform/`) must contain zero SDL types or symbols.
2. **Exception and RTTI Compatibility**: SDL3 is written in pure C99/C11, natively supporting Hikari's `-fno-exceptions` and `-fno-rtti` compiler flags.
3. **Deterministic Input Snapshots**: Raw SDL events are polled and translated into a value-type `InputSnapshot` per frame. No dynamic heap allocations are performed during input polling or translation.
4. **Headless Execution Support**: In headless environments (CI runners, automated simulation tests), the platform layer configures SDL hints (`SDL_HINT_VIDEO_DRIVER=dummy`/offscreen) to allow headless lifecycle and input testing without requiring a physical display server.
5. **License Compliance**: SDL3 is distributed under the zlib license, which is fully compatible with Hikari's Apache 2.0 license.

## Consequences

### Positive
- Unified, well-tested support for modern desktop platforms (Linux X11/Wayland, Windows Win32, macOS Cocoa).
- First-class gamepad mapping database with automatic controller hot-plug detection.
- Excellent Vulkan surface integration (`SDL_Vulkan_CreateSurface`) for future Phase 4 development.
- Zero SDL leaks into high-level engine modules.

### Negative
- Introduces an external binary dependency that must be present on target dev systems or fetched via CMake.

## Alternatives Considered

- **GLFW**: Lightweight and popular, but gamepad handling and hot-plugging are less comprehensive than SDL3, and SDL3 offers better long-term Wayland and modern OS platform support.
- **Native OS APIs directly (Win32, X11, Wayland, Cocoa)**: High engineering overhead, massive platform-specific boilerplate, and complex maintenance without gameplay benefit.
- **SDL2**: Predecessor to SDL3, but lacks modern Wayland native support, redesigned cleaner API surfaces, and precision input improvements found in SDL3.
