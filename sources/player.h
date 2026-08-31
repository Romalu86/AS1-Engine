#pragma once

#include "core/types.h"
#include <cstddef>
#include <cstdint>

namespace as1
{
    class MAP;
    class RESOURCE;
    class SPRITE;
    class STRING;
    namespace input { struct InputMessageState; }

    // Canonical raw subobject owners used by the PLAYER path vtables.  Their
    // ECX/this is PLAYER+0x28 or PLAYER+0x31C respectively, not PLAYER itself.
    void* scalarDeletingDestructorPathOwner(void* pathOwner, unsigned char deleteSelfFlag) noexcept;
    int releasePathSprites(void* pathOwner) noexcept;
    void clearPathSpriteReferences(void* pathOwner, SPRITE* sprite) noexcept;
    void* scalarDeletingDestructorPathPendingList(void* pendingOwner, unsigned char deleteSelfFlag) noexcept;

    class PLAYER
    {
    public:
        static constexpr std::size_t RetailObjectSize = 0x32C;
        static DWORD CurrentImageBaseVtable() noexcept;
        static DWORD CurrentImageActiveVtable() noexcept;
        static DWORD CurrentImagePathVtable() noexcept;
        static DWORD CurrentImagePendingVtable() noexcept;

#ifdef _MSC_VER
#pragma warning(suppress : 26495) // Retail initializeActivePlayerState/initializeBasePlayerState initialize only explicit raw slots.
#endif
        PLAYER() noexcept {}
        ~PLAYER() noexcept;

        PLAYER* initializeActivePlayerState(int controlMode, int playerSlot) noexcept;
        int submitPathCoordinate(STRING* className, float targetX, float targetY) noexcept;
        PLAYER* scalarDeletingDestructorActivePlayer(unsigned char deleteSelfFlag) noexcept;
        PLAYER* scalarDeletingDestructorBasePlayer(unsigned char deleteSelfFlag) noexcept;
        std::intptr_t removeActivePlayerSpriteReference(SPRITE* sprite) noexcept;
        std::intptr_t clearSpriteReferenceViaVtable(SPRITE* sprite) noexcept;
        void noOpSpriteCallback(SPRITE* sprite) noexcept;
        void destroyPathFindState() noexcept;
        void destroyBaseSpriteOwnerState() noexcept;
        PLAYER* initializeBasePlayerState(int controlMode, int playerSlot) noexcept;
        void resetPlayerSpriteReferences() noexcept;
        void setFlagmanSprite(SPRITE* sprite) noexcept;
        int saveControlledSpriteReference(RESOURCE* mapResource) noexcept;
        int loadControlledSpriteReference(RESOURCE* mapResource) noexcept;
        STRING* getAuxiliaryUnitName(STRING* out) noexcept;
        int submitPathCoordinateToOwner(STRING* className, float targetX, float targetY) noexcept;
        int pathFindTerrainCellStep() const noexcept;
        bool validatePathCoordinateClass(const STRING* className, int* terrainCellStepOut = nullptr) noexcept;
        int enqueuePathCoordinateSound() noexcept;
        void updatePathOwnerFrame() noexcept;
        void advancePathRouteWindow() noexcept;
        void clearPendingPathCoordinates() noexcept;
        void destroyPendingPathCoordinates() noexcept;
        void clearPlayerPathSpriteReferences(SPRITE* sprite) noexcept;
        void releaseSpriteReferenceBaseOwner(SPRITE* sprite) noexcept;
        std::intptr_t removePlayerSpriteReference(SPRITE* sprite) noexcept;
        int removeEmbeddedSpriteReference(SPRITE* sprite) noexcept;
        void processInput(as1::input::InputMessageState* inputState) noexcept;
        void processInputGlobalListPrepass() noexcept;
        void processInputAttackWeaponPreselect(SPRITE* controlled) noexcept;
        void processInputDigitWeaponSelect(SPRITE* controlled, std::uint32_t lastCode) noexcept;
        void processInputWheelWeaponSelect(SPRITE* controlled, as1::input::InputMessageState* inputState) noexcept;
        void processInputDirectionCommandDispatch(SPRITE* controlled, const as1::input::InputMessageState* inputState, int& outDirection, std::uint32_t& outDeltaMs) noexcept;
        void processInputPostMovementHelper(SPRITE* controlled, std::uint32_t flags) noexcept;
        int controlMode() const noexcept;
        int playerSlot() const noexcept;
        SPRITE* controlledSprite() const noexcept;
        void setControlledSprite(SPRITE* value) noexcept;
        SPRITE* auxiliarySprite() const noexcept;
        void setAuxiliarySprite(SPRITE* value) noexcept;
        DWORD money() const noexcept { return m_state.base.money; }
        DWORD getMoney() const noexcept { return m_state.base.money; }
        DWORD setMoney(DWORD value) noexcept { m_state.base.money = value; return value; }
        DWORD setPathSecondaryFlag(int value) noexcept
        {
            const DWORD result = (m_state.path.vtable & ~2u) | (value != 0 ? 2u : 0u);
            m_state.path.vtable = result;
            return result;
        }
        void processInputDispatchChildTail(SPRITE* controlled, int direction, std::uint32_t deltaMs) noexcept;
        void releaseAuxiliarySpriteAfterInput() noexcept;

