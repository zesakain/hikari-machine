/// @file sdl_window.cpp
/// @brief SDL3 concrete window implementation.

#include "sdl_window.h"

#include "core/assert.h"

#include <array>
#include <string>

namespace eng::platform {

namespace {
constexpr const char* kWindowPtrProp = "Hikari_WindowPtr";
} // namespace

SdlWindow::SdlWindow(SDL_Window* window, uint32_t width, uint32_t height) noexcept
    : window_(window), width_(width), height_(height) {
    ENG_ASSERT(window_ != nullptr, "SdlWindow requires a valid SDL_Window handle");
    const SDL_PropertiesID props = SDL_GetWindowProperties(window_);
    if (props != 0) {
        SDL_SetPointerProperty(props, kWindowPtrProp, this);
    }
    // Enable OS text input events by default so text characters and IME events are delivered
    SDL_StartTextInput(window_);
}

SdlWindow::~SdlWindow() noexcept {
    if (window_ != nullptr) {
        const SDL_PropertiesID props = SDL_GetWindowProperties(window_);
        if (props != 0) {
            SDL_SetPointerProperty(props, kWindowPtrProp, nullptr);
        }
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
}

uint32_t SdlWindow::getWidth() const noexcept {
    if (window_ == nullptr) {
        return width_;
    }
    int w = 0;
    int h = 0;
    if (SDL_GetWindowSize(window_, &w, &h)) {
        return static_cast<uint32_t>(w);
    }
    return width_;
}

uint32_t SdlWindow::getHeight() const noexcept {
    if (window_ == nullptr) {
        return height_;
    }
    int w = 0;
    int h = 0;
    if (SDL_GetWindowSize(window_, &w, &h)) {
        return static_cast<uint32_t>(h);
    }
    return height_;
}

bool SdlWindow::isMinimized() const noexcept {
    if (window_ == nullptr) {
        return false;
    }
    const SDL_WindowFlags flags = SDL_GetWindowFlags(window_);
    return (flags & SDL_WINDOW_MINIMIZED) != 0;
}

bool SdlWindow::shouldClose() const noexcept {
    return shouldClose_;
}

void SdlWindow::setShouldClose(bool close) noexcept {
    shouldClose_ = close;
}

void SdlWindow::setTitle(std::string_view title) {
    if (window_ == nullptr) {
        return;
    }
    // Small stack buffer to avoid allocation for standard window titles
    if (title.size() < 256) {
        std::array<char, 256> buf{};
        for (size_t i = 0; i < title.size(); ++i) {
            buf[i] = title[i];
        }
        buf[title.size()] = '\0';
        SDL_SetWindowTitle(window_, buf.data());
    } else {
        const std::string nullTerminated(title);
        SDL_SetWindowTitle(window_, nullTerminated.c_str());
    }
}

void SdlWindow::setSize(uint32_t width, uint32_t height) {
    if (window_ == nullptr) {
        return;
    }
    width_ = width;
    height_ = height;
    SDL_SetWindowSize(window_, static_cast<int>(width), static_cast<int>(height));
}

void* SdlWindow::getNativeHandle() const noexcept {
    return static_cast<void*>(window_);
}

void SdlWindow::startTextInput() {
    if (window_ != nullptr) {
        SDL_StartTextInput(window_);
    }
}

void SdlWindow::stopTextInput() {
    if (window_ != nullptr) {
        SDL_StopTextInput(window_);
    }
}

bool SdlWindow::isTextInputActive() const noexcept {
    if (window_ == nullptr) {
        return false;
    }
    return SDL_TextInputActive(window_);
}

void SdlWindow::setTextInputArea(int32_t x, int32_t y, int32_t width, int32_t height) {
    if (window_ != nullptr) {
        SDL_Rect rect{
            .x = x,
            .y = y,
            .w = width,
            .h = height,
        };
        SDL_SetTextInputArea(window_, &rect, 0);
    }
}

} // namespace eng::platform
