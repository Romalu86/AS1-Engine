#include "creature.h"
#include "core/application.h"
#include "map.h"
#include "sprite_collector_hash.h"
#include "graphics/angle.h"
#include "vid/vid.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace as1
{
    CREATURE::CREATURE(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent)
        : UNIT(owner, vid, xyz, direction, parent)
    {
        setZSpeedDirect(vid->maximumZSpeed());
        m_regionTrackingFlags |= 1u;
        StartMove();
        m_currentRegion = findContainingRegion(X(), Y());
    }

    namespace
    {

        std::uint32_t creatureFrameDeltaMilliseconds(const VID* vid) noexcept
        {
            const std::uint32_t delta = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
            const std::uint32_t minDelta = static_cast<std::uint32_t>(vid->defaultFrameSpeed());
            return std::max(delta, minDelta);
        }

        bool creatureX87EqualOrUnordered(double lhs, double rhs) noexcept
        {
            return std::isnan(lhs) || std::isnan(rhs) || lhs == rhs;
        }

        bool creatureX87LessOrUnordered(long double lhs, long double rhs) noexcept
        {
            return std::isnan(lhs) || std::isnan(rhs) || lhs < rhs;
        }
    }

    int CREATURE::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        const int actionArgument1 = static_cast<int>(argument1Carrier);
        const int actionArgument2 = argument2Carrier;
        const int actionArgument3 = argument3Carrier;
        const int op = opcode;

        switch (op)
        {
        case 0x32:
        case 0x34:
            ChangeSpeed(0.0f);
            if (turnTimer() == 0)
                setTurnTimer(10);
            return 0;

        case 0x55:
            if (actionArgument1 > 0)
            {
                for (SPRITE* candidate = GlobalHashFirstInBoxAroundDot(X() - 150.0f, Y() - 150.0f,
                                                                       X() + 150.0f, Y() + 150.0f);
                     candidate;
                     candidate = GlobalHashNextInBoxAroundDot())
                {
                    // Retail dereferences candidate VID unconditionally here.
                    VID* const candidateVid = candidate->Vid();
                    if (candidateVid->spriteClassId() != B_CREATURE)
                        continue;
                    if (creatureX87LessOrUnordered(
                            approximatePlanarDistance(X() - candidate->X(), Y() - candidate->Y()), 150.0L))
                        candidate->StartMove();
                }
            }
            return dispatchBaseActionOpcode(opcode, actionArgument1, actionArgument2, actionArgument3);

        case 0x82:
        {
            int animation = currentAnimation();
            if (animation >= 15)
                return 0;
            if (animation == 8 || animation == 13)
                ChangeAnimation(0);

            if ((m_regionTrackingFlags & 1u) != 0u && m_currentRegion == nullptr)
            {
                m_regionTrackingFlags &= ~1u;
                m_currentRegion = findContainingRegion(X(), Y());
            }

            const DWORD actionBits = runtimeFlags() & SPRITE::CommandBitsMask;
            if (goalSprite() != nullptr && actionBits == 4u)
            {
                if (!creatureX87EqualOrUnordered(Speed(), 0.0) && currentAnimation() != 2)
                    ChangeAnimation(2);
            }
            else
            {
                const std::uint32_t now = core::CurrentTimeMilliseconds();
                if (actionBits == 12u || actionBits == 16u || (now & ~0x7FFu) > applicationBucketTime())
                {
                    const int decision = computeAttackDecisionCode(creatureFrameDeltaMilliseconds(Vid()));
                    setAttackDecisionCode(decision);

                    if (decision == 1 && creatureX87EqualOrUnordered(Speed(), 0.0))
                    {
                        bool stop = goalSprite() != nullptr;
                        if (!stop)
                        {
                            SPRITE* const child = childChain();
                            if (child)
                            {
                                // Retail loads self VID only after the child-null
                                // test, compares child VID to [selfVID+0x5C], and
                                // then dereferences that child VID for +0x220/+0x40.
                                // Preserve those exact null/fault boundaries.
                                VID* const selfVid = Vid();
                                VID* const childVid = child->Vid();
                                if (childVid == selfVid->linkedVid())
                                {
                                    stop = childVid->hasWeaponChildDescriptor() != 0u &&
                                           childVid->weaponCount() != 0u &&
                                           child->goalSprite() != nullptr;
                                }
                            }
                        }
                        if (stop)
                            SetCommand(0, nullptr);
                    }

                    if ((decision == 2 || decision == 4) &&
                        (behaviorFlags() & 1) != 0 &&
                        (actionBits == 0u || actionBits == 4u))
                    {
                        if (SPRITE* const target = SeekEnemy())
                            SetCommand(4, target);
                    }
                }

                const DWORD currentActionBits = runtimeFlags() & SPRITE::CommandBitsMask;
                if (currentActionBits != 4u && currentActionBits != 12u && currentActionBits != 16u)
                {
                    if (currentAnimation() == 4 && (std::rand() % 3) != 0)
                        RotateTact(directionIndex() - 32, creatureFrameDeltaMilliseconds(Vid()));
                    if (currentAnimation() == 5 && (std::rand() % 3) != 0)
                        RotateTact(directionIndex() + 32, creatureFrameDeltaMilliseconds(Vid()));

                    const std::uint32_t now2 = core::CurrentTimeMilliseconds();
                    if ((now2 & ~0x7FFu) > applicationBucketTime())
                    {
                        if ((std::rand() % 4) == 0)
                        {
                            StartMove();
                        }
                        else if (creatureX87EqualOrUnordered(Speed(), 0.0) &&
                                 (std::rand() % 3) == 0 &&
                                 Vid()->hasAnimation12Content())
                        {
                            ChangeAnimation(12);
                        }
                        else if ((std::rand() % 4) == 0)
                        {
                            if ((std::rand() % 2) == 0)
                                RotateTact(directionIndex() + 32, creatureFrameDeltaMilliseconds(Vid()));
                            else
                                RotateTact(directionIndex() - 32, creatureFrameDeltaMilliseconds(Vid()));
                        }
                        else
                        {
                            Stop();
                        }
                    }
                }
            }

            animation = currentAnimation();
            if (animation == 13 || animation == 9)
                ChangeAnimation(0);
            return 0;
        }

        default:
            return dispatchBaseActionOpcode(opcode, actionArgument1, actionArgument2, actionArgument3);
        }
    }

    SPRITE* CREATURE::findContainingRegion(float x, float y) noexcept
    {
        core::ApplicationDrawDispatcherState& state = core::GlobalApplicationDrawDispatcherState();
        int cursor = state.drawPassBucket(7).count();
        for (SPRITE* candidate = core::Application::previousSpriteOfTypeInDrawPass(state, 7, &cursor, 0x40);
             candidate;
             candidate = core::Application::previousSpriteOfTypeInDrawPass(state, 7, &cursor, 0x40))
        {
            VID* const candidateVid = candidate->Vid();
            if (candidateVid->spriteClassId() != B_REGION)
                continue;

            const REGION* const region = static_cast<const REGION*>(candidate);
            const float halfX = region->regionWidth() * 0.5f;
            if (candidate->X() - halfX > x || x > candidate->X() + halfX)
                continue;
            const float halfY = region->regionHeight() * 0.5f;
            if (candidate->Y() - halfY <= y && y <= candidate->Y() + halfY)
                return candidate;
        }
        return nullptr;
    }

    void CREATURE::MoveTact()
    {
        performCreatureMovementTact();
    }

    void CREATURE::performCreatureMovementTact() noexcept
    {
        if ((m_regionTrackingFlags & 1u) != 0u && m_currentRegion == nullptr)
        {
            m_regionTrackingFlags &= ~1u;
            m_currentRegion = findContainingRegion(X(), Y());
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
                    RotateTact(directionIndex() - 0x40, deltaMs);
                else if (animation == 5)
                    RotateTact(directionIndex() + 0x40, deltaMs);
                else if ((std::rand() & 0x80000001) == 0)
                    RotateTact(directionIndex() - 0x40, deltaMs);
                else
                    RotateTact(directionIndex() + 0x40, deltaMs);
            }
            setTurnTimer(remainingTurnTicks >= 0 ? remainingTurnTicks - 1 : remainingTurnTicks + 1);
        }
        else if (SPRITE* const target = goalSprite())
        {
            if (Speed() != 0.0f)
            {
                const int reverse = Speed() < 0.0f ? 0x80 : 0;
                const int dx = static_cast<int>(target->X() - X());
                const int dy = static_cast<int>(target->Y() - Y());
                const int desired = (AngleFromXY(dx, dy, nullptr) + reverse) & 0xFF;
                RotateTact(GlideDirection(desired), deltaMs);
                const DWORD flags = runtimeFlags();
                if ((flags & 0x00002000u) != 0u && (flags & 0x00004000u) != 0u)
                    Stop();
            }
        }

        VID* const vid = Vid();
        MAP* const owner = mapOwner();
        if ((vid->properties() & P_ZEROZ) != 0u)
            candidate.z = owner->GetGroundZ(vid, VECTOR2{candidate.x, candidate.y}, Direction());

        if (CanPlaceWithCrush(candidate.x, candidate.y, candidate.z) != nullptr)
        {
            dispatchVirtualAction(ActionCode::ACT_PATH_BLOCK,
                                     static_cast<int>(candidate.x - X()),
                                     static_cast<int>(candidate.y - Y()),
                                     static_cast<int>(candidate.z - Z()));
            return;
        }

        if (m_currentRegion != nullptr)
        {
            const REGION* const region = static_cast<const REGION*>(m_currentRegion);
            const float halfX = region->regionWidth() * 0.5f;
            const float halfY = region->regionHeight() * 0.5f;
            if (candidate.x < region->X() - halfX || candidate.x > region->X() + halfX ||
                candidate.y < region->Y() - halfY || candidate.y > region->Y() + halfY)
            {
                SPRITE* const nextRegion = findContainingRegion(candidate.x, candidate.y);
                if (!nextRegion || nextRegion->Vid() != m_currentRegion->Vid())
                {
                    dispatchVirtualAction(ActionCode::ACT_PATH_BLOCK,
                                             static_cast<int>(candidate.x - X()),
                                             static_cast<int>(candidate.y - Y()),
                                             static_cast<int>(candidate.z - Z()));
                    return;
                }
                m_currentRegion = nextRegion;
            }
        }

        steerAwayFromMapBoundary(candidate.x, candidate.y);
        ChangeCoor(candidate.x, candidate.y, candidate.z);
    }

    void CREATURE::DeletePointerToSprite(SPRITE* sprite)
    {
        clearCreatureRegionReference(sprite);
    }

    void CREATURE::clearCreatureRegionReference(SPRITE* sprite) noexcept
    {
        if (m_currentRegion == sprite)
            m_currentRegion = nullptr;
        SPRITE::DeletePointerToSprite(sprite);
    }
}
