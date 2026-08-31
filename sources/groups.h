#pragma once

#include "base_sprite_list.h"
#include "core/types.h"
#include <cstddef>
#include <cstdint>

namespace as1
{
    class MAP;
    class RESOURCE;
    class SPRITE;

    class Group : public SPRITE_POINTER_LIST
    {
    public:
        Group(Group* insertAfter = nullptr, SPRITE* firstSprite = nullptr) noexcept;
        ~Group();

        Group(const Group&) = delete;
        Group& operator=(const Group&) = delete;

        Group* initializeAndLink(Group* insertAfter, SPRITE* firstSprite) noexcept;
        Group* scalarDeletingDestructor(unsigned char deleteSelfFlag) noexcept;
        void unlinkAndReleaseStorage() noexcept;
        int drawMemberLabel(int value) const;
        void addSprite(SPRITE* sprite);
        int drawGroupOrdinals() const;

        float centerX() const noexcept { return m_centerX; }
        float centerY() const noexcept { return m_centerY; }
        Group* nextGroup() const noexcept { return m_next; }
        void setNextGroup(Group* next) noexcept { m_next = next; }

        static constexpr std::uint32_t RETAIL_VTABLE_TOKEN = 0x0047372Cu; // reference value only; never stored live
        static std::uint32_t CurrentImageGroupVtable() noexcept;

    private:
        float m_centerX;          // +0x10
        float m_centerY;          // +0x14
        std::uint32_t m_raw18;    // +0x18: not touched by 435200/435260/435310
        std::uint32_t m_raw1C;    // +0x1C: not touched by 435200/435260/435310
        Group* m_next;            // +0x20 (x86)
    };

    // GROUPS is the sentinel Group physically embedded at Application+0x264.
    // Dynamic Group nodes are 0x24-byte allocations linked through Group+0x20.
    class GROUPS : public Group
    {
    public:
        GROUPS() noexcept;
        ~GROUPS();

        GROUPS(const GROUPS&) = delete;
        GROUPS& operator=(const GROUPS&) = delete;

        int loadFromResource(RESOURCE* map);
        int saveToResource(RESOURCE* map) const;
        void Load(RESOURCE* map);
        int Save(RESOURCE* map) const;
        void removeSpriteReferences(SPRITE* sprite);

        Group* first() noexcept;
        const Group* first() const noexcept;
        std::size_t size() const noexcept;
        bool empty() const noexcept;
        std::size_t refCount() const noexcept;
    };

#if defined(_WIN32) && !defined(_WIN64)
    static_assert(sizeof(Group) == 0x24, "Retail Group x86 ABI size must be 0x24 bytes");
    static_assert(sizeof(GROUPS) == 0x24, "Retail GROUPS sentinel x86 ABI size must be 0x24 bytes");
#endif
}
