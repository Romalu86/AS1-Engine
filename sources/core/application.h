#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/types.h"
#include "base_sprite_list.h"

namespace as1
{
    namespace application_flags
    {
        constexpr std::uint32_t EnemyCanAttackNeutralTrains = 1u << 1;
        constexpr std::uint32_t BucketTimingActive = 1u << 4;
        constexpr std::uint32_t MapLoading = 1u << 5;
        constexpr std::uint32_t PendingCommandOrLoad = 1u << 6;
        constexpr std::uint32_t ScriptControlBit7 = 1u << 7;
        constexpr std::uint32_t DemoWriteToResource = 1u << 8;
        constexpr std::uint32_t DemoUseResource = 1u << 9;
        constexpr std::uint32_t ScriptCallbacksDisabled = 1u << 17;
    }
    class GRAPH;
    class MAP;
    class SPRITE;
    class VID;
    class SCRIPT;
    struct WEAPON;
}

namespace as1::core
{

    // Centralized physical layout of the retail Win32 Application owner.
    // These constants document the ABI layout; gameplay code should use semantic
    // accessors instead of repeating pointer arithmetic.
    namespace retail_application_layout
    {
        constexpr std::size_t Flags = 0x04u;
        constexpr std::size_t TickScale = 0x0Cu;
        constexpr std::size_t ApplicationTitle = 0x10u;
        constexpr std::size_t CurrentMapName = 0x14u;
        constexpr std::size_t PendingCommand = 0x18u;
        constexpr std::size_t RegistryPath = 0x1Cu;
        constexpr std::size_t WorldFrameCounter = 0x20u;
        constexpr std::size_t WorldStartTime = 0x24u;
        constexpr std::size_t MapExtentX = 0x28u;
        constexpr std::size_t MapExtentY = 0x2Cu;
        constexpr std::size_t ScrollType = 0x30u;
        constexpr std::size_t ScrollMinX = 0x34u;
        constexpr std::size_t ScrollMaxX = 0x38u;
        constexpr std::size_t ScrollMinY = 0x3Cu;
        constexpr std::size_t ScrollMaxY = 0x40u;
        constexpr std::size_t CameraShiftX = 0x44u;
        constexpr std::size_t CameraShiftY = 0x48u;
        constexpr std::size_t ScriptRuntime = 0x14Cu;
        constexpr std::size_t DemoResource = 0x1A4u;
        constexpr std::size_t RelationTable = 0x1E4u;
        constexpr std::size_t TerrainGrid = 0x204u;
        constexpr std::size_t TerrainGridWidth = 0x208u;
        constexpr std::size_t TerrainGridHeight = 0x20Cu;
        constexpr std::size_t InstanceHandle = 0x210u;
        constexpr std::size_t MainWindow = 0x214u;
        constexpr std::size_t Accelerator = 0x218u;
        constexpr std::size_t ActivePlayerIndex = 0x21Cu;
        constexpr std::size_t PlayerSlots = 0x220u;
        constexpr std::size_t PlayerSlotStride = sizeof(std::uint32_t);
        constexpr std::size_t InputState = 0x230u;
        constexpr std::size_t BaseSpriteList = 0x24Cu;
        constexpr std::size_t Groups = 0x264u;
        constexpr std::size_t WeaponCount = 0x288u;
        constexpr std::size_t WeaponTable = 0x28Cu;
        constexpr std::size_t VidCount = 0x290u;
        constexpr std::size_t VidTable = 0x294u;
        constexpr std::size_t VidTableBytes = 0x2000u;
        constexpr std::size_t ShellOwnedSpriteVtable = 0x2294u;
        constexpr std::size_t ShellOwnedSpritePointer = 0x2298u;
        constexpr std::size_t ShellFlags = 0x229Cu;
    }

    void BindApplicationPhysicalOwner(void* owner) noexcept;
    void* ApplicationPhysicalOwner() noexcept;
    void InitializeApplicationPhysicalMapStorage(void* owner) noexcept;
    void InitializeApplicationPhysicalDrawStorage(void* owner) noexcept;
    void DestroyApplicationPhysicalDrawStorage(void* owner) noexcept;

