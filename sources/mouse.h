
#pragma once

#include "sprite.h"
#include <array>
#include <cstddef>

namespace as1
{
    class MOUSE : public SPRITE
    {
    public:
        static constexpr std::size_t CursorCount = 36u;
        static constexpr int AnimatedCursorCount = 17;

        MOUSE();
        MOUSE(VID* vid, float x, float y, float z, int direction, SPRITE* parent);
        virtual ~MOUSE();


        int Action(int opcode, std::intptr_t rawVar1, int rawVar2, int rawVar3) override;
        int dispatchMouseActionOpcode(int opcode, std::intptr_t rawVar1, int rawVar2, int rawVar3) noexcept;


        MOUSE* scalarDeletingDestructor(unsigned char flags) noexcept;
        void destroyMouseState();
        void HardwareOn();
        void HardwareOff();
        void setCursorId(int cursorId);
        void setCursorAnimation(int animationId);

        // Public cursor animation name retained for callers.
        void ChangeAnimation(int animationId) { setCursorAnimation(animationId); }

        int currentCursorId() const noexcept { return currentAnimation(); }
        void setCurrentCursorIdDirect(int value) noexcept { setCurrentAnimationDirect(value); }
        int hardwareCursorEnabled() const noexcept { return m_hardwareCursorEnabled; }
        void setHardwareCursorEnabled(int value) noexcept { m_hardwareCursorEnabled = value; }
        int cursorHandlesLoaded() const noexcept { return m_cursorHandlesLoaded; }
        void setCursorHandlesLoaded(int value) noexcept { m_cursorHandlesLoaded = value; }
        void* cursorHandle(std::size_t index) const noexcept;
        void setCursorHandle(std::size_t index, void* handle) noexcept;

    private:
        // Retail MOUSE+0x48 aliases the inherited SPRITE animation slot.
        // Do not add storage here: the derived tail begins at +0x70.
        int m_hardwareCursorEnabled = 1;
        std::array<void*, CursorCount> m_cursorHandles{};
        int m_cursorHandlesLoaded = 0;
    };

    extern MOUSE* Mouse;
    MOUSE*& mouseInstanceRef() noexcept;
    SPRITE* mouseSprite() noexcept;

}
