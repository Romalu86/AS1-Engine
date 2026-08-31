#pragma once
#include "core/types.h"
#include "sprite_act_const.h"
#include "vid/vid.h"
#include <vector>
#include <string>
#include <array>
#include <map>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace as1
{

    int PathSearchScore0() noexcept;
    int PathSearchScore1() noexcept;
    class SPRITE_POINTER_LIST;
    class SPRITE_LIST;
    class VID;
    class MAP;
    class GRAPH;
    class SPRITE;
    class STRING;
    class BaseStream;
    namespace core { class WeakController; struct PathPosition; }

    namespace RetailSpriteLayout
    {
        constexpr std::size_t CommandWordsFromRecordStack = 0x3Cu;
        constexpr std::size_t SharedPrimaryState = 0x70u;
        constexpr std::size_t SharedSecondaryState = 0x74u;
        constexpr std::size_t ExtendedStateBase = 0x78u;
        constexpr std::size_t LegacyCommandState1 = 0x7Cu;
        constexpr std::size_t AmmoFixedPoint = 0x80u;
        constexpr std::size_t LegacyCommandState2 = 0x84u;
        constexpr std::size_t TurnTimer = 0x88u;
        constexpr std::size_t BehaviorFlags = 0x8Cu;
        constexpr std::size_t CommandWordListVtable = 0x90u;
        constexpr std::size_t DerivedStateBase = 0xA0u;
        constexpr std::size_t EngineChainPrevious = 0xA4u;
        constexpr std::size_t EngineChainNext = 0xA8u;
        constexpr std::size_t EngineCommandReferenceOwner = 0xACu;
        constexpr std::size_t EngineCommandArgument0 = 0xB0u;
        constexpr std::size_t EngineCommandArgument1 = 0xB4u;
        constexpr std::size_t EngineCommandArgument2 = 0xB8u;
        constexpr std::size_t EngineAccelerationDelay = 0xBCu;
        constexpr std::size_t EngineTargetSpeed = 0xC0u;
        constexpr std::size_t PushLineActive = 0xC4u;
        constexpr std::size_t PrimaryPathNode = 0xC8u;
        constexpr std::size_t PrimaryPathProgress = 0xCCu;
        constexpr std::size_t PrimaryPathAuxiliary = 0xD0u;
        constexpr std::size_t PrimaryPathEdgeIndex = 0xD4u;
        constexpr std::size_t SecondaryPathNode = 0xD8u;
        constexpr std::size_t SecondaryPathProgress = 0xDCu;
        constexpr std::size_t SecondaryPathAuxiliary = 0xE0u;
        constexpr std::size_t SecondaryPathEdgeIndex = 0xE4u;
        constexpr std::size_t SecondaryStateFlag = 0xE8u;
        constexpr std::size_t ProductionBatchCompletionPending = 0xECu;
        constexpr std::size_t PreviousPathX = 0xF0u;
        constexpr std::size_t PreviousPathY = 0xF4u;
        constexpr std::size_t PreviousPathZ = 0xF8u;
        constexpr std::size_t RouteActionReady = 0xFCu;
        constexpr std::size_t RouteActionStartTime = 0x100u;
        constexpr std::size_t ProductionSequenceId = 0x104u;
        constexpr std::size_t PathBuffer = 0x108u;
        constexpr std::size_t PathBufferSize = 0xACCu;
        constexpr std::size_t WordStride = sizeof(std::uint32_t);
    }

    enum SpriteTypeMask : DWORD
    {
        U_TERRAIN = 1,
        U_OBJECT = 2,
        U_UNIT = 4,
        U_MONSTER = 4,
        U_AVIA = 8,
        U_MENU = 16,
        U_RAILWAY = 32,
        U_REGION = 64,
        U_CANNON = 512,
        U_SPRITE = 1024
    };

    enum SpriteClassId : DWORD
    {

        B_TERRAIN = 0,
        B_OBJECT = 1,
        B_UNIT = 2,
        B_BUILDING = 3,
        B_AVIA = 4,
        B_CANNON = 5,
        B_PRIMITIVE = 6,
        // Retail createSpriteViaApplicationFactory has a private class-7 constructor route; no
        // EXPORT.LGC symbolic name is used here until a stable semantic name is available.
        B_BUILDEDTERRAIN = 8, // private retail class-8 route, not an EXPORT.LGC define
        B_SPRITE = 9,
        B_FRAME = 10,
        B_LINKER = 12,
        B_TEXT = 19,
        B_CIV_ROBOT = 20,
        B_ENGINE = 21,
        B_RAIL = 22,
        B_REGION = 23,
        B_DEPO = 24,
        B_CREATURE = 25,
        B_BALLOON = 26,
        B_MISSILE = 27
    };

    enum ObjectPropertyFlag : DWORD
    {
        P_RANDBIRTH = 1u << 0,
        P_GRAVITY = 1u << 1,
        P_GRAVITY2 = 1u << 2,
        P_BUILDSIZETOGRIDZ = 1u << 3,
        P_TRACK = 1u << 4,
        P_BUILDVIDZTOGRIDZ = 1u << 5,
        P_HASH = 1u << 6,
        P_MAP = 1u << 6,
        P_BIRTHASSMOKE = 1u << 7,
        P_NOISE = 1u << 8,
        P_ZEROZ = 1u << 9,
        P_RANDSPEED = 1u << 10,
        P_GAMMA = 1u << 11,
        P_WIND = 1u << 12,
        P_SKIPMAPED = 1u << 13,
        P_CRUSH = 1u << 14,
        P_ALWAYSTOP = 1u << 15,
        P_WAVE = 1u << 16,
        P_INVISIBLEFORENEMY = 1u << 17,
        P_CREATECHILDEND = 1u << 18,
        P_VERTDIR = 1u << 19,
        P_MOVEWITHANYDIRECTION = 1u << 20,
        P_BLUR = 1u << 21,
        P_RANDZSPEED = 1u << 22,
        P_DBLLIGHT = 1u << 23,
        P_ONEPHASE = 1u << 24,
        P_NOTCHANGELINKERCOOR = 1u << 25,
        P_RADIALDAMAGE = 1u << 26,
        P_SELFMOVING = 1u << 27,
        P_BOUNCE = 1u << 28,
        P_HARDWAREDIRECT = 1u << 29,
        P_GROUND = 1u << 30,
        P_MAPPEDBUILD = 1u << 30,
        P_NOTDAMAGEFORFRIEND = 1u << 31
    };

    struct RestoreCommand
    {
        std::string layout;
        std::uint32_t opcode = 0;
        std::array<std::uint32_t, 4> raw{};
        std::array<SPRITE*, 3> resolvedSpriteArgs{};
        bool opcodeFromExportLgc = false;
        bool opcodeLooksLikeAnimation = false;
    };

    struct SpriteCommandRecord
    {
        std::uint32_t opcode = 0;
        std::uint32_t argument1 = 0;
        std::uint32_t argument2 = 0;
        std::uint32_t argument3 = 0;
    };
    SpriteCommandRecord* copyCommandRecord(SpriteCommandRecord* destination, const SpriteCommandRecord* source) noexcept;
    static_assert(sizeof(SpriteCommandRecord) == 0x10, "SPRITE command record must remain 16 bytes");

    void* destroyCommandRecordListOwner(void* rawListOwner, unsigned char deleteSelfFlag) noexcept;
    std::uint32_t currentCommandRecordListVtable() noexcept;

    void* destroyCommandWordListOwner(void* rawListOwner, unsigned char deleteSelfFlag) noexcept;
    std::uint32_t currentCommandWordListVtable() noexcept;

    class SpriteCommandStack
    {
    public:
        SpriteCommandStack();
        SpriteCommandStack(const SpriteCommandStack& other);
        SpriteCommandStack& operator=(const SpriteCommandStack& other);
        SpriteCommandStack(SpriteCommandStack&& other) noexcept;
        SpriteCommandStack& operator=(SpriteCommandStack&& other) noexcept;
        ~SpriteCommandStack();

        void clear();
        void releaseCommandRecordsRetailTail();
        void clearTargetReferences(SPRITE* target);
        void initializeCommandWords();
        void releaseCommandWordsRetailTail();

        void markCommandWordsPhysicalOwner(bool enabled) noexcept;
        void appendCommandRecord(const SpriteCommandRecord& command);
        void prependCommandRecord(const SpriteCommandRecord& command);
        void insertCommandRecord(size_t index, const SpriteCommandRecord& command);
        void ensureCommandRecordCapacityRetail(std::uint32_t requiredCapacity);
        void setCommandRecordCount(std::uint32_t count);
        void serializeCommandRecordsText(STRING& out) const;
        std::string serializeCommandRecordsText() const;
        void parseCommandRecordsText(const STRING& text);
        void queueCommandBeforeStopSentinel(std::uint32_t opcode, int argument1, int argument2, int argument3);
        void saveCommandRecordsToStream(BaseStream* stream);
        void restoreCommandRecordsFromStream(BaseStream* stream, const SPRITE* ownerSprite);
        bool restoreOldMapCommandRecordsFromStream(BaseStream* stream, int mapVersion, const SPRITE* ownerSprite, int* armyBucket);
        void serializeCommandWordsText(STRING& out) const;
        std::string serializeCommandWordsText() const;
        void parseCommandWordsText(STRING text);
        std::uint32_t commandWordCount() const noexcept;
        const std::int16_t* commandWordData() const noexcept;
        int findLastCommandWord(std::uint16_t word) const noexcept;
        int findLastCommandWordPointer(const std::int16_t* word) const noexcept;
        int removeCommandWordAt(int index) noexcept;
        void ensureCommandWordCapacityRetail(std::uint32_t requiredCapacity);
        void appendCommandWordRetail(std::int16_t word);

        struct CommandRecordStorage
        {
            std::uint32_t words[4];
        };

        struct CommandRecordList
        {
            // Exact retail SPRITE+0x58 embedded 16-byte list owner on x86.
            std::uint32_t vtableTag = 0;
            std::uint32_t count = 0;
            std::uint32_t capacity = 0;
            CommandRecordStorage* records = nullptr;
        };

        struct CommandWordList
        {

            std::uint32_t count = 0;
            std::uint32_t capacity = 0;
            std::int16_t* words = nullptr;
        };

        size_t size() const { return m_commandRecords.count; }
        bool empty() const { return m_commandRecords.count == 0; }

    private:
        friend class SPRITE;

        void releaseCommandRecords();
        void copyCommandRecordsFrom(const SpriteCommandStack& other);
        void ensureCommandRecordCapacity(std::uint32_t requiredCapacity);
        void writeCommandRecord(std::size_t index, const SpriteCommandRecord& command);

        void releaseCommandWords();
        void copyCommandWordsFrom(const SpriteCommandStack& other);
        void ensureCommandWordCapacity(std::uint32_t requiredCapacity);
        void appendCommandWord(std::int16_t word);

        CommandWordList& commandWords() noexcept;
        const CommandWordList& commandWords() const noexcept;

        CommandRecordList m_commandRecords;
    };

    struct SpriteRestoreState
    {
        int oldAddress = 0;
        std::vector<BYTE> payload;
        std::vector<std::uint32_t> words;
        std::vector<BYTE> tailBytes;
        std::string layout;
        std::vector<RestoreCommand> commands;
        std::vector<std::uint32_t> objectRefs;
        std::vector<std::uint32_t> extraValues;
        std::string textLabel;
        std::string note;
        int actionOpcode = 0;
        int mapVersion = 0;
        bool appliedThroughSpriteAction = false;
        bool actionExecutionEnabled = false;
        bool decodedUsingExportLgcConstants = false;
    };

    struct SpriteHostState
    {
        MAP* owner = nullptr;
        int oldAddress = 0;
        int number = 0;

        float linkerX = 0.0f;
        float linkerY = 0.0f;
        float linkerZ = 0.0f;
        int linkerDirection = 0;
        SPRITE* linkerOwner = nullptr;

        std::array<std::uint32_t, 2> actionAuxCommandMask{};
        bool runtimeInitializedFromVid = false;
        Gamma gamma;
        VECTOR scale{1.0f, 1.0f, 1.0f};
        bool hiddenByCliping = false;

        std::uint32_t extendedSpriteVtableSnapshot = 0;
        int sharedPrimaryState = -1;
        int sharedSecondaryState = 0;
        int legacyCommandState0 = 0;
        int legacyCommandState1 = 0;
        int legacyCommandState2 = -1;
        int turnTimer = 0;
        int behaviorFlags = 0;
        std::uint32_t commandWordListVtable = 0;
        int ammoFixedPointValue = 0;
        std::array<int, 21> extendedStateValue{};

        core::WeakController* primaryPathNode = nullptr;
        int primaryPathProgress = 0;
        int primaryPathAuxiliary = 0;
        int primaryPathEdgeIndex = 0;
        core::WeakController* secondaryPathNode = nullptr;
        int secondaryPathProgress = 0;
        int secondaryPathAuxiliary = 0;
        int secondaryPathEdgeIndex = 0;
        int balloonTargetBusy = 0;
        std::array<unsigned char, 2500> pathBuffer{};
        int pathBufferSize = 0;

        SPRITE* engineChainPrevious = nullptr;
        SPRITE* engineChainNext = nullptr;
        SPRITE* engineCommandReferenceOwner = nullptr;
        int engineCommandArgument0 = 0;
        int engineCommandArgument1 = 0;
        int engineCommandArgument2 = 0;
        int engineAccelerationDelay = 0;
        float engineTargetSpeed = 0.0f;
        int pushLineActive = 0;
        int routeActionReady = 0;
        int routeActionStartTimeMs = 0;
        float previousPathX = 0.0f;
        float previousPathY = 0.0f;
        float previousPathZ = 0.0f;
    };

    class SPRITE
    {
    public:
        SPRITE(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir);
        SPRITE(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent);
        virtual ~SPRITE();
        SPRITE* baseSpriteScalarDeletingDestructor(unsigned char flags) noexcept;
        SPRITE* linkedSpriteScalarDeletingDestructor(unsigned char flags) noexcept;
        SPRITE* initializeExtendedSpriteState(VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent) noexcept;
        SPRITE* initializeCommandSpriteState(VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent) noexcept;
        SPRITE* commandSpriteScalarDeletingDestructor(unsigned char flags) noexcept;
        void destroyCommandSpriteState() noexcept;
        void destroyBaseSpriteState();

        VID* Vid() const { return m_vid; }
        float X() const { return m_xyz.x; }
        float Y() const { return m_xyz.y; }
        float Z() const { return m_xyz.z; }
        float xCoordinateValue() const noexcept { return m_xyz.x; }
        float yCoordinateValue() const noexcept { return m_xyz.y; }
        const VECTOR& xyz() const { return m_xyz; }
        ANGLE Direction() const { return m_direction; }
        int directionIndex() const noexcept { return m_direction.Int(); }
        int directionIndexValue() const noexcept { return m_direction.Int(); }
        int SizeTo(const VECTOR2& target) const;
        ANGLE DirectionTo(const VECTOR2& target) const;
        int currentAnimation() const { return m_currentAnimation; }
        void setCurrentAnimationDirect(int value) noexcept { m_currentAnimation = value; }
        int currentFrame() const { return m_currentFrame; }
        void setCurrentFrameDirect(int value) noexcept { m_currentFrame = value; }
        void setXPosition(float value) noexcept { m_xyz.x = value; }
        void setYPosition(float value) noexcept { m_xyz.y = value; }
        int currentFrameBegin() const { return m_currentFrameBegin; }
        int currentFrameEnd() const { return m_currentFrameEnd; }
        void ChangeAnimation(int animationId);
        void setCurrentAnimation(int value);
        int renderFrameIndexForClock(std::uint32_t clockMilliseconds) const;
        int renderFrameOffsetForClock(std::uint32_t clockMilliseconds) const;
        virtual int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier);
        virtual void Tact();
        virtual void MoveTact();
        virtual void DeletePointerToSprite(SPRITE* sprite);
        virtual void Draw();
        virtual void DrawDebugOverlay(); // retail slot +0x18; base target is drawBaseDebugOverlay
        void drawBaseDebugOverlay();
        virtual void DrawRelationDebugOverlay();

        // Retail dispatchDebugOverlay debug-dispatch owner used by Application::drawApplicationDebugOverlayPass.
        int dispatchDebugOverlay();
        int DrawDebugOverlay(GRAPH& graph) const;
        void DrawSelectionOverlay(GRAPH& graph) const;

        float Speed() const { return m_speed; }
        void setSpeedDirect(float value) noexcept { m_speed = value; }
        void ChangeSpeed(float value);
        float ZSpeed() const { return m_zSpeed; }
        void setZSpeedDirect(float value) noexcept { m_zSpeed = value; }
        void ChangeZSpeed(float value);
        DWORD Timer() const { return m_actionTimer; }
        void SetTimer(DWORD value);
        bool runtimeInitializedFromVid() const { return hostState().runtimeInitializedFromVid; }

        int oldAddress() const { return hostState().oldAddress; }
        void setOldAddress(int value);
        int getNumber() const { return hostState().number; }
        std::uint32_t rawResolveOldSpriteHandleLow32(int oldAddress) const noexcept;
        void setNumber(int value);
        int AddListReference();
        int ReleaseListReference();
        int listReferenceCount() const { return m_listReferenceCount; }
        MAP* mapOwner() const noexcept { return hostState().owner; }
        VID* vidPointer() const noexcept { return m_vid; }
        int isSpriteClass(int spriteClass) const noexcept;
        void setListReferenceCount(int value) noexcept { m_listReferenceCount = value; }
        SPRITE* bestTargetSprite() const noexcept { return m_bestTargetSprite; }
        void setBestTargetSprite(SPRITE* value) noexcept { m_bestTargetSprite = value; }
        void initializeActionAuxState(SPRITE* ownerSprite) noexcept;
        bool ensureActionAuxStateForLocalAction() noexcept;
        SPRITE* initializeBaseSprite(VID* vid, float x, float y, float z, int direction, SPRITE* parent) noexcept;
        void dispatchSpriteCommandMask(const std::uint32_t* commandMask) noexcept;
        int dispatchVirtualAction(std::uint32_t opcode, int argument1, int argument2, int argument3) noexcept;
        int dispatchVirtualAction(ActionCode opcode, int argument1, int argument2, int argument3) noexcept
        {
            return dispatchVirtualAction(static_cast<std::uint32_t>(opcode), argument1, argument2, argument3);
        }
        void setGoalSprite(SPRITE* goal) noexcept;
        void advanceAnimationFrameTimeCapped(int delta) noexcept;
        int SetCommand(int argument1, SPRITE* goal) noexcept;
        int SetCommandWithoutLink(int argument1, SPRITE* goal) noexcept;
        int Move(SPRITE* goal) noexcept;
        void ChangeDirection(int direction) noexcept;
        SPRITE* CanPlace(float x, float y, float z) noexcept;
        SPRITE* CanPlaceWithCrush(float x, float y, float z) noexcept;
        SPRITE* probeMovementFootprint(float x, float y) noexcept;
        static void initializeRetailStartupTrigTables() noexcept;
        static float rawDirectionSin(int index) noexcept;
        static float rawDirectionSinUnchecked(DWORD index) noexcept;
        static float rawDirectionCos(int index) noexcept;
        static float rawDirectionCosUnchecked(DWORD index) noexcept;
        static float rawDirectionSinAux(int index) noexcept;
        static float rawDirectionCosAux(int index) noexcept;
        int switchLinkedWeaponSlot(int value) noexcept;
        int GlideDirection(int value) noexcept;
        int RotateTact(int value, std::uint32_t deltaMs) noexcept;
        int advancePrimitiveFrame() noexcept;
        int computeAttackDecisionCode(std::uint32_t deltaMs) noexcept;
        SPRITE* SeekEnemy() noexcept;
        int enemyPriority(float candidateMetric, float selectedMetric, SPRITE* candidate, SPRITE* selected) noexcept;
        void copyCommandPrefixTo(SPRITE* target) noexcept;
        void clearCommandStackAndReleaseTargets() noexcept;
        int traceMovementCollisionTo(float* xOut, float* yOut, float* zOut) noexcept;
        void Stop() noexcept;
        int StartMove() noexcept;
        int healthRatio255() noexcept;
        void updateAnimationFrameTime(int frameTime) noexcept;
        void playSfxAtWorldPosition(int nsfx) noexcept;
        int spawnAnimationChild() noexcept;
        int canWeaponAffectTarget(SPRITE* owner) noexcept;
        int hasLinkedVidChild() const noexcept;
        int setAttackCommandForTarget(SPRITE* owner) noexcept;
        int sumLinkedChildContributions() noexcept;
        void dispatchEnginePrivateCommand(int opcode, int argument1, int argument2, int argument3) noexcept;
        void dispatchEnginePrivateCommandAtPathPoint(int opcode, int x, int y) noexcept;
        int inheritAdjacentEngineCommand() noexcept;
        SPRITE* engineChainHead() noexcept;
        SPRITE* engineChainTail() noexcept;
        bool isInEngineChain(SPRITE* target) noexcept;
        SPRITE* findCrossingConstraintOwner() noexcept;
        SPRITE* resolvePathOwnerRelation(int* relationOut) noexcept;
        SPRITE* reverseEngineChain() noexcept;
        int scaledEngineChainLength() noexcept;
        void updateEngineChainSpeedTarget() noexcept;
        void approachEngineTargetSpeed(float* speedOut) noexcept;
        void resetEngineChainMovement() noexcept;
        int minimumEngineWeaponRange() noexcept;
        void updatePositionFromPathEndpoints() noexcept;
        void splitEngineChainAtPosition(float x, float y) noexcept;
        void initializeEnginePathEndpoints() noexcept;
        int updateSecondaryPathPosition(core::PathPosition* pathPair) noexcept;
        SPRITE* findEnginePathRelationSprite() noexcept;
        int classifyEngineChainRelation(SPRITE* target, int strictProgressGate) noexcept;
        int attachEngineChain(SPRITE* value) noexcept;
        void clearPathNodeOwnership() noexcept;
        core::WeakController* claimPathNodeOwnership() noexcept;
        int canLinkEngineChain(SPRITE* target) noexcept;
        SPRITE* validateEngineChainLinks() noexcept;
        struct EngineChainMetrics
        {
            // Retail 0x40-byte aggregate used as ECX by collectEngineChainMetrics/accumulateEngineChainSprite.
            // The SPRITE chain head is the explicit argument; the aggregate is
            // the owner, matching the original __thiscall ABI.
            std::uint32_t categoryFlags;
            float maxBattleRange;
            float minBattleRange;
            float weapon10Sum;
            float weapon0CSum;
            float activeWeapon0CSum;
            int movementDelayMs;
            int spriteCount;
            int spriteFrameTimeSum;
            int vidFrameTimeSum;
            int routeMetricSum;
            int weaponMetricSum;
            int distanceSampleCount;
            int fixedDistanceSum;
            int distanceWeightSum;
            int averageDistanceRatio;

            EngineChainMetrics* collectEngineChainMetrics(SPRITE* root) noexcept;
            int accumulateEngineChainSprite(SPRITE* sprite) noexcept;
            int weaponRatioScaledByEight() const noexcept;
        };
        static_assert(sizeof(EngineChainMetrics) == 0x40, "collectEngineChainMetrics retail aggregate must be 0x40 bytes");
        static_assert(offsetof(EngineChainMetrics, categoryFlags) == 0x00, "collectEngineChainMetrics +0x00 mismatch");
        static_assert(offsetof(EngineChainMetrics, maxBattleRange) == 0x04, "collectEngineChainMetrics +0x04 mismatch");
        static_assert(offsetof(EngineChainMetrics, minBattleRange) == 0x08, "collectEngineChainMetrics +0x08 mismatch");
        static_assert(offsetof(EngineChainMetrics, movementDelayMs) == 0x18, "collectEngineChainMetrics +0x18 mismatch");
        static_assert(offsetof(EngineChainMetrics, spriteFrameTimeSum) == 0x20, "collectEngineChainMetrics +0x20 mismatch");
        static_assert(offsetof(EngineChainMetrics, vidFrameTimeSum) == 0x24, "collectEngineChainMetrics +0x24 mismatch");
        static_assert(offsetof(EngineChainMetrics, distanceSampleCount) == 0x30, "collectEngineChainMetrics +0x30 mismatch");
        static_assert(offsetof(EngineChainMetrics, averageDistanceRatio) == 0x3C, "collectEngineChainMetrics +0x3C mismatch");
        float resolveEngineChainCollision(SPRITE* target, int mode) noexcept;
        int resolveEngineChainPathInteraction(core::PathPosition* pathPair, float* distanceOut) noexcept;
        void applyEngineChainPathMovement(core::PathPosition* pathPair, float speed, int delay) noexcept;
        void clearCommandsTargetingThisSprite() noexcept;
        int createRouteMarkerSprites(core::WeakController* pathNode) noexcept;
        int createPathSpritesFromBuffer(core::WeakController* pathNode, SPRITE_POINTER_LIST* list, int nvid) noexcept;
        int evaluateEngineTargetRangeState() noexcept;
        int pathBufferReachesSecondaryTarget(core::WeakController* pathNode) noexcept;
        void updateEngineChainRoute() noexcept;
        void suppressDrawRecursive() noexcept;
        void restoreDrawRecursive() noexcept;
        int animationFrameTime() const noexcept { return m_animationFrameTime; }
        void setAnimationFrameTime(int value) noexcept { m_animationFrameTime = value; }
        void releaseBestTargetSprite() noexcept;
        void deleteChildChainSlot40() noexcept;
        void clearChildBacklinkSlot44() noexcept;
        void releaseActionAuxState() noexcept;
        void releaseCommandRecordsRetailTail() noexcept;
        const std::array<std::uint32_t, 2>& actionAuxCommandMask() const noexcept { return hostState().actionAuxCommandMask; }
        bool hasActionAuxState() const noexcept { return m_actionAuxState != nullptr; }
        std::uint32_t actionAuxPrimaryValue() const noexcept { return m_actionAuxState ? m_actionAuxState->primaryValue : 0; }
        std::uint32_t actionAuxStateValue() const noexcept { return m_actionAuxState ? m_actionAuxState->state : 0; }
        bool spriteGammaOverride(GammaRawPair& out) const noexcept
        {
            if (!m_actionAuxState ||
                (m_actionAuxState->commandMask0 == 0u && m_actionAuxState->commandMask1 == 0u))
                return false;
            out.first = m_actionAuxState->commandMask0;
            out.second = m_actionAuxState->commandMask1;
            return true;
        }
        float actionAuxX() const noexcept { return m_actionAuxState ? m_actionAuxState->sourceX : 0.0f; }
        float actionAuxY() const noexcept { return m_actionAuxState ? m_actionAuxState->sourceY : 0.0f; }
        float actionAuxZ() const noexcept { return m_actionAuxState ? m_actionAuxState->sourceZ : 0.0f; }
        float blurHistoryX() const noexcept { return m_actionAuxState->sourceX; }
        float blurHistoryY() const noexcept { return m_actionAuxState->sourceY; }
        float blurHistoryZ() const noexcept { return m_actionAuxState->sourceZ; }
        std::uint32_t applicationBucketTime() const noexcept { return m_applicationBucketTime; }
        void setApplicationBucketTime(std::uint32_t value) noexcept { m_applicationBucketTime = value; }
        std::uint32_t animationLastTick() const noexcept { return m_animationLastTick; }
        void setAnimationLastTick(std::uint32_t value) noexcept { m_animationLastTick = value; }
        static constexpr unsigned CommandBitsShift = 2u;
        static constexpr DWORD CommandValueMask = 31u;
        static constexpr DWORD CommandBitsMask = CommandValueMask << CommandBitsShift;
        static constexpr unsigned ArmyBitsShift = 10u;
        static constexpr DWORD ArmyValueMask = 3u;
        static constexpr DWORD ArmyBitsMask = ArmyValueMask << ArmyBitsShift;
        static constexpr DWORD MovementStartedFlag = 1u << 7;
        static constexpr DWORD SpatialHashRemovedFlag = 1u << 8;
        static constexpr DWORD CrossedGoalXFlag = 1u << 13;
        static constexpr DWORD CrossedGoalYFlag = 1u << 14;
        static constexpr DWORD CrossedGoalAxesMask = CrossedGoalXFlag | CrossedGoalYFlag;
        static constexpr DWORD DrawSuppressedFlag = 1u << 15;

        DWORD runtimeFlags() const noexcept { return m_runtimeFlags; }
        DWORD commandBits() const noexcept { return m_runtimeFlags & CommandBitsMask; }
        int commandIndex() const noexcept { return static_cast<int>((m_runtimeFlags >> CommandBitsShift) & CommandValueMask); }
        DWORD armyBits() const noexcept { return m_runtimeFlags & ArmyBitsMask; }
        int armyIndex() const noexcept { return static_cast<int>((m_runtimeFlags >> ArmyBitsShift) & ArmyValueMask); }
        bool sameArmy(const SPRITE& other) const noexcept { return armyIndex() == other.armyIndex(); }
        int isCommandIndex(int value) const noexcept { return value == commandIndex() ? 1 : 0; }
        void setRuntimeFlags(DWORD value) noexcept { m_runtimeFlags = value; }
        int attackDecisionCode() const noexcept { return m_attackDecisionCode; }
        void setAttackDecisionCode(int value) noexcept { m_attackDecisionCode = value; }
        SPRITE* goalSprite() const noexcept { return m_goalSprite; }
        void setGoalSpriteDirect(SPRITE* value) noexcept { m_goalSprite = value; }
        // Retail [SPRITE+0x50]: shared wait/action timer used by Tact and
        // ACT_PAUSE/ACT_GET_TIMER/ACT_SET_TIMER.  Keep one authoritative owner.
        std::uint32_t actionTimer() const noexcept { return m_actionTimer; }
        void setActionTimer(std::uint32_t value) noexcept { m_actionTimer = value; }
        float linkerX() const noexcept;
        float linkerY() const noexcept;
        float linkerZ() const noexcept;
        int linkerDirection() const noexcept;
        SPRITE* linkerOwner() const noexcept;
        void setLinkerState(float x70, float y74, float z78, int direction7C, SPRITE* owner80) noexcept;
        // Retail shared SPRITE/TERRAIN-derived slot +0x70. initializeExtendedSpriteState
        // initializes it to -1; CANNON uses bit0, while ENGINE repair owners
        // applyRepairPulseTo/updateRepairTarget use the integer accumulator semantics.
        int sharedPrimaryState() const noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::SharedPrimaryState);