    short* ApplicationTerrainGrid() noexcept;
    void SetApplicationTerrainGrid(short* value) noexcept;
    int ApplicationTerrainGridWidth() noexcept;
    void SetApplicationTerrainGridWidth(int value) noexcept;
    int ApplicationTerrainGridHeight() noexcept;
    void SetApplicationTerrainGridHeight(int value) noexcept;
    int ApplicationWeaponCount() noexcept;
    void SetApplicationWeaponCount(int value) noexcept;
    WEAPON* ApplicationWeaponTable() noexcept;
    void SetApplicationWeaponTable(WEAPON* value) noexcept;

    class ApplicationVidTable
    {
    public:
        static constexpr std::size_t kCapacity = 0x800u;
        static constexpr std::uint32_t kCountOffset = retail_application_layout::VidCount;
        static constexpr std::uint32_t kFirstSlotOffset = retail_application_layout::VidTable;
        static constexpr std::uint32_t kEndSlotOffset = retail_application_layout::ShellOwnedSpriteVtable;

        void clear() noexcept;
        bool setSlot(int nvid, VID* vid) noexcept;
        void setWeaponSentinel(WEAPON* weapon) noexcept { SetApplicationWeaponTable(weapon); }
        WEAPON* weaponSentinel() const noexcept { return ApplicationWeaponTable(); }
        // Semantic view of the retail Application VID depot. Physical offsets
        // remain private to this bridge; gameplay code uses count()/slot().
        int count() const noexcept;
        VID* slot(int index) const noexcept;
        void setStoredCount(int value) noexcept;
        void setSlotCell(int index, VID* vid) noexcept;
        VID* const* slotData() const noexcept;
        std::size_t capacity() const noexcept { return kCapacity; }
        std::size_t loadedSlotCount() const noexcept;
        std::size_t loadedSlotCountWithinGammaScan() const noexcept;
        std::vector<VID*> loadedSlotsSnapshot() const;

    };

    ApplicationVidTable& GlobalApplicationVidTable() noexcept;


    struct ApplicationDrawPassBucket
    {
        static constexpr std::uint32_t CountOffset = 0x50u;
        static constexpr std::uint32_t ListOffset = 0x58u;
        static constexpr std::uint32_t Stride = 0x10u;

        // Physical retail bucket owner: vtable/count/capacity/SPRITE**.
        SPRITE_POINTER_LIST list;

        int count() const noexcept { return list.activeCount(); }
        SPRITE* const* data() const noexcept { return list.data(); }
        SPRITE* spriteAt(int index) const noexcept;
        int findAndNull(SPRITE* sprite) noexcept;
        void append(SPRITE* sprite);
        void compactSparse() noexcept { list.compactSparse(); }
        void clear() noexcept { list.clearSpriteReferences(); }
    };
#if UINTPTR_MAX == 0xFFFFFFFFu
    static_assert(sizeof(ApplicationDrawPassBucket) == 0x10, "Application draw bucket retail stride must be 0x10");
#endif

    struct ApplicationDrawDispatcherState
    {
        static constexpr int PassCount = 16;
        static constexpr std::uint32_t ScrollMinXOffset = 0x34u;
        static constexpr std::uint32_t ScrollMaxXOffset = 0x38u;
        static constexpr std::uint32_t ScrollMinYOffset = 0x3Cu;
        static constexpr std::uint32_t ScrollMaxYOffset = 0x40u;
        static constexpr std::uint32_t CameraXOffset = 0x44u;
        static constexpr std::uint32_t CameraYOffset = 0x48u;
        static constexpr std::uint32_t BucketCountBaseOffset = 0x50u;
        static constexpr std::uint32_t BucketListBaseOffset = 0x58u;
        static constexpr std::uint32_t BucketStride = 0x10u;

#ifndef _WIN32
        std::uint32_t flagsSlot04 = 0;
        float scrollMinX = 0.0f;
        float scrollMaxX = 0.0f;
        float scrollMinY = 0.0f;
        float scrollMaxY = 0.0f;
        float cameraX = 0.0f;
        float cameraY = 0.0f;
        std::array<ApplicationDrawPassBucket, PassCount> passBuckets{};
#endif

        static constexpr std::uint32_t FlagsOffset = 0x04u;
        static constexpr std::uint32_t BucketTimingFlag = application_flags::BucketTimingActive;

