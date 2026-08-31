#pragma once

#include "sprite.h"

namespace as1
{
    class UNIT : public SPRITE
    {
    public:
        UNIT(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent = nullptr);
        ~UNIT() override;
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;
        void MoveTact() override;
        void DrawDebugOverlay() override;
        void drawBaseDebugOverlayThunk();

    private:
        friend struct UnitRetailLayoutProbe;
        // Retail initializeCommandSpriteState physical tail.  UNIT is allocated with exactly
        // 0xA0 bytes in createSpriteViaApplicationFactory; these slots therefore must follow the
        // 0x70-byte SPRITE prefix in-place, not in SpriteHostState.
        int m_sharedPrimaryState;                         // +0x70 = -1 (initializeExtendedSpriteState)
        int m_sharedSecondaryState;                         // +0x74 = 0
        int m_legacyCommandState0;                         // +0x78 = 0
        int m_legacyCommandState1;                         // +0x7C = 0
        int m_ammoFixedPoint;                         // +0x80 fixed-point weapon counter
        int m_legacyCommandState2;                         // +0x84 = -1
        int m_turnTimer;                         // +0x88 turn timer = 0
        int m_behaviorFlags;                         // +0x8C behavior/weapon value
        std::uint32_t m_commandWordListVtable;
        std::uint32_t m_commandWordCount;      // +0x94
        std::uint32_t m_commandWordCapacity;   // +0x98
        std::int16_t* m_commandWords;        // +0x9C
    };

#if UINTPTR_MAX == 0xFFFFFFFFu
    struct UnitRetailLayoutProbe
    {
        static constexpr std::size_t slot70 = offsetof(UNIT, m_sharedPrimaryState);
        static constexpr std::size_t slot74 = offsetof(UNIT, m_sharedSecondaryState);
        static constexpr std::size_t slot78 = offsetof(UNIT, m_legacyCommandState0);
        static constexpr std::size_t slot7C = offsetof(UNIT, m_legacyCommandState1);
        static constexpr std::size_t slot80 = offsetof(UNIT, m_ammoFixedPoint);
        static constexpr std::size_t slot84 = offsetof(UNIT, m_legacyCommandState2);
        static constexpr std::size_t slot88 = offsetof(UNIT, m_turnTimer);
        static constexpr std::size_t slot8C = offsetof(UNIT, m_behaviorFlags);
        static constexpr std::size_t slot90 = offsetof(UNIT, m_commandWordListVtable);
        static constexpr std::size_t slot94 = offsetof(UNIT, m_commandWordCount);
        static constexpr std::size_t slot98 = offsetof(UNIT, m_commandWordCapacity);
        static constexpr std::size_t slot9C = offsetof(UNIT, m_commandWords);
    };
    static_assert(UnitRetailLayoutProbe::slot70 == 0x70, "UNIT +70 mismatch");
    static_assert(UnitRetailLayoutProbe::slot74 == 0x74, "UNIT +74 mismatch");
    static_assert(UnitRetailLayoutProbe::slot78 == 0x78, "UNIT +78 mismatch");
    static_assert(UnitRetailLayoutProbe::slot7C == 0x7C, "UNIT +7C mismatch");
    static_assert(UnitRetailLayoutProbe::slot80 == 0x80, "UNIT +80 mismatch");
    static_assert(UnitRetailLayoutProbe::slot84 == 0x84, "UNIT +84 mismatch");
    static_assert(UnitRetailLayoutProbe::slot88 == 0x88, "UNIT +88 mismatch");
    static_assert(UnitRetailLayoutProbe::slot8C == 0x8C, "UNIT +8C mismatch");
    static_assert(UnitRetailLayoutProbe::slot90 == 0x90, "UNIT +90 mismatch");
    static_assert(UnitRetailLayoutProbe::slot94 == 0x94, "UNIT +94 mismatch");
    static_assert(UnitRetailLayoutProbe::slot98 == 0x98, "UNIT +98 mismatch");
    static_assert(UnitRetailLayoutProbe::slot9C == 0x9C, "UNIT +9C mismatch");
    static_assert(sizeof(UNIT) == 0xA0, "retail UNIT allocation must be exactly 0xA0 on x86");
#endif
}
