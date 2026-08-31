#include "avia.h"
#include "map.h"
#include "core/application.h"
#include "graphics/angle.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <new>

namespace as1
{
    AVIA::AVIA(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent)
        : UNIT(owner, vid, xyz, direction, parent)
    {
        StartMove();
        setZSpeedDirect(Vid()->maximumZSpeed());
    }

    namespace
    {

        std::uint32_t manFrameDeltaMilliseconds(const VID* vid) noexcept
        {
            const std::uint32_t delta = as1::core::CurrentTimeMilliseconds() - as1::core::PreviousWorldTimeMilliseconds();
            // Retail AVIA/BALLOON AI paths dereference [this+0x1C] unconditionally
            // before reading VID+0x2A2; a null VID is a fault boundary, not a
            // zero-default timing value.
            const std::uint32_t frameDefault = static_cast<std::uint32_t>(vid->defaultFrameSpeed());
            return std::max(delta, frameDefault);
        }

        int manRandModulo(int divisor) noexcept
        {
            return std::rand() % divisor;
        }

        bool x87LessOrUnordered(double lhs, double rhs) noexcept
        {
            // FCOM/FCOMPP followed by `test ah,1` treats unordered as C0=1.
            return std::isnan(lhs) || std::isnan(rhs) || lhs < rhs;
        }

        bool manActionCoordinateOutsideMap(float x, float y, const MAP* map) noexcept
        {
            return x87LessOrUnordered(x, -120.0) ||
                   x87LessOrUnordered(y, -100.0) ||
                   x87LessOrUnordered(static_cast<double>(map->SizeX()) + 120.0, x) ||
                   x87LessOrUnordered(static_cast<double>(map->SizeY()) + 100.0, y);
        }
    }

