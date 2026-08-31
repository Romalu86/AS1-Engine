#pragma once

#include "creature.h"

namespace as1
{
    class CIV_ROBOT : public CREATURE
    {
    public:
        CIV_ROBOT(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent = nullptr);
        ~CIV_ROBOT() override;
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;
        void MoveTact() override;
        void DeletePointerToSprite(SPRITE* sprite) override;
        void performMovementTact() noexcept;
        SPRITE* findNearbyEligibleTarget() noexcept;
        void changeLinkedChildAnimationWhenIdle(int animation) noexcept;
        int rotateLinkedChildTowardDirection(std::uint8_t direction) noexcept;
        void clearRetainedTargetReference(SPRITE* sprite) noexcept;

        CIV_ROBOT* scalarDeletingDestructorCivRobot(unsigned char flags) noexcept;

        std::uint32_t behaviorState() const noexcept { return m_behaviorState; }
        void setBehaviorState(std::uint32_t value) noexcept { m_behaviorState = value; }
        SPRITE* retainedTargetSprite() const noexcept { return m_retainedTargetSprite; }
        void setRetainedTargetSprite(SPRITE* value) noexcept { m_retainedTargetSprite = value; }

    private:
        friend struct CivRobotRetailLayoutProbe;
        std::uint32_t m_behaviorState;
        SPRITE* m_retainedTargetSprite;
        std::uint32_t m_damageReactionPending;
        std::uint32_t m_targetRefreshPending;
    };

#if UINTPTR_MAX == 0xFFFFFFFFu
    struct CivRobotRetailLayoutProbe
    {
        static constexpr std::size_t behaviorStateOffset = offsetof(CIV_ROBOT, m_behaviorState);
        static constexpr std::size_t retainedTargetSpriteOffset = offsetof(CIV_ROBOT, m_retainedTargetSprite);
        static constexpr std::size_t slotB0 = offsetof(CIV_ROBOT, m_damageReactionPending);
        static constexpr std::size_t slotB4 = offsetof(CIV_ROBOT, m_targetRefreshPending);
    };
    static_assert(CivRobotRetailLayoutProbe::behaviorStateOffset == 0xA8, "CIV_ROBOT +A8 mismatch");
    static_assert(CivRobotRetailLayoutProbe::retainedTargetSpriteOffset == 0xAC, "CIV_ROBOT +AC mismatch");
    static_assert(CivRobotRetailLayoutProbe::slotB0 == 0xB0, "CIV_ROBOT +B0 mismatch");
    static_assert(CivRobotRetailLayoutProbe::slotB4 == 0xB4, "CIV_ROBOT +B4 mismatch");
    static_assert(sizeof(CIV_ROBOT) == 0xB8, "retail CIV_ROBOT allocation must be exactly 0xB8 on x86");
#endif

    int releaseSpriteListReference(SPRITE* sprite) noexcept;
}
