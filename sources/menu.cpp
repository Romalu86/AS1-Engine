#include "menu.h"
#include "menu_const.h"
#include "core/log.h"
#include <algorithm>

namespace as1
{
    void MENU::Clear()
    {
        m_head = MENU_HEAD{};
        m_mode = SpriteSectionMode::None;
        m_items.clear();
        m_loadedSectionSubresourceCount = 0;
        m_storageSummary = MENU_STORAGE_SUMMARY{};
    }

    bool MENU::Load(const STRING& path)
    {
        Clear();
        RESOURCE resource;
        if (resource.openFile(path, MENU_CONST::ROOT_MENU))
            return Load(resource);

        LOG::ResourceError("%s", MENU_CONST::LOG_OPEN_ERROR_CODE, path.c_str(), 0, MENU_CONST::LOG_MENU);
        return false;
    }

    bool MENU::Load(RESOURCE& resource)
    {
        Clear();

        if (resource.GoBegin(MENU_CONST::HEAD) != 0)
        {
            LOG::ResourceError("%s", MENU_CONST::LOG_SECTION_MISSING_ERROR_CODE, MENU_CONST::LOG_HEAD_IN_MENU, 0, MENU_CONST::LOG_MENU);
            return false;
        }

        if (!m_head.Read(resource))
        {
            LOG::ResourceError("%s", MENU_CONST::LOG_SECTION_MISSING_ERROR_CODE, MENU_CONST::LOG_HEAD_IN_MENU, 0, MENU_CONST::LOG_MENU);
            return false;
        }

        if (resource.GoBegin(MENU_CONST::SPR) == 0)
        {
            const bool ok = LoadSprSection(resource);
            if (ok)
                RebuildStorageSummary();
            return ok;
        }

        if (resource.GoBegin(MENU_CONST::SPRI) == 0)
        {
            const bool ok = LoadSpriSection(resource);
            if (ok)
                RebuildStorageSummary();
            return ok;
        }

        LOG::ResourceError("%s", MENU_CONST::LOG_SECTION_MISSING_ERROR_CODE, MENU_CONST::LOG_SPR_OR_SPRI_IN_MENU, 0, MENU_CONST::LOG_MENU);
        return false;
    }

    bool MENU::ReadRemainingSubresourcePayload(RESOURCE& resource, MENU_ITEM& item)
    {
        const int remaining = resource.GetBytesToEndSub();
        if (remaining <= 0)
            return true;
        item.commandPayload.resize(static_cast<std::size_t>(remaining));
        const bool ok = resource.read(item.commandPayload.data(), static_cast<unsigned>(remaining)) == 0;
        if (ok)
            item.DecodeCommandPayload();
        return ok;
    }

    bool MENU::LoadSprSection(RESOURCE& resource)
    {
        m_mode = SpriteSectionMode::SPR;
        m_loadedSectionSubresourceCount = static_cast<int>(resource.CurrentSubCount());
        while (true)
        {
            MENU_ITEM item;
            item.sourceOrder = static_cast<int>(m_items.size());
            item.subresourceSize = resource.SubSize();
            if (!item.Read(resource, m_head.version))
                return false;
            if (item.terminator)
                break;

            if (!ReadRemainingSubresourcePayload(resource, item))
                return false;
            m_items.push_back(item);

            if (resource.GoNextSub(MENU_CONST::SPR) != 0)
                break;
        }
        return true;
    }

    bool MENU::LoadSpriSection(RESOURCE& resource)
    {
        m_mode = SpriteSectionMode::SPRI;
        m_loadedSectionSubresourceCount = static_cast<int>(resource.CurrentSubCount());
        while (resource.GetBytesToEndSub() > 0)
        {
            MENU_ITEM item;
            item.sourceOrder = static_cast<int>(m_items.size());
            item.subresourceSize = resource.SubSize();
            if (!item.Read(resource, m_head.version))
                return false;
            if (item.terminator)
                break;

            // Runtime centering remains owned by the GRAPH/sprite binding route.
            item.DecodeCommandPayload();
            m_items.push_back(item);
        }
        return true;
    }
    void MENU::RebuildStorageSummary()
    {
        m_storageSummary = MENU_STORAGE_SUMMARY{};
        m_storageSummary.headVersion = m_head.version;
        m_storageSummary.headSizeX = m_head.sizeX;
        m_storageSummary.headSizeY = m_head.sizeY;
        m_storageSummary.headShiftX = m_head.shiftX;
        m_storageSummary.headShiftY = m_head.shiftY;
        m_storageSummary.itemCount = static_cast<int>(m_items.size());
        m_storageSummary.sectionSubresourceCount = m_loadedSectionSubresourceCount;

        for (const auto& item : m_items)
        {
            if (item.payloadRecord.present)
                ++m_storageSummary.payloadRecordCount;
            if (item.payloadRecord.hasTextReference)
                ++m_storageSummary.textReferenceCount;
            if (item.payloadRecord.hasResourceLink)
                ++m_storageSummary.resourceLinkCount;
        }
    }

}