    private:
        // Retail physical owner map recovered from initializeBasePlayerState/initializePathOwner and
        // all known 0x429xxx users.  Pointer slots stay DWORD-sized because the
        // shipped runtime is Win32/x86; methods convert them to native pointers
        // only on the production target.
        struct BaseOwnerLayout
        {
            DWORD vtable;
            DWORD money;               // +0x04, starting money reset to 1000 by initializeBasePlayerState/410410
            int controlMode;           // +0x08
            int playerSlot;           // +0x0C
            DWORD controlledSprite;    // +0x10 SPRITE*
            DWORD listVtable;
            int listCount;             // +0x18
            int listCapacity;          // +0x1C
            DWORD listItems;           // +0x20 SPRITE**
            DWORD auxiliarySprite;     // +0x24 SPRITE*
        };

        struct PendingPathOwnerLayout
        {
            DWORD vtable;
            int count;                  // path+0x2F8
            int capacity;               // path+0x2FC
            DWORD entries;              // path+0x300, 16-byte entry array after cookie
        };

        struct PathOwnerLayout
        {
            DWORD vtable;
            int routeCount;             // +0x004
            DWORD updateInterval;       // +0x008
            float baseX;                // +0x00C
            float baseY;                // +0x010
            int terrainVid;             // +0x014
            int secondaryTerrainVid;    // +0x018
            int rowDelta;               // +0x01C
            DWORD lastUpdate;           // +0x020
            DWORD primarySprites[45];   // +0x024..+0x0D7
            DWORD secondarySprites[45]; // +0x0D8..+0x18B
            float targetX[45];          // +0x18C..+0x23F
            float targetY[45];          // +0x240..+0x2F3
            PendingPathOwnerLayout pending; // +0x2F4..+0x303

            // Retail constructor owner initializePathOwner. ECX is exactly PLAYER+0x28;
            // six 32-bit stack arguments are callee-cleaned (retn 0x18).
            PathOwnerLayout* initializePathOwner(int terrainVid, int secondaryTerrainVid,
                                         DWORD baseXBits, DWORD baseYBits,
                                         int routeCount, DWORD updateInterval) noexcept;

            void clearPathSpriteReferences(SPRITE* sprite) noexcept;
        };

        struct PlayerLayout
        {
            BaseOwnerLayout base;        // +0x000..+0x027
            PathOwnerLayout path;        // +0x028..+0x32B
        };

        static_assert(sizeof(BaseOwnerLayout) == 0x28, "PLAYER base owner retail size must be 0x28");
        static_assert(offsetof(BaseOwnerLayout, money) == 0x04, "PLAYER money must stay at +0x04");
        static_assert(offsetof(BaseOwnerLayout, controlMode) == 0x08, "PLAYER controlMode must stay at +0x08");
        static_assert(offsetof(BaseOwnerLayout, controlledSprite) == 0x10, "PLAYER controlled sprite must stay at +0x10");
        static_assert(offsetof(BaseOwnerLayout, listVtable) == 0x14, "PLAYER embedded list vptr must stay at +0x14");
        static_assert(offsetof(BaseOwnerLayout, listItems) == 0x20, "PLAYER embedded list storage must stay at +0x20");
        static_assert(offsetof(BaseOwnerLayout, auxiliarySprite) == 0x24, "PLAYER base owner tail must stay at +0x24");
        static_assert(sizeof(PendingPathOwnerLayout) == 0x10, "PLAYER pending path owner retail size must be 0x10");
        static_assert(offsetof(PathOwnerLayout, routeCount) == 0x004, "PLAYER PATH route count must stay at +0x04");
        static_assert(offsetof(PathOwnerLayout, terrainVid) == 0x014, "PLAYER PATH terrain VID must stay at +0x14");
        static_assert(offsetof(PathOwnerLayout, lastUpdate) == 0x020, "PLAYER PATH update timestamp must stay at +0x20");
        static_assert(offsetof(PathOwnerLayout, primarySprites) == 0x024, "PLAYER PATH primary sprites must stay at +0x24");
        static_assert(offsetof(PathOwnerLayout, secondarySprites) == 0x0D8, "PLAYER PATH secondary sprites must stay at +0xD8");
        static_assert(offsetof(PathOwnerLayout, targetX) == 0x18C, "PLAYER PATH X targets must stay at +0x18C");
        static_assert(offsetof(PathOwnerLayout, targetY) == 0x240, "PLAYER PATH Y targets must stay at +0x240");
        static_assert(offsetof(PathOwnerLayout, pending) == 0x2F4, "PLAYER PATH pending owner must stay at path+0x2F4");
        static_assert(offsetof(PathOwnerLayout, pending) + offsetof(PendingPathOwnerLayout, entries) == 0x300, "PLAYER PATH pending entries must stay at path+0x300");
        static_assert(sizeof(PathOwnerLayout) == 0x304, "PLAYER path owner retail size must be 0x304");
        static_assert(offsetof(PlayerLayout, path) == 0x28, "PLAYER PATH owner must start at +0x28");
        static_assert(sizeof(PlayerLayout) == RetailObjectSize, "PLAYER retail owner map must be 0x32C");

        friend int releasePathSprites(void* pathOwner) noexcept;
        friend void clearPathSpriteReferences(void* pathOwner, SPRITE* sprite) noexcept;
        friend void* scalarDeletingDestructorPathOwner(void* pathOwner, unsigned char deleteSelfFlag) noexcept;
        friend void* scalarDeletingDestructorPathPendingList(void* pendingOwner, unsigned char deleteSelfFlag) noexcept;

        PlayerLayout m_state;
    };

    static_assert(sizeof(PLAYER) == PLAYER::RetailObjectSize, "PLAYER retail size must stay 0x32C");
}
