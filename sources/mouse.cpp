
#include "mouse.h"

#include "core/log.h"
#include "core/application.h"
#include "map.h"
#include "sprite_collector_hash.h"

#include <algorithm>
#include <new>
#include <cstring>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace as1
{
    namespace
    {
        constexpr const char* kCursorDirectory = "cursores\\";
        constexpr const char* kCursorExtension = ".ani";

        constexpr const char* kCursorNames[MOUSE::CursorCount] = {
            "arrow",
            "noammo",
            "move",
            "clash",
            "repair",
            "attack",
            "farattack",
            "select",
            "nomove",
            "cycle",
            "link",
            "unlink",
            "cantmove",
            "patrol",
            "delete",
            "capture",
            "mine",
            "unmine",
            "small-arrow",
            "small-noammo",
            "small-move",
            "small-taran",
            "small-repair",
            "small-attack",
            "small-farattack",
            "small-select",
            "small-nomove",
            "cycle",
            "small-link",
            "small-unlink",
            "small-cantmove",
            "small-patrol",
            "delete",
            "small-capture",
            "small-mine",
            "small-unmine",
        };

        float mouseFildToF32(std::int32_t value) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            float result = 0.0f;
            __asm
            {
                fild dword ptr [value]
                fstp dword ptr [result]
            }
            return result;
#else
            return static_cast<float>(value);
#endif
        }

#ifdef _WIN32
        HCURSOR asCursor(void* handle) noexcept
        {
            return static_cast<HCURSOR>(handle);
        }

        void* fromCursor(HCURSOR handle) noexcept
        {
            return static_cast<void*>(handle);
        }
#else
        void* asCursor(void* handle) noexcept
        {
            return handle;
        }

        void* fromCursor(void* handle) noexcept
        {
            return handle;
        }
#endif
    }

    MOUSE* Mouse = nullptr;

    MOUSE*& mouseInstanceRef() noexcept
    {
        return Mouse;
    }

    SPRITE* mouseSprite() noexcept
    {
        return static_cast<SPRITE*>(Mouse);
    }

    MOUSE::MOUSE()
        : MOUSE(MAP::NullVid(), 0.0f, 0.0f, 0.0f, 0, nullptr)
    {
    }

    MOUSE::MOUSE(VID* vid, float x, float y, float z, int direction, SPRITE* parent)
        : SPRITE(nullptr, vid, VECTOR{x, y, z}, ANGLE(direction), parent)
    {

        setCursorHandlesLoaded(0);
        m_cursorHandles.fill(nullptr);
        setHardwareCursorEnabled(1);

        if (Vid() != MAP::NullVid() || childChain() != nullptr)
            removeFromDrawBucketsRecursive();

        if (Vid() == MAP::NullVid())
            setListReferenceCount(listReferenceCount() + 1);
    }

    MOUSE::~MOUSE()
    {
        // Language destructor represents destroyMouseState prefix; SPRITE::~SPRITE
        // performs the final destroyBaseSpriteState exactly once.
        LOG::Write("Mouse  release");
    }

    MOUSE* MOUSE::scalarDeletingDestructor(unsigned char flags) noexcept
    {

        MOUSE* const self = this;
        destroyMouseState();
        if ((flags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    void MOUSE::destroyMouseState()
    {

        LOG::Write("Mouse  release");
        destroyBaseSpriteState();
    }

    void* MOUSE::cursorHandle(std::size_t index) const noexcept
    {
        if (index >= m_cursorHandles.size())
            return nullptr;
        return m_cursorHandles[index];
    }

    void MOUSE::setCursorHandle(std::size_t index, void* handle) noexcept
    {
        if (index < m_cursorHandles.size())
            m_cursorHandles[index] = handle;
    }

    void MOUSE::HardwareOn()
    {

        if (m_cursorHandlesLoaded)
            return;

        m_cursorHandlesLoaded = 1;
        if (!m_hardwareCursorEnabled)
            return;

#ifdef _WIN32
        for (std::size_t i = 0; i < m_cursorHandles.size(); ++i)
        {
            if (m_cursorHandles[i])
            {
                ::DestroyCursor(asCursor(m_cursorHandles[i]));
                m_cursorHandles[i] = nullptr;
            }

            const char* name = kCursorNames[i];
            if (name && name[0])
            {
                std::string fileName;
                fileName.reserve(std::strlen(kCursorDirectory) + std::strlen(name) + std::strlen(kCursorExtension) + 1);
                fileName += kCursorDirectory;
                fileName += name;
                fileName += kCursorExtension;
                m_cursorHandles[i] = fromCursor(::LoadCursorFromFileA(fileName.c_str()));
            }

            if (!m_cursorHandles[i])
                m_cursorHandles[i] = fromCursor(::LoadCursorA(nullptr, IDC_ARROW));
        }

        ::SetCursor(nullptr);
        ::SetCursor(asCursor(m_cursorHandles[static_cast<std::size_t>(currentCursorId())]));
#else
        // Non-Windows syntax/test build: preserve slot and ownership semantics only.
        for (void*& slot : m_cursorHandles)
            slot = nullptr;
#endif
    }

    void MOUSE::HardwareOff()
    {

        if (!m_cursorHandlesLoaded)
            return;

        m_cursorHandlesLoaded = 0;
        if (!m_hardwareCursorEnabled)
            return;

#ifdef _WIN32
        ::SetCursor(nullptr);
        for (void*& slot : m_cursorHandles)
        {
            if (slot)
            {
                ::DestroyCursor(asCursor(slot));
                slot = nullptr;
            }
        }
#else
        for (void*& slot : m_cursorHandles)
            slot = nullptr;
#endif
    }

    int MOUSE::Action(int opcode, std::intptr_t rawVar1, int rawVar2, int rawVar3)
    {
        return dispatchMouseActionOpcode(opcode, rawVar1, rawVar2, rawVar3);
    }

    int MOUSE::dispatchMouseActionOpcode(int opcode, std::intptr_t rawVar1, int rawVar2, int rawVar3) noexcept
    {

        if (opcode == 0x3D)
        {
            setCursorId(static_cast<int>(rawVar1));
            return 0;
        }

        if (opcode == 0x3F)
        {
            // Retail FILDs all three integer arguments and then calls
            // ChangeCoor with binary32 stack temporaries.
            ChangeCoor(mouseFildToF32(static_cast<int>(rawVar1)),
                       mouseFildToF32(rawVar2),
                       mouseFildToF32(rawVar3));
            return 0;
        }

        if (opcode != 0x3E)
            return dispatchActionOpcode(static_cast<std::uint32_t>(opcode),
                              static_cast<int>(rawVar1), rawVar2, rawVar3);

        const int vidIndex = static_cast<int>(rawVar1);
        core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
        if (vidIndex < 0 || vidIndex >= table.count())
            return 0;

        VID* const replacement = table.slot(vidIndex);
        if (!replacement)
            return 0;

        VID* const current = Vid();
        if (current && current->nvid() == vidIndex)
            return 0;

        // Exact retail ordering: remove draw/list ownership, execute the base
        // ACT_CHANGE_VID command, remove this+child-chain from the global
        // spatial hash, then rebuild draw/list ownership.
        addToDrawBucketsRecursive();
        (void)dispatchActionOpcode(static_cast<std::uint32_t>(ActionCode::ACT_CHANGE_VID), vidIndex, rawVar2, rawVar3);
        for (SPRITE* node = this; node; node = node->childChain())
            (void)RemoveSpriteFromGlobalHashForActionSwitch(node);
        removeFromDrawBucketsRecursive();
        return 0;
    }

    void MOUSE::setCursorId(int cursorId)
    {

        if (m_hardwareCursorEnabled)
        {
#ifdef _WIN32
            if (currentCursorId() != cursorId && m_cursorHandlesLoaded)
                ::SetCursor(asCursor(m_cursorHandles[static_cast<std::size_t>(cursorId)]));
#endif
            if (cursorId < AnimatedCursorCount)
            {
                setCursorAnimation(cursorId);
                return;
            }

            setCurrentCursorIdDirect(cursorId);
            return;
        }

        setCursorAnimation(cursorId);
    }

    void MOUSE::setCursorAnimation(int animationId)
    {
        // Retail setCursorId dispatches directly into the inherited
        // SPRITE::ChangeAnimation/setCursorAnimation owner for cursor ids < 17 (and
        // for every id while hardware cursors are disabled).
        SPRITE::ChangeAnimation(animationId);
    }
}
