#include "groups.h"

#include "core/resource.h"
#include "core/application.h"
#include "graph.h"
#include "map.h"
#include "sprite.h"

#include <cstdint>
#include <new>

namespace as1
{
    namespace
    {
        constexpr std::int32_t END_GROUP_INT = -1;

        class GroupVtableOwner final
        {
        public:
            virtual Group* deletingDestructor(unsigned char flags) noexcept
            {
                return reinterpret_cast<Group*>(this)->scalarDeletingDestructor(flags);
            }
        };

        std::uint32_t currentImageGroupVtable() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            static GroupVtableOwner owner;
            return static_cast<std::uint32_t>(*reinterpret_cast<const std::uintptr_t*>(&owner));
#else
            return 0u;
#endif
        }
    }

    std::uint32_t Group::CurrentImageGroupVtable() noexcept
    {
        return currentImageGroupVtable();
    }

    Group::Group(Group* insertAfter, SPRITE* firstSprite) noexcept
    {
        initializeAndLink(insertAfter, firstSprite);
    }

    Group::~Group()
    {
        // Native retail reaches unlinkAndReleaseStorage through Group's deleting destructor.
        // Host C++ subsequently runs SPRITE_POINTER_LIST::~SPRITE_POINTER_LIST;
        // unlinkAndReleaseStorage has already nulled the storage, so that compiler-generated
        // host tail is inert. Native scalar-deleting-destructor ABI remains a
        // Compiler support item; this does not create a second ownership model.
        unlinkAndReleaseStorage();
    }

    Group* Group::initializeAndLink(Group* insertAfter, SPRITE* firstSprite) noexcept
    {
        m_items = nullptr;
        m_count = 0;
        m_capacity = 0;
        setRetailVtableToken(CurrentImageGroupVtable());

        if (insertAfter)
        {
            m_next = insertAfter->m_next;
            insertAfter->m_next = this;
        }
        else
        {
            m_next = this;
        }

        if (firstSprite)
            addSprite(firstSprite);
        return this;
    }

    Group* Group::scalarDeletingDestructor(unsigned char deleteSelfFlag) noexcept
    {
        // Scalar deleting destructor releases group storage first,
        // then operator delete only when bit 0 is set.
        Group* const self = this;
        unlinkAndReleaseStorage();
        if ((deleteSelfFlag & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    void Group::unlinkAndReleaseStorage() noexcept
    {
        
        setRetailVtableToken(CurrentImageGroupVtable());
        Group* const next = m_next;
        Group* predecessor = next;
        for (Group* cursor = next->m_next; cursor != this; cursor = cursor->m_next)
            predecessor = cursor;
        predecessor->m_next = next;

        setRetailVtableToken(SPRITE_LIST::CurrentImageRelationListVtable());
        if (m_items)
            ::operator delete(m_items);
        m_items = nullptr;
        m_count = 0;
    }

    int Group::drawMemberLabel(int value) const
    {
        GRAPH* const graph = GRAPH::CurrentGraph();
        const auto& drawState = core::GlobalApplicationDrawDispatcherState();
        int result = static_cast<int>(m_count);
        for (int index = 0; index < result; ++index)
        {
            SPRITE* const sprite = m_items[static_cast<std::size_t>(index)];
            const float screenX = sprite->X() - drawState.cameraShiftX();
            const float screenY = sprite->Y() - sprite->Z() - drawState.cameraShiftY();
            graph->DrawText(screenX, screenY, "%i", value);
            result = static_cast<int>(m_count);
        }
        return result;
    }

    void Group::addSprite(SPRITE* sprite)
    {
        if (m_count != 0)
        {
            m_centerX = (sprite->X() + m_centerX) * 0.5f;
            m_centerY = (sprite->Y() + m_centerY) * 0.5f;
            append(sprite);
        }
        else
        {
            m_centerX = sprite->X();
            m_centerY = sprite->Y();
            append(sprite);
        }
    }

    int Group::drawGroupOrdinals() const
    {
        const Group* current = (m_next != this) ? m_next : nullptr;
        int ordinal = 0;
        int result = static_cast<int>(reinterpret_cast<std::uintptr_t>(m_next) & 0xFFFFFFFFu);
        while (current)
        {
            result = current->drawMemberLabel(ordinal++);
            current = current->m_next;
            if (current == this)
                current = nullptr;
        }
        return result;
    }

    GROUPS::GROUPS() noexcept
        : Group(nullptr, nullptr)
    {
    }

    GROUPS::~GROUPS() = default;

    Group* GROUPS::first() noexcept
    {
        Group* const node = nextGroup();
        return node != this ? node : nullptr;
    }

    const Group* GROUPS::first() const noexcept
    {
        const Group* const node = nextGroup();
        return node != this ? node : nullptr;
    }

    std::size_t GROUPS::size() const noexcept
    {
        std::size_t result = 0;
        for (const Group* node = first(); node; ++result)
        {
            const Group* const next = node->nextGroup();
            node = next != this ? next : nullptr;
        }
        return result;
    }

    bool GROUPS::empty() const noexcept
    {
        return nextGroup() == this;
    }

    std::size_t GROUPS::refCount() const noexcept
    {
        std::size_t result = 0;
        for (const Group* node = first(); node;)
        {
            result += node->count();
            const Group* const next = node->nextGroup();
            node = next != this ? next : nullptr;
        }
        return result;
    }

    int GROUPS::loadFromResource(RESOURCE* resource)
    {
        MAP* const mapOwner = MAP::Current();

        auto readGroupSprite = [mapOwner, resource]() -> SPRITE*
        {
            std::int32_t raw = END_GROUP_INT;
            resource->read(&raw, 4u);
            if (raw == END_GROUP_INT)
                return reinterpret_cast<SPRITE*>(static_cast<std::intptr_t>(-1));
            return mapOwner->ResolveRelationHandle(raw);
        };

        SPRITE* firstSprite = readGroupSprite();
        while (firstSprite != reinterpret_cast<SPRITE*>(static_cast<std::intptr_t>(-1)))
        {
            void* const memory = ::operator new(sizeof(Group), std::nothrow);
            Group* group = memory ? new (memory) Group(this, firstSprite) : nullptr;

            SPRITE* member = readGroupSprite();
            while (member != reinterpret_cast<SPRITE*>(static_cast<std::intptr_t>(-1)))
            {
                group->addSprite(member);
                member = readGroupSprite();
            }
            firstSprite = readGroupSprite();
        }
        return END_GROUP_INT;
    }

    int GROUPS::saveToResource(RESOURCE* resource) const
    {
        for (const Group* node = first(); node;)
        {
            for (std::size_t i = 0; i < node->count(); ++i)
            {
                const std::uint32_t raw = static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(node->at(i)) & 0xFFFFFFFFu);
                resource->write(&raw, 4u);
            }
            std::int32_t terminator = END_GROUP_INT;
            resource->write(&terminator, 4u);

            const Group* const next = node->nextGroup();
            node = next != this ? next : nullptr;
        }
        std::int32_t terminator = END_GROUP_INT;
        return resource->write(&terminator, 4u);
    }

    void GROUPS::Load(RESOURCE* resource)
    {
        (void)loadFromResource(resource);
    }

    int GROUPS::Save(RESOURCE* resource) const
    {
        return saveToResource(resource);
    }

    void GROUPS::removeSpriteReferences(SPRITE* sprite)
    {
        Group* node = first();
        while (node)
        {
            if (node->removeAndReleaseReference(sprite) != 0 || node->activeCount() != 0)
            {
                Group* const next = node->nextGroup();
                if (next == this)
                    return;
                node = next;
            }
            else
            {
                Group* const next = node->nextGroup() != this ? node->nextGroup() : nullptr;
                delete node;
                node = next;
            }
        }
    }
}
