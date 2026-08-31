#pragma once
#include "menu_item.h"
#include "core/as_string.h"
#include "core/resource.h"
#include <vector>

namespace as1
{
    struct MENU_STORAGE_SUMMARY
    {
        int headVersion = 0;
        int headSizeX = 0;
        int headSizeY = 0;
        int headShiftX = 0;
        int headShiftY = 0;
        int sectionSubresourceCount = 0;
        int itemCount = 0;
        int payloadRecordCount = 0;
        int textReferenceCount = 0;
        int resourceLinkCount = 0;
    };


    class MENU
    {
    public:
        enum class SpriteSectionMode
        {
            None,
            SPR,
            SPRI,
        };

        bool Load(const STRING& path);
        bool Load(RESOURCE& resource);
        void Clear();

        const MENU_HEAD& Head() const { return m_head; }
        SpriteSectionMode Mode() const { return m_mode; }
        const std::vector<MENU_ITEM>& Items() const { return m_items; }
        const MENU_STORAGE_SUMMARY& StorageSummary() const { return m_storageSummary; }
        std::size_t Count() const { return m_items.size(); }

    private:
        bool LoadSprSection(RESOURCE& resource);
        bool LoadSpriSection(RESOURCE& resource);
        bool ReadRemainingSubresourcePayload(RESOURCE& resource, MENU_ITEM& item);
        void RebuildStorageSummary();

        MENU_HEAD m_head;
        SpriteSectionMode m_mode = SpriteSectionMode::None;
        std::vector<MENU_ITEM> m_items;
        int m_loadedSectionSubresourceCount = 0;
        MENU_STORAGE_SUMMARY m_storageSummary;
    };
}