        std::uint32_t flags() const noexcept;
        void setFlags(std::uint32_t value) noexcept;
        bool bucketTimingEnabled() const noexcept { return (flags() & BucketTimingFlag) != 0; }
        float scrollMinXLimit() const noexcept;
        float scrollMaxXLimit() const noexcept;
        float scrollMinYLimit() const noexcept;
        float scrollMaxYLimit() const noexcept;
        void setScrollMinXLimit(float value) noexcept;
        void setScrollMaxXLimit(float value) noexcept;
        void setScrollMinYLimit(float value) noexcept;
        void setScrollMaxYLimit(float value) noexcept;
        float cameraShiftX() const noexcept;
        float cameraShiftY() const noexcept;
        double cameraRelativeX(float value) const noexcept;
        double cameraRelativeY(float value) const noexcept;
        void setCameraShiftX(float value) noexcept;
        void setCameraShiftY(float value) noexcept;
        ApplicationDrawPassBucket& drawPassBucket(int pass) noexcept;
        const ApplicationDrawPassBucket& drawPassBucket(int pass) const noexcept;
        void clear() noexcept;
    };

    ApplicationDrawDispatcherState& GlobalApplicationDrawDispatcherState() noexcept;


    struct ApplicationFrameRuntimeState
    {
#ifndef _WIN32
        SPRITE* currentFrameSlot260 = nullptr;
#endif
        SPRITE* currentFrameSprite() const noexcept;
        void setCurrentFrameSprite(SPRITE* sprite) noexcept;
        bool clearCurrentFrameSpriteIfMatches(SPRITE* sprite) noexcept;
    };

    ApplicationFrameRuntimeState& GlobalApplicationFrameRuntimeState() noexcept;

    // Frame-pump current-time scalar shared by gameplay/audio owners.
    std::uint32_t CurrentTimeMilliseconds() noexcept;
    void SetCurrentTimeMilliseconds(std::uint32_t value) noexcept;

    float ApplicationTickScale() noexcept;
    void SetApplicationTickScale(float value) noexcept;
    std::uint32_t ApplicationWorldFrameCounter() noexcept;
    void SetApplicationWorldFrameCounter(std::uint32_t value) noexcept;
    std::uint32_t ApplicationWorldStartTime() noexcept;
    void SetApplicationWorldStartTime(std::uint32_t value) noexcept;

    float ApplicationMapWidth() noexcept;
    void SetApplicationMapWidth(float value) noexcept;
    float ApplicationMapHeight() noexcept;
    void SetApplicationMapHeight(float value) noexcept;
    std::uint32_t ApplicationScrollType() noexcept;
    void SetApplicationScrollType(std::uint32_t value) noexcept;
    std::uint32_t DemoStartTimestampMilliseconds() noexcept;
    void SetDemoStartTimestampMilliseconds(std::uint32_t value) noexcept;

    std::uint32_t PreviousWorldTimeMilliseconds() noexcept;
    void SetPreviousWorldTimeMilliseconds(std::uint32_t value) noexcept;

    float& ApplicationScrollVelocityX() noexcept;
    float& ApplicationScrollVelocityY() noexcept;
    // Retail drawApplicationDebugOverlayPass process-global FPS state. These are independent of the
    // Application object and must not be mirrored in ApplicationWinHostState.
    std::uint32_t& LastFpsSampleTimeMilliseconds() noexcept;
    std::uint32_t& DisplayedFramesPerSecond() noexcept;
    std::uint32_t& AccumulatedFpsFrameCount() noexcept;
    std::uint32_t BucketTimingSnapshotMilliseconds() noexcept;
    void SetBucketTimingSnapshotMilliseconds(std::uint32_t value) noexcept;

    std::uint32_t DemoRealTimeBaseMilliseconds() noexcept;
    void SetDemoRealTimeBaseMilliseconds(std::uint32_t value) noexcept;
    std::uint32_t DemoRecordedTimeBaseMilliseconds() noexcept;
    void SetDemoRecordedTimeBaseMilliseconds(std::uint32_t value) noexcept;

    static constexpr std::size_t kScriptCallbackSlotCount = 64u;
    int scriptCallbackSlot(std::size_t index) noexcept;
    void setScriptCallbackSlot(std::size_t index, int value) noexcept;
    void resetScriptCallbackSlots() noexcept;

