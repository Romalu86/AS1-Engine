#include "map.h"
#include "vid/vid_software.h"
#include "vid/vid_software_png.h"
#include "vid/vid_software16.h"
#include "vid/vid_hardware.h"
#include "vid/vid_hardware_z.h"
#include "vid/vid_surface.h"
#include "vid/vid_light.h"
#include "core/application.h"
#include "core/resource.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "core/file_storage.h"
#include "core/configuration.h"
#include "images/picture.h"
#include "graph.h"
#include "unit.h"
#include "avia.h"
#include "creature.h"
#include "civ_robot.h"
#include "engine.h"
#include "balloon.h"
#include "depo.h"
#include "cannon.h"
#include "building.h"
#include "rail.h"
#include "sprite_act_const.h"
#include "script/action_constants.h"
#include "sprite_collector_hash.h"
#include "mouse.h"
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <sstream>
#include <set>
#include <iomanip>
#include <filesystem>
#include <set>
#include <cmath>
#include <chrono>
#include <functional>
#include <limits>
#include <new>
#include <cstdlib>
#ifdef _WIN32
#include "win/application_win.h"
#include "d3d8.h"
#endif

namespace as1
{
    long double approximatePlanarDistance(float dx, float dy) noexcept
    {
        const long double ax = std::fabs(static_cast<long double>(dx));
        const long double ay = std::fabs(static_cast<long double>(dy));
        if (ax <= ay)
            return ax * 0.5L + ay;
        return ax + ay * 0.5L;
    }

    namespace
    {
        constexpr float UNLIMITED = 999999.0f;

        VID* g_retailNullVid = new (std::nothrow) VID();

        MAP* g_currentMapOwner = nullptr;

        int retailFtolLow32ForMap(float value) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            __int64 converted = 0;
            unsigned short oldControl = 0;
            unsigned short truncateControl = 0;
            __asm
            {
                fld value
                fstcw oldControl
                fwait
                mov ax, oldControl
                or ah, 0Ch
                mov truncateControl, ax
                fldcw truncateControl
                fistp qword ptr converted
                fldcw oldControl
            }
            return static_cast<int>(static_cast<unsigned int>(converted));
#else
            const long double d = static_cast<long double>(value);
            if (!std::isfinite(d) ||
                d < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                d > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
#endif
        }

        int retailFtolMulLow32ForMap(float value, float multiplier) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            __int64 converted = 0;
            unsigned short oldControl = 0;
            unsigned short truncateControl = 0;
            __asm
            {
                fld value
                fmul multiplier
                fstcw oldControl
                fwait
                mov ax, oldControl
                or ah, 0Ch
                mov truncateControl, ax
                fldcw truncateControl
                fistp qword ptr converted
                fldcw oldControl
            }
            return static_cast<int>(static_cast<unsigned int>(converted));
#else
            const long double d = static_cast<long double>(value) * static_cast<long double>(multiplier);
            if (!std::isfinite(d) ||
                d < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                d > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
#endif
        }

        bool x87LessOrUnorderedForMap(float lhs, float rhs) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            unsigned short status = 0;
            __asm
            {
                fld lhs
                fcomp rhs
                fnstsw ax
                mov status, ax
            }
            return (status & 0x0100u) != 0u;
#else
            return lhs < rhs || std::isnan(lhs) || std::isnan(rhs);
#endif
        }

        int mapGridCoordinate(float value, float limit) noexcept
        {
            // Retail sampleTerrainHeight axis conversion. Keep inline x87 outside a
            // lambda because MSVC x86 rejects __asm inside lambda bodies.
#if defined(_MSC_VER) && defined(_M_IX86)
            static const float kZero = 0.0f;
            static const float kOne = 1.0f;
            static const float kOneEighth = 0.125f;
            unsigned short status = 0;
            __asm
            {
                fld value
                fcomp kZero
                fnstsw ax
                mov status, ax
            }
            if ((status & 0x0100u) != 0u)
                return 0;

            __asm
            {
                fld value
                fcomp limit
                fnstsw ax
                mov status, ax
            }
            const bool useOriginal = (status & 0x0100u) != 0u;

            __int64 converted = 0;
            unsigned short oldControl = 0;
            unsigned short truncateControl = 0;
            if (useOriginal)
            {
                __asm { fld value }
            }
            else
            {
                __asm
                {
                    fld limit
                    fsub kOne
                }
            }
            __asm
            {
                fmul kOneEighth
                fstcw oldControl
                fwait
                mov ax, oldControl
                or ah, 0Ch
                mov truncateControl, ax
                fldcw truncateControl
                fistp qword ptr converted
                fldcw oldControl
            }
            return static_cast<int>(static_cast<unsigned int>(converted));
#else
            if (value < 0.0f)
                return 0;
            const float chosen = (value < limit || std::isnan(value)) ? value : (limit - 1.0f);
            const long double scaled = static_cast<long double>(chosen) * 0.125L;
            if (!std::isfinite(scaled) ||
                scaled < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                scaled > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(scaled));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
#endif
        }