#else
            return hostState().sharedPrimaryState;
#endif
        }
        void setSharedPrimaryState(int value) noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::SharedPrimaryState) = value;
#else
            hostState().sharedPrimaryState = value;
#endif
        }
        int sharedSecondaryState() const noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::SharedSecondaryState);
#else
            return hostState().sharedSecondaryState;
#endif
        }
        void setSharedSecondaryState(int value) noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::SharedSecondaryState) = value;
#else
            hostState().sharedSecondaryState = value;
#endif
        }
        int ammoFixedPoint() const noexcept;
        int turnTimer() const noexcept;
        void setTurnTimer(int value) noexcept;
        int behaviorFlags() const noexcept;
        void setBehaviorFlags(int value) noexcept;
        void setCommandWordListVtable(std::uint32_t value) noexcept;
        bool isDrawSuppressed() const noexcept { return (m_runtimeFlags & DrawSuppressedFlag) != 0; }
        SPRITE* childChain() const noexcept { return m_childChain; }
        // Retail raw virtual/debug helpers childChainDebugAccessor/goalSpriteDebugAccessor.
        SPRITE* childChainDebugAccessor() const noexcept { return m_childChain; }
        SPRITE* goalSpriteDebugAccessor() const noexcept { return m_goalSprite; }
        SPRITE* engineChainPrevious() const noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<SPRITE* const*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::EngineChainPrevious);
