#include "unit.h"
#include "map.h"
#ifdef _WIN32
#include "win/application_win.h"
#endif
#include "core/application.h"
#include "graphics/angle.h"

#include <cstdlib>
#include <cmath>
#include <cstdint>

namespace
{
    int retailFtolLow32Unit(float value) noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        std::int64_t converted = 0;
        unsigned short oldControl = 0;
        unsigned short truncControl = 0;
        __asm
        {
            fld dword ptr [value]
            fstcw word ptr [oldControl]
            fwait
            mov ax, word ptr [oldControl]
            or ah, 0Ch
            mov word ptr [truncControl], ax
            fldcw word ptr [truncControl]
            fistp qword ptr [converted]
            fldcw word ptr [oldControl]
        }
        return static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(converted)));
#else
        const long double d = static_cast<long double>(value);
        if (!std::isfinite(d) ||
            d < static_cast<long double>(INT64_MIN) ||
            d > static_cast<long double>(INT64_MAX))
            return 0;
        const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
        return static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(converted)));
#endif
    }

    int retailFsubFtolLow32Unit(float lhs, float rhs) noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        std::int64_t converted = 0;
        unsigned short oldControl = 0;
        unsigned short truncControl = 0;
        __asm
        {
            fld dword ptr [lhs]
            fsub dword ptr [rhs]
            fstcw word ptr [oldControl]
            fwait
            mov ax, word ptr [oldControl]
            and ax, 0F3FFh
            or ax, 0C00h
            mov word ptr [truncControl], ax
            fldcw word ptr [truncControl]
            fistp qword ptr [converted]
            fldcw word ptr [oldControl]
        }
        return static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(converted)));
#else
        const long double value = static_cast<long double>(lhs) - static_cast<long double>(rhs);
        if (!std::isfinite(value) ||
            value < static_cast<long double>(INT64_MIN) ||
            value > static_cast<long double>(INT64_MAX))
            return 0;
        const std::int64_t converted = static_cast<std::int64_t>(std::trunc(value));
        return static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(converted)));
#endif
    }

    int retailFsubStoreF32FtolLow32Unit(float lhs, float rhs) noexcept
    {
        // The retail movement routine spills the X delta through a binary32 temporary before __ftol,
        // while the Y delta stays live in x87.  Preserve that asymmetric spill.
#if defined(_MSC_VER) && defined(_M_IX86)
        float stored = 0.0f;
        __asm
        {
            fld dword ptr [lhs]
            fsub dword ptr [rhs]
            fstp dword ptr [stored]
        }
        return retailFtolLow32Unit(stored);
#else
        const float stored = static_cast<float>(
            static_cast<long double>(lhs) - static_cast<long double>(rhs));
        return retailFtolLow32Unit(stored);
#endif
    }

    bool retailX87C3EqualUnit(float lhs, float rhs) noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        unsigned short status = 0;
        __asm
        {
            fld dword ptr [lhs]
            fcomp dword ptr [rhs]
            fnstsw ax
            mov word ptr [status], ax
        }
        return (status & 0x4000u) != 0u;
#else
        // FCOMP sets C3 for equality and for unordered operands.
        return lhs == rhs || std::isnan(lhs) || std::isnan(rhs);
#endif
    }
}

namespace as1
{
    UNIT::UNIT(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent)
        : SPRITE(owner, vid, xyz, direction, parent)
    {
        m_sharedPrimaryState = -1;
        m_sharedSecondaryState = 0;
        m_commandWordListVtable = currentCommandWordListVtable();
        m_commandWords = nullptr;
        m_commandWordCount = 0;
        m_commandWordCapacity = 0;
        m_commandStack.markCommandWordsPhysicalOwner(true);
        m_legacyCommandState2 = -1;
        m_legacyCommandState0 = 0;
        m_legacyCommandState1 = 0;
        m_turnTimer = 0;

        const VID* valueOwner = vid;
        if (childChain())
        {
            VID* const childVid = childChain()->Vid();
            VID* const linkVid = vid->linkedVid();
            if (childVid == linkVid &&
                childVid->hasWeaponChildDescriptor() != 0u &&
                childVid->weaponCount() != 0u)
                valueOwner = linkVid;
        }
        m_behaviorFlags = valueOwner->weaponIntAt(0x34);

        const VID* counterOwner = vid;
        if (VID* const linkVid = vid->linkedVid())
        {
            if (linkVid->hasWeaponChildDescriptor() != 0u &&
                linkVid->weaponCount() != 0u)
                counterOwner = linkVid;
        }
        m_ammoFixedPoint = counterOwner->weaponRecordAmmoCapacity() << 6;
    }

