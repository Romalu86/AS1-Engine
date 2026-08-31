#include "base_sprite_list.h"

#include <algorithm>
#include <new>
#include <cstring>
#include "sprite.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "core/resource.h"
#include "core/application.h"
#include "graph.h"
#include "map.h"
#include "vid/vid.h"

namespace as1
{
    namespace
    {

        class CoreListVtableOwner final
        {
        public:
            virtual void* deletingDestructor(unsigned char flags) noexcept
            {
                return reinterpret_cast<SPRITE_POINTER_LIST*>(this)->pointerListDeletingDestructor(flags);
            }
        };

        class BaseSpriteListVtableOwner final
        {
        public:
            virtual void* deletingDestructor(unsigned char flags) noexcept
            {
                return reinterpret_cast<SPRITE_LIST*>(this)->destroyBaseSpriteListRecord((flags & 1u) != 0u);
            }
        };

        class RelationListVtableOwner final
        {
        public:
            virtual void* deletingDestructor(unsigned char flags) noexcept
            {
                auto* const raw = reinterpret_cast<std::uint32_t*>(this);
                void* const items = reinterpret_cast<void*>(static_cast<std::uintptr_t>(raw[3]));
                if (items)
                    ::operator delete(items);
                raw[3] = 0;
                raw[1] = 0;
                if ((flags & 1u) != 0u)
                    ::operator delete(static_cast<void*>(this));
                return this;
            }
        };