#else
            return hostState().engineChainPrevious;
#endif
        }
        SPRITE* engineChainNext() const noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<SPRITE* const*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::EngineChainNext);
#else
            return hostState().engineChainNext;
#endif
        }
        SPRITE* engineCommandReferenceOwner() const noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<SPRITE* const*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::EngineCommandReferenceOwner);
#else
            return hostState().engineCommandReferenceOwner;
#endif
        }
        int balloonTargetBusy() const noexcept { return hostState().balloonTargetBusy; }
        void setBalloonTargetBusy(int value) noexcept { hostState().balloonTargetBusy = value; }
        SPRITE* childBacklink() const noexcept { return m_childBacklink; }
        void setChildChain(SPRITE* value) noexcept { m_childChain = value; }
        void setEngineChainPrevious(SPRITE* value) noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            *reinterpret_cast<SPRITE**>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::EngineChainPrevious) = value;
#else
            hostState().engineChainPrevious = value;
#endif
        }
        void setEngineChainNext(SPRITE* value) noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            *reinterpret_cast<SPRITE**>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::EngineChainNext) = value;
#else
            hostState().engineChainNext = value;
#endif
        }
        void setEngineCommandReferenceOwner(SPRITE* value) noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            *reinterpret_cast<SPRITE**>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::EngineCommandReferenceOwner) = value;