    std::uint32_t ChildRotationCorrectionPending() noexcept;
    void SetChildRotationCorrectionPending(std::uint32_t value) noexcept;
    std::uint32_t BulkSpriteDeleteActive() noexcept;
    void SetBulkSpriteDeleteActive(std::uint32_t value) noexcept;

    std::uint32_t RealTimeMilliseconds() noexcept;
    void SetRealTimeMilliseconds(std::uint32_t value) noexcept;

    std::uint32_t ApplicationFlags() noexcept;
    void SetApplicationFlags(std::uint32_t value) noexcept;

    std::uint32_t ActivePlayerIndex() noexcept;
    void SetActivePlayerIndex(std::uint32_t value) noexcept;

    SCRIPT* ApplicationScriptRuntime() noexcept;

    enum ApplicationDebugFlag : std::uint32_t
    {
        ApplicationDebugShowFpsAndObjectCount = 0x00010000u,
        ApplicationDebugDrawTerrainGrid       = 0x00000800u,
        ApplicationDebugDrawSpriteBuckets     = 0x00008000u,
        ApplicationDebugDrawCurrentSprite     = 0x00001000u,
        ApplicationDebugDrawAuxiliaryList     = 0x00002000u,
        ApplicationDebugDrawScrollBox         = 0x00004000u,
    };

    struct ApplicationDebugPassState
    {

        std::uint32_t flags = 0;
    };

    struct ApplicationDebugPassContext
    {
        GRAPH* graph = nullptr;
        MAP* map = nullptr;
        SPRITE* selectedSprite = nullptr;
    };

    struct ApplicationCreateSpriteRequest
    {
        MAP* owner = nullptr;
        VID* vid = nullptr;
        VECTOR xyz{};
        ANGLE direction{};
        SPRITE* parent = nullptr;
        bool remoteControlled = false;
    };

    class Application
    {
    public:

        static void DrawDebugPass(ApplicationDebugPassState& state, const ApplicationDebugPassContext& context);

        static std::unique_ptr<SPRITE> CreateSprite(const ApplicationCreateSpriteRequest& request);

        static int callScriptFunction(std::uint32_t applicationFlags, SCRIPT* scriptOwner, int functionIndex, int firstArgument, int secondArgument);
        // Exact global-owner call shape used by retail callers: ecx is the
        // Application singleton, so +0x04 flags and +0x14C SCRIPT are read from
        // the physical Application raw slots in the same call.
        static int callScriptFunction(int functionIndex, int firstArgument, int secondArgument);

        static int drawSpritePass(ApplicationDrawDispatcherState& state, int pass);
        static int beginBucketTimingSnapshot(ApplicationDrawDispatcherState& state);
        static int endBucketTimingSnapshot(ApplicationDrawDispatcherState& state);
        static SPRITE* previousSpriteInDrawPass(ApplicationDrawDispatcherState& state, int pass, int* cursor);
        static int removeSpriteFromDrawBucket(ApplicationDrawDispatcherState& state, SPRITE* sprite);
        static char* appendSpriteToDrawBucketAndReleaseListReference(ApplicationDrawDispatcherState& state, SPRITE* sprite);
        static SPRITE* previousSpriteOfTypeInDrawPass(ApplicationDrawDispatcherState& state, int pass, int* cursor, int spriteTypeMask);
        static SPRITE* findSpriteAtPointByBounds(MAP& map, ApplicationDrawDispatcherState& state, int filter, float x, float y);
        static SPRITE* findSpriteAtPointByFilter(MAP& map, ApplicationDrawDispatcherState& state, int filter, float x, float y);
        static SPRITE* findNearestSpriteByFilter(MAP& map, ApplicationDrawDispatcherState& state, int filter, float x, float y, float radius);

    private:
        static void drawFpsAndObjectCount(ApplicationDebugPassState& state, const ApplicationDebugPassContext& context);
        static void drawTerrainGrid(const ApplicationDebugPassContext& context);
        static void drawSpriteBuckets(const ApplicationDebugPassContext& context);
        static void drawCurrentSprite(const ApplicationDebugPassContext& context);
        static void drawScrollBox(const ApplicationDebugPassContext& context);
        static void drawAuxiliaryList(const ApplicationDebugPassContext& context);
    };
}