    int AVIA::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        switch (opcode)
        {
        case 0x32:
            return 0;

        case 0x33:
            ChangeAnimation(0x0F);
            return 0;

        case 0x34:
        {
            const int deltaX = static_cast<int>(argument1Carrier);
            const int deltaY = argument2Carrier;
            const int deltaZ = argument3Carrier;
            ChangeCoor(X() + static_cast<float>(deltaX), Y() + static_cast<float>(deltaY), Z() + static_cast<float>(deltaZ));
            if (manActionCoordinateOutsideMap(X(), Y(), mapOwner()))
                ChangeAnimation(0x0F);
            return 0;
        }

        case 0x82:
            updateFlightBehavior(static_cast<int>(argument1Carrier), argument2Carrier);
            return 0;

        case 0x21:
        {
            const float targetX = static_cast<float>(static_cast<int>(argument1Carrier));
            const float targetY = static_cast<float>(argument2Carrier);
            MAP* const map = mapOwner();
            const float targetZ =
                map->GetGroundZ(Vid(), VECTOR2{targetX, targetY}, ANGLE{}) + 80.0f;
            SPRITE* const helper = new (std::nothrow) SPRITE(
                map, MAP::NullVid(), VECTOR(targetX, targetY, targetZ), ANGLE(0), nullptr);
            Move(helper);
            return 0;
        }

        case 0x55:
            // Retail case 0x55 is a direct tail to dispatchExtendedSpriteActionOpcode, not UNIT::Action.
            return dispatchExtendedSpriteActionOpcode(opcode,
                              static_cast<int>(argument1Carrier),
                              argument2Carrier,
                              argument3Carrier);

        default:
            return dispatchBaseActionOpcode(opcode,
                              static_cast<int>(argument1Carrier),
                              argument2Carrier,
                              argument3Carrier);
        }
    }

    int AVIA::updateFlightIdleBehavior() noexcept
    {
        const int animation = currentAnimation();
        if (animation == 4)
        {
            if (manRandModulo(3) == 0)
                ChangeAnimation(2);
            else
                ChangeDirection(directionIndex() - 0x20);
        }
        else if (animation == 5)
        {
            if (manRandModulo(3) == 0)
                ChangeAnimation(2);
            else
                ChangeDirection(directionIndex() + 0x20);
        }
        else if (animation == 2)
        {
            if (manRandModulo(11) == 0)
                ChangeAnimation(manRandModulo(2) == 0 ? 5 : 4);
        }
        else
        {
            ChangeAnimation(2);
        }

        // Retail keeps both quotient and remainder from the *same* rand() value:
        // EAX is rand()/21 on the non-trigger path, while only a zero remainder
        // enters the target-acquisition branch.  Returning rand()%21 here changes
        // the AVIA/BALLOON AI state machine even though the trigger probability is
        // the same.
        const int randomValue = std::rand();
        int result = randomValue / 21;
        if ((randomValue % 21) == 0 && (behaviorFlags() & 1) != 0)
        {
            if (SPRITE* const target = SeekEnemy())
                result = SetCommand(4, target);
            else
                result = 0;
        }
        return result;
    }

    int AVIA::updateFlightCombatBehavior() noexcept
    {
        const std::uint32_t deltaMs = manFrameDeltaMilliseconds(Vid());
        int result = computeAttackDecisionCode(deltaMs);
        setAttackDecisionCode(result);

        if ((behaviorFlags() & 1) == 0)
            return result;

        if (result == 5)
        {
            const int randomValue = std::rand();
            result = randomValue / 21;
            if ((randomValue % 21) != 0)
                return result;
        }

        if (SPRITE* const target = SeekEnemy())
            return SetCommand(4, target);
        return 0;
    }

    int AVIA::faceFlightTargetAndUpdateCombat() noexcept
    {
        SPRITE* const target = goalSprite();
        const std::uint32_t deltaMs = manFrameDeltaMilliseconds(Vid());
        const int dx = static_cast<int>(target->X() - X());
        const int dy = static_cast<int>(target->Y() - Y());
        const int direction = AngleFromXY(dx, dy, nullptr) & 0xFF;
        const int turnResult = RotateTact(direction, deltaMs);

        if (turnResult == 0)
            ChangeAnimation(2);

        return updateFlightCombatBehavior();
    }

    void AVIA::updateAltitudeState() noexcept
    {
        VID* const vid = Vid();
        const float maxZSpeed = vid->maximumZSpeed();

        const float lowerGround = mapOwner()->GetGroundZ(vid, VECTOR2{X(), Y()}, Direction());
        const double lower = static_cast<double>(lowerGround) +
                             static_cast<double>(vid->moveUpZ()) - 10.0;
        // `test ah,41h` after fcomp: only ordered-greater reaches +maxZSpeed.
        if (!std::isnan(lower) && !std::isnan(Z()) && lower > static_cast<double>(Z()))
        {
            setZSpeedDirect(maxZSpeed);
            return;
        }

        const float upperGround = mapOwner()->GetGroundZ(vid, VECTOR2{X(), Y()}, Direction());
        const double upper = static_cast<double>(upperGround) +
                             static_cast<double>(vid->moveUpZ()) + 10.0;
        // `test ah,1`: ordered-less OR unordered selects -maxZSpeed.
        if (std::isnan(upper) || std::isnan(Z()) || upper < static_cast<double>(Z()))
        {
            setZSpeedDirect(-maxZSpeed);
            return;
        }
        setZSpeedDirect(0.0f);
    }

    int AVIA::updateFlightAuxiliaryBehavior() noexcept
    {
        return 0;
    }

    int AVIA::probeForwardFlightObstacle() noexcept
    {
        const DWORD direction = directionIndex();
        const float x = X() + rawDirectionSinUnchecked(direction) * 128.0f;
        const float y = Y() - rawDirectionCosUnchecked(direction) * 128.0f;
        return probeMovementFootprint(x, y) ? 1 : 0;
    }

    void AVIA::chooseFlightAvoidanceTurn() noexcept
    {
        const int probeDirection = (directionIndex() + 0x10) & 0xFF;
        const float x = X() + rawDirectionSin(probeDirection) * 128.0f;
        const float y = Y() - rawDirectionCos(probeDirection) * 128.0f;
        if (probeMovementFootprint(x, y))
        {
            ChangeAnimation(5);
            ChangeDirection(directionIndex() + 0x10);
            return;
        }

        ChangeAnimation(4);
        ChangeDirection(directionIndex() - 0x10);
    }

    int AVIA::updateFlightBehavior(int behaviorArgument1, int behaviorArgument2) noexcept
    {
        (void)behaviorArgument1;
        (void)behaviorArgument2;
        int result = currentAnimation();
        if (result >= 0x0F || result == 0x0C)
            return result;

        if (result >= 7 && result != 0x0A)
            ChangeAnimation(0);

        updateAltitudeState();
        (void)updateFlightAuxiliaryBehavior();

        SPRITE* const backlink = childBacklink();
        result = static_cast<int>(reinterpret_cast<std::uintptr_t>(backlink) & 0xFFFFFFFFu);
        if (backlink)
            return result;

        if ((runtimeFlags() & 0x00000080u) == 0u)
            setRuntimeFlags(runtimeFlags() | 0x00000080u);

        if (goalSprite() != nullptr)
            return updateFlightCombatBehavior();
        return updateFlightIdleBehavior();
    }

}