#else
            hostState().engineCommandReferenceOwner = value;
#endif
        }

        SPRITE*& engineChainPreviousRef() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<SPRITE**>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::EngineChainPrevious);
#else
            return hostState().engineChainPrevious;
#endif
        }
        SPRITE*& engineChainNextRef() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<SPRITE**>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::EngineChainNext);
#else
            return hostState().engineChainNext;
#endif
        }
        SPRITE*& engineCommandReferenceOwnerRef() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<SPRITE**>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::EngineCommandReferenceOwner);
#else
            return hostState().engineCommandReferenceOwner;
#endif
        }
        int& engineCommandArgument0Ref() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::EngineCommandArgument0);
#else
            return hostState().engineCommandArgument0;
#endif
        }
        int& engineCommandArgument1Ref() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::EngineCommandArgument1);
#else
            return hostState().engineCommandArgument1;
#endif
        }
        int& engineCommandArgument2Ref() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::EngineCommandArgument2);
#else
            return hostState().engineCommandArgument2;
#endif
        }
        int& engineAccelerationDelayRef() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::EngineAccelerationDelay);
#else
            return hostState().engineAccelerationDelay;
#endif
        }
        float& engineTargetSpeedRef() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::EngineTargetSpeed);
#else
            return hostState().engineTargetSpeed;
