#pragma once

#include "core/base_stream.h"
#include "core/types.h"
#include <cstdint>
#include <cstddef>

namespace as1::input
{
    struct InputWindowMessageContext;

    struct InputMessageState
    {
        std::uint32_t flags = 0;
        int wheelDelta = 0;
        float worldX = 0.0f;
        float worldY = 0.0f;
        float clientX = 0.0f;
        float clientY = 0.0f;
        std::uint32_t lastCode = 0;

        InputMessageState* initializePreservingPersistentFlags() noexcept;
        void resetFrameState();
        void clearTransientButtons();
        std::uint32_t clearLeftButtonState();
        std::uint32_t clearRightButtonState();
        void clearFirstButtonTransient();
        void clearSecondButtonTransient();

        int handleWindowMessage(std::uintptr_t hwnd, std::uint32_t message,
                       std::uint32_t wParam, std::uint32_t lParam,
                       const InputWindowMessageContext* context);

        int writeRawState(BaseStream* stream) const;
        int readRawState(BaseStream* stream);
    };
    static_assert(sizeof(InputMessageState) == 0x1C, "retail InputMessageState must remain 28 bytes");
    static_assert(offsetof(InputMessageState, flags) == 0x00, "InputMessageState flags must stay at +0x00");
    static_assert(offsetof(InputMessageState, worldX) == 0x08, "InputMessageState worldX must stay at +0x08");
    static_assert(offsetof(InputMessageState, worldY) == 0x0C, "InputMessageState worldY must stay at +0x0C");
    static_assert(offsetof(InputMessageState, clientX) == 0x10, "InputMessageState clientX must stay at +0x10");
    static_assert(offsetof(InputMessageState, clientY) == 0x14, "InputMessageState clientY must stay at +0x14");
    static_assert(offsetof(InputMessageState, lastCode) == 0x18, "InputMessageState lastCode must stay at +0x18");

    struct InputControlKeys
    {

        std::uint32_t left0 = 0x25;
        std::uint32_t left1 = 0x25;
        std::uint32_t right0 = 0x27;
        std::uint32_t right1 = 0x27;
        std::uint32_t up0 = 0x26;
        std::uint32_t up1 = 0x26;
        std::uint32_t down0 = 0x28;
        std::uint32_t down1 = 0x28;
        std::uint32_t first0 = 1;
        std::uint32_t first1 = 1;
        std::uint32_t second0 = 2;
        std::uint32_t second1 = 2;
        std::uint32_t firstMouseReleaseClears = 1;
        std::uint32_t secondMouseReleaseClears = 1;
    };
    static_assert(offsetof(InputControlKeys, left0) == 0x00, "InputControlKeys::left0 offset mismatch");
    static_assert(offsetof(InputControlKeys, left1) == 0x04, "InputControlKeys::left1 offset mismatch");
    static_assert(offsetof(InputControlKeys, right0) == 0x08, "InputControlKeys::right0 offset mismatch");
    static_assert(offsetof(InputControlKeys, right1) == 0x0C, "InputControlKeys::right1 offset mismatch");
    static_assert(offsetof(InputControlKeys, up0) == 0x10, "InputControlKeys::up0 offset mismatch");
    static_assert(offsetof(InputControlKeys, up1) == 0x14, "InputControlKeys::up1 offset mismatch");
    static_assert(offsetof(InputControlKeys, down0) == 0x18, "InputControlKeys::down0 offset mismatch");
    static_assert(offsetof(InputControlKeys, down1) == 0x1C, "InputControlKeys::down1 offset mismatch");
    static_assert(offsetof(InputControlKeys, first0) == 0x20, "InputControlKeys::first0 offset mismatch");
    static_assert(offsetof(InputControlKeys, first1) == 0x24, "InputControlKeys::first1 offset mismatch");
    static_assert(offsetof(InputControlKeys, second0) == 0x28, "InputControlKeys::second0 offset mismatch");
    static_assert(offsetof(InputControlKeys, second1) == 0x2C, "InputControlKeys::second1 offset mismatch");
    static_assert(offsetof(InputControlKeys, firstMouseReleaseClears) == 0x30, "InputControlKeys::firstMouseReleaseClears offset mismatch");
    static_assert(offsetof(InputControlKeys, secondMouseReleaseClears) == 0x34, "InputControlKeys::secondMouseReleaseClears offset mismatch");
    static_assert(sizeof(InputControlKeys) == 0x38, "InputControlKeys size mismatch");

    struct InputWindowRect
    {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
    };

    struct InputWindowMessageContext
    {
        using GetWindowRectFn = bool (*)(std::uintptr_t hwnd, InputWindowRect& rect, void* user);
        void* user = nullptr;
        GetWindowRectFn getWindowRect = nullptr;
        float viewportMinX = 0.0f;
        float viewportMaxX = 0.0f;
        float viewportMinY = 0.0f;
        float viewportMaxY = 0.0f;
        float worldOffsetX = 0.0f;
        float worldOffsetY = 0.0f;
        bool hasViewport = false;
    };

    struct InputWindowGlobals
    {
        int windowPositionX = 0;
        int windowPositionY = 0;
    };

    InputControlKeys& inputControlKeys();
    std::uint32_t& relativeControlEnabled() noexcept;
    InputWindowGlobals& inputWindowGlobals();

    int HandleInputWindowMessage(InputMessageState& state,
                                    std::uintptr_t hwnd,
                                    std::uint32_t message,
                                    std::uint32_t wParam,
                                    std::uint32_t lParam,
                                    const InputWindowMessageContext* context = nullptr);
}
