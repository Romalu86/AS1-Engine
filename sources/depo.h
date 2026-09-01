#pragma once

#include "unit.h"

namespace as1
{
    class DEPO : public UNIT
    {
    public:
        DEPO(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent = nullptr);
        ~DEPO() override;
        DEPO* depoScalarDeletingDestructor(unsigned char flags) noexcept;
        void destroyDepoState() noexcept;
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;
        void MoveTact() override;
        void updateDepoAnimationTact() noexcept;
        int enqueueDepoPurchase(int nvid) noexcept;
        void startNextDepoBuild() noexcept;
        int spawnNextDepoUnit(int actionArgument1, int actionArgument2) noexcept;

        std::uint32_t createdEngineSequence() const noexcept { return m_createdEngineSequence; }
        std::uint32_t activeQueueCursor() const noexcept { return m_activeQueueCursor; }
        std::uint32_t queueCapacity() const noexcept { return m_queueCapacity; }
        std::uint32_t queueCount() const noexcept { return m_queueCount; }
        std::uint16_t queuedNvidAt(int index) const noexcept;
        std::uint32_t queuedBuildTimeAt(int index) const noexcept;
        std::uint32_t queuedCompletionFlagAt(int index) const noexcept;

    private:
        friend struct DepoRetailLayoutProbe;
        std::uint32_t m_createdEngineSequence = 0;                 // +0xA0
        std::array<std::uint16_t, 20> m_queuedNvids{};   // +0xA4..+0xCB
        std::array<std::uint8_t, 0xA0> m_reservedAfterQueuedNvids;      // untouched retail gap
        std::array<std::uint32_t, 20> m_queuedBuildTimes{}; // +0x16C..+0x1BB
        std::array<std::uint8_t, 0x140> m_reservedAfterBuildTimes;    // untouched retail gap
        std::array<std::uint32_t, 20> m_queuedCompletionFlags{}; // +0x2FC..+0x34B
        std::array<std::uint8_t, 0x140> m_reservedAfterCompletionFlags;    // untouched retail gap
        std::uint32_t m_activeQueueCursor = 0;
        std::uint32_t m_queueCapacity = 0;
        std::uint32_t m_queueCount = 0;
    };

#if UINTPTR_MAX == 0xFFFFFFFFu
    struct DepoRetailLayoutProbe
    {
        static constexpr std::size_t slotA0 = offsetof(DEPO, m_createdEngineSequence);
        static constexpr std::size_t wordA4 = offsetof(DEPO, m_queuedNvids);
        static constexpr std::size_t dword16C = offsetof(DEPO, m_queuedBuildTimes);
        static constexpr std::size_t dword2FC = offsetof(DEPO, m_queuedCompletionFlags);
        static constexpr std::size_t slot48C = offsetof(DEPO, m_activeQueueCursor);
        static constexpr std::size_t slot490 = offsetof(DEPO, m_queueCapacity);
        static constexpr std::size_t slot494 = offsetof(DEPO, m_queueCount);
    };
#endif
}