#endif
        }
        int& pushLineActiveRef() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::PushLineActive);
#else
            return hostState().pushLineActive;
#endif
        }
        core::WeakController*& primaryPathNodeRef() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<core::WeakController**>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::PrimaryPathNode);
#else
            return hostState().primaryPathNode;
#endif
        }
        int& primaryPathProgressRef() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::PrimaryPathProgress);
#else
            return hostState().primaryPathProgress;
#endif
        }
        int& primaryPathAuxiliaryRef() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::PrimaryPathAuxiliary);
#else
            return hostState().primaryPathAuxiliary;
#endif
        }
        int& primaryPathEdgeIndexRef() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::PrimaryPathEdgeIndex);
#else
            return hostState().primaryPathEdgeIndex;
#endif
        }
        core::WeakController*& secondaryPathNodeRef() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<core::WeakController**>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::SecondaryPathNode);
#else
            return hostState().secondaryPathNode;
#endif
        }
        int& secondaryPathProgressRef() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::SecondaryPathProgress);
#else
            return hostState().secondaryPathProgress;
#endif
        }
        int& secondaryPathAuxiliaryRef() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::SecondaryPathAuxiliary);
#else
            return hostState().secondaryPathAuxiliary;
#endif
        }
        int& secondaryPathEdgeIndexRef() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::SecondaryPathEdgeIndex);
#else
            return hostState().secondaryPathEdgeIndex;
#endif
        }
        float& previousPathXRef() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::PreviousPathX);
#else
            return hostState().previousPathX;
