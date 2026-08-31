#pragma once

#include "sprite.h"
#include "core/weak_controller.h"

namespace as1
{
    class RAIL : public SPRITE
    {
    public:
        RAIL(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent = nullptr);
        ~RAIL() override;
        RAIL* railScalarDeletingDestructor(unsigned char flags) noexcept;
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;
        void MoveTact() override;
        int dispatchRailActionOpcode(int opcode, int actionArgument1, float actionArgument2, int actionArgument3);
        int handleRailDamageAction(int actionArgument1, float actionArgument2, int actionArgument3);
        int rebuildRailNodes(int direction, float moveUpZ);
        void destroyRailState() noexcept;
        void handleRailNodeReleased(std::uintptr_t ownerHandle) noexcept;
        void setRailNodes(core::WeakController* slot78, core::WeakController* slot7C) noexcept;
        core::WeakController* firstRailNode() const noexcept { return m_firstRailNode; }
        core::WeakController* secondRailNode() const noexcept { return m_secondRailNode; }

    private:
        int m_sharedPrimaryState = -1;
        int m_sharedSecondaryState = 0;
        core::WeakController* m_firstRailNode = nullptr;
        core::WeakController* m_secondRailNode = nullptr;
    };
#if UINTPTR_MAX == 0xFFFFFFFFu
    static_assert(sizeof(RAIL) == 0x80, "retail RAIL allocation must be 0x80 on x86");
#endif
}