        std::uint32_t currentMilliseconds()
        {
            using clock = std::chrono::steady_clock;
            return static_cast<std::uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count());
        }

        std::string normalizeGamePath(std::string s)
        {
            std::replace(s.begin(), s.end(), '\\', '/');
            while (!s.empty() && (s.front() == '/' || s.front() == '.'))
            {
                if (s.front() == '.')
                {
                    s.erase(s.begin());
                    if (!s.empty() && s.front() == '/')
                        s.erase(s.begin());
                }
                else
                    s.erase(s.begin());
            }
            return s;
        }

        std::string lowerCopy(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        struct MapCameraClampRect
        {
            float screenW = 640.0f;
            float screenH = 480.0f;
            float clipLeft = 0.0f;
            float clipTop = 0.0f;
            float clipRight = 640.0f;
            float clipBottom = 480.0f;
            float minX = 0.0f;
            float minY = 0.0f;
            float maxX = 0.0f;
            float maxY = 0.0f;
        };

        bool graphSub42BA90CallsApplicationPass(int pass) noexcept
        {
            return pass >= 0 && pass <= 10;
        }

        int graphSub42BA90ApplicationPassSequenceIndex(int pass) noexcept
        {
            return graphSub42BA90CallsApplicationPass(pass) ? pass : -1;
        }

        MapCameraClampRect buildMapCameraClampRect(const GRAPH& graph,
                                                const VECTOR2& scrollMin,
                                                const VECTOR2& scrollMax)
        {
            MapCameraClampRect out;

            out.screenW = graph.screenWidth();
            out.screenH = graph.screenHeight();

            const GraphViewportState& vp = graph.viewportState();
            out.clipLeft = vp.left;
            out.clipTop = vp.top;
            out.clipRight = vp.right;
            out.clipBottom = vp.bottom;
            out.minX = scrollMin.x - out.clipLeft;
            out.minY = scrollMin.y - out.clipTop;
            out.maxX = scrollMax.x - out.clipRight;
            out.maxY = scrollMax.y - out.clipBottom;
            return out;
        }

        bool hasExtension(const std::string& path, const char* ext)
        {
            const std::string l = lowerCopy(path);
            const std::string e = ext;
            return l.size() >= e.size() && l.compare(l.size() - e.size(), e.size(), e) == 0;
        }

        bool findExistingCaseInsensitive(const std::filesystem::path& requested, std::filesystem::path& resolved)
        {
            if (std::filesystem::exists(requested))
            {
                resolved = requested;
                return true;
            }
            std::filesystem::path cur;
            if (requested.is_absolute())
                cur = requested.root_path();
            std::filesystem::path rel = requested.is_absolute() ? requested.relative_path() : requested;
            for (const auto& part : rel)
            {
                const std::string wanted = lowerCopy(part.string());
                std::filesystem::path base = cur.empty() ? std::filesystem::path(".") : cur;
                if (!std::filesystem::exists(base) || !std::filesystem::is_directory(base))
                    return false;
                bool found = false;
                for (const auto& entry : std::filesystem::directory_iterator(base))
                {
                    if (lowerCopy(entry.path().filename().string()) == wanted)
                    {
                        cur = entry.path();
                        found = true;
                        break;
                    }
                }
                if (!found)
                    return false;
            }
            if (std::filesystem::exists(cur))
            {
                resolved = cur;
                return true;
            }
            return false;
        }

        std::filesystem::path inferResourceRootFromMapPath(const STRING& mapPathString)
        {
            namespace fs = std::filesystem;
            fs::path mapPath(mapPathString.str());
            if (mapPath.empty())
                return {};
            if (!mapPath.is_absolute())
                mapPath = fs::absolute(mapPath);

            fs::path dir = mapPath.parent_path();
            if (dir.empty())
                return {};

            fs::path root = dir;
            const std::string leaf = lowerCopy(dir.filename().string());
            if (leaf == "maps")
                root = dir.parent_path();
            if (root.empty())
                return {};

            fs::path resolved;
            if (findExistingCaseInsensitive(root / "objects.res", resolved))
                return root;
            return {};
        }
    }

    WEAPON WEAPON::fromAS1Record(const BYTE* data, size_t size)
    {
        if (!data || size != AS1_RECORD_SIZE)
            throw std::runtime_error("WEAPON::fromAS1Record: AS1 WEAP record must be exactly 68 bytes");

        WEAPON out;
        std::memcpy(out.raw.data(), data, AS1_RECORD_SIZE);
        return out;
    }

    bool SFX::isNull() const
    {
        if (wavNames.empty())
            return true;
        for (const auto& name : wavNames)
        {
            const std::string n = lowerCopy(name.str());
            if (n != "wav/null.wav" && n != "wav\\null.wav")
                return false;
        }
        return true;
    }

    SFX SFX::fromAS1Record(const BYTE* data, size_t size, const std::filesystem::path& gameRoot)
    {
        if (!data || size == 0)
            throw std::runtime_error("SFX::fromAS1Record: empty AS1 SFX record");

        SFX out;
        out.raw.assign(data, data + size);
        out.controlByte = data[0];

        size_t pos = 1;
        while (pos < size)
        {
            while (pos < size && data[pos] == 0)
                ++pos;
            if (pos >= size)
                break;
            const size_t begin = pos;
            while (pos < size && data[pos] != 0)
                ++pos;
            if (pos > begin)
            {
                std::string text(reinterpret_cast<const char*>(data + begin), pos - begin);
                STRING wav(text);
                out.wavNames.push_back(wav);
                const std::filesystem::path requested = gameRoot / std::filesystem::path(normalizeGamePath(text));
                std::filesystem::path resolved;
                if (!findExistingCaseInsensitive(requested, resolved))
                    out.missingFiles.push_back(wav);
            }
        }
        return out;
    }

    DWORD RelationTable::encodePointer(const void* pointer) noexcept
    {
        return static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(pointer));
    }

    void RelationTable::initializeList(RawList& list) noexcept
    {
        list.items = 0;
        list.vtable = SPRITE_LIST::CurrentImageRelationListVtable();
        list.count = 0;
        list.capacity = 0;
    }

    void RelationTable::destroyList(RawList& list) noexcept
    {
        list.vtable = SPRITE_LIST::CurrentImageRelationListVtable();
        if (void* const items = decodePointer<void>(list.items))
            ::operator delete(items);
        list.items = 0;
        list.count = 0;
    }

    RelationTable::RelationTable() noexcept
    {
        initializeList(m_old);
        initializeList(m_new);
    }

    RelationTable::~RelationTable() noexcept
    {
        destroyList(m_new);
        destroyList(m_old);
    }

    void RelationTable::append(int oldSpriteAddr, SPRITE* newSpritePtr)
    {
        if (!oldSpriteAddr)
            return;

        if (m_old.count >= m_old.capacity)
        {
            const int oldCapacity = m_old.capacity;
            const int newCapacity = 2 * oldCapacity + 4;
            if (newCapacity > oldCapacity)
            {
                int* const oldItems = decodePointer<int>(m_old.items);
                int* const newItems = static_cast<int*>(::operator new(static_cast<size_t>(newCapacity) * sizeof(int)));
                if (!newItems)
                    fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", newCapacity);
                if (oldItems)
                {
                    // Retail copies the old CAPACITY entries, not only count.
                    for (int i = 0; i < oldCapacity; ++i)
                        newItems[i] = oldItems[i];
                    ::operator delete(oldItems);
                }
                m_old.items = encodePointer(newItems);
                m_old.capacity = newCapacity;
            }
        }
        decodePointer<int>(m_old.items)[m_old.count++] = oldSpriteAddr;

        if (m_new.count >= m_new.capacity)
        {
            const int oldCapacity = m_new.capacity;
            const int newCapacity = 2 * oldCapacity + 4;
            if (newCapacity > oldCapacity)
            {
                DWORD* const oldItems = decodePointer<DWORD>(m_new.items);
                DWORD* const newItems = static_cast<DWORD*>(::operator new(static_cast<size_t>(newCapacity) * sizeof(DWORD)));
                if (!newItems)
                    fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", newCapacity);
                if (oldItems)
                {
                    for (int i = 0; i < oldCapacity; ++i)
                        newItems[i] = oldItems[i];
                    ::operator delete(oldItems);
                }
                m_new.items = encodePointer(newItems);
                m_new.capacity = newCapacity;
            }
        }
        decodePointer<DWORD>(m_new.items)[m_new.count++] = encodePointer(newSpritePtr);
    }

    SPRITE* RelationTable::getPointer(int oldSpriteAddr) const noexcept
    {
        int index = m_old.count;
        if (!index)
            return nullptr;
        const int* const oldItems = decodePointer<int>(m_old.items);
        while (index)
        {
            --index;
            if (oldItems[index] == oldSpriteAddr)
                return decodePointer<SPRITE>(decodePointer<DWORD>(m_new.items)[index]);
        }
        return nullptr;
    }

    int RelationTable::getIndex(int oldSpriteAddr) const noexcept
    {
        int index = m_old.count;
        const int* const oldItems = decodePointer<int>(m_old.items);
        while (index)
        {
            --index;
            if (oldItems[index] == oldSpriteAddr)
                return index;
        }
        return -1;
    }

    void RelationTable::clear() noexcept
    {
        void* const oldItems = decodePointer<void>(m_old.items);
        m_old.capacity = 0;
        m_old.count = 0;
        if (oldItems)
            ::operator delete(oldItems);
        m_old.items = 0;

        void* const newItems = decodePointer<void>(m_new.items);
        m_new.capacity = 0;
        m_new.count = 0;
        if (newItems)
            ::operator delete(newItems);
        m_new.items = 0;
    }

    MAP::MAP(GRAPH* graph) : m_graph(graph)
    {
        g_currentMapOwner = this;
    }

    MAP::MAP(GRAPH* graph, STRING resourceRoot) : m_graph(graph), m_resourceRoot(std::move(resourceRoot))
    {
        g_currentMapOwner = this;
    }

    MAP::~MAP()
    {
#ifndef _WIN32
        if (m_weaponTable)
        {
            ::operator delete(m_weaponTable);
            m_weaponTable = nullptr;
            m_weaponCount = 0;
        }
#endif
        if (g_currentMapOwner == this)
            g_currentMapOwner = nullptr;
#ifndef _WIN32
        deleteSpritesCollectorHash();
#endif
    }

    MAP* MAP::Current()
    {
        return g_currentMapOwner;
    }

    RESOURCE& MAP::demoResource() noexcept
    {
#ifdef _WIN32
        void* const owner = core::ApplicationPhysicalOwner();
        return *reinterpret_cast<RESOURCE*>(static_cast<std::uint8_t*>(owner) + core::retail_application_layout::DemoResource);
#else
        return m_demoResource;
#endif
    }

    const RESOURCE& MAP::demoResource() const noexcept
    {
#ifdef _WIN32
        const void* const owner = core::ApplicationPhysicalOwner();
        return *reinterpret_cast<const RESOURCE*>(static_cast<const std::uint8_t*>(owner) + core::retail_application_layout::DemoResource);
#else
        return m_demoResource;
#endif
    }

    RelationTable& MAP::relationTable() noexcept
    {
#ifdef _WIN32
        void* const owner = core::ApplicationPhysicalOwner();
        return *reinterpret_cast<RelationTable*>(static_cast<std::uint8_t*>(owner) + core::retail_application_layout::RelationTable);
#else
        return m_relationTable;
#endif
    }

    const RelationTable& MAP::relationTable() const noexcept
    {
#ifdef _WIN32
        const void* const owner = core::ApplicationPhysicalOwner();
        return *reinterpret_cast<const RelationTable*>(static_cast<const std::uint8_t*>(owner) + core::retail_application_layout::RelationTable);
#else
        return m_relationTable;
#endif
    }

    SPRITE* MAP::ResolveRelationHandle(int oldAddress) const noexcept
    {
        return relationTable().getPointer(oldAddress);
    }

    short* MAP::terrainGrid() noexcept
    {
#ifdef _WIN32
        return core::ApplicationTerrainGrid();
#else
        return m_gridZ.empty() ? nullptr : m_gridZ.data();
#endif
    }

    const short* MAP::terrainGrid() const noexcept
    {
#ifdef _WIN32
        return core::ApplicationTerrainGrid();
#else
        return m_gridZ.empty() ? nullptr : m_gridZ.data();
#endif
    }

    int MAP::terrainGridWidth() const noexcept
    {
#ifdef _WIN32
        return core::ApplicationTerrainGridWidth();
#else
        return m_noGridX;
#endif
    }

    int MAP::terrainGridHeight() const noexcept
    {
#ifdef _WIN32
        return core::ApplicationTerrainGridHeight();
#else
        return m_noGridY;
#endif
    }

    void MAP::setTerrainGridDimensions(int x, int y) noexcept
    {
#ifdef _WIN32
        core::SetApplicationTerrainGridWidth(x);
        core::SetApplicationTerrainGridHeight(y);
#else
        m_noGridX = x;
        m_noGridY = y;
#endif
    }

    void MAP::replaceTerrainGridStorage(short* grid) noexcept
    {
#ifdef _WIN32
        core::SetApplicationTerrainGrid(grid);
#else
        if (!grid)
            m_gridZ.clear();
#endif
    }

    void MAP::releaseTerrainGridStorage() noexcept
    {
#ifdef _WIN32
        if (short* const grid = core::ApplicationTerrainGrid())
            ::operator delete(static_cast<void*>(grid));
        core::SetApplicationTerrainGrid(nullptr);
#else
        m_gridZ.clear();
#endif
    }

    void MAP::setWeaponTableState(int count, WEAPON* table) noexcept
    {
#ifdef _WIN32
        core::SetApplicationWeaponCount(count);
        core::SetApplicationWeaponTable(table);
#else
        m_weaponCount = count;
        m_weaponTable = table;
#endif
    }

    GROUPS& MAP::groupOwner() noexcept
    {
#ifdef _WIN32
        void* const owner = core::ApplicationPhysicalOwner();
        return *reinterpret_cast<GROUPS*>(static_cast<std::uint8_t*>(owner) + core::retail_application_layout::Groups);
#else
        return m_groupOwner;
#endif
    }

    const GROUPS& MAP::groupOwner() const noexcept
    {
#ifdef _WIN32
        const void* const owner = core::ApplicationPhysicalOwner();
        return *reinterpret_cast<const GROUPS*>(static_cast<const std::uint8_t*>(owner) + core::retail_application_layout::Groups);
#else
        return m_groupOwner;
#endif
    }

    SCRIPT& MAP::scriptRuntime()
    {
        return *core::ApplicationScriptRuntime();
    }

    const SCRIPT& MAP::script() const
    {
        return *core::ApplicationScriptRuntime();
    }

    size_t MAP::noVid() const
    {
        const int count = core::GlobalApplicationVidTable().count();
        return count > 0 ? static_cast<size_t>(count) : 0u;
    }

    void MAP::resetGameResourceRuntime()
    {
        m_vids.clear();
        core::GlobalApplicationVidTable().clear();
    }

    void MAP::deleteSpritesCollectorHash()
    {
        DeleteGlobalSpriteHashMap();
    }

    void MAP::reinitSpritesCollectorHash()
    {
        core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
        ReinitGlobalSpriteHashMapFromVidTable(m_sizeXY.x,
                                              m_sizeXY.y,
                                              appVidTable.slotData(),
                                              appVidTable.count());
    }

    bool MAP::createStartupSpriteHashMap()
    {
        core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
        appVidTable.setWeaponSentinel(weaponTable());
        MAP::NullVid()->setWeaponRecord(appVidTable.weaponSentinel());

        void* const storage = ::operator new(0x44u, std::nothrow);
        SPRITE_COLLECTOR_HASH_MAP* const created = storage
            ? new (storage) SPRITE_COLLECTOR_HASH_MAP(m_sizeXY.x,
                                                      m_sizeXY.y,
                                                      appVidTable.slotData(),
                                                      appVidTable.count())
            : nullptr;
        SetGlobalSpriteHashMap(created);
        return created != nullptr;
    }

    void MAP::reinitSpritesCollector()
    {

        reinitSpritesCollectorHash();
    }

    bool MAP::loadGameResources()
    {
        RESOURCE res;
        const STRING resourcePath(resolveGameFile(m_objectsResource).string());
        if (!res.openFile(resourcePath, RESOURCE::ResTypes::DATA))
            return false;

        return loadGameResourcesFromResource(&res, true, true);
    }

    bool MAP::ensureGameResourcesLoaded()
    {
        if (!m_vids.empty())
            return true;
        return loadGameResources();
    }

    bool MAP::loadGameResourcesFromResource(RESOURCE* res, bool loadSfxTable, bool loadConstantsBlock)
    {
        if (!res)
            throw std::runtime_error("MAP::loadGameResourcesFromResource: null RESOURCE");

        resetGameResourceRuntime();

        LoadWeapon(res);
        if (loadSfxTable)
            LoadSfx(res);
        if (loadConstantsBlock)
            LoadConstants(res);
        const bool loaded = loadVids(res, true, false);
        if (loaded)
            finishResourcesLoad();
        return loaded;
    }

    bool MAP::hostLoadVidDepot(RESOURCE* res)
    {
        if (!res)
            return false;

        const std::uint32_t loadStart = currentMilliseconds();

        // [Application+0x28C] is loaded only when no WEAPON table exists yet.
        if (!weaponTable())
        {
            void* rawTable = nullptr;
            const int count = loadResourceSectionArray(*res, RESOURCE::ResTypes::WEAPON, &rawTable,
                                         static_cast<unsigned>(WEAPON::AS1_RECORD_SIZE));
            setWeaponTableState(count, static_cast<WEAPON*>(rawTable));
            if (count)
                LOG::Write("LoadWeapon::No=%-5i             sizeof(WEAPON)=%-4i",
                           count, static_cast<int>(WEAPON::AS1_RECORD_SIZE));
        }

        if (res->GoBegin(RESOURCE::ResTypes::OBJECT))
        {
            LOG::ResourceError("%s", 11, "load 'VID'", 0, "");
            return false;
        }

        core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
        const bool overlayExistingDepot = appVidTable.count() != 0;

        do
        {
            int nvid = 0;
            (void)res->read(&nvid, sizeof(nvid));

            if (nvid >= static_cast<int>(core::ApplicationVidTable::kCapacity))
                LOG::ResourceError("%s", 4, "nvid > MAX_VID", nvid, "");

            if (VID* const previous = appVidTable.slot(nvid))
            {
                (void)ReleaseVidForScalarDeletingDestructor(previous);
                delete previous;
                appVidTable.setSlotCell(nvid, nullptr);
                LOG::ResourceError("%s", 5, "This VID already exist", nvid, "");
            }

#ifdef _WIN32
            VID* const created = win::applicationWinInstance()
                ? win::applicationWinInstance()->createVidFromResource(res, nvid)
                : hostCreateVid(res, nvid);
#else
            VID* const created = hostCreateVid(res, nvid);
#endif
            if (created)
            {
                appVidTable.setSlotCell(nvid, created);
                if (nvid >= appVidTable.count())
                    appVidTable.setStoredCount(nvid + 1);
                if (overlayExistingDepot)
                    created->type = static_cast<WORD>(created->type | VID_TYPE_LINKXYZ); // byte [VID+0x2A1] |= 2
                bindWeaponForVid(created);
                if (m_graph)
                    m_graph->advanceMovieFrameClock(appVidTable.slot(0));
            }
        }
        while (!res->GoNextSub(RESOURCE::ResTypes::OBJECT));

        int maxSizeX = 0;
        int maxSizeY = 0;
        const int rawCount = appVidTable.count();
        for (int index = 0; index < rawCount; ++index)
        {
            VID* const vid = appVidTable.slot(index);
            if (!vid)
                continue;
            vid->SetChildAndLink();
            maxSizeX = std::max(maxSizeX, static_cast<int>(vid->vidWidth()));
            maxSizeY = std::max(maxSizeY, static_cast<int>(vid->vidHeight()));
        }

        const std::uint32_t elapsed = currentMilliseconds() - loadStart;
        LOG::Write("LoadVid::No   =%-15i   sizeof(VID)   =%-5i    load time     =%ims   MaxSizeX,Y=%i,%i",
                   rawCount, 1052, static_cast<int>(elapsed), maxSizeX, maxSizeY);
        return true;
    }

    bool MAP::reloadGameResourceParameters()
    {
        RESOURCE res;
        const STRING resourcePath(resolveGameFile(m_objectsResource).string());
        if (!res.openFile(resourcePath, RESOURCE::ResTypes::DATA))
        {
            LOG::ResourceError("%s", 7, "resource file", 0, "");
            return false;
        }
        return reloadGameResourceParametersFromResource(&res);
    }

    bool MAP::reloadGameResourceParametersFromResource(RESOURCE* res)
    {
        if (!res)
            throw std::runtime_error("MAP::reloadGameResourceParametersFromResource: null RESOURCE");

        core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
        int rawCount = appVidTable.count();
        for (int index = 0; index < rawCount; ++index)
        {
            VID* const tableVid = appVidTable.slot(index);
            if (!tableVid)
                continue;
            VID* const exchanged = tableVid->exchangedVidRef();
            if (exchanged && exchanged != tableVid)
                (void)swapVidReferences(exchanged, tableVid);
            rawCount = appVidTable.count();
        }

        if (WEAPON* const previous = weaponTable())
            ::operator delete(previous);
        setWeaponTableState(0, nullptr);
        void* rawWeaponTable = nullptr;
        const int loadedWeaponCount = loadResourceSectionArray(*res, RESOURCE::ResTypes::WEAPON,
                                           &rawWeaponTable,
                                           static_cast<unsigned>(WEAPON::AS1_RECORD_SIZE));
        setWeaponTableState(loadedWeaponCount, static_cast<WEAPON*>(rawWeaponTable));

        if (res->GoBegin(RESOURCE::ResTypes::OBJECT))
        {
            LOG::ResourceError("%s", 11, "load 'VID'", 0, "");
            return false;
        }

        do
        {
            int objectNvid = 0;
            if (res->read(&objectNvid, sizeof(objectNvid)) != 0)
                throw std::runtime_error("MAP::reloadGameResourceParametersFromResource: failed to read nvid");

            if (objectNvid >= 0x800)
                LOG::ResourceError("%s", 4, "nvid > MAX_VID", objectNvid, "");

            VID* const slotVid = appVidTable.slot(objectNvid);
            if (!slotVid)
                continue;

            VID* const exchanged = slotVid->exchangedVidRef();
            const int targetNvid = exchanged->nvid();
            VID* const target = appVidTable.slot(targetNvid);
            if (!target)
                continue;

            STRING objectName;
            objectName.Read(res);
            target->name = objectName;
            target->LoadParameters(res);

            WEAPON* const weaponRecords = weaponTable();
            const int nWeapon = target->nWeapon;
            if (nWeapon < weaponCount())
                target->weapon = weaponRecords + nWeapon;
            else
                target->weapon = weaponRecords;
        }
        while (!res->GoNextSub(RESOURCE::ResTypes::OBJECT));

        rawCount = appVidTable.count();
        for (int index = 0; index < rawCount; ++index)
        {
            VID* const vid = appVidTable.slot(index);
            if (vid && (vid->formatFlags() & VID_TYPE_LINKXYZ) == 0u)
                vid->SetChildAndLink();
        }
        return true;
    }

    void MAP::LoadWeapon(RESOURCE* res)
    {
        if (!res)
            throw std::runtime_error("MAP::LoadWeapon: null RESOURCE");

        if (WEAPON* const previous = weaponTable())
            ::operator delete(previous);
        setWeaponTableState(0, nullptr);

        void* rawTable = nullptr;
        const int count = loadResourceSectionArray(*res, RESOURCE::ResTypes::WEAPON, &rawTable,
                                    static_cast<unsigned>(WEAPON::AS1_RECORD_SIZE));
        setWeaponTableState(count, static_cast<WEAPON*>(rawTable));

        LOG::Write("LoadWeapon::No=%-5i             sizeof(WEAPON)=%-4i",
                   count,
                   static_cast<int>(WEAPON::AS1_RECORD_SIZE));
    }

    void MAP::LoadSfx(RESOURCE* res)
    {
        m_sfx.clear();
        if (!res)
            throw std::runtime_error("MAP::LoadSfx: null RESOURCE");

        if (res->GoBegin(RESOURCE::ResTypes::SFX))
            return;

        const std::filesystem::path root(m_resourceRoot.str().empty() ? "." : m_resourceRoot.str());
        do
        {
            const int subSize = res->SubSize();
            if (subSize <= 0)
                throw std::runtime_error("MAP::LoadSfx: empty AS1 SFX record");
            std::vector<BYTE> record(static_cast<size_t>(subSize));
            if (res->read(record.data(), static_cast<unsigned>(record.size())) != 0)
                throw std::runtime_error("MAP::LoadSfx: failed to read SFX record");
            m_sfx.push_back(SFX::fromAS1Record(record.data(), record.size(), root));
        }
        while (!res->GoNextSub(RESOURCE::ResTypes::SFX));
    }

    void MAP::LoadConstants(RESOURCE* res)
    {
        // Original startup calls BASE_CONSTANTS::Load through the CNST owner after opening objects.res.
        // Keep the owner route visible here until the global startup object graph is split from MAP.
        (void)m_constants.Load(res);
    }

    const SFX* MAP::Sfx(int nsfx) const
    {
        if (nsfx < 0 || static_cast<size_t>(nsfx) >= m_sfx.size())
            return nullptr;
        return &m_sfx[static_cast<size_t>(nsfx)];
    }

    bool MAP::loadVids(RESOURCE* res, bool initialLoad, bool extra)
    {
        (void)extra;
        if (!res)
            throw std::runtime_error("MAP::loadVids: null RESOURCE");

        const std::uint32_t loadStart = currentMilliseconds();

        if (initialLoad && !extra)
            core::GlobalApplicationVidTable().clear();

        if (res->GoBegin(RESOURCE::ResTypes::OBJECT))
            return !initialLoad;

        do
        {
            int nvid = 0;
            if (res->read(&nvid, sizeof(nvid)) != 0)
                throw std::runtime_error("MAP::loadVids: failed to read nvid");
            if (nvid < 0)
                throw std::runtime_error("MAP::loadVids: negative nvid");

            VID* created = hostCreateVid(res, nvid);
            if (created)
            {
                core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
                table.setSlotCell(nvid, created);
                if (nvid >= table.count())
                    table.setStoredCount(nvid + 1);
                bindWeaponForVid(created);
            }
        }
        while (!res->GoNextSub(RESOURCE::ResTypes::OBJECT));

        finishResourcesLoad();

        int maxSizeX = 0;
        int maxSizeY = 0;
        for (const auto& vid : m_vids)
        {
            if (!vid)
                continue;
            if (vid->vidSizeX > maxSizeX)
                maxSizeX = vid->vidSizeX;
            if (vid->vidSizeY > maxSizeY)
                maxSizeY = vid->vidSizeY;
        }

        const std::uint32_t elapsed = currentMilliseconds() - loadStart;
        LOG::Write("LoadVid::No   =%-15i   sizeof(VID)   =%-5i    load time     =%ims   MaxSizeX,Y=%i,%i",
                   core::GlobalApplicationVidTable().count(),
                   1052,
                   static_cast<int>(elapsed),
                   maxSizeX,
                   maxSizeY);
        return true;
    }

    void MAP::finishResourcesLoad()
    {
        core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
        const int rawCount = appVidTable.count();
        for (int index = 0; index < rawCount; ++index)
        {
            VID* const vid = appVidTable.slot(index);
            if (vid)
                vid->SetChildAndLink();
        }
    }

    VID* MAP::hostCreateVid(RESOURCE* res, int nvid)
    {
        STRING objectName;
        objectName.Read(res);

        VID loaded_vid;
        loaded_vid.nVid = nvid;
        loaded_vid.name = objectName;
        loaded_vid.noCadr = 32000; // sentinel only for OBJ-side parameter probing, exactly as src2022 MAP::hostCreateVid does.
        const size_t parameterBegin = res->position();
        loaded_vid.LoadParameters(res);

        STRING vidFileName;
        vidFileName.Read(res);
        const std::string sourceVidName = normalizeGamePath(vidFileName.str());
        std::string loadVidName = sourceVidName;

        res->seek(parameterBegin);

        {
            core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
            const int rawCount = table.count();
            const std::string retailVidName = vidFileName.str();

            for (int index = 0; index < rawCount; ++index)
            {
                VID* const existing = table.slot(index);
                if (!existing)
                    continue;

                if (existing->sourceVidPath().str() != retailVidName)
                    continue;

                const DWORD existingClass = existing->spriteClassId();
                if (loaded_vid.spriteClassId() == B_BUILDEDTERRAIN)
                {
                    if (existingClass != B_BUILDEDTERRAIN)
                        continue;
                }
                else if (existingClass == B_BUILDEDTERRAIN)
                {
                    continue;
                }

                if ((existing->formatFlags() & VID_TYPE_HARDWARE) == 0u)
                {
                    if (existing->gammaRaw.first != loaded_vid.gammaRaw.first ||
                        existing->gammaRaw.second != loaded_vid.gammaRaw.second)
                    {
                        continue;
                    }
                    if (((existing->properties() ^ loaded_vid.properties()) & P_GAMMA) != 0u)
                        continue;
                }

                std::unique_ptr<VID> mirror(existing->CreateMirror());
                if (!mirror)
                    break;

                mirror->nVid = nvid;
                mirror->name = objectName;
                mirror->vidName = vidFileName;
                mirror->LoadParameters(res);
                mirror->SetLayer();

                VID* const rawMirror = mirror.get();
                std::size_t ownerIndex = static_cast<std::size_t>(std::max(nvid, 0));
                if (ownerIndex >= m_vids.size())
                    m_vids.resize(ownerIndex + 1u);
                if (m_vids[ownerIndex])
                {
                    auto empty = std::find_if(m_vids.begin(), m_vids.end(),
                        [](const std::unique_ptr<VID>& holder) { return holder.get() == nullptr; });
                    if (empty != m_vids.end())
                        ownerIndex = static_cast<std::size_t>(std::distance(m_vids.begin(), empty));
                    else
                    {
                        ownerIndex = m_vids.size();
                        m_vids.emplace_back();
                    }
                }
                m_vids[ownerIndex] = std::move(mirror);
                return rawMirror;
            }

            // The scan has consumed the parameter block only on a successful
            // mirror route.  The ordinary load path begins from the same saved
            // OBJ parameter offset as retail.
            res->seek(parameterBegin);
        }

        std::unique_ptr<VID> vid;
        WORD realType = 0;
        bool realHeadLoaded = false;
        RESOURCE resourceForLoad;
        bool generatedTempVid = false;

        if (!hasExtension(loadVidName, ".vid"))
        {
#ifdef _WIN32
            const std::filesystem::path sourcePath = resolveGameFile(STRING(loadVidName));
            const STRING colorPath(sourcePath.string());
            int pictureOpenResult = 1;

            if (loaded_vid.spriteClass != 19)
            {
                images::PICTURE_COMPOSITE_RESOURCE composite;
                pictureOpenResult = composite.openFilenames(colorPath, STRING(""), STRING(""));
                if (pictureOpenResult == 0)
                {
                    char* const tempName = _tempnam("c:\\tmp", "vid");
                    if (tempName)
                    {
                        loadVidName = tempName;
                        std::free(tempName);
                        composite.writeVidResource(STRING(loadVidName), 0u);
                        generatedTempVid = true;
                    }
                }
            }
            else
            {
                images::PICTURE_SCROLL_COMPOSITE_RESOURCE composite;
                pictureOpenResult = composite.openFilenames(colorPath, STRING(""), STRING(""));
                if (pictureOpenResult == 0)
                {
                    char* const tempName = _tempnam("c:\\tmp", "vid");
                    if (tempName)
                    {
                        loadVidName = tempName;
                        std::free(tempName);
                        composite.writeVidResource(STRING(loadVidName), 0u);
                        generatedTempVid = true;
                    }
                }
            }

            if (pictureOpenResult != 0 || !generatedTempVid)
                LOG::Write("Load::Can't open file %s", sourceVidName.c_str());
#endif
        }

        const std::filesystem::path resolvedVidPath = generatedTempVid
            ? std::filesystem::path(loadVidName)
            : resolveGameFile(STRING(loadVidName));
        if (!resourceForLoad.openFile(STRING(resolvedVidPath.string()), RESOURCE::ResTypes::VID))
        {
            LOG::ResourceError("VID [%i-%s]", 7, loadVidName.c_str(), 0, nvid, objectName.c_str());
            if (generatedTempVid)
                std::remove(loadVidName.c_str());
            return nullptr;
        }
        if (resourceForLoad.GoBegin(RESOURCE::ResTypes::HEAD))
        {
            LOG::ResourceError("VID [%i-%s]", 3, "HEAD", 0, nvid, objectName.c_str());
            if (generatedTempVid)
                std::remove(loadVidName.c_str());
            return nullptr;
        }
        if (resourceForLoad.read(&realType, sizeof(realType)) != 0)
        {
            LOG::ResourceError("VID [%i-%s]", 3, "HEAD type", 0, nvid, objectName.c_str());
            if (generatedTempVid)
                std::remove(loadVidName.c_str());
            return nullptr;
        }
        loaded_vid.type = realType;
        vid.reset(createVIDByType(realType, loaded_vid.spriteClass));
        realHeadLoaded = true;

        vid->type = realType;
        vid->nVid = nvid;
        vid->name = objectName;
        vid->vidName = STRING(loadVidName);
        vid->loadBasicParameters(&resourceForLoad);

        vid->LoadParameters(res);

        if (realHeadLoaded)
        {
            vid->Load(&resourceForLoad);
        }

        if (generatedTempVid)
        {
            resourceForLoad.close();
            std::remove(loadVidName.c_str());
        }

        vid->SetLayer();

        VID* raw = vid.get();

        // m_vids is host lifetime storage only.  swapVidReferences swaps retail raw
        // Application table identities without swapping allocation ownership,
        // therefore an NVID index is not a stable unique_ptr index after an
        // exchange. Prefer the matching empty NVID cell, otherwise reuse any
        // released holder before appending a new host-only owner.
        std::size_t ownerIndex = static_cast<std::size_t>(std::max(nvid, 0));
        if (ownerIndex >= m_vids.size())
            m_vids.resize(ownerIndex + 1u);
        if (m_vids[ownerIndex])
        {
            auto empty = std::find_if(m_vids.begin(), m_vids.end(),
                [](const std::unique_ptr<VID>& holder) { return holder.get() == nullptr; });
            if (empty != m_vids.end())
                ownerIndex = static_cast<std::size_t>(std::distance(m_vids.begin(), empty));
            else
            {
                ownerIndex = m_vids.size();
                m_vids.emplace_back();
            }
        }
        m_vids[ownerIndex] = std::move(vid);
        return raw;
    }

    VID* MAP::createVIDByType(WORD type, DWORD spriteClass) const
    {
        if (type & VID_TYPE_NEW_ZBUFFER)
            return new VID_SURFACE();
        if (type & VID_TYPE_LIGHT)
            return new VID_LIGHT();
        if ((type & VID_TYPE_HARDWARE) && (type & VID_TYPE_ALPHA) && (type & VID_TYPE_ZBUFFER))
            return new VID_HARDWARE_Z();
        if (type & VID_TYPE_HARDWARE)
            return new VID_HARDWARE();
        if (spriteClass == B_BUILDEDTERRAIN)
            return new VID_SOFTWARE16();
        if (m_graph->GraphFlag34Bit1())
            return new VID_SOFTWARE();
        return new VID_SOFTWARE16();
    }

    VID* MAP::createVIDByType(VID::VidType type, bool, bool) const
    {
        switch (type)
        {
        case VID::VidType::type_VID_SOFTWARE:     return new VID_SOFTWARE();
        case VID::VidType::type_VID_SOFTWARE_PNG: return new VID_SOFTWARE_PNG();
        case VID::VidType::type_VID_SOFTWARE16:   return new VID_SOFTWARE16();
        case VID::VidType::type_VID_HARDWARE:   return new VID_HARDWARE();
        case VID::VidType::type_VID_HARDWARE_Z: return new VID_HARDWARE_Z();
        case VID::VidType::type_VID_SURFACE:    return new VID_SURFACE();
        case VID::VidType::type_VID_LIGHT:      return new VID_LIGHT();
        default:                                return new VID();
        }
    }

    VID* MAP::Vid(int nvid) const
    {
        const core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
        if (nvid < 0 || nvid >= table.count())
            return nullptr;
        return table.slot(nvid);
    }

    VID* MAP::NullVid()
    {
        return g_retailNullVid;
    }

    bool MAP::HasVidSlot(int nvid) const
    {
        const core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
        return nvid >= 0 && nvid < table.count() &&
               table.slot(nvid) != nullptr;
    }

    VID* MAP::VidOrNull(int nvid) const
    {
        VID* const result = Vid(nvid);
        return result ? result : NullVid();
    }

    void MAP::bindWeaponForVid(VID* vid)
    {
        WEAPON* const table = weaponTable();
        const int count = weaponCount();
        if (!vid || !table || count <= 0)
            return;

        if (vid->nWeapon >= 0 && vid->nWeapon < count)
            vid->weapon = table + vid->nWeapon;
        else
        {
            vid->ReportResourceError(10, "nWeapon > noWeapon", vid->nWeapon);
            vid->weapon = table;
        }
    }

    int MAP::swapVidReferences(VID* first, VID* second)
    {
        const auto retailPointerInt = [](const void* pointer) noexcept -> int {
            const std::uint32_t raw = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer));
            std::int32_t signedRaw = 0;
            std::memcpy(&signedRaw, &raw, sizeof(signedRaw));
            return static_cast<int>(signedRaw);
        };

        if (!first || !second || first == second)
            return retailPointerInt(first);

        core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
        const int rawCount = appVidTable.count();

        for (int index = 0; index < rawCount; ++index)
        {
            VID* vid = appVidTable.slot(index);
            if (!vid)
                continue;

            if (vid->linkVid == first)
                vid->linkVid = second;
            else if (vid->linkVid == second)
                vid->linkVid = first;

            for (int i = 0; i < VID::NO_ANIMATION; ++i)
            {
                if (vid->childVid[i] == first)
                    vid->childVid[i] = second;
                else if (vid->childVid[i] == second)
                    vid->childVid[i] = first;
            }
        }

        const int firstSlot = first->nVid;
        const int secondSlot = second->nVid;
        appVidTable.setSlotCell(firstSlot, second);
        appVidTable.setSlotCell(secondSlot, first);

        // m_vids is host lifetime storage only. Retail swapVidReferences moves raw
        // Application table identities, never allocation ownership. Do not
        // synchronize the unique_ptr container with these slot writes.

        std::swap(first->exchangedVid, second->exchangedVid);
        std::swap(first->nVid, second->nVid);

        // swapVidReferences swaps 18 DWORDs from [VID+0x3B8..0x3FC].  In the restored
        // C++ layout this proven tail corresponds to the first 18 script slots.
        const int count = std::min(VID::ScriptFunctionSlotCount, static_cast<int>(VID::_funcs_count));
        for (int i = 0; i < count; ++i)
            std::swap(first->scriptFunction[i], second->scriptFunction[i]);
        // Retail leaves EAX = first-second using 32-bit SUB wraparound.
        const std::uint32_t rawDifference =
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(first)) -
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(second));
        std::int32_t signedDifference = 0;
        std::memcpy(&signedDifference, &rawDifference, sizeof(signedDifference));
        return static_cast<int>(signedDifference);
    }

    std::filesystem::path MAP::resolveGameFile(const STRING& gamePath) const
    {
        namespace fs = std::filesystem;
        const fs::path root(m_resourceRoot.str().empty() ? "." : m_resourceRoot.str());
        const std::string normalized = normalizeGamePath(gamePath.str());
        fs::path candidate(normalized);
        if (candidate.is_absolute() && fs::exists(candidate))
            return candidate;

        std::vector<fs::path> attempts;
        attempts.push_back(root / candidate);

        const std::string lower = lowerCopy(normalized);
        attempts.push_back(root / fs::path(lower));

        if (lower.rfind("vid/", 0) == 0)
            attempts.push_back(root / "Vid" / lower.substr(4));
        if (lower.rfind("maps/", 0) == 0)
            attempts.push_back(root / "Maps" / lower.substr(5));
        if (lower.rfind("wav/", 0) == 0)
            attempts.push_back(root / "Wav" / lower.substr(4));

        for (const auto& p : attempts)
        {
            if (fs::exists(p))
                return p;
            fs::path resolved;
            if (findExistingCaseInsensitive(p, resolved))
                return resolved;
        }
        return attempts.front();
    }

    void MAP::hostReleaseLinkVidRuntime()
    {
        core::ApplicationDrawDispatcherState& drawState = core::GlobalApplicationDrawDispatcherState();
        for (int pass = 0; pass < core::ApplicationDrawDispatcherState::PassCount; ++pass)
        {
            core::ApplicationDrawPassBucket& bucket = drawState.drawPassBucket(pass);
            int cursor = bucket.count() - 1;
            while (cursor >= 0)
            {
                SPRITE* sprite = bucket.spriteAt(cursor);
                while (!sprite && --cursor >= 0)
                    sprite = bucket.spriteAt(cursor);
                if (!sprite)
                    break;

                VID* const vid = sprite->Vid();
                if ((vid->formatFlags() & VID_TYPE_LINKXYZ) != 0)
                {

                    ReleaseSpriteForScalarDeletingDestructor(sprite);
                    delete sprite;
                }
                --cursor;
            }
        }

        // Then retail scans exactly Application+0x290 VID slots backwards,
        // calls each matching VID deleting-vtable slot +4 with flag 1, clears
        // the raw slot, and trims only the trailing null count afterwards.
        core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
        const int originalCount = appVidTable.count();
        for (int index = originalCount - 1; index >= 0; --index)
        {
            VID* const vid = appVidTable.slot(index);
            if (!vid || (vid->formatFlags() & VID_TYPE_LINKXYZ) == 0)
                continue;

            // swapVidReferences exchanges raw NVID identity/table slots but not host
            // allocation ownership.  The holder index therefore cannot be
            // derived from the current retail NVID after an exchange. Release
            // the matching host owner by pointer before invoking the raw
            // deleting route, otherwise a later vector teardown can double-free
            // the already deleted VID.
            (void)ReleaseVidForScalarDeletingDestructor(vid);
            delete vid;
            appVidTable.setSlotCell(index, nullptr);
        }

        int trimmedCount = originalCount;
        while (trimmedCount > 0 && appVidTable.slot(trimmedCount - 1) == nullptr)
            --trimmedCount;
        appVidTable.setStoredCount(trimmedCount);

        // Host ownership indices are not NVID indices after swapVidReferences exchanges.
        // Retail trims only Application+0x290; it does not have a second owner
        // container to truncate.  Shrinking m_vids by trimmedCount can destroy a
        // surviving VID whose raw Application+0x294 slot still points at it,
        // leaving STEXT/font and render callers with a dangling VID pointer.
        // Keep the host owner vector intact; released LINKXYZ owners are already
        // nulled by ReleaseVidForScalarDeletingDestructor above.
    }

    void MAP::releaseSpriteReferencesHost(SPRITE* sprite)
    {
#ifdef _WIN32
        // Win32 retail has no second MAP owner for clearSpriteReferencesAcrossRuntime.
        win::applicationWinInstance()->clearSpriteReferencesAcrossRuntime(sprite);
#else

        scriptRuntime().clearSpriteReferencesFromExecutionStack(sprite);

        // removeSpriteReferences(Application+0x264, target).
        groupOwner().removeSpriteReferences(sprite);

        if (sprite->listReferenceCount() > 1)
        {
            SPRITE_COLLECTOR_HASH_MAP* hash = GlobalSpriteHashMap();
            int cursor = static_cast<int>(hash->overflowList().count()) - 1;
            while (cursor >= 0)
            {
                const SPRITE_POINTER_LIST& overflow = hash->overflowList();
                SPRITE* current = overflow.at(static_cast<std::size_t>(cursor));
                while (!current && --cursor >= 0)
                    current = overflow.at(static_cast<std::size_t>(cursor));
                if (!current)
                    break;

                current->DeletePointerToSprite(sprite);
                hash = GlobalSpriteHashMap();
                if (cursor > static_cast<int>(hash->overflowList().count()))
                    cursor = static_cast<int>(hash->overflowList().count());
                --cursor;
            }
        }

        // Application+0x58: 16 BaseSpriteList owners, stride 0x10.
        core::ApplicationDrawDispatcherState& drawState = core::GlobalApplicationDrawDispatcherState();
        for (int pass = 0; pass < core::ApplicationDrawDispatcherState::PassCount; ++pass)
        {
            if (sprite->listReferenceCount() <= 1)
                continue;

            const core::ApplicationDrawPassBucket& bucket = drawState.drawPassBucket(pass);
            int cursor = bucket.count() - 1;
            while (cursor >= 0)
            {
                SPRITE* current = bucket.spriteAt(cursor);
                while (!current && --cursor >= 0)
                    current = bucket.spriteAt(cursor);
                if (!current)
                    break;
                current->DeletePointerToSprite(sprite);
                --cursor;
            }
        }
    #endif
    }

    void MAP::hostReleaseWorldRuntime()
    {
        relationTable().clear();

        // Retail [Application+0x0C] = 1.0f precedes GRAPH cleanup.
        core::SetApplicationTickScale(1.0f);
#ifdef _WIN32
        if (win::ApplicationWin* const app = win::applicationWinInstance())
            app->resetTickScale();
#endif

        m_graph->resetMapRenderRuntimeState();

        // Application+0x1A4 is the persistent DEMO RESOURCE. Bit 0x0200 is
        // playback; bit 0x0100 is recording. Retail tests both independently.
        std::uint32_t rawFlags = core::ApplicationFlags();
        if ((rawFlags & application_flags::DemoUseResource) != 0u)
        {
            Mouse->HardwareOn();
            closeResourceOwner(demoResource());
        }
        if ((rawFlags & application_flags::DemoWriteToResource) != 0u)
        {
            const std::int32_t endMarker = -1;
            demoResource().write(&endMarker, sizeof(endMarker));
            endResourceSection(demoResource());
            closeResourceOwner(demoResource());
        }

        rawFlags &= ~(application_flags::DemoUseResource | application_flags::DemoWriteToResource);
        core::SetApplicationFlags(rawFlags);
#ifdef _WIN32
        if (win::ApplicationWin* const app = win::applicationWinInstance())
            app->setFlags(rawFlags);
#endif
        core::SetApplicationScrollType(1u);

        LOG::Write("Player release");

#ifdef _WIN32
        if (win::ApplicationWin* const app = win::applicationWinInstance())
        {
            PLAYER* const players[4] = {
                app->playerSlotByIndex(0),
                app->playerSlotByIndex(1),
                app->playerSlotByIndex(2),
                app->playerSlotByIndex(3)
            };
            for (PLAYER* const player : players)
                if (player)
                    player->resetPlayerSpriteReferences();
        }
#endif
        m_playerSlots.clear();
        m_playerSprites.clear();

        LOG::Write("Sprite release");

        core::ApplicationDrawDispatcherState& drawState = core::GlobalApplicationDrawDispatcherState();
        core::SetBulkSpriteDeleteActive(1u);
        for (int pass = 0; pass < core::ApplicationDrawDispatcherState::PassCount; ++pass)
        {
            core::ApplicationDrawPassBucket& bucket = drawState.drawPassBucket(pass);
            int cursor = bucket.count() - 1;
            while (cursor >= 0)
            {
                SPRITE* sprite = bucket.spriteAt(cursor);
                while (!sprite && --cursor >= 0)
                    sprite = bucket.spriteAt(cursor);
                if (!sprite)
                    break;

                ReleaseSpriteForScalarDeletingDestructor(sprite);
                delete sprite;

                --cursor;
            }
        }

        core::SetBulkSpriteDeleteActive(0u);
        for (int pass = 0; pass < core::ApplicationDrawDispatcherState::PassCount; ++pass)
        {
            const core::ApplicationDrawPassBucket& bucket = drawState.drawPassBucket(pass);
            for (int cursor = bucket.count() - 1; cursor >= 0; --cursor)
            {
                SPRITE* const survivor = bucket.spriteAt(cursor);
                if (!survivor)
                    continue;
                const int nvid = survivor->Vid() ? survivor->Vid()->nVid : -1;
                LOG::ResourceError("SPRITE %i", 10, "Sprite exist after delete", cursor, nvid);
                break;
            }
        }

        if (!groupOwner().empty())
            LOG::ResourceError("%s", 10, "Incorrect delete groups in DeleteAll()", 0, "");

        LOG::Write("Menu release");

        SPRITE_LIST& frameList = applicationFrameSpriteList();
        if (frameList.activeCount() != 0)
        {
            SPRITE* const first = frameList.at(0);
            const int nvid = first->Vid() ? first->Vid()->nVid : -1;
            LOG::ResourceError("SPRITE %i", 10, "Menu sprite exist after delete", 0, nvid);
            frameList.releaseRepeatedReferencesRetail();
        }

        const std::uint32_t now = core::CurrentTimeMilliseconds();
        const std::uint32_t start = core::ApplicationWorldStartTime();
        const std::uint32_t elapsed = now - start;
        const std::uint32_t averageFps = elapsed != 0u
            ? (1000u * core::ApplicationWorldFrameCounter()) / elapsed
            : 0u;
        LOG::Write("Average fps=%i", static_cast<int>(averageFps));

        LOG::Write("Script release");

        scriptRuntime().resetScriptVmState();
        core::SetApplicationWorldFrameCounter(0u);
#ifdef _WIN32
        if (win::ApplicationWin* const app = win::applicationWinInstance())
            app->resetWorldFrameCounter();
#endif
#ifdef _WIN32
        if (win::ApplicationWin* const app = win::applicationWinInstance())
            app->releaseLinkVidRuntime();
        else
            hostReleaseLinkVidRuntime();
#else
        hostReleaseLinkVidRuntime();
#endif

        const core::ApplicationVidTable& vidTable = core::GlobalApplicationVidTable();
        const int vidCount = vidTable.count();
        for (int index = 0; index < vidCount; ++index)
        {
            if (VID* const vid = vidTable.slot(index))
                vid->initializeRuntimeCountersAndCallbacks();
        }

        core::resetScriptCallbackSlots();

        m_spriteRecordCount = 0;
        m_nullSpriteRecordCount = 0;
        m_missingVidSpriteRecordCount = 0;
        m_invalidSpriteRecordCount = 0;
        m_restoreEntries.clear();
        m_attachedRestoreEntries = 0;
        m_restoreCommandCount = 0;
        m_appliedRestoreStates = 0;
        m_actionStackSprites = 0;
        m_resolvedRestoreObjectRefs = 0;
        m_unresolvedRestoreObjectRefs = 0;
        m_restoreLayoutCounts.clear();
        m_scrollMinXY = VECTOR2{};
        m_scrollMaxXY = VECTOR2{};
        m_originalShiftXY = VECTOR2{};
        m_shiftDeltaXY = VECTOR2{};
        m_spriteByOldAddress.clear();
    }

    void MAP::clearLoadedMapRuntime()
    {
        m_sprites.clear();

        m_spriteRecordCount = 0;
        m_nullSpriteRecordCount = 0;
        m_missingVidSpriteRecordCount = 0;
        m_invalidSpriteRecordCount = 0;
        m_restoreEntries.clear();
        m_attachedRestoreEntries = 0;
        m_restoreCommandCount = 0;
        m_appliedRestoreStates = 0;
        m_actionStackSprites = 0;
        m_resolvedRestoreObjectRefs = 0;
        m_unresolvedRestoreObjectRefs = 0;
        m_restoreLayoutCounts.clear();
        m_playerSlots.clear();
        m_playerSprites.clear();
        m_scrollMinXY = VECTOR2{};
        m_scrollMaxXY = VECTOR2{};
        m_originalShiftXY = VECTOR2{};
        m_shiftDeltaXY = VECTOR2{};
        m_spriteByOldAddress.clear();
    }

    bool MAP::loadMapResourceSections(RESOURCE& mapResource)
    {
        if (!startLoadMap(&mapResource))
            return false;

        loadGridZ(&mapResource);
        if (!loadSprites(&mapResource))
            return false;

        core::buildCrossingLinkGrid(&core::globalWeakControllerMap());

        if (!loadSpriteRestoreData(&mapResource))
            return false;

        // PLAY/GROU exist only on the normal GRPH-present branch.
        if (!m_useLegacyCompactSpriteRecords)
        {
            if (!loadPlayers(&mapResource))
                return false;
            if (!loadGroups(&mapResource))
                return false;
        }
        return true;
    }

    void MAP::installScriptNativeContext()
    {
        ScriptNativeContext nativeContext;
        // The only remaining split-owner callback is the portable non-Win32
        // carrier for Application+0x18.  Retail Win32 SCRIPT writes the
        // Application slot directly from case 0x62.
        nativeContext.queueMapLoadSlot18Flag40 = [](const STRING& path)
        {
            const std::uint32_t flags = core::ApplicationFlags() | application_flags::PendingCommandOrLoad;
            core::SetApplicationFlags(flags);
#ifdef _WIN32
            win::ApplicationWin* const app = win::applicationWinInstance();
            app->setPendingCommand(path);
            app->setFlags(flags);
#endif
        };

        scriptRuntime().SetNativeContext(nativeContext);
    }

    void MAP::saveMapHost(const STRING& outputName)
    {
#ifdef _WIN32
        win::applicationWinInstance()->saveMap(outputName);
#else

        RESOURCE output;
        if (std::strcmp(outputName.c_str(), STRING::SharedEmptyText()) == 0)
            return;

        static const STRING kTemporaryMapName("tmp_del!.map");
        const bool replacingCurrentMap = std::strcmp(outputName.c_str(), m_fileName.c_str()) == 0;
        if (replacingCurrentMap)
        {
            std::rename(m_fileName.c_str(), kTemporaryMapName.c_str());
            m_fileName = kTemporaryMapName;
        }

        if (!output.openFileForWrite(outputName, RESOURCE::ResTypes::MAP))
        {
            LOG::Write("Can't open file %s", outputName.c_str());
            return;
        }

        // Retail copies WEAP/OBJ from the previous MAP only when any loaded VID
        // has bit 0x200 in VID+0x2A0.  Do not synthesize those sections.
        core::ApplicationVidTable& vidTable = core::GlobalApplicationVidTable();
        bool copyObjectSections = false;
        for (int i = 0; i < vidTable.count(); ++i)
        {
            VID* const vid = vidTable.slot(i);
            if (vid && (vid->formatFlags() & 0x0200u) != 0u)
            {
                copyObjectSections = true;
                break;
            }
        }
        if (copyObjectSections)
        {
            RESOURCE previous;
            if (previous.openFile(m_fileName, RESOURCE::ResTypes::MAP))
            {
                output.CopySectionTypeFrom(previous, RESOURCE::ResTypes::WEAPON);
                output.CopySectionTypeFrom(previous, RESOURCE::ResTypes::OBJECT);
                previous.clear();
            }
            else
            {
                LOG::Write("Can't open file %s", m_fileName.c_str());
            }
        }

        output.BeginSection(RESOURCE::ResTypes::GRAPH);
        if (m_graph)
            m_graph->saveGraphParameters(&output);
        output.EndSection();

        output.BeginSection(RESOURCE::ResTypes::HEAD);
        output.write(&m_sizeXY.x, 4u);
        output.write(&m_sizeXY.y, 4u);
        output.write(&m_shiftXY.x, 4u);
        output.write(&m_shiftXY.y, 4u);
        const std::uint32_t now = core::CurrentTimeMilliseconds();
        output.write(&now, 4u);
        const int retailVersion = 10;
        output.write(&retailVersion, 4u);
        output.EndSection();

        output.BeginSection(RESOURCE::ResTypes::GRID);
        if (const short* const grid = terrainGrid())
        {
            const unsigned bytes = static_cast<unsigned>(
                2u * static_cast<unsigned>(terrainGridWidth()) * static_cast<unsigned>(terrainGridHeight()));
            output.write(grid, bytes);
        }
        output.EndSection();

        const SPRITE_LIST& frameList = applicationFrameSpriteList();
        core::ApplicationDrawDispatcherState& drawState = core::GlobalApplicationDrawDispatcherState();
        auto forEachRetailSaveSprite = [&](const auto& fn)
        {
            for (int pass = 0; pass < core::ApplicationDrawDispatcherState::PassCount; ++pass)
            {
                const core::ApplicationDrawPassBucket& bucket = drawState.drawPassBucket(pass);
                for (int index = bucket.count() - 1; index >= 0; --index)
                {
                    SPRITE* const sprite = bucket.spriteAt(index);
                    if (!sprite)
                        continue;
                    if (sprite->childBacklink())
                        continue;
                    if (frameList.contains(sprite))
                        continue;
                    fn(sprite);
                }
            }
        };

        output.BeginSection(RESOURCE::ResTypes::SPRITE);
        forEachRetailSaveSprite([&](SPRITE* sprite)
        {
            sprite->serializeSpriteRecord(&output);
        });
        const std::int32_t spriteTerminator = -1;
        output.write(&spriteTerminator, 4u);
        output.EndSection();

        forEachRetailSaveSprite([&](SPRITE* sprite)
        {
            const std::size_t begin = output.position();
            output.BeginSection(RESOURCE::ResTypes::SPRITEDATA);
            const std::uint32_t rawSprite = static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(sprite) & 0xFFFFFFFFu);
            output.write(&rawSprite, 4u);

            // Retail vtable +0x04 call: Action(0x50, RESOURCE*, 0, 0).
            sprite->Action(static_cast<int>(ActionCode::ACT_SAVE), reinterpret_cast<std::intptr_t>(&output), 0, 0);
            if (output.position() > begin + 5u)
                output.EndSection();
        });
        output.BeginSection(RESOURCE::ResTypes::SPRITEDATA);
        output.write(&spriteTerminator, 4u);
        output.EndSection();

        output.BeginSection(RESOURCE::ResTypes::PLAY);