#endif
        }
        float& previousPathYRef() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::PreviousPathY);
#else
            return hostState().previousPathY;
#endif
        }
        float& previousPathZRef() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::PreviousPathZ);
#else
            return hostState().previousPathZ;
#endif
        }
        int& routeActionReadyRef() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::RouteActionReady);
#else
            return hostState().routeActionReady;
#endif
        }
        int& routeActionStartTimeRef() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::RouteActionStartTime);
#else
            return hostState().routeActionStartTimeMs;
#endif
        }
        int& pathBufferSizeRef() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::PathBufferSize);
#else
            return hostState().pathBufferSize;
#endif
        }
        core::WeakController* primaryPathNode() const noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<core::WeakController* const*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::PrimaryPathNode);
#else
            return hostState().primaryPathNode;
#endif
        }
        int primaryPathEdgeIndex() const noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::PrimaryPathEdgeIndex);
#else
            return hostState().primaryPathEdgeIndex;
#endif
        }
        core::WeakController* secondaryPathNode() const noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<core::WeakController* const*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::SecondaryPathNode);
#else
            return hostState().secondaryPathNode;
#endif
        }
        int secondaryPathEdgeIndex() const noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::SecondaryPathEdgeIndex);
#else
            return hostState().secondaryPathEdgeIndex;
#endif
        }
        int engineCommandArgument0Value() const noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::EngineCommandArgument0);
#else
            return hostState().engineCommandArgument0;
#endif
        }
        void setEngineCommandArgument0(int value) noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::EngineCommandArgument0) = value;
#else
            hostState().engineCommandArgument0 = value;
#endif
        }
        int engineCommandArgument1Value() const noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::EngineCommandArgument1);
#else
            return hostState().engineCommandArgument1;
#endif
        }
        void setEngineCommandArgument1(int value) noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::EngineCommandArgument1) = value;
#else
            hostState().engineCommandArgument1 = value;
#endif
        }
        core::WeakController* engineCommandArgument0Node() const noexcept { return reinterpret_cast<core::WeakController*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(engineCommandArgument0Value()))); }
        core::WeakController* engineCommandArgument1Node() const noexcept { return reinterpret_cast<core::WeakController*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(engineCommandArgument1Value()))); }
        core::WeakController* engineCommandArgument2Node() const noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            const int value = *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::EngineCommandArgument2);
#else
            const int value = hostState().engineCommandArgument2;
#endif
            return reinterpret_cast<core::WeakController*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(value)));
        }
        std::uint32_t commandRecordCount() const noexcept { return m_commandStack.m_commandRecords.count; }
        std::uint32_t commandRecordWord(std::size_t index, std::size_t word) const noexcept { return m_commandStack.m_commandRecords.records[index].words[word]; }
        unsigned char* pathBufferData() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::PathBuffer;
#else
            return hostState().pathBuffer.data();
#endif
        }
        const unsigned char* pathBufferData() const noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::PathBuffer;
#else
            return hostState().pathBuffer.data();
#endif
        }
        int pathBufferSize() const noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::PathBufferSize);
#else
            return hostState().pathBufferSize;
#endif
        }
        void setPathBufferSize(int value) noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::PathBufferSize) = value;
#else
            hostState().pathBufferSize = value;
#endif
        }
        void setChildBacklink(SPRITE* value) noexcept { m_childBacklink = value; }
        size_t childChainCount() const noexcept;
        size_t childChainVidCount(const VID* vid) const noexcept;
        bool childChainContainsVid(const VID* vid) const noexcept;
        size_t childChainApplicationBucketCandidateCount() const noexcept;
        bool isHiddenByCliping() const { return hostState().hiddenByCliping; }
        void setHiddenByCliping(bool value) { hostState().hiddenByCliping = value; }

        void ChangeArmy(int value);
        void MoveHashBeforeCoordinateWrite(float nextX, float nextY);
        void performBaseMovementTact() noexcept;
        int steerAwayFromMapBoundary(float x, float y) noexcept;
        void computeNextMovementPosition(float* xOut, float* yOut, float* zOut) noexcept;
        SPRITE* CanPlaceWithCrushAndGlide(float* xOut, float* yOut, float* zOut) noexcept;
        void ChangeCoor(float x, float y, float z) noexcept;
        int dispatchActionOpcode(std::uint32_t opcode, int argument1 = 0, int argument2 = 0, int argument3 = 0);

        const Gamma& gamma() const { return hostState().gamma; }
        const Gamma& GetGamma() const;
        Gamma GetUniqueGamma() const;
        void setGamma(const Gamma& value) { SetGamma(value); }
        void SetGamma(const Gamma& value);
        void addGamma(const Gamma& gammaDelta);

        const VECTOR& scale() const { return hostState().scale; }
        void setScale(const VECTOR& value) { hostState().scale = value; }

        void attachChildSprite(SPRITE* child);
        size_t CreateChildForAnimation(int animationSlot, bool birthConstructorRoute = false);
        size_t CreateChild();

        int removeFromDrawBucketsRecursive();
        char* addToDrawBucketsRecursive();
        unsigned int serializeSpriteRecord(RESOURCE* resource) noexcept;
        int changeArmyBucket(int bucketIndex) noexcept;
        void ensureLinkedVidChild() noexcept;

        int insertChildChainHead(SPRITE* child);
        int appendChildChain(SPRITE* child);
        void detachFromChildChain();
        int deleteChildByVid(VID* childVid);

        static SpriteCommandRecord buildCommandRecord(std::uint32_t opcode, int argument1, int argument2, int argument3);
        void serializeCommandRecordsText(STRING& out) const;
        std::string serializeCommandRecordsText() const;
        void parseCommandRecordsText(const STRING& text);
        void queueCommandBeforeStopSentinel(std::uint32_t opcode, int argument1, int argument2, int argument3);

        void serializeCommandWordsText(STRING& out) const;
        std::string serializeCommandWordsText() const;
        void parseCommandWordsText(STRING text);
        std::uint32_t commandWordCount() const noexcept { return m_commandStack.commandWordCount(); }
        const std::int16_t* commandWordData() const noexcept { return m_commandStack.commandWordData(); }
        int findLastCommandWord(std::uint16_t word) const noexcept { return m_commandStack.findLastCommandWord(word); }
        int removeCommandWordValue(std::uint16_t word) noexcept;
        int commandWordAt(int index) const noexcept;
        int appendCommandWordValue(std::uint16_t word) noexcept;
        void growCommandWordStorage() noexcept;
        int clearCommandWordList() noexcept;
        int hasCommandOpcode(std::uint32_t opcode) const noexcept;
        void appendCommandRecord(const SpriteCommandRecord& command);
        void prependCommandRecord(const SpriteCommandRecord& command);
        std::uint32_t lastCommandOpcode() const noexcept;

        bool forceAmmoCapacityAfterAdd() const noexcept { return sharedSecondaryState() != 0; }
        int ammoCount() const noexcept;
        void setAmmoFixedPoint(int value) noexcept;
        int addAmmoUnits(int value) noexcept;
        int refillAmmoByCapacityFraction(int divisor) noexcept;
        int animationRemainingPercent() const noexcept;
        int ammoMissingPercent() const noexcept;
        int dispatchBaseActionOpcode(int opcode, int argument1, int argument2, int argument3) noexcept;
        int dispatchBaseActionOpcode(ActionCode opcode, int argument1, int argument2, int argument3) noexcept
        {
            return dispatchBaseActionOpcode(static_cast<int>(opcode), argument1, argument2, argument3);
        }
        int extendedStateValue(int index) const noexcept;
        int setExtendedStateValue(int index, int value) noexcept;
        int derivedStateValue(int index) const noexcept;
        int setDerivedStateValue(int index, int value) noexcept;
        int dispatchExtendedSpriteActionOpcode(int opcode, int argument1, int argument2, int argument3) noexcept;
        int repairLinkedChildState(int createMissingLinker) noexcept;
        int dispatchPrivateClass7ActionOpcode(int opcode, int argument1, int argument2, int argument3) noexcept;

        const SpriteCommandStack& commandStack() const { return m_commandStack; }
        size_t noCommandStackEntry() const { return m_commandStack.size(); }

    private:
        friend class PRIMITIVE;
        friend class UNIT;
        friend class ENGINE;
        friend struct SpriteRetailPrefixProbe;
        void initializeAnimationRouteFromVid();

        struct ActionAuxState
        {
            float sourceX = 0.0f;
            float sourceY = 0.0f;
            float sourceZ = 0.0f;
            std::uint32_t lastUpdateTime = 0;
            std::uint32_t primaryValue = 0;
            std::uint32_t state = 0;
            std::uint32_t commandMask0 = 0;
            std::uint32_t commandMask1 = 0;
        };
        static_assert(sizeof(ActionAuxState) == 0x20, "SPRITE+0x68 command owner must remain 0x20 bytes");

        // Native retail SPRITE base prefix.  On Win32/x86 the virtual-table
        // pointer occupies +0x00 and the members below intentionally follow
        // the exact 0x04..0x6C field order used by initializeBaseSprite/destroyBaseSpriteState.
        int m_attackDecisionCode = 0;                 // +0x04
        int m_currentFrameBegin = 0;                    // +0x08
        int m_currentFrame = 0;                         // +0x0C
        int m_currentFrameEnd = 0;                      // +0x10
        std::uint32_t m_applicationBucketTime = 0; // +0x14
        std::uint32_t m_animationLastTick = 0; // +0x18
        VID* m_vid = nullptr;                           // +0x1C
        float m_speed = 0.0f;                           // +0x20
        float m_zSpeed = 0.0f;                          // +0x24
        // Retail initializeBaseSprite does not zero the high WORD before applying
        // `and 0xFFFF0C00`; those bits are intentionally inherited from the
        // raw 0x70-byte allocation. Do not add a C++ default initializer here.
        DWORD m_runtimeFlags;                         // +0x28
        int m_listReferenceCount = 0;                   // +0x2C
        VECTOR m_xyz;                                   // +0x30..+0x38
        SPRITE* m_goalSprite = nullptr;      // +0x3C
        SPRITE* m_childChain = nullptr;         // +0x40
        SPRITE* m_childBacklink = nullptr;      // +0x44
        int m_currentAnimation = 0;                     // +0x48
        ANGLE m_direction;                              // +0x4C
        std::uint32_t m_actionTimer = 0;             // +0x50
        int m_animationFrameTime = 0;              // +0x54
        SpriteCommandStack m_commandStack;              // +0x58..+0x67 on x86
        ActionAuxState* m_actionAuxState = nullptr; // +0x68
        SPRITE* m_bestTargetSprite = nullptr;          // +0x6C

        SpriteHostState& hostState() noexcept;
        const SpriteHostState& hostState() const noexcept;
        void releaseHostState() noexcept;
    };