        template <class T>
        std::uint32_t currentImageVtableOf() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            static T owner;
            return static_cast<std::uint32_t>(*reinterpret_cast<const std::uintptr_t*>(&owner));
#else
            return 0u;
#endif
        }
    }

    std::uint32_t SPRITE_POINTER_LIST::CurrentImageCoreListVtable() noexcept
    {
        return currentImageVtableOf<CoreListVtableOwner>();
    }

    std::uint32_t SPRITE_LIST::CurrentImageBaseSpriteListVtable() noexcept
    {
        return currentImageVtableOf<BaseSpriteListVtableOwner>();
    }

    std::uint32_t SPRITE_LIST::CurrentImageRelationListVtable() noexcept
    {
        return currentImageVtableOf<RelationListVtableOwner>();
    }

    SPRITE_POINTER_LIST::SPRITE_POINTER_LIST() noexcept
    {
        initializePointerListRecord();
    }

    SPRITE_POINTER_LIST::~SPRITE_POINTER_LIST()
    {
        destroyCoreListStorage();
    }

    SPRITE_LIST::SPRITE_LIST() noexcept
    {
        initializeBaseSpriteListRecord();
    }

    SPRITE_LIST::~SPRITE_LIST()
    {
        // Retail BaseSpriteList destruction switches to the core-list vtable and
        // releases only the pointer-array storage. Sprite references are released
        // by explicit owner routes before this destructor where required.
        destroyBaseSpriteListRecord(false);
    }

    SPRITE_POINTER_LIST g_spriteWorkList;

    bool SPRITE_POINTER_LIST::ensureCapacityForOneMore()
    {
        if (m_count < m_capacity)
            return true;

        const int nextCapacity = m_capacity * 2 + 4;
        if (nextCapacity <= m_capacity)
            return true;

        SPRITE** next = static_cast<SPRITE**>(::operator new(sizeof(SPRITE*) * static_cast<std::size_t>(nextCapacity), std::nothrow));
        if (!next)
            fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", nextCapacity);

        if (m_items)
        {
            for (int i = 0; i < m_capacity; ++i)
                next[i] = m_items[i];
            ::operator delete(m_items);
        }
        m_items = next;
        m_capacity = nextCapacity;
        return true;
    }

    void SPRITE_POINTER_LIST::append(SPRITE* sprite)
    {

        if (!sprite)
            return;
        sprite->AddListReference();
        if (!ensureCapacityForOneMore())
            return;
        m_items[m_count++] = sprite;
    }

    void SPRITE_POINTER_LIST::InsertSorted(SPRITE* sprite)
    {
        if (!sprite)
            return;
        if (!ensureCapacityForOneMore())
            return;
        int pos = 0;
        for (; pos < m_count; ++pos)
        {
            SPRITE* cur = m_items[pos];
            const int lo = cur ? cur->oldAddress() : 0;
            const int ro = sprite->oldAddress();
            if (lo > ro || (lo == ro && cur > sprite))
                break;
        }
        for (int i = m_count; i > pos; --i)
            m_items[i] = m_items[i - 1];
        sprite->AddListReference();
        m_items[pos] = sprite;
        ++m_count;
    }

    bool SPRITE_POINTER_LIST::removeSorted(SPRITE* sprite)
    {
        return removeSortedError(sprite) == 0;
    }

    int SPRITE_POINTER_LIST::removeSortedError(SPRITE* sprite)
    {
        return removeAndReleaseReference(sprite);
    }

    int SPRITE_POINTER_LIST::removeAndReleaseReference(SPRITE* sprite)
    {
        if (!sprite || m_count == 0 || !m_items)
            return 1;
        for (int i = m_count - 1; i >= 0; --i)
        {
            if (m_items[i] == sprite)
                return releaseOne(static_cast<std::size_t>(i), true);
        }
        return 1;
    }

    int SPRITE_POINTER_LIST::findAndNull(SPRITE* sprite) noexcept
    {
        // Application::removeSpriteFromDrawBucket scans the selected 0x10-byte list from
        // count-1 toward zero and only nulls the matching slot.  It does not
        // alter count/capacity and does not touch SPRITE+0x2C.
        int index = m_count - 1;
        if (index < 0)
            return index;
        while (m_items[index] != sprite)
        {
            --index;
            if (index < 0)
                return index;
        }
        m_items[index] = nullptr;
        return index;
    }

    void SPRITE_POINTER_LIST::compactSparse() noexcept
    {

        const int oldCount = m_count;
        if (oldCount <= 0)
            return;

        int firstNull = 0;
        while (firstNull < oldCount && m_items[firstNull] != nullptr)
            ++firstNull;
        if (firstNull >= oldCount)
            return;

        int holes = 1;
        for (int src = firstNull + 1; src < oldCount; ++src)
        {
            if (m_items[src] != nullptr)
                m_items[src - holes] = m_items[src];
            else
                ++holes;
        }

        const int newCount = oldCount - holes;
        if (newCount > 0)
        {
            if (newCount < oldCount)
                m_count = newCount;
            return;
        }

        SPRITE** const storage = m_items;
        m_capacity = 0;
        m_count = 0;
        if (storage)
            ::operator delete(storage);
        m_items = nullptr;
    }

    void SPRITE_POINTER_LIST::clear()
    {
        clearSpriteReferences();
    }

    void SPRITE_POINTER_LIST::deleteAllSprites()
    {
        releaseRepeatedReferences();
    }

    SPRITE* SPRITE_POINTER_LIST::NextIterateNon0(int* cursor) const
    {
        if (!cursor)
            return nullptr;
        int i = *cursor;
        if (i < 0)
            i = 0;
        for (; i < m_count; ++i)
        {
            SPRITE* sprite = m_items ? m_items[i] : nullptr;
            *cursor = i + 1;
            if (sprite)
                return sprite;
        }
        *cursor = m_count;
        return nullptr;
    }

    SPRITE* SPRITE_POINTER_LIST::beginReverseIteration(int* cursor) const noexcept
    {

        if (m_count == 0)
            return nullptr;
        const int index = m_count - 1;
        *cursor = index;
        return m_items[index];
    }

    SPRITE* SPRITE_POINTER_LIST::continueReverseIteration(int* cursor) const noexcept
    {

        if (*cursor > m_count)
            *cursor = m_count;
        const int index = *cursor - 1;
        *cursor = index;
        return index >= 0 ? m_items[index] : nullptr;
    }

    void SPRITE_POINTER_LIST::add(SPRITE* sprite)
    {
        append(sprite);
    }

    void SPRITE_POINTER_LIST::clearNoRelease() noexcept
    {
        m_count = 0;
    }

    void SPRITE_POINTER_LIST::releaseRepeatedReferences()
    {
        (void)releaseRepeatedReferencesRetail();
    }

    int SPRITE_POINTER_LIST::releaseRepeatedReferencesRetail()
    {
        int result = m_count;
        if (result > 0)
        {
            int base = 0;
            do
            {
                for (int probe = result - 1; probe > base; --probe)
                {
                    if (m_items[base] && m_items[probe] == m_items[base])
                        collapseDuplicateReference(static_cast<std::size_t>(probe));
                }
                result = m_count;
                ++base;
            }
            while (base < result);
        }
        for (int i = m_count - 1; i >= 0; --i)
        {
            if (m_items[i])
                result = releaseByIndexRetail(i);
        }
        return result;
    }

    int SPRITE_LIST::loadMenuSpriteList(const STRING& path)
    {

        RESOURCE resource;
        if (!resource.openFile(path, RESOURCE::ResTypes::MENU))
        {
            LOG::ResourceError("MENU", 7, path.c_str(), 0, 0);
            return 1;
        }

        if (resource.GoBegin(RESOURCE::ResTypes::HEAD) != 0)
        {
            LOG::ResourceError("MENU", 11, "'HEAD'in menu", 0, 0);
            return 1;
        }

        int version = 0;
        int sizeX = 0;
        int sizeY = 0;
        int shiftX = 0;
        int shiftY = 0;
        resource.read(&version, 4);
        resource.read(&sizeX, 4);
        resource.read(&sizeY, 4);
        resource.read(&shiftX, 4);
        resource.read(&shiftY, 4);

        MAP* const map = MAP::Current();
        GRAPH* const graph = GRAPH::CurrentGraph();

        const auto centerSprite = [&](SPRITE* sprite)
        {
            if (!sprite)
                return;
            const float x = sprite->X()
                - static_cast<float>(shiftX)
                - static_cast<float>(sizeX / 2)
                + static_cast<float>(graph->SizeX()) * 0.5f;
            const float y = sprite->Y()
                - static_cast<float>(shiftY)
                - static_cast<float>(sizeY / 2)
                + static_cast<float>(graph->SizeY()) * 0.5f;
            sprite->ChangeCoor(x, y, sprite->Z());
        };

        SPRITE* const endSprite = reinterpret_cast<SPRITE*>(~static_cast<std::uintptr_t>(0));

        if (resource.GoBegin(RESOURCE::ResTypes::SPRITE) == 0)
        {
            for (;;)
            {
                SPRITE* const sprite = map->LoadSprite(&resource, version);
                if (sprite == endSprite)
                    break;
                if (sprite)
                {
                    centerSprite(sprite);
#if UINTPTR_MAX <= UINT32_MAX
                    // Retail vtable +0x04 receives (0x51, RESOURCE*, version, 0)
                    // before GoNextSub('SPR ').  The target executable is x86,
                    // so the physical pointer is exactly one DWORD there.
                    sprite->dispatchVirtualAction(ActionCode::ACT_RESTORE,
                        static_cast<int>(reinterpret_cast<std::uintptr_t>(&resource)),
                        version,
                        0);
#endif
                }

                if (resource.GoNextSub(RESOURCE::ResTypes::SPRITE) != 0)
                    break;
            }
            return 0;
        }

        // If SPR is absent, retail requires one SPRI section and consumes
        // packed records from that single subresource until the -1 sentinel.
        if (resource.GoBegin(RESOURCE::ResTypes::SPRI) == 0)
        {
            for (;;)
            {
                SPRITE* const sprite = map->LoadSprite(&resource, version);
                if (sprite == endSprite)
                    break;
                centerSprite(sprite);
            }
            return 0;
        }

        LOG::ResourceError("MENU", 11, "'SPR ' or 'SPRI' in menu", 0, 0);
        return 1;
    }

    void SPRITE_LIST::initializeListState() noexcept
    {
        initializeBaseSpriteListRecord();
    }

    void SPRITE_LIST::initializeBaseSpriteListRecord() noexcept
    {

        m_items = nullptr;
        m_count = 0;
        m_capacity = 0;
        m_selectedSprite = nullptr;
        setRetailVtableToken(CurrentImageBaseSpriteListVtable());
        m_iterationFlags &= ~0x3u;
    }

    SPRITE_POINTER_LIST* SPRITE_POINTER_LIST::initializePointerListRecord() noexcept
    {

        m_items = nullptr;
        m_count = 0;
        m_capacity = 0;
        setRetailVtableToken(CurrentImageCoreListVtable());
        return this;
    }

    void SPRITE_POINTER_LIST::initializeHashBucketRecordState() noexcept
    {
        (void)initializePointerListRecord();
    }

    void* SPRITE_POINTER_LIST::pointerListDeletingDestructor(unsigned char deletingDestructorFlags) noexcept
    {
        // Retail pointerListDeletingDestructor.  Bit 2 selects the vector-deleting route whose
        // DWORD element-count cookie lives at this[-4].  Every 0x10-byte record
        // is destroyed from the end to the beginning.  Bit 0 controls only
        // storage release.  The array branch returns the cookie allocation base;
        // the scalar branch returns the original record pointer.
        if ((deletingDestructorFlags & 2u) != 0u)
        {
            unsigned char* const first = reinterpret_cast<unsigned char*>(this);
            auto* const cookie = reinterpret_cast<std::uint32_t*>(first) - 1;
            const std::uint32_t count = *cookie;
            for (std::uint32_t i = count; i != 0u; --i)
            {
                auto* const record = reinterpret_cast<SPRITE_POINTER_LIST*>(
                    first + static_cast<std::size_t>(i - 1u) * 0x10u);
                record->destroyCoreListStorage();
            }
            void* const result = static_cast<void*>(cookie);
            if ((deletingDestructorFlags & 1u) != 0u)
                ::operator delete(result);
            return result;
        }

        SPRITE_POINTER_LIST* const self = this;
        destroyCoreListStorage();
        if ((deletingDestructorFlags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    void SPRITE_POINTER_LIST::destroyCoreListStorage() noexcept
    {

        setRetailVtableToken(SPRITE_LIST::CurrentImageRelationListVtable());
        ::operator delete(m_items);
        m_items = nullptr;
        m_count = 0;
    }

    SPRITE_LIST* SPRITE_LIST::destroyListStorage(bool deleteSelfFlag) noexcept
    {
        return destroyBaseSpriteListRecord(deleteSelfFlag);
    }

    SPRITE_LIST* SPRITE_LIST::destroyBaseSpriteListRecord(bool deleteSelfFlag) noexcept
    {

        setRetailVtableToken(SPRITE_LIST::CurrentImageRelationListVtable());
        ::operator delete(m_items);
        m_items = nullptr;
        m_count = 0;
        if (deleteSelfFlag)
            ::operator delete(this);
        return this;
    }


    int SPRITE_LIST::selectSpriteByInput(const SelectionInputRoute& input)
    {

        m_selectedSprite = nullptr;
        m_iterationFlags &= ~0x3u;

        for (int i = 0; i < m_count; ++i)
        {
            SPRITE* sprite = m_items[i];
            if (!sprite)
                continue;

            VID* vid = sprite->Vid();

            if (vid->movementMask() == 0u)
                continue;

            const int animation = sprite->currentAnimation();
            if (animation == 14 || animation >= 15 || animation == 7 || animation == 6)
                continue;

            const float halfX = vid->halfSizeX();
            const float halfY = vid->halfSizeY();
            const float baseY = sprite->Y() - sprite->Z();
            const float bottomY = baseY - vid->sizeZ() - halfY;
            const float topY = baseY + halfY;
            const bool insideX = (input.probeX >= sprite->X() - halfX) && (input.probeX <= sprite->X() + halfX);
            const bool insideY = (input.probeY > bottomY) && (input.probeY < topY);
            if (!insideX || !insideY)
            {
                const int resetAnimation = animation & 1;
                if (input.changeSpriteAnimation)
                    input.changeSpriteAnimation(input.user, sprite, resetAnimation);
                continue;
            }

            if (!m_selectedSprite || m_selectedSprite->Z() < sprite->Z())
                m_selectedSprite = sprite;
        }

        if (!m_selectedSprite)
            return 0;

        const int currentAnimation = m_selectedSprite->currentAnimation();
        if (input.flags & 0x1u)
        {
            m_iterationFlags |= 0x1u;
            if (input.clearInputTransient)
                input.clearInputTransient(input.user, 0);
            const int nextAnimation = (currentAnimation & 1) | 4;
            if (input.changeSpriteAnimation)
                input.changeSpriteAnimation(input.user, m_selectedSprite, nextAnimation);
            return 1;
        }

        if (input.flags & 0x4u)
        {
            m_iterationFlags |= 0x2u;
            if (input.clearInputTransient)
                input.clearInputTransient(input.user, 1);
        }

        if ((currentAnimation & ~1) != 4)
        {
            const int nextAnimation = (currentAnimation & 1) | 2;
            if (input.changeSpriteAnimation)
                input.changeSpriteAnimation(input.user, m_selectedSprite, nextAnimation);
        }
        return 0;
    }

    int SPRITE_LIST::selectedSpriteNvid() const noexcept
    {

        const SPRITE* selected = m_selectedSprite;
        if (!selected)
            return 0;

        const VID* vid = selected->Vid();
        return vid->nvid();
    }

    int SPRITE_LIST::selectedSpriteDirectionFrame() const noexcept
    {

        const SPRITE* selected = m_selectedSprite;
        if (!selected)
            return 0;

        const VID* vid = selected->Vid();

        const std::uint32_t directionByte =
            static_cast<std::uint32_t>(vid->directionQuantizationOffset() +
                                       selected->directionIndex()) & 0xFFu;
        const std::uint32_t noDir = static_cast<std::uint32_t>(vid->directionCount());
        return static_cast<int>((directionByte * noDir) >> 8);
    }

    void SPRITE_POINTER_LIST::clearSpriteReferences()
    {

        for (int i = m_count - 1; i >= 0; --i)
        {
            SPRITE* sprite = m_items[i];
            if (!sprite)
                continue;
            const int refs = releaseSpriteReference(sprite, true);
            if (refs > 0 && i >= 0 && i < m_count)
            {
                --m_count;
                m_items[i] = m_items[m_count];
            }
        }
    }

    int SPRITE_POINTER_LIST::releaseOneByIndex(int index)
    {
        return releaseByIndexRetail(index);
    }

    int SPRITE_POINTER_LIST::releaseByIndexRetail(int index)
    {
        if (index < 0 || index >= m_count)
            return 1;
        SPRITE* sprite = m_items[index];
        --m_count;
        m_items[index] = m_items[m_count];
        const int refs = sprite->ReleaseListReference();
        if (refs < 0)
        {
            VID* vid = sprite->Vid();
            LOG::ResourceError("SPRITE %i", 4, "noRef\tat Release", refs, vid ? vid->nVid : -1);
            return 0;
        }

        DeleteSpriteThroughVirtualDeletingDestructor(sprite);
        return 0;
    }

    void SPRITE_POINTER_LIST::releaseAllReferences()
    {
        clearSpriteReferences();
    }

    std::size_t SPRITE_POINTER_LIST::count() const noexcept
    {
        return m_count > 0 ? static_cast<std::size_t>(m_count) : 0u;
    }

    bool SPRITE_POINTER_LIST::empty() const noexcept
    {
        return m_count == 0;
    }

    SPRITE* SPRITE_POINTER_LIST::at(std::size_t index) const noexcept
    {
        return m_items && index < static_cast<std::size_t>(m_count) ? m_items[index] : nullptr;
    }

    int SPRITE_POINTER_LIST::activeCount() const noexcept
    {
        return m_count;
    }

    int SPRITE_POINTER_LIST::storageCapacity() const noexcept
    {
        return m_capacity;
    }

    SPRITE* const* SPRITE_POINTER_LIST::data() const noexcept
    {
        return m_items;
    }

    bool SPRITE_POINTER_LIST::contains(SPRITE* sprite) const noexcept
    {
        if (!m_items)
            return false;
        for (int i = 0; i < m_count; ++i)
            if (m_items[i] == sprite)
                return true;
        return false;
    }

    int SPRITE_POINTER_LIST::releaseOne(std::size_t index, bool eraseEntry)
    {
        if (!m_items || index >= static_cast<std::size_t>(m_count))
            return 1;
        SPRITE* sprite = m_items[index];
        if (eraseEntry)
        {
            --m_count;
            m_items[index] = m_items[m_count];
        }
        else
            m_items[index] = nullptr;
        if (sprite)
            releaseSpriteReference(sprite, true);
        return 0;
    }

    int SPRITE_POINTER_LIST::collapseDuplicateReference(std::size_t index)
    {
        if (index >= static_cast<std::size_t>(m_count))
            return 1;
        SPRITE* sprite = m_items[index];
        releaseSpriteReference(sprite, true);
        --m_count;
        m_items[index] = m_items[m_count];
        return 0;
    }

    int SPRITE_POINTER_LIST::releaseDuplicateAtIndex(std::size_t index)
    {
        return collapseDuplicateReference(index);
    }

    int SPRITE_POINTER_LIST::releaseSpriteReference(SPRITE* sprite, bool callDeleteWhenZero)
    {
        if (!sprite)
            return 0;
        const int refs = sprite->ReleaseListReference();
        if (refs < 0)
        {
            VID* vid = sprite->Vid();
            LOG::ResourceError("SPRITE %i", 4, "noRef\tat Release", refs, vid ? vid->nVid : -1);
        }
        else if (refs == 0 && callDeleteWhenZero)
            DeleteSpriteThroughVirtualDeletingDestructor(sprite);
        return refs;
    }

    SPRITE_LIST& applicationGlobalSpriteList()
    {
        static SPRITE_LIST list;
        return list;
    }

    SPRITE_LIST& applicationFrameSpriteList()
    {

#ifdef _WIN32
        if (void* const owner = as1::core::ApplicationPhysicalOwner())
            return *reinterpret_cast<SPRITE_LIST*>(static_cast<std::uint8_t*>(owner) + as1::core::retail_application_layout::BaseSpriteList);
#endif
        static SPRITE_LIST portableFallback;
        return portableFallback;
    }

    SPRITE_OWNER_SLOT::SPRITE_OWNER_SLOT(DestroyProc destroyProc) noexcept
        : m_destroyProc(destroyProc)
    {
    }

    SPRITE_OWNER_SLOT::~SPRITE_OWNER_SLOT()
    {
        release();
    }

    void SPRITE_OWNER_SLOT::setDestroyProc(DestroyProc destroyProc) noexcept
    {
        m_destroyProc = destroyProc;
    }

    void SPRITE_OWNER_SLOT::bind(void* object) noexcept
    {
        m_object = object;
    }

    void* SPRITE_OWNER_SLOT::clearIfMatches(void* object) noexcept
    {
        void* previous = m_object;
        if (previous == object)
            m_object = nullptr;
        return previous;
    }

    void* SPRITE_OWNER_SLOT::get() const noexcept
    {
        return m_object;
    }

    bool SPRITE_OWNER_SLOT::empty() const noexcept
    {
        return m_object == nullptr;
    }

    int SPRITE_OWNER_SLOT::release()
    {
        void* object = m_object;
        if (object && m_destroyProc)
            m_destroyProc(object, true);
        m_object = nullptr;
        return object ? 1 : 0;
    }
}