#ifdef _WIN32
        if (win::ApplicationWin* const app = win::applicationWinInstance())
        {
            PLAYER* const players[4] = {
                app->playerSlotByIndex(0),
                app->playerSlotByIndex(1),
                app->playerSlotByIndex(2),
                app->playerSlotByIndex(3)
            };
            for (PLAYER* const player : players)
                player->saveControlledSpriteReference(&output);
        }
#else
        // Retail target is Win32. Keep portable syntax builds deterministic
        // without inventing an alternate PLAYER owner.
        const std::uint32_t zero = 0u;
        for (int i = 0; i < 4; ++i)
            output.write(&zero, 4u);
#endif
        output.EndSection();

        output.BeginSection(RESOURCE::ResTypes::GROUP);
        groupOwner().Save(&output);
        output.EndSection();
        output.clear();

        if (std::strcmp(m_fileName.c_str(), kTemporaryMapName.c_str()) == 0)
        {
            std::remove(kTemporaryMapName.c_str());
            m_fileName = outputName;
        }
    #endif
    }

    void MAP::load(const STRING& name)
    {
#ifdef _WIN32
        STRING ownedName(name);
        win::applicationWinInstance()->runCommandLineMap(ownedName.DetachOwnedStorage());
#else

        STRING loadName = name;

        {
            const std::uint32_t flags = core::ApplicationFlags() | application_flags::MapLoading;
            core::SetApplicationFlags(flags);
#ifdef _WIN32
            if (win::ApplicationWin* const app = win::applicationWinInstance())
                app->setFlags(flags);
#endif
        }

        if (m_sizeXY.x != 0.0f || m_sizeXY.y != 0.0f)
            hostReleaseWorldRuntime();

        if (!demoResource().isOpen() &&
            demoResource().openFile(loadName, RESOURCE::ResTypes::DEMO))
        {
            readStringLineFromStream(loadName, &demoResource());
            const std::uint32_t flags = core::ApplicationFlags() | application_flags::DemoUseResource;
            core::SetApplicationFlags(flags);
#ifdef _WIN32
            if (win::ApplicationWin* const app = win::applicationWinInstance())
                app->setFlags(flags);
#endif
        }

#ifdef _WIN32
        if (win::ApplicationWin* const app = win::applicationWinInstance())
            app->setCurrentMapName(loadName);
#endif

        deleteSpritesCollectorHash();

        const std::filesystem::path inferredRoot = inferResourceRootFromMapPath(loadName);
        if (!inferredRoot.empty())
        {
            const std::filesystem::path currentRoot(m_resourceRoot.str().empty() ? "." : m_resourceRoot.str());
            if (std::filesystem::absolute(currentRoot).lexically_normal() != std::filesystem::absolute(inferredRoot).lexically_normal())
            {
                m_resourceRoot = STRING(inferredRoot.string());
                resetGameResourceRuntime();
            }
        }
        if (!ensureGameResourcesLoaded())
            throw std::runtime_error("MAP::load: object resource database is not loaded");

        m_fileName = loadName;
        clearLoadedMapRuntime();

        RESOURCE mapResource;
        const auto mapPath = resolveGameFile(loadName);
        if (!mapResource.openFile(STRING(mapPath.string()), RESOURCE::ResTypes::MAP))
            throw std::runtime_error("MAP::load: can't open MAP file: " + mapPath.string());

        std::uint32_t demoStart = currentMilliseconds();
        if ((core::ApplicationFlags() & application_flags::DemoUseResource) != 0u)
            demoResource().read(&demoStart, sizeof(demoStart));
        core::SetDemoStartTimestampMilliseconds(demoStart);

        (void)hostLoadVidDepot(&mapResource);

        if (!loadMapResourceSections(mapResource))
            return;
        {
            const std::uint32_t flags = core::ApplicationFlags() & ~application_flags::MapLoading;
            core::SetApplicationFlags(flags);
#ifdef _WIN32
            if (win::ApplicationWin* const app = win::applicationWinInstance())
                app->setFlags(flags);
#endif
        }

        mapResource.close();
        relationTable().clear();
        installScriptNativeContext();
        loadScript();
        runRetailPostLoadScriptPasses();
    
#endif
    }

    bool MAP::startLoadMap(RESOURCE* map)
    {
        if (!map)
            return false;

        const bool hasGraphSection = (map->GoBegin(RESOURCE::ResTypes::GRAPH) == 0);
        m_useLegacyCompactSpriteRecords = !hasGraphSection;

        if (m_graph)
            m_graph->LoadParameters(map);

        if (map->GoBegin(RESOURCE::ResTypes::HEAD))
        {
            LOG::ResourceError("MAP", 11, "HEAD", 0);
            return false;
        }

        if (!m_useLegacyCompactSpriteRecords)
        {
            map->read(&m_sizeXY.x, 4);
            map->read(&m_sizeXY.y, 4);
            map->read(&m_shiftXY.x, 4);
            map->read(&m_shiftXY.y, 4);
            map->read(&m_currentTime, 4);
            map->read(&m_version, 4);

            if (m_version <= 9)
            {
                int ix = 0, iy = 0, sx = 0, sy = 0;
                std::memcpy(&ix, &m_sizeXY.x, 4);
                std::memcpy(&iy, &m_sizeXY.y, 4);
                std::memcpy(&sx, &m_shiftXY.x, 4);
                std::memcpy(&sy, &m_shiftXY.y, 4);
                m_sizeXY.x = static_cast<float>(ix);
                m_sizeXY.y = static_cast<float>(iy);
                m_shiftXY.x = static_cast<float>(sx);
                m_shiftXY.y = static_cast<float>(sy);
            }
        }
        else
        {
            int ix = 0;
            int iy = 0;
            std::int16_t sx = 0;
            std::int16_t sy = 0;
            map->read(&ix, 4);
            map->read(&iy, 4);
            map->read(&sx, 2);
            map->read(&sy, 2);
            map->read(&m_currentTime, 4);
            map->read(&m_version, 4);
            m_sizeXY.x = static_cast<float>(ix);
            m_sizeXY.y = static_cast<float>(iy);
            m_shiftXY.x = static_cast<float>(sx);
            m_shiftXY.y = static_cast<float>(sy);
            m_useLegacyCompactSpriteRecords = true;
        }

        core::SetApplicationMapWidth(m_sizeXY.x);
        core::SetApplicationMapHeight(m_sizeXY.y);

        if (!m_useLegacyCompactSpriteRecords)
        {
            core::SetPreviousWorldTimeMilliseconds(1u);
            core::SetCurrentTimeMilliseconds(10u);

            std::uint32_t flags = core::ApplicationFlags();
            if ((flags & application_flags::DemoUseResource) == 0u && demoResource().isOpen())
            {
                flags |= application_flags::DemoWriteToResource;
                core::SetApplicationFlags(flags);
#ifdef _WIN32
                if (win::ApplicationWin* const app = win::applicationWinInstance())
                    app->setFlags(flags);
#endif
                demoResource().BeginSection(RESOURCE::ResTypes::DEMO);
                const unsigned mapNameBytes = static_cast<unsigned>(m_fileName.str().size() + 1u);
                demoResource().write(m_fileName.c_str(), mapNameBytes);
                const std::uint32_t demoStart = core::DemoStartTimestampMilliseconds();
                demoResource().write(&demoStart, sizeof(demoStart));
                const std::uint32_t worldClock = core::CurrentTimeMilliseconds();
                demoResource().write(&worldClock, sizeof(worldClock));
            }

            if ((flags & application_flags::DemoUseResource) != 0u)
            {
                std::uint32_t playbackTime = core::CurrentTimeMilliseconds();
                demoResource().read(&playbackTime, sizeof(playbackTime));
                core::SetCurrentTimeMilliseconds(playbackTime);
            }
        }
        else
        {
            core::SetCurrentTimeMilliseconds(m_currentTime);
            core::SetPreviousWorldTimeMilliseconds(m_currentTime);
        }

        core::SetApplicationWorldStartTime(core::CurrentTimeMilliseconds());
#ifdef _WIN32
        if (win::ApplicationWin* const app = win::applicationWinInstance())
            app->setWorldStartTime(core::CurrentTimeMilliseconds());
#endif

        m_originalShiftXY = m_shiftXY;
        core::GlobalApplicationDrawDispatcherState().setCameraShiftX(m_shiftXY.x);
        core::GlobalApplicationDrawDispatcherState().setCameraShiftY(m_shiftXY.y);
        // Original normal and compact HEAD routes both materialize the camera
        // clamp owners as min=(0,0), max=(SizeX,SizeY).
        SetScrollBox(0.0f, 0.0f, m_sizeXY.x, m_sizeXY.y);
        m_shiftDeltaXY = VECTOR2{};

        setTerrainGridDimensions(static_cast<int>(m_sizeXY.x) / 8,
                                     static_cast<int>(m_sizeXY.y) / 8);

        reinitSpritesCollector();

        if (m_graph && !m_useLegacyCompactSpriteRecords)
        {
            const MapCameraClampRect cameraRect = buildMapCameraClampRect(*m_graph, m_scrollMinXY, m_scrollMaxXY);
            SetShiftCoor(m_shiftXY.x + cameraRect.screenW * 0.5f,
                         m_shiftXY.y + cameraRect.screenH * 0.5f,
                         0);
        }
        return true;
    }

    int MAP::hostReinitializeGridFromMapSize()
    {
        releaseTerrainGridStorage();
        const int gridX = static_cast<int>(m_sizeXY.x) / 8;
        const int gridY = static_cast<int>(m_sizeXY.y) / 8;
        setTerrainGridDimensions(gridX, gridY);
#ifdef _WIN32
        const std::size_t bytes = 2u * static_cast<std::size_t>(gridX) * static_cast<std::size_t>(gridY);
        short* const grid = static_cast<short*>(::operator new(bytes));
        replaceTerrainGridStorage(grid);
        std::memset(grid, 0, bytes);
#else
        m_gridZ.resize(static_cast<std::size_t>(gridX * gridY));
        std::fill(m_gridZ.begin(), m_gridZ.end(), static_cast<short>(0));
#endif
        return 0;
    }

    void MAP::loadGridZ(RESOURCE* map)
    {
        ResetGroundZ();
        const int gridX = static_cast<int>(m_sizeXY.x) / 8;
        const int gridY = static_cast<int>(m_sizeXY.y) / 8;
        setTerrainGridDimensions(gridX, gridY);

        if (map->GoBegin(RESOURCE::ResTypes::GRID))
        {
            (void)map->GoBegin(RESOURCE::ResTypes::ANY);
            (void)hostReinitializeGridFromMapSize();
            return;
        }

        const int expectedBytes = 2 * gridX * gridY;
        const int actualBytes = map->SubSize();
        if (actualBytes != expectedBytes)
        {
            LOG::ResourceError("MAP", 4, "grid", actualBytes);
            (void)hostReinitializeGridFromMapSize();
            return;
        }

#ifdef _WIN32
        short* const grid = static_cast<short*>(::operator new(static_cast<std::size_t>(expectedBytes)));
        replaceTerrainGridStorage(grid);
        (void)map->read(grid, static_cast<unsigned>(expectedBytes));
#else
        m_gridZ.resize(static_cast<std::size_t>(gridX * gridY));
        if (!m_gridZ.empty())
            (void)map->read(m_gridZ.data(), static_cast<unsigned>(expectedBytes));
        if (m_gridZ.empty())
            (void)hostReinitializeGridFromMapSize();
#endif
    }

    namespace
    {
        constexpr int END_SPRITE_INT = -1;
        SPRITE* const END_SPRITE_PTR = reinterpret_cast<SPRITE*>(~uintptr_t(0));

        const char* actionName(std::uint32_t opcode)
        {
            return actionCodeName(opcode);
        }

        const char* animationCommandName(std::uint32_t opcode)
        {
            return animationCodeName(opcode);
        }

        std::uint32_t readU32LE(const BYTE* data)
        {
            std::uint32_t v = 0;
            std::memcpy(&v, data, 4);
            return v;
        }

        bool tailIsAllZero(const std::vector<BYTE>& tail)
        {
            for (BYTE b : tail)
                if (b != 0)
                    return false;
            return true;
        }

        std::string hexTail(const std::vector<BYTE>& tail)
        {
            std::ostringstream ss;
            ss << std::hex << std::setfill('0');
            for (BYTE b : tail)
                ss << std::setw(2) << static_cast<unsigned>(b);
            return ss.str();
        }

        std::string commandLayoutName(std::uint32_t opcode)
        {
            if (const char* a = actionName(opcode))
                return a;
            if (const char* a = animationCommandName(opcode))
                return a;
            return "RESTORE_UNKNOWN_COMMAND";
        }

        std::string legacyShapeName(const std::vector<std::uint32_t>& words, const std::vector<BYTE>& tail)
        {
            if (words.empty())
                return "EMPTY";

            bool allZero = true;
            for (std::uint32_t v : words)
                allZero = allZero && (v == 0);
            if (allZero)
            {
                if (words.size() == 1) return "RESTORE_ZERO_1";
                if (words.size() == 2) return "RESTORE_ZERO_2";
                if (words.size() == 3) return "RESTORE_ZERO_3";
                return "RESTORE_ZERO_" + std::to_string(words.size());
            }

            if (words.size() == 4 && words[0] == 0 && words[1] == 0 && words[2] != 0 && words[3] == 3 && tail.empty())
                return "RESTORE_TEXT_LABEL_REF";

            if (words.size() >= 4 && words[0] == 0 && words.back() == 3 && tail.empty())
            {
                const std::uint32_t count = words[1];
                const size_t payloadCount = words.size() - 3;
                if (count > 0 && count == payloadCount)
                    return "RESTORE_DROP_LIST_" + std::to_string(payloadCount);
            }

            if (words.size() >= 4 && words[0] == 0)
            {
                const std::uint32_t count = words[1];
                const size_t payloadCount = words.size() - 3;
                if (count > 0 && count == payloadCount && words.back() != 0 && words.back() != 3 && tailIsAllZero(tail))
                    return "RESTORE_SPAWN_NVID_BEHAVE";
            }

            if (words.size() == 3 && words[0] == 0 && words[1] == 0 && words[2] != 0)
                return "RESTORE_BEHAVE_FLAGS";

            if (words.size() >= 3 && words[0] == 0 && tailIsAllZero(tail))
            {
                const std::uint32_t count = words[1];
                const size_t payloadCount = words.size() - 2;
                if (count > 0 && count == payloadCount)
                    return "RESTORE_SPAWN_NVID";
            }

            if (words.size() >= 4 && words[0] == 0 && words.back() == 0 && tailIsAllZero(tail))
            {
                const std::uint32_t count = words[1];
                const size_t payloadCount = words.size() - 3;
                if (count > 0 && count == payloadCount)
                    return "RESTORE_REWARD_LIST_" + std::to_string(payloadCount);
            }

            if (const char* a = actionName(words[0]))
                return a;

            std::ostringstream ss;
            ss << "RESTORE_SHAPE";
            std::map<std::uint32_t, char> classes;
            char next = 'A';
            for (std::uint32_t v : words)
            {
                ss << '_';
                if (v <= 3)
                    ss << v;
                else
                {
                    auto it = classes.find(v);
                    if (it == classes.end())
                        it = classes.emplace(v, next++).first;
                    ss << it->second;
                }
            }
            if (!tail.empty())
                ss << "_TAIL" << tail.size();
            return ss.str();
        }

        bool decodeZeroTerminatedCP1251(const std::vector<BYTE>& raw, size_t start, std::string& text, size_t& endOffset)
        {
            if (start >= raw.size())
                return false;
            size_t zero = start;
            while (zero < raw.size() && raw[zero] != 0)
                ++zero;
            if (zero == start || zero >= raw.size())
                return false;
            text.assign(reinterpret_cast<const char*>(raw.data() + start), zero - start);
            endOffset = zero + 1;
            return true;
        }
    }

    bool MAP::loadSprites(RESOURCE* map)
    {
        if (!map)
            return false;
        if (map->GoBegin(RESOURCE::ResTypes::SPRITE))
        {
            LOG::ResourceError("MAP", 11, "SPR ", 0);
            return false;
        }

        while (true)
        {
            SPRITE* spr = nullptr;
            if (m_useLegacyCompactSpriteRecords)
                spr = OldLoadSprite(map);
            else
            {
#ifdef _WIN32
                spr = win::applicationWinInstance()->loadSpriteFromMapResource(map, m_version);
#else
                spr = LoadSprite(map, m_version);
#endif
            }
            if (spr == END_SPRITE_PTR)
                break;

            if (!m_useLegacyCompactSpriteRecords)
                m_graph->advanceMovieFrameClock(core::GlobalApplicationVidTable().slot(0));
        }
        return true;
    }

    bool MAP::loadSpriteRestoreData(RESOURCE* map)
    {
        m_restoreEntries.clear();
        m_attachedRestoreEntries = 0;
        m_restoreCommandCount = 0;
        m_appliedRestoreStates = 0;
        m_actionStackSprites = 0;
        m_resolvedRestoreObjectRefs = 0;
        m_unresolvedRestoreObjectRefs = 0;
        m_restoreLayoutCounts.clear();
        if (!map)
            return false;
        if (map->GoBegin(RESOURCE::ResTypes::SPRITEDATA))
        {
            LOG::ResourceError("MAP", 11, "SPRD", 0);
            return false;
        }

#if defined(_WIN32) && UINTPTR_MAX == 0xFFFFFFFFu
        const int restoreOpcode = spriteRestoreActionOpcode();
        while (true)
        {
            SPRITE* const sprite = readSpriteRelationHandle(map);
            if (sprite == END_SPRITE_PTR)
                break;

            if (!m_useLegacyCompactSpriteRecords)
                m_graph->advanceMovieFrameClock(core::GlobalApplicationVidTable().slot(0));

            if (sprite)
            {
                sprite->dispatchVirtualAction(
                    static_cast<std::uint32_t>(restoreOpcode),
                    static_cast<int>(reinterpret_cast<std::uintptr_t>(map)),
                    m_version,
                    0);
                ++m_attachedRestoreEntries;
                ++m_appliedRestoreStates;
            }

            if (map->GoNextSub(RESOURCE::ResTypes::SPRITEDATA))
                break;
        }
#else
        // Portable analysis builds cannot represent the retail 32-bit RESOURCE*
        // Action argument in an int.  Keep the diagnostic decoder here only;
        // it is intentionally excluded from the Win32/x86 retail target.
        while (true)
        {
            MapSpriteRestoreRecord rec;
            if (!readSpriteRestoreSubResource(map, rec))
                break;

            if (rec.sprite)
            {
                // Portable diagnostics only: decode and count the shipped SPRD
                // payload, but never mirror it into a second SPRITE runtime
                // state.  The production Win32/x86 route above enters the
                // retail virtual Action(0x51/0xC8, RESOURCE*, version, 0) owner
                // directly and restores the physical command/state owners.
                rec.attachedToSprite = true;
                rec.state.actionOpcode = spriteRestoreActionOpcode();
                rec.state.mapVersion = m_version;
                rec.state.appliedThroughSpriteAction = false;
                ++m_attachedRestoreEntries;
                if (rec.state.layout == "ACTION_STACK")
                    ++m_actionStackSprites;
            }

            m_restoreCommandCount += rec.state.commands.size();
            m_resolvedRestoreObjectRefs += rec.resolvedObjectRefs.size();
            m_resolvedRestoreObjectRefs += rec.resolvedCommandArgRefs;
            ++m_restoreLayoutCounts[rec.state.layout.empty() ? std::string("UNKNOWN") : rec.state.layout];
            m_restoreEntries.push_back(std::move(rec));

            if (map->GoNextSub(RESOURCE::ResTypes::SPRITEDATA))
                break;
        }
#endif
        return true;
    }

    bool MAP::readSpriteRestoreSubResource(RESOURCE* map, MapSpriteRestoreRecord& out)
    {
        if (!map)
            return false;

        const int subSize = map->SubSize();
        if (subSize < 4)
            return false;

        int oldAddress = 0;
        map->read(&oldAddress, 4);
        if (oldAddress == END_SPRITE_INT)
            return false;

        const int payloadSize = subSize - 4;
        std::vector<BYTE> payload(static_cast<size_t>(payloadSize));
        if (payloadSize > 0 && map->read(payload.data(), static_cast<unsigned>(payloadSize)) != 0)
            throw std::runtime_error("MAP::readSpriteRestoreSubResource: failed to read SPRD payload");

        out.oldAddress = oldAddress;
        out.sprite = ResolveOldSpriteHandle(oldAddress);
        out.state = decodeSpriteRestoreData(oldAddress, payload);
        out.state.actionOpcode = spriteRestoreActionOpcode();
        out.state.mapVersion = m_version;
        out.state.appliedThroughSpriteAction = false;
        out.state.actionExecutionEnabled = false;
        if (out.state.note.empty())
            out.state.note = "SPRD payload decoded by MAP and routed through the concrete sprite Action owner.";
        out.resolvedObjectRefs.clear();
        for (std::uint32_t ref : out.state.objectRefs)
            if (SPRITE* ptr = ResolveOldSpriteHandle(static_cast<int>(ref)))
                out.resolvedObjectRefs.push_back(ptr);
        out.resolvedCommandArgRefs = 0;
        for (const RestoreCommand& cmd : out.state.commands)
            for (SPRITE* ptr : cmd.resolvedSpriteArgs)
                if (ptr)
                    ++out.resolvedCommandArgRefs;
        return true;
    }

    int MAP::spriteRestoreActionOpcode() const
    {
        // The GRPH-absent compact legacy branch calls ACT_RESTORE_OLD_MAP
        // (0xC8). GRPH-present maps, including shipped AS1 version-9 maps, use
        // LoadSprite and ACT_RESTORE (0x51 / decimal 81).
        return m_useLegacyCompactSpriteRecords ? SpriteActConst::ACT_RESTORE_OLD_MAP : SpriteActConst::ACT_RESTORE;
    }

    SpriteRestoreState MAP::decodeSpriteRestoreData(int oldAddress, const std::vector<BYTE>& payload) const
    {
        SpriteRestoreState state;
        state.oldAddress = oldAddress;
        state.payload = payload;

        const size_t wordCount = payload.size() / 4;
        state.words.reserve(wordCount);
        for (size_t i = 0; i < wordCount; ++i)
        {
            const std::uint32_t word = readU32LE(payload.data() + i * 4);
            state.words.push_back(word);
            if (word != 0 && word != 0xFFFFFFFFu && relationTable().getPointer(static_cast<int>(word)))
                state.objectRefs.push_back(word);
        }
        state.tailBytes.assign(payload.begin() + static_cast<std::ptrdiff_t>(wordCount * 4), payload.end());

        if (state.words.empty() && state.tailBytes.empty())
        {
            state.layout = "EMPTY";
            return state;
        }

        // Restore stacks are encoded as: count + count*4 command words + optional trailer.
        // This mirrors the src2022 readSprite()/Action(act_restore, ptr.index(), version) flow while keeping the payload readable.
        if (!state.words.empty())
        {
            const std::uint32_t count = state.words[0];
            const size_t neededWords = 1 + static_cast<size_t>(count) * 4;
            if (count > 0 && count <= 1024 && state.words.size() >= neededWords)
            {
                state.layout = "ACTION_STACK";
                state.decodedUsingExportLgcConstants = true;
                size_t pos = 1;
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    RestoreCommand cmd;
                    cmd.opcode = state.words[pos];
                    cmd.layout = commandLayoutName(cmd.opcode);
                    cmd.opcodeFromExportLgc = isActionCode(cmd.opcode);
                    cmd.opcodeLooksLikeAnimation = isAnimationCode(cmd.opcode);
                    for (int j = 0; j < 4; ++j)
                        cmd.raw[static_cast<size_t>(j)] = state.words[pos + static_cast<size_t>(j)];
                    for (int j = 0; j < 3; ++j)
                    {
                        const std::uint32_t arg = cmd.raw[static_cast<size_t>(j + 1)];
                        if (arg != 0 && arg != 0xFFFFFFFFu)
                            cmd.resolvedSpriteArgs[static_cast<size_t>(j)] = relationTable().getPointer(static_cast<int>(arg));
                    }
                    state.commands.push_back(cmd);
                    pos += 4;
                }

                if (state.words.size() > neededWords || !state.tailBytes.empty())
                {
                    std::vector<BYTE> trailer;
                    for (size_t i = neededWords; i < state.words.size(); ++i)
                    {
                        const std::uint32_t v = state.words[i];
                        trailer.push_back(static_cast<BYTE>(v & 0xFF));
                        trailer.push_back(static_cast<BYTE>((v >> 8) & 0xFF));
                        trailer.push_back(static_cast<BYTE>((v >> 16) & 0xFF));
                        trailer.push_back(static_cast<BYTE>((v >> 24) & 0xFF));
                    }
                    trailer.insert(trailer.end(), state.tailBytes.begin(), state.tailBytes.end());
                    if (trailer.size() >= 4)
                    {
                        const std::uint32_t extraCount = readU32LE(trailer.data());
                        const size_t extraEnd = 4 + static_cast<size_t>(extraCount) * 4;
                        if (extraCount <= 1024 && extraEnd <= trailer.size())
                        {
                            for (size_t i = 0; i < extraCount; ++i)
                                state.extraValues.push_back(readU32LE(trailer.data() + 4 + i * 4));
                            size_t textEnd = 0;
                            std::string label;
                            if (decodeZeroTerminatedCP1251(trailer, extraEnd, label, textEnd))
                            {
                                state.textLabel = label;
                                state.note = "STACK_WITH_TEXT_LABEL";
                            }
                            else if (!trailer.empty())
                                state.note = "STACK_WITH_RAW_TRAILER=" + hexTail(trailer);
                        }
                        else
                            state.note = "STACK_WITH_RAW_TRAILER=" + hexTail(trailer);
                    }
                    else if (!trailer.empty())
                        state.note = "STACK_WITH_RAW_TRAILER=" + hexTail(trailer);
                }
                return state;
            }
        }

        state.layout = legacyShapeName(state.words, state.tailBytes);
        return state;
    }

    bool MAP::loadPlayers(RESOURCE* map)
    {
        m_playerSlots.clear();
        m_playerSprites.clear();
        if (!map)
            return false;
        if (map->GoBegin(RESOURCE::ResTypes::PLAY))
        {
            LOG::ResourceError("MAP", 11, "PLAY", 0);
            return false;
        }

#ifdef _WIN32
        win::ApplicationWin* const app = win::applicationWinInstance();
        PLAYER* const players[4] = {
            app->playerSlotByIndex(0),
            app->playerSlotByIndex(1),
            app->playerSlotByIndex(2),
            app->playerSlotByIndex(3)
        };
        for (PLAYER* const player : players)
        {
            player->loadControlledSpriteReference(map);
            SPRITE* const controlled = player->controlledSprite();
            m_playerSlots.push_back(controlled ? controlled->oldAddress() : 0);
            m_playerSprites.push_back(controlled);
        }
        return true;
#endif

        // Non-Windows validation builds do not have the Win32 Application owner.
        // Consume the same four serialized handles so MAP section traversal remains
        // testable, but do not invent a second runtime PLAYER owner.
        for (int i = 0; i < 4; ++i)
        {
            int oldAddress = 0;
            SPRITE* const resolved = ReadSpriteHandle(map, &oldAddress);
            m_playerSlots.push_back(oldAddress);
            m_playerSprites.push_back(resolved);
        }
        return true;
    }

    bool MAP::loadGroups(RESOURCE* map)
    {
        if (!map)
            return false;
        if (map->GoBegin(RESOURCE::ResTypes::GROUP))
        {
            LOG::ResourceError("MAP", 11, "GROU", 0);
            return false;
        }

        groupOwner().Load(map);
        return true;
    }

    float MAP::ToScreenX(float x) const
    {
        return x - m_shiftXY.x;
    }

    float MAP::ToScreenY(float y, float z) const
    {
        return y - z - m_shiftXY.y;
    }

    VECTOR2 MAP::ToScreenScaled(const VECTOR& world) const
    {
        return VECTOR2{ToScreenX(world.x), ToScreenY(world.y, world.z)};
    }

    float MAP::ToScreenScaledShiftX(float x, float shiftX, float scale) const
    {
        const float safeScale = (scale == 0.0f) ? 1.0f : scale;
        return (x - shiftX) * safeScale;
    }

    float MAP::ToScreenScaledShiftY(float y, float z, float shiftY, float scale) const
    {
        const float safeScale = (scale == 0.0f) ? 1.0f : scale;
        return (y - z - shiftY) * safeScale;
    }

    float MAP::FromScreenScaledShiftX(float x, float shiftX, float scale) const
    {
        const float safeScale = (scale == 0.0f) ? 1.0f : scale;
        return x / safeScale + shiftX;
    }

    float MAP::FromScreenScaledShiftY(float y, float z, float shiftY, float scale) const
    {
        const float safeScale = (scale == 0.0f) ? 1.0f : scale;
        return y / safeScale + z + shiftY;
    }

    void MAP::SetScrollBox(float minX, float minY, float maxX, float maxY)
    {

        m_scrollMinXY.x = minX;
        m_scrollMinXY.y = minY;
        m_scrollMaxXY.x = maxX;
        m_scrollMaxXY.y = maxY;
        core::ApplicationDrawDispatcherState& appDraw =
            core::GlobalApplicationDrawDispatcherState();
        appDraw.setScrollMinXLimit(minX);
        appDraw.setScrollMaxXLimit(maxX);
        appDraw.setScrollMinYLimit(minY);
        appDraw.setScrollMaxYLimit(maxY);
    }

    void MAP::SetShiftCoor(float centerX, float centerY, int effect)
    {
        core::ApplicationDrawDispatcherState& appDraw =
            core::GlobalApplicationDrawDispatcherState();
        const VECTOR2 scrollMin{appDraw.scrollMinXLimit(), appDraw.scrollMinYLimit()};
        const VECTOR2 scrollMax{appDraw.scrollMaxXLimit(), appDraw.scrollMaxYLimit()};
        const MapCameraClampRect cameraRect = buildMapCameraClampRect(*m_graph, scrollMin, scrollMax);
        float shiftX = static_cast<float>(
            static_cast<double>(centerX) - static_cast<double>(cameraRect.screenW) * 0.5);
        double shiftYLive =
            static_cast<double>(centerY) - static_cast<double>(cameraRect.screenH) * 0.5;

        // x87 `test ah,1`: less-than OR unordered clamps to the minimum.
        if (shiftX < cameraRect.minX || std::isnan(shiftX) || std::isnan(cameraRect.minX))
            shiftX = cameraRect.minX;
        if (shiftYLive < static_cast<double>(cameraRect.minY) ||
            std::isnan(shiftYLive) || std::isnan(cameraRect.minY))
            shiftYLive = cameraRect.minY;

        // x87 `test ah,41h`: only ordered-greater clamps to the maximum;
        // less/equal/unordered keep the candidate value.
        if (!std::isnan(shiftX) && !std::isnan(cameraRect.maxX) && shiftX > cameraRect.maxX)
            shiftX = cameraRect.maxX;
        if (!std::isnan(shiftYLive) && !std::isnan(cameraRect.maxY) &&
            shiftYLive > static_cast<double>(cameraRect.maxY))
            shiftYLive = cameraRect.maxY;

        const float currentCameraX = appDraw.cameraShiftX();
        const float currentCameraY = appDraw.cameraShiftY();
        const bool xEqualOrUnordered =
            (currentCameraX == shiftX) || std::isnan(currentCameraX) || std::isnan(shiftX);
        const bool yEqualOrUnordered =
            (static_cast<double>(currentCameraY) == shiftYLive) ||
            std::isnan(currentCameraY) || std::isnan(shiftYLive);
        if (xEqualOrUnordered && yEqualOrUnordered)
            return;

        if (effect == 2)
        {
            // Retail pushes __ftol(centerY), then __ftol(centerX).
            m_graph->setEffect(2,
                                retailFtolLow32ForMap(centerX),
                                retailFtolLow32ForMap(centerY),
                                0);
            return;
        }

        const float deltaX = static_cast<float>(
            static_cast<double>(shiftX) - static_cast<double>(currentCameraX));
        const float deltaY = static_cast<float>(
            shiftYLive - static_cast<double>(currentCameraY));
        const float committedShiftY = static_cast<float>(shiftYLive);
        appDraw.setCameraShiftX(shiftX);
        appDraw.setCameraShiftY(committedShiftY);
        m_shiftXY.x = shiftX;
        m_shiftXY.y = committedShiftY;

        SPRITE_LIST& frameList = applicationFrameSpriteList();
        const int persistentCount = frameList.activeCount();
        for (int i = 0; i < persistentCount; ++i)
        {
            SPRITE* sprite = frameList.at(static_cast<std::size_t>(i));
            if (sprite)
                sprite->ChangeCoor(sprite->X() + deltaX, sprite->Y() + deltaY, sprite->Z());
        }

        SPRITE* currentMouseSprite = mouseSprite();
        currentMouseSprite->ChangeCoor(currentMouseSprite->X() + deltaX, currentMouseSprite->Y() + deltaY, currentMouseSprite->Z());

        m_shiftDeltaXY.x += deltaX;
        m_shiftDeltaXY.y += deltaY;

        // GRAPH receives the committed Application top-left camera and the
        // D3DTS_VIEW payload. MAP::m_shiftXY is only a synchronized mirror.
        m_graph->SetCamera(m_shiftXY.x, m_shiftXY.y);

#ifdef _WIN32
        D3DMATRIX view{};
        view._11 = 1.0f;
        view._22 = 1.0f;
        view._32 = -1.0f;
        view._33 = 1.0f;
        view._41 = -m_shiftXY.x - cameraRect.screenW * 0.5f;
        view._42 = -m_shiftXY.y - cameraRect.screenH * 0.5f;
        view._44 = 1.0f;

        IDirect3DDevice8* device = static_cast<IDirect3DDevice8*>(m_graph->deviceHandle());
        const HRESULT result = device->SetTransform(D3DTS_VIEW, &view);
        if (FAILED(result))
            LOG::ResourceError("%s", 8, "Transform view", static_cast<int>(result), "GRAPH");
#endif
    }

    void MAP::DrawSpriteNumberLabels(GRAPH& graph) const
    {
        SPRITE_COLLECTOR_HASH_MAP* const hash = GlobalSpriteHashMap();
        SPRITE_POINTER_LIST& list = hash->mutableOverflowList();
        int* const cursor = hash->reverseCursorAddress();
        const auto& drawState = core::GlobalApplicationDrawDispatcherState();

        for (SPRITE* sprite = list.beginReverseIteration(cursor);
             sprite;
             sprite = list.continueReverseIteration(cursor))
        {
            if (sprite->childBacklink() != nullptr)
                continue;

            const float screenX = sprite->X() - drawState.cameraShiftX();
            const float screenY = sprite->Y() - sprite->Z() - drawState.cameraShiftY();
            (void)graph.drawFormattedText(screenX, screenY, "%i", sprite->animationFrameTime());
        }
    }

    void MAP::DrawOverlaySpriteList() const
    {
        SPRITE_COLLECTOR_HASH_MAP* const hash = GlobalSpriteHashMap();
        SPRITE_POINTER_LIST& list = hash->mutableOverflowList();
        int* const cursor = hash->reverseCursorAddress();
        for (SPRITE* sprite = list.beginReverseIteration(cursor);
             sprite;
             sprite = list.continueReverseIteration(cursor))
        {
            sprite->Draw();
        }
    }

    void MAP::DrawDebugTerrainGrid(GRAPH& graph) const
    {
        const short* const grid = terrainGrid();
        const int gridX = terrainGridWidth();
        const int gridY = terrainGridHeight();
        if (gridX <= 1 || gridY <= 1 || !grid)
            return;

        constexpr float kCell = 8.0f;
        constexpr DWORD kGridColor = 0x00808080u;
        const float viewLeft = 0.0f;
        const float viewTop = 0.0f;
        const float viewRight = static_cast<float>(graph.SizeX());
        const float viewBottom = static_cast<float>(graph.SizeY());

        auto insideView = [&](float x, float y) -> bool
        {
            return x >= viewLeft && x < viewRight && y >= viewTop && y < viewBottom;
        };

        auto gridZAt = [&](int x, int y) -> float
        {
            const size_t index = static_cast<size_t>(y) * static_cast<size_t>(gridX) + static_cast<size_t>(x);
            return static_cast<float>(grid[index]);
        };

        for (int gy = 1; gy < gridY; ++gy)
        {
            for (int gx = 1; gx < gridX; ++gx)
            {
                const float x0 = static_cast<float>(gx) * kCell - graph.cameraX();
                const float y0 = static_cast<float>(gy) * kCell - gridZAt(gx, gy) - graph.cameraY();
                const float x1 = static_cast<float>(gx - 1) * kCell - graph.cameraX();
                const float y1 = static_cast<float>(gy - 1) * kCell - gridZAt(gx - 1, gy) - graph.cameraY();

                if (insideView(x0, y0) || insideView(x1, y1))
                    graph.DrawLine(x0, y0, x1, y1, kGridColor);
            }
        }
    }

    SPRITE* MAP::DebugCurrentSprite() const
    {
        for (SPRITE* player : m_playerSprites)
        {
            if (player)
                return player;
        }
        for (const auto& holder : m_sprites)
        {
            if (holder)
                return holder.get();
        }
        return nullptr;
    }

    void MAP::DrawDebugCurrentSprite(GRAPH& graph, const SPRITE* sprite) const
    {
        const SPRITE* current = sprite ? sprite : DebugCurrentSprite();
        if (current)
            current->DrawSelectionOverlay(graph);
    }

    void MAP::DrawDebugScrollBox(GRAPH& graph) const
    {
        (void)graph;

        (void)groupOwner().drawGroupOrdinals();
    }

    void MAP::DrawDebugAuxiliaryList(GRAPH& graph) const
    {
        (void)graph;

        (void)core::drawWeakControllerMapDebug(&core::globalWeakControllerMap());
    }

    SPRITE* MAP::OldLoadSprite(BaseStream* res)
    {
        if (!res)
            throw std::runtime_error("MAP::OldLoadSprite: null stream");

        int oldAddr = 0;
        std::int16_t nvid16 = 0;
        std::int16_t x16 = 0;
        std::int16_t y16 = 0;
        std::int16_t z16 = 0;
        BYTE directionByte = 0;
        BYTE legacyTailByte = 0;

        res->read(&oldAddr, 4);
        if (oldAddr == END_SPRITE_INT)
            return END_SPRITE_PTR;

        res->read(&nvid16, 2);
        res->read(&x16, 2);
        res->read(&y16, 2);
        res->read(&z16, 2);
        res->read(&directionByte, 1);
        res->read(&legacyTailByte, 1);
        (void)legacyTailByte;

        const int nvid = static_cast<int>(nvid16);
        SPRITE* newSprite = nullptr;
        ++m_spriteRecordCount;
        const bool isNullRecord = (nvid == -1);
        const bool hasVid = !isNullRecord && ValidateVid(nvid);
        if (hasVid)
        {
            newSprite = CreateSprite(Vid(nvid),
                                     VECTOR(static_cast<float>(x16), static_cast<float>(y16), static_cast<float>(z16)),
                                     ANGLE(static_cast<int>(directionByte)));
            BindLoadedSpriteHandle(oldAddr, newSprite);
        }
        else
        {
            BindLoadedSpriteHandle(oldAddr, nullptr);
            LOG::ResourceError("%s", 3, "sprite, this vid not exist", nvid, "");
            if (isNullRecord)
                ++m_nullSpriteRecordCount;
            else
            {
                ++m_missingVidSpriteRecordCount;
                ++m_invalidSpriteRecordCount;
            }
        }
        return newSprite;
    }

    SPRITE* MAP::LoadSprite(BaseStream* res, int version)
    {
        int oldAddr = 0;
        int nvid = 0;
        int army = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        ANGLE direct;

        res->read(&oldAddr, 4);
        if (oldAddr == END_SPRITE_INT)
            return END_SPRITE_PTR;

        res->read(&nvid, 4);
        if (version > 9)
        {
            res->read(&x, 4);
            res->read(&y, 4);
            res->read(&z, 4);
        }
        else
        {
            int ix = 0, iy = 0, iz = 0;
            res->read(&ix, 4);
            res->read(&iy, 4);
            res->read(&iz, 4);
            x = static_cast<float>(ix);
            y = static_cast<float>(iy);
            z = static_cast<float>(iz);
        }

        direct.Read(res);
        res->read(&army, 4);

        SPRITE* newSprite = nullptr;
        ++m_spriteRecordCount;
        const bool isNullRecord = (nvid == -1);
        const bool hasVid = !isNullRecord && ValidateVid(nvid);
        if (hasVid)
        {
            newSprite = CreateSprite(Vid(nvid), VECTOR(x, y, z), direct);
            if (newSprite)
            {
                BindLoadedSpriteHandle(oldAddr, newSprite);
                newSprite->changeArmyBucket(army);
            }
        }
        else
        {
            LOG::ResourceError("%s", 3, "sprite, this vid not exist", nvid, "");
            if (isNullRecord)
                ++m_nullSpriteRecordCount;
            else
            {
                ++m_missingVidSpriteRecordCount;
                ++m_invalidSpriteRecordCount;
            }
        }
        if (!newSprite)
            BindLoadedSpriteHandle(oldAddr, nullptr);
        return newSprite;
    }

    SPRITE* MAP::CreateSpriteViaFactory(VID* vid, const VECTOR& v, const ANGLE& direction, SPRITE* parent, bool remoteControlled)
    {
        if (!vid)
            return nullptr;

        as1::core::ApplicationCreateSpriteRequest request{};
        request.owner = this;
        request.vid = vid;
        request.xyz = v;
        request.direction = direction;
        request.parent = parent;
        request.remoteControlled = remoteControlled;

        std::unique_ptr<SPRITE> spr;

#ifdef _WIN32
        if (as1::win::ApplicationWin* application = as1::win::applicationWinInstance())
            spr = application->CreateSprite(request);
#else
        if (!spr)
        {
            // Portable harnesses have no Win32 Application vtable owner.  Keep
            // this representation fallback out of the retail Windows route.
            switch (vid->spriteClass)
            {
            case B_TERRAIN:
            case B_OBJECT:
                spr = std::make_unique<TERRAIN>(this, vid, v, direction, parent);
                break;
            case B_BUILDING:
                spr = std::make_unique<BUILDING>(this, vid, v, direction, parent);
                break;
            case B_RAIL:
                spr = std::make_unique<RAIL>(this, vid, v, direction, parent);
                break;
            case B_DEPO:
                spr = std::make_unique<DEPO>(this, vid, v, direction, parent);
                break;
            case B_CIV_ROBOT:
                spr = std::make_unique<CIV_ROBOT>(this, vid, v, direction, parent);
                break;
            case B_ENGINE:
                spr = std::make_unique<ENGINE>(this, vid, v, direction, parent);
                break;
            case B_CREATURE:
                spr = std::make_unique<CREATURE>(this, vid, v, direction, parent);
                break;
            case B_BALLOON:
                spr = std::make_unique<BALLOON>(this, vid, v, direction, parent);
                break;
            default:
                spr = as1::core::Application::CreateSprite(request);
                break;
            }
        }
#endif

        if (!spr)
            return nullptr;

        SPRITE* raw = spr.get();
        m_sprites.push_back(std::move(spr));
        return raw;
    }

    bool MAP::ReleaseSpriteForScalarDeletingDestructor(SPRITE* sprite)
    {

        if (!sprite)
            return false;

        auto it = std::find_if(m_sprites.begin(), m_sprites.end(),
            [sprite](const std::unique_ptr<SPRITE>& holder) { return holder.get() == sprite; });
        if (it == m_sprites.end())
            return false;

        it->release();
        m_sprites.erase(it);
        return true;
    }

    bool MAP::ReleaseVidForScalarDeletingDestructor(VID* vid)
    {
        if (!vid)
            return false;

        auto it = std::find_if(m_vids.begin(), m_vids.end(),
            [vid](const std::unique_ptr<VID>& holder) { return holder.get() == vid; });
        if (it == m_vids.end())
            return false;

        (void)it->release();
        return true;
    }

    void MAP::CreateEmptyHardwareGround()
    {
        core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
        if (appVidTable.count() < 1025)
            appVidTable.setStoredCount(1025);

        if (VID* const oldGround = appVidTable.slot(1024))
        {
            (void)ReleaseVidForScalarDeletingDestructor(oldGround);
            delete oldGround;
        }

        const int groundSizeY = retailFtolLow32ForMap(SizeY());
        const int groundSizeX = retailFtolLow32ForMap(SizeX());
        auto ground = std::unique_ptr<VID_HARDWARE>(
            new (std::nothrow) VID_HARDWARE(1024, groundSizeX, groundSizeY));
        VID_HARDWARE* const rawGround = ground.get();
        rawGround->weapon = weaponTable();

        // Host holder indices cease to be NVID identities after swapVidReferences.
        // The old raw-slot owner released above leaves its actual holder empty;
        // use an empty holder (normally that one) and append only if necessary.
        auto empty = std::find_if(m_vids.begin(), m_vids.end(),
            [](const std::unique_ptr<VID>& holder) { return holder.get() == nullptr; });
        if (empty != m_vids.end())
            *empty = std::move(ground);
        else
            m_vids.emplace_back(std::move(ground));

        appVidTable.setSlotCell(1024, rawGround);

        CreateSprite(rawGround,
                     VECTOR(SizeX() * 0.5f, SizeY() * 0.5f, 0.0f),
                     ANGLE(0),
                     nullptr,
                     false,
                     false);
        // Direct retail tail call-site: writeLogLine(g_fileLogger,
        // "Create Empty Hardware\tGround"). Keep the exact logger route and
        // text (including the tab) rather than passing through LOG::Write.
        (void)writeLogLine(g_fileLogger, "Create Empty Hardware\tGround");
    }

    SPRITE* MAP::CreateSprite(VID* vid, const VECTOR& v, const ANGLE& direction, SPRITE* parent, bool remoteControlled, bool createChildRoute)
    {
        SPRITE* raw = CreateSpriteViaFactory(vid, v, direction, parent, remoteControlled);
        if (!raw)
            return nullptr;

        (void)createChildRoute;
        return raw;
    }

    void MAP::SetFlagman(int playerIndex, SPRITE* sprite) noexcept
    {
#ifdef _WIN32
        win::ApplicationWin* const app = win::applicationWinInstance();
        PLAYER* const player = app->startupPlayerSlotByIndex(playerIndex);
        player->setFlagmanSprite(sprite);
        return;
#endif

        // Portable owner mirror for tests without the Win32 shell. Keep the
        // same retain/release semantics as retail setFlagmanSprite and the native
        // PLAYER implementation; no separate gameplay behavior is introduced.
        const std::size_t slot = static_cast<std::size_t>(playerIndex & 3);
        if (m_playerSprites.size() < 4u)
            m_playerSprites.resize(4u, nullptr);
        if (m_playerSlots.size() < 4u)
            m_playerSlots.resize(4u, 0);

        if (sprite)
            sprite->setListReferenceCount(sprite->listReferenceCount() + 1);

        SPRITE* const previous = m_playerSprites[slot];
        if (previous)
        {
            const int nextRef = previous->listReferenceCount() - 1;
            previous->setListReferenceCount(nextRef);
            if (nextRef < 0)
            {
                VID* const vid = previous->Vid();
                LOG::ResourceError("SPRITE %i", 4, "noRef\tat Release", nextRef, vid ? vid->nVid : -1);
            }
            else if (nextRef == 0)
            {
                DeleteSpriteThroughVirtualDeletingDestructor(previous);
            }
        }

        m_playerSprites[slot] = sprite;
        m_playerSlots[slot] = sprite ? sprite->oldAddress() : 0;
    }

    SPRITE* MAP::flagmanSpriteForPlayer(int playerIndex) const noexcept
    {
#ifdef _WIN32
        if (win::ApplicationWin* const app = win::applicationWinInstance())
        {
            if (PLAYER* const player = app->startupPlayerSlotByIndex(playerIndex))
                return player->controlledSprite();
        }
#endif
        const std::size_t slot = static_cast<std::size_t>(playerIndex & 3);
        return slot < m_playerSprites.size() ? m_playerSprites[slot] : nullptr;
    }

    SPRITE* MAP::readSpriteRelationHandle(BaseStream* stream) const
    {
        std::int32_t handle = 0;
        stream->read(&handle, 4u);
        if (handle == -1)
            return END_SPRITE_PTR;
        return relationTable().getPointer(handle);
    }

    SPRITE* MAP::ReadSpriteHandle(BaseStream* stream, int* oldAddress) const
    {
        int handle = END_SPRITE_INT;
        if (stream)
            stream->read(&handle, 4);
        if (oldAddress)
            *oldAddress = handle;
        if (handle == END_SPRITE_INT)
            return nullptr;
        return ResolveOldSpriteHandle(handle);
    }

    SPRITE* MAP::ResolveOldSpriteHandle(int oldAddress) const
    {
        if (oldAddress == 0 || oldAddress == END_SPRITE_INT)
            return nullptr;

        return relationTable().getPointer(oldAddress);
    }

    SPRITE* MAP::SpriteByOldAddress(int oldAddress) const
    {
        if (oldAddress == 0 || oldAddress == END_SPRITE_INT)
            return nullptr;
        const auto it = m_spriteByOldAddress.find(oldAddress);
        if (it != m_spriteByOldAddress.end())
            return it->second;
        for (const auto& sprite : m_sprites)
            if (sprite && sprite->oldAddress() == oldAddress)
                return sprite.get();
        return nullptr;
    }

    void MAP::BindLoadedSpriteHandle(int oldAddress, SPRITE* sprite)
    {
        if (oldAddress == 0 || oldAddress == END_SPRITE_INT)
            return;
        if (sprite)
            sprite->setOldAddress(oldAddress);
        relationTable().append(oldAddress, sprite);
        m_spriteByOldAddress[oldAddress] = sprite;
    }

    void MAP::setTerrainHeightAtWorldPosition(float x, float y, float z)
    {
        float mapSizeX = m_sizeXY.x;
        float mapSizeY = m_sizeXY.y;
#ifdef _WIN32
        mapSizeX = core::ApplicationMapWidth();
        mapSizeY = core::ApplicationMapHeight();
#endif
        if (x87LessOrUnorderedForMap(x, 0.0f))
            return;
        if (!x87LessOrUnorderedForMap(x, mapSizeX))
            return;
        if (x87LessOrUnorderedForMap(y, 0.0f))
            return;
        if (!x87LessOrUnorderedForMap(y, mapSizeY))
            return;

        const int zInt = retailFtolLow32ForMap(z);
        const int negativeGridY = retailFtolMulLow32ForMap(y, -0.125f);
        const int yProduct = static_cast<int>(
            static_cast<std::uint32_t>(negativeGridY) *
            static_cast<std::uint32_t>(terrainGridWidth()));
        const int xInt = retailFtolLow32ForMap(x);
        const int xDiv8 = (xInt + (xInt < 0 ? 7 : 0)) >> 3;
        const int index = static_cast<int>(
            static_cast<std::uint32_t>(xDiv8) - static_cast<std::uint32_t>(yProduct));
        terrainGrid()[index] = static_cast<short>(static_cast<unsigned int>(zInt) & 0xFFFFu);
    }

    float MAP::sampleTerrainHeight(float x, float y) const noexcept
    {
        float mapSizeX = m_sizeXY.x;
        float mapSizeY = m_sizeXY.y;
#ifdef _WIN32
        mapSizeX = core::ApplicationMapWidth();
        mapSizeY = core::ApplicationMapHeight();
#endif
        const int gridX = mapGridCoordinate(x, mapSizeX);
        const int gridY = mapGridCoordinate(y, mapSizeY);
        return static_cast<float>(terrainGrid()[static_cast<std::size_t>(gridX + terrainGridWidth() * gridY)]);
    }

    float MAP::GetGroundZ(const VECTOR2& v) const
    {
        return sampleTerrainHeight(v.x, v.y);
    }

    float MAP::GetGroundZ(const VID* vid, const VECTOR2& v, ANGLE) const
    {
        if (vid->spriteClass != B_AVIA)
            return GetGroundZ(v);

        float mapSizeX = m_sizeXY.x;
        float mapSizeY = m_sizeXY.y;
#ifdef _WIN32
        mapSizeX = core::ApplicationMapWidth();
        mapSizeY = core::ApplicationMapHeight();
#endif

        // 0x40E6A8..0x40E6FC has asymmetric temporary stores.  All four final
        // raw bounds are FSTP m32real before the clamp stage, so model those
        // exact binary32 boundaries explicitly.
        const long double halfXExtended =
            static_cast<long double>(vid->sizeXYZ.x) * 0.5L;
        const long double halfYExtended =
            static_cast<long double>(vid->sizeXYZ.y) * 0.5L;
        const float halfYStored = static_cast<float>(halfYExtended); // explicit binary32 store before the next x87 operation
        const float maxXRaw = static_cast<float>(
            static_cast<long double>(v.x) + halfXExtended - 3.0L);
        const float maxYRaw = static_cast<float>(
            static_cast<long double>(v.y) + halfYExtended - 3.0L);
        const float minXRaw = static_cast<float>(
            static_cast<long double>(v.x) - (halfXExtended - 3.0L));
        const float minYRaw = static_cast<float>(
            static_cast<long double>(v.y) -
            (static_cast<long double>(halfYStored) - 3.0L));

        const auto scaledGridCoordinate = [](float value, float limit) noexcept -> long double
        {
            // FCOMP/FNSTSW + TEST AH,1: less-than OR unordered clamps to zero
            // for the first comparison.  The second comparison uses the same
            // C0 test and therefore keeps the original value for < or unordered;
            // otherwise it uses (limit-1).  FMUL 0.125 remains live in x87.
            if (x87LessOrUnorderedForMap(value, 0.0f))
                return 0.0L;
            if (x87LessOrUnorderedForMap(value, limit))
                return static_cast<long double>(value) * 0.125L;
            return (static_cast<long double>(limit) - 1.0L) * 0.125L;
        };

        const long double minX = scaledGridCoordinate(minXRaw, mapSizeX);
        const long double minY = scaledGridCoordinate(minYRaw, mapSizeY);
        const long double maxX = scaledGridCoordinate(maxXRaw, mapSizeX);
        const long double maxY = scaledGridCoordinate(maxYRaw, mapSizeY);

        std::int32_t result = -16383; // retail EDI = 0xFFFFC001
        if (minY > maxY)
            return static_cast<float>(result);

        const short* const grid = terrainGrid();
        const std::int32_t gridX = terrainGridWidth();
        long double scanY = minY;
        for (;;)
        {
            if (minX <= maxX)
            {
                const std::int32_t gy = static_cast<std::int32_t>(std::trunc(scanY));
                const std::int32_t row = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(gy) *
                    static_cast<std::uint32_t>(gridX));

                long double scanX = minX;
                for (;;)
                {
                    const std::int32_t gx = static_cast<std::int32_t>(std::trunc(scanX));
                    const std::int32_t index = static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(row) +
                        static_cast<std::uint32_t>(gx));
                    const std::int32_t z = static_cast<std::int16_t>(grid[index]);
                    if (z > result)
                        result = z;

                    scanX += 1.0L;
                    if (scanX > maxX)
                        break;
                }
            }

            scanY += 1.0L;
            if (scanY > maxY)
                break;
        }

        // Retail leaves the signed maximum in EDI and returns it through FILD.
        return static_cast<float>(result);
    }

    void MAP::ResetGroundZ() { releaseTerrainGridStorage(); }

    void MAP::loadScript()
    {
        std::filesystem::path mapPath(m_fileName.str());
        STRING requested(mapPath.replace_extension(".lgc").string());
        const std::filesystem::path scriptPath = resolveGameFile(requested);

        const STRING resolved(scriptPath.string());
        (void)scriptRuntime().loadScriptFile(resolved, m_resourceRoot);
        processScriptFunctions();
    }

    void MAP::runRetailPostLoadScriptPasses()
    {
        const std::uint32_t stackFlags = core::ApplicationFlags();
        if ((stackFlags & application_flags::DemoUseResource) != 0u)
            scriptRuntime().readExecutionStackFromStream(&demoResource());
        else if ((stackFlags & application_flags::DemoWriteToResource) != 0u)
            scriptRuntime().writeExecutionStackToStream(&demoResource());

        core::SetRealTimeMilliseconds(currentMilliseconds());

        core::ApplicationDrawDispatcherState& drawState = core::GlobalApplicationDrawDispatcherState();

        if ((core::ApplicationFlags() & 0x00000001u) == 0u)
        {
            for (int pass = 0; pass < core::ApplicationDrawDispatcherState::PassCount; ++pass)
            {
                const core::ApplicationDrawPassBucket& bucket = drawState.drawPassBucket(pass);
                for (int cursor = bucket.count() - 1; cursor >= 0; --cursor)
                {
                    SPRITE* const sprite = bucket.spriteAt(cursor);
                    if (!sprite)
                        continue;
                    VID* const vid = sprite->Vid();
                    const int functionIndex = vid->birthScriptFunction();
                    if (functionIndex >= 0)
                    {
                        const int spriteArg = static_cast<int>(static_cast<std::uint32_t>(
                            reinterpret_cast<std::uintptr_t>(sprite)));
                        (void)core::Application::callScriptFunction(functionIndex, spriteArg, 0);
                    }
                }
            }
        }

        for (int pass = 0; pass < core::ApplicationDrawDispatcherState::PassCount; ++pass)
        {
            const core::ApplicationDrawPassBucket& bucket = drawState.drawPassBucket(pass);
            for (int cursor = bucket.count() - 1; cursor >= 0; --cursor)
            {
                SPRITE* const sprite = bucket.spriteAt(cursor);
                if (!sprite)
                    continue;
                VID* const vid = sprite->Vid();
                if (vid->spriteClassId() == 21u)
                    (void)sprite->validateEngineChainLinks();
            }
        }

        if ((core::ApplicationFlags() & application_flags::DemoUseResource) == 0u)
            Mouse->HardwareOn();
    }

    void MAP::processScriptFunctions()
    {
        static constexpr const char* kGlobalFunctionNames[] = {
            "main",
            "TrainNotAmmo",
            "TrainNotPower",
            "TrainDamage",
            "TrainCreated",
            "TrainSplit",
            "TrainDestroy",
            "TrainDestroyPower",
            "TrainArrive",
            "???TrainNotArrive",
            "TrainAttacked",
            "DepoDestroy",
            "DepoBirth",
            "DepoAttacked",
            "DepoFree",
            "BuildingCapture",
            "MasterDestroy",
            "???",
            "???SuperWeaponWounded",
            "MineBlast",
            "MineRemove",
            "EnemyLinked",
            "TrainClash",
            "UnitCreated",
            "UnitDestroy"
        };

        const auto& functionTable = scriptRuntime().functionTable().items();
        const int functionCount = scriptRuntime().functionCount();
        for (int functionIndex = 0; functionIndex < functionCount; ++functionIndex)
        {
            const script::LogicFunctionRecord& fn = functionTable[static_cast<std::size_t>(functionIndex)];
            if (fn.flags != 3)
                continue;

            const std::string name = fn.name.str();
            for (std::size_t globalIndex = 0; globalIndex < std::size(kGlobalFunctionNames); ++globalIndex)
            {
                if (name == kGlobalFunctionNames[globalIndex])
                    core::setScriptCallbackSlot(globalIndex, functionIndex);
            }

            if (name.size() < 4 || name[0] != 'F' ||
                !std::isdigit(static_cast<unsigned char>(name[1])) ||
                !std::isdigit(static_cast<unsigned char>(name[2])) ||
                !std::isdigit(static_cast<unsigned char>(name[3])))
                continue;

            const int nVid3 = (name[1] - '0') * 100 + (name[2] - '0') * 10 + (name[3] - '0');

            // Fddd_Destroy: strncmp(name+5,"DESTROY",7), then slot +0x3FC.
            if (name.size() >= 12 && name[4] == '_' && name.compare(5, 7, "DESTROY") == 0)
            {
                if (ValidateVid(nVid3))
                    Vid(nVid3)->setScriptFunctionAt(VID::DestroyScriptFunctionIndex, functionIndex);
                continue;
            }

            // Fddd_N or Fddd_NN.  Retail tests name[6] only for zero; it does
            // not run isdigit on the animation characters before arithmetic.
            if (name.size() >= 6 && name[4] == '_')
            {
                int nAnim = name[5] - '0';
                if (name.size() >= 7 && name[6] != '\0')
                    nAnim = (name[5] - '0') * 10 + (name[6] - '0');
                if (ValidateVid(nVid3) && nAnim < VID::NO_ANIMATION)
                    Vid(nVid3)->setScriptFunctionAt(nAnim, functionIndex);
                continue;
            }

            // Fdddd_N or Fdddd_NN.
            if (name.size() >= 7 && std::isdigit(static_cast<unsigned char>(name[4])) && name[5] == '_')
            {
                const int nVid4 = nVid3 * 10 + (name[4] - '0');
                int nAnim = name[6] - '0';
                if (name.size() >= 8 && name[7] != '\0')
                    nAnim = (name[6] - '0') * 10 + (name[7] - '0');
                if (ValidateVid(nVid4) && nAnim < VID::NO_ANIMATION)
                    Vid(nVid4)->setScriptFunctionAt(nAnim, functionIndex);
            }
        }
    }

}