#if UINTPTR_MAX == 0xFFFFFFFFu
    static_assert(sizeof(SpriteCommandStack) == 0x10,
                  "retail SPRITE+0x58 command owner must be exactly 0x10 bytes on x86");

    struct SpriteRetailPrefixProbe
    {
        static constexpr std::size_t slot04 = offsetof(SPRITE, m_attackDecisionCode);
        static constexpr std::size_t frameBegin08 = offsetof(SPRITE, m_currentFrameBegin);
        static constexpr std::size_t frame0C = offsetof(SPRITE, m_currentFrame);
        static constexpr std::size_t frameEnd10 = offsetof(SPRITE, m_currentFrameEnd);
        static constexpr std::size_t bucketTime14 = offsetof(SPRITE, m_applicationBucketTime);
        static constexpr std::size_t animationTick18 = offsetof(SPRITE, m_animationLastTick);
        static constexpr std::size_t vid1C = offsetof(SPRITE, m_vid);
        static constexpr std::size_t speed20 = offsetof(SPRITE, m_speed);
        static constexpr std::size_t zSpeed24 = offsetof(SPRITE, m_zSpeed);
        static constexpr std::size_t flags28 = offsetof(SPRITE, m_runtimeFlags);
        static constexpr std::size_t refCount2C = offsetof(SPRITE, m_listReferenceCount);
        static constexpr std::size_t xyz30 = offsetof(SPRITE, m_xyz);
        static constexpr std::size_t xyz34 = offsetof(SPRITE, m_xyz) + sizeof(float);
        static constexpr std::size_t xyz38 = offsetof(SPRITE, m_xyz) + sizeof(float) * 2u;
        static constexpr std::size_t target3C = offsetof(SPRITE, m_goalSprite);
        static constexpr std::size_t child40 = offsetof(SPRITE, m_childChain);
        static constexpr std::size_t backlink44 = offsetof(SPRITE, m_childBacklink);
        static constexpr std::size_t animation48 = offsetof(SPRITE, m_currentAnimation);
        static constexpr std::size_t direction4C = offsetof(SPRITE, m_direction);
        static constexpr std::size_t timer50 = offsetof(SPRITE, m_actionTimer);
        static constexpr std::size_t frameTime54 = offsetof(SPRITE, m_animationFrameTime);
        static constexpr std::size_t commandList58 = offsetof(SPRITE, m_commandStack);
        static constexpr std::size_t commandOwner68 = offsetof(SPRITE, m_actionAuxState);
        static constexpr std::size_t ptrSprite6C = offsetof(SPRITE, m_bestTargetSprite);
    };

    static_assert(SpriteRetailPrefixProbe::slot04 == 0x04, "SPRITE +04 mismatch");
    static_assert(SpriteRetailPrefixProbe::frameBegin08 == 0x08, "SPRITE +08 mismatch");
    static_assert(SpriteRetailPrefixProbe::frame0C == 0x0C, "SPRITE +0C mismatch");
    static_assert(SpriteRetailPrefixProbe::frameEnd10 == 0x10, "SPRITE +10 mismatch");
    static_assert(SpriteRetailPrefixProbe::bucketTime14 == 0x14, "SPRITE +14 mismatch");
    static_assert(SpriteRetailPrefixProbe::animationTick18 == 0x18, "SPRITE +18 mismatch");
    static_assert(SpriteRetailPrefixProbe::vid1C == 0x1C, "SPRITE +1C mismatch");
    static_assert(SpriteRetailPrefixProbe::speed20 == 0x20, "SPRITE +20 mismatch");
    static_assert(SpriteRetailPrefixProbe::zSpeed24 == 0x24, "SPRITE +24 mismatch");
    static_assert(SpriteRetailPrefixProbe::flags28 == 0x28, "SPRITE +28 mismatch");
    static_assert(SpriteRetailPrefixProbe::refCount2C == 0x2C, "SPRITE +2C mismatch");
    static_assert(SpriteRetailPrefixProbe::xyz30 == 0x30, "SPRITE +30 mismatch");
    static_assert(SpriteRetailPrefixProbe::xyz34 == 0x34, "SPRITE +34 mismatch");
    static_assert(SpriteRetailPrefixProbe::xyz38 == 0x38, "SPRITE +38 mismatch");
    static_assert(SpriteRetailPrefixProbe::target3C == 0x3C, "SPRITE +3C mismatch");
    static_assert(SpriteRetailPrefixProbe::child40 == 0x40, "SPRITE +40 mismatch");
    static_assert(SpriteRetailPrefixProbe::backlink44 == 0x44, "SPRITE +44 mismatch");
    static_assert(SpriteRetailPrefixProbe::animation48 == 0x48, "SPRITE +48 mismatch");
    static_assert(SpriteRetailPrefixProbe::direction4C == 0x4C, "SPRITE +4C mismatch");
    static_assert(SpriteRetailPrefixProbe::timer50 == 0x50, "SPRITE +50 mismatch");
    static_assert(SpriteRetailPrefixProbe::frameTime54 == 0x54, "SPRITE +54 mismatch");
    static_assert(SpriteRetailPrefixProbe::commandList58 == 0x58, "SPRITE +58 mismatch");
    static_assert(SpriteRetailPrefixProbe::commandOwner68 == 0x68, "SPRITE +68 mismatch");
    static_assert(SpriteRetailPrefixProbe::ptrSprite6C == 0x6C, "SPRITE +6C mismatch");
    static_assert(sizeof(SPRITE) == 0x70, "retail base SPRITE allocation must be exactly 0x70 bytes on x86");