    UNIT::~UNIT()
    {
#ifdef _WIN32
        win::applicationWinInstance()->transferFrom(this);
#else
        if (MAP* const owner = mapOwner())
            owner->releaseSpriteReferencesHost(this);
#endif
        setCommandWordListVtable(currentCommandWordListVtable());
        m_commandStack.releaseCommandWordsRetailTail();
        m_commandStack.markCommandWordsPhysicalOwner(false);
    }

    int UNIT::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        const int argument1 = static_cast<int>(argument1Carrier);
        const int argument2 = argument2Carrier;
        const int argument3 = argument3Carrier;
        return dispatchBaseActionOpcode(opcode, argument1, argument2, argument3);
    }

    void UNIT::MoveTact()
    {
        VID* const vid = Vid();
        if (vid->spriteClassId() != B_UNIT && vid->spriteClassId() != B_AVIA)
        {
            performBaseMovementTact();
            return;
        }

        VECTOR candidate{X(), Y(), Z()};
        computeNextMovementPosition(&candidate.x, &candidate.y, &candidate.z);
        const std::uint32_t deltaMs = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();

        int remainingTurnTicks = turnTimer();
        if (remainingTurnTicks != 0)
        {
            if ((remainingTurnTicks & 1) != 0)
            {
                const int animation = currentAnimation();
                if (animation == 4)
                {
                    RotateTact(directionIndex() - 0x40, deltaMs);
                }
                else if (animation == 5)
                {
                    RotateTact(directionIndex() + 0x40, deltaMs);
                }
                else if ((std::rand() & 0x80000001) == 0)
                {
                    RotateTact(directionIndex() - 0x40, deltaMs);
                }
                else
                {
                    RotateTact(directionIndex() + 0x40, deltaMs);
                }
            }

            if (remainingTurnTicks < 0)
                ++remainingTurnTicks;
            else
                --remainingTurnTicks;
            setTurnTimer(remainingTurnTicks);
        }
        else if (SPRITE* const target = goalSprite())
        {
            const float speed = Speed();
            if (speed != 0.0f && !std::isnan(speed))
            {
                const int invertDirection = speed < 0.0f ? 0x80 : 0;
                const int dx = retailFsubStoreF32FtolLow32Unit(target->X(), X());
                const int dy = retailFsubFtolLow32Unit(target->Y(), Y());
                const int targetDirection = AngleFromXY(dx, dy, nullptr) + invertDirection;
                RotateTact(GlideDirection(targetDirection), deltaMs);

                const DWORD flags = runtimeFlags();
                if ((flags & 0x00002000u) != 0u && (flags & 0x00004000u) != 0u)
                    Stop();
            }
        }

        if (retailX87C3EqualUnit(X(), candidate.x) &&
            retailX87C3EqualUnit(Y(), candidate.y) &&
            retailX87C3EqualUnit(Z(), candidate.z))
            return;

        if (CanPlaceWithCrushAndGlide(&candidate.x, &candidate.y, &candidate.z) == nullptr)
        {
            steerAwayFromMapBoundary(candidate.x, candidate.y);
            ChangeCoor(candidate.x, candidate.y, candidate.z);
            return;
        }

        if (turnTimer() == 0)
            setTurnTimer(10);
    }

    void UNIT::DrawDebugOverlay()
    {
        drawBaseDebugOverlayThunk();
    }

    void UNIT::drawBaseDebugOverlayThunk()
    {
        // Retail drawBaseDebugOverlayThunk is exactly a tail jump to drawBaseDebugOverlay.
        SPRITE::drawBaseDebugOverlay();
    }

}