#endif

    class TERRAIN : public SPRITE
    {
    public:
        TERRAIN(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent = nullptr);
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;

    private:
        // Retail initializeExtendedSpriteState extends the 0x70 SPRITE prefix by exactly two
        // DWORDs.  Class 0/1 factory allocation is 0x78.
        int m_sharedPrimaryState = -1;
        int m_sharedSecondaryState = 0;
    };
#if UINTPTR_MAX == 0xFFFFFFFFu
    static_assert(sizeof(TERRAIN) == 0x78, "retail class 0/1 terrain allocation must be 0x78 on x86");
#endif

    class LINKER : public SPRITE
    {
    public:
        LINKER(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent = nullptr);
        ~LINKER() override;

    private:
        // Retail B_LINKER allocation is 0x84.  These fields are consumed by
        // ChangeDirection through fixed +0x70..+0x80 offsets.
        float m_linkOffsetX = 0.0f;
        float m_linkOffsetY = 0.0f;
        float m_linkOffsetZ = 0.0f;
        int m_linkDirection = 0;
        SPRITE* m_linkOwner = nullptr;
    };
#if UINTPTR_MAX == 0xFFFFFFFFu
    static_assert(sizeof(LINKER) == 0x84, "retail LINKER allocation must be 0x84 on x86");
#endif
    class PRIMITIVE : public SPRITE
    {
    public:
        using SPRITE::SPRITE;
        void Tact() override;
        void MoveTact() override;
        void DeletePointerToSprite(SPRITE* sprite) override;
        void DrawDebugOverlay() override;
    };
    class REGION : public SPRITE
    {
    public:
        REGION(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent = nullptr);
        ~REGION() override;
        REGION* regionScalarDeletingDestructor(unsigned char flags) noexcept;
        void destroyRegionState() noexcept;
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;
        void Draw() override;
        void DrawDebugOverlay() override; // retail +0x18 -> drawRegionDebugBounds

        void drawRegionDebugBounds();
        double regionScreenLeft() const noexcept;
        double regionScreenTop() const noexcept;
        double regionScreenRight() const noexcept;
        double regionScreenBottom() const noexcept;
        void drawRegionTilesAndFog();
        int rebuildRegionFogRamp(int start, int end, int color);

        static constexpr std::uint32_t FogAnimatedFlag = 1u << 0;
        static constexpr std::uint32_t FogBlendFlag = 1u << 1;
        static constexpr std::uint32_t FullViewportFlag = 1u << 3;

        std::uint32_t regionFlags() const noexcept { return m_regionFlags; }
        float regionWidth() const noexcept { return m_regionWidth; }
        float regionHeight() const noexcept { return m_regionHeight; }
        VID* sourceMappedVid(int index) const noexcept { return m_sourceVidMap[index]; }
        VID* targetMappedVid(int index) const noexcept { return m_targetVidMap[index]; }

    private:
        int m_fogRampPhase;
        int m_lastFogRampPhase;
        void* m_fogRamp;
        std::uint32_t m_regionFlags;
        int m_fogEnd;
        int m_fogStart;
        std::uint32_t m_fogColor;
        int m_reservedRegionState8C;
        float m_regionWidth;
        float m_regionHeight;
        VID* m_savedRegionVid;
        int m_persistedRegionState;
        VID* m_sourceVidMap[6];
        VID* m_targetVidMap[6];
    };

    VID* resolveRegionMappedVid(VID* sourceVid, float x, float y, float z) noexcept;

    void DeleteSpriteThroughVirtualDeletingDestructor(SPRITE* sprite) noexcept;

    class FRAME : public SPRITE
    {
    public:
        FRAME(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent = nullptr);
        ~FRAME() override;
        FRAME* frameScalarDeletingDestructor(unsigned char flags) noexcept;
        void destroyFrameState() noexcept;
        int dispatchFrameActionOpcode(int opcode, std::intptr_t actionArgument1 = 0, std::intptr_t actionArgument2 = 0, std::intptr_t actionArgument3 = 0);
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;
    };

    class STEXT : public FRAME
    {
    public:
        STEXT(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent = nullptr);
        ~STEXT() override;
        STEXT* textScalarDeletingDestructor(unsigned char flags) noexcept;

        const char* text() const noexcept { return m_text; }
        const char* textClass() const noexcept { return m_textClass; }
        int textLength() const noexcept { return m_textLength; }
        int textFlags() const noexcept { return m_textFlags; }
        void initializeTextState(char* text70, char* class74, int length78, int flags7C) noexcept;
        void assignText(const char* text);
        void assignTextClass(const char* text);
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;
        void Draw() override;
        STRING expandTextScriptExpression(const STRING& expression) const;
        int dispatchTextActionOpcode(int opcode, std::intptr_t actionArgument1 = 0, std::intptr_t actionArgument2 = 0, std::intptr_t actionArgument3 = 0);

    private:
        static char* cloneOwnedText(const char* text);
        static void releaseOwnedText(char*& owner) noexcept;

        char* m_text = nullptr;
        char* m_textClass = nullptr;
        int m_textLength = 0;
        int m_textFlags = 0;
    };
#if UINTPTR_MAX == 0xFFFFFFFFu
    static_assert(sizeof(PRIMITIVE) == 0x70, "retail PRIMITIVE allocation must be 0x70 on x86");
    static_assert(sizeof(FRAME) == 0x70, "retail FRAME allocation must be 0x70 on x86");
    static_assert(sizeof(STEXT) == 0x80, "retail STEXT allocation must be 0x80 on x86");
    static_assert(sizeof(REGION) == 0xD0, "retail REGION allocation must be 0xD0 on x86");
#endif
}
