#pragma once
#include "../core/types.h"
#include "../core/as_string.h"
#include <array>
#include <bitset>
#include <cstring>
#include <string>

namespace as1
{
    extern int g_vidAllocatedBytes;

    class RESOURCE;
    class SPRITE;
    class MAP;
    class BASE_TEXTURE;
    class VID_TEXCOOR;
    struct WEAPON;

    constexpr DWORD VID_TYPE_TEXTURE          = 0x00000001;
    constexpr DWORD VID_TYPE_ALPHA            = 0x00000002;
    constexpr DWORD VID_TYPE_ZBUFFER          = 0x00000004;
    constexpr DWORD VID_TYPE_PALETTE          = 0x00000008;
    constexpr DWORD VID_TYPE_NEWVERSION       = 0x00000010;
    constexpr DWORD VID_TYPE_HARDWARE         = 0x00000020;
    constexpr DWORD VID_TYPE_PSEUDO3D         = 0x00000040;
    constexpr DWORD VID_TYPE_LIGHT            = 0x00000080;
    constexpr DWORD VID_TYPE_COMPRESS         = 0x00000100;
    constexpr DWORD VID_TYPE_LINKXYZ          = 0x00000200;
    constexpr DWORD VID_TYPE_3D               = 0x00000400;
    constexpr DWORD VID_TYPE_SHADOW           = 0x00000800;
    constexpr DWORD VID_TYPE_NEW_ZBUFFER      = 0x00001000;
    constexpr DWORD VID_TYPE_NORMALS          = 0x00002000;
    constexpr DWORD VID_TYPE_FONT             = 0x00004000;
    constexpr DWORD VID_TYPE_MATERIAL         = 0x00008000;

    class VID
    {
    public:
        enum class VidType
        {
            type_VID                  = 0,
            type_VID_BASE_SOFTWARE    = 1,
            type_VID_SOFTWARE         = 2,
            type_VID_SOFTWARE_PNG     = 3,
            type_VID_FONT             = 4,
            type_VID_SURFACE          = 5,
            type_VID_SURFACE_SOFTWARE = 6,
            type_VID_HARD_SOFTWARE    = 7,
            type_VID_HARDWARE_Z       = 8,
            type_VID_LIGHT            = 9,
            type_VID_SOFTWARE16       = 10,
            type_VID_HARDWARE         = 11
        };

        virtual VID* CreateMirror();                                                   // +0x00
        virtual ~VID();                                                                // +0x04 scalar deleting dtor
        virtual void AddVidToVid(SPRITE* sprite);                                      // +0x08
        virtual void Draw(const SPRITE* sprite);                                       // +0x0C
        virtual int DrawShadow(const SPRITE* sprite) const;                           // +0x10
        virtual void DrawToVid(SPRITE* sprite, void* texSize, BASE_TEXTURE* texture, BASE_TEXTURE* zTexture); // +0x14
        virtual void Load(RESOURCE* resource);                                         // +0x18
        virtual void SetGammaRaw(const GammaRawPair& rawGamma, unsigned n_gamma);       // +0x1C
        virtual void SetGridZ(const SPRITE* sprite);                                   // +0x20
        virtual int HaveShadow() const;                                                // +0x24
        virtual void SetLayer();                                                       // +0x28


        enum { NO_ANIMATION = 17 };
        enum ScriptFunction
        {
            DESTROY     = NO_ANIMATION,
            DAMAGE      = NO_ANIMATION + 1,
            COLLISION   = NO_ANIMATION + 2,
            DETECT      = NO_ANIMATION + 3,
            LASTINGEFF  = NO_ANIMATION + 4,
            TACT        = NO_ANIMATION + 5,
            // AS1 stores exactly 18 DWORD callback slots at +0x3B8..+0x3FC:
            // 17 animation slots plus DESTROY.  The later symbolic names above
            // are parser tokens, not additional storage inside the 2003 VID.
            _funcs_count= DESTROY + 1
        };

        VID();

        void loadBasicParameters(RESOURCE* globalRes);
        void LoadParameters(RESOURCE* res);
        std::intptr_t logVidResourceError(int errorCode, const char* detailText, int detailValue) const;
        void ReportResourceError(int errorCode, const char* detailText, int detailValue) const;
        void SetChildAndLink();
        void CalcImportantForSync();
        // vtable +0x28 declared in the retail prefix above.
        int RealDirection(const ANGLE& dir) const;
        ANGLE SteppedDirection(const ANGLE& dir) const;
        STRING GetNumberName() const;
        void SetGamma(const Gamma& gamma, unsigned n_gamma = 0);

        void SetGammaToPalette(BYTE* palette, const Gamma& gamma);
        void SetGammaToPaletteRaw(BYTE* palette, const GammaRawPair& rawGamma);
        // vtable +0x20 declared in the retail prefix above.
        void ResetGridZ(const SPRITE* sprite);
        void calcBuildSizeToGridZ();


        struct PaletteEntry
        {
            BYTE b = 0;
            BYTE g = 0;
            BYTE r = 0;
            BYTE a = 0;
        };

        struct DataRun
        {
            int row = 0;
            int x = 0;
            int skip = 0;
            int count = 0;
            std::vector<WORD> zWords;
            std::vector<WORD> colorWords;
            std::vector<BYTE> paletteIndexes;
        };

        struct DataFrame
        {
            int frameIndex = 0;
            std::uint32_t declaredRawSize = 0;
            WORD marker = 0;
            int top = 0;
            int rowCount = 0;
            int runCount = 0;
            int visiblePixels = 0;
            bool malformed = false;
            bool skipped = false;
            std::string error;
            std::vector<DataRun> runs;
        };

        struct SurfacePage
        {
            int surfaceIndex = 0;
            WORD width = 0;
            WORD height = 0;
            std::uint32_t rawBytes = 0;
            bool compressed = false;
            std::vector<WORD> pixels16;
            std::string error;
        };

        struct SurfaceRecord
        {
            int recordIndex = 0;
            DWORD marker = 0;
            int surface = 0;
            int srcX = 0;
            int srcY = 0;
            int width = 0;
            int height = 0;
            int dstX = 0;
            int dstY = 0;
            int nextRecord = 0;
        };

        struct LightFrame
        {
            int frameIndex = 0;
            BYTE b = 0;
            BYTE g = 0;
            BYTE r = 0;
            BYTE a = 0;
        };

        enum class FrameSurfaceKind
        {
            SoftwareData,
            HardwareSurfRecord,
            LightColor
        };

        enum class FramePixelFormat
        {
            BGRA8888,
            R5G6B5,
            A4R4G4B4,
            RGB444,
            LightBGRA
        };

        struct FrameSurface
        {
            int frameIndex = 0;
            FrameSurfaceKind kind = FrameSurfaceKind::SoftwareData;
            FramePixelFormat pixelFormat = FramePixelFormat::BGRA8888;
            int width = 0;
            int height = 0;
            int originX = 0;
            int originY = 0;
            int visiblePixels = 0;
            int sourceSurface = -1;
            int sourceRecord = -1;
            int nextRecord = -1;
            std::uint64_t contentHash = 0;
            bool malformed = false;
            bool skipped = false;
            std::string error;
            std::vector<DWORD> bgra32;

            std::vector<BYTE> paletteIndexes;
            std::vector<WORD> pixels16;
            std::vector<WORD> zWords;
        };

        // Retail draw/shadow/HaveShadow slots are declared in the vtable prefix.
        // The following helpers are source diagnostics/conveniences only and must
        // never create additional entries in the native VID vtable.
        bool transparencyCheck() const;
        int PaletteSize() const;
        bool isLoaded() const;
        bool unloadable() const;

        void buildFrameSurfaces();
        const std::vector<FrameSurface>& frameSurfaces() const;
        bool hasFrameSurfaces() const;
        const FrameSurface* FrameSurfaceByRecord(int recordIndex) const;
        void collectHardwareSurfaceChain(const FrameSurface* first, std::vector<const FrameSurface*>& out) const;

        // Host-only decode/inspection products live in an external sidecar.
        // They are deliberately not members of the retail VID object and
        // therefore cannot change the Win32/x86 VID physical layout.
        const std::vector<PaletteEntry>& palette() const;
        const std::vector<DataFrame>& dataFrames() const;
        const std::vector<SurfacePage>& surfacePages() const;
        const std::vector<SurfaceRecord>& surfaceRecords() const;
        const std::vector<LightFrame>& lightFrames() const;
        const std::vector<std::string>& decodeWarnings() const;
        const std::vector<DWORD>& gammaPaletteBuffer() const;
        bool hasDecodedPalette() const;
        bool hasDecodedDataFrames() const;
        bool hasDecodedSurfacePages() const;
        bool hasDecodedSurfaceRecords() const;
        bool hasDecodedLightFrames() const;
        bool hasCompressedSurf() const;
        size_t compressedSurfBytes() const;

        bool hasValidFrameCount() const { return noCadr > 0 && noCadr < 32000; }
        bool hasPalette() const { return (type & VID_TYPE_PALETTE) != 0; }
        bool hasAlpha() const { return (type & VID_TYPE_ALPHA) != 0; }
        bool hasZBuffer() const { return (type & VID_TYPE_ZBUFFER) != 0 || (type & VID_TYPE_NEW_ZBUFFER) != 0; }
        bool isLight() const { return (type & VID_TYPE_LIGHT) != 0; }
        bool isHardware() const { return (type & VID_TYPE_HARDWARE) != 0; }
        bool isCompressed() const { return (type & VID_TYPE_COMPRESS) != 0; }

        WORD formatFlags() const { return type; }

        const GammaRawPair& armyGammaOverride(unsigned index) const noexcept { return altGammaRaw[index & 3u]; }
        WORD defaultFrameSpeed() const { return frameSpeedDefault; }
        WORD totalFrames() const { return static_cast<WORD>(noCadr); }
        WORD vidWidth() const { return static_cast<WORD>(vidSizeX); }
        WORD vidHeight() const { return static_cast<WORD>(vidSizeY); }
        void setVidWidth(short value) noexcept { vidSizeX = value; }
        void setVidHeight(short value) noexcept { vidSizeY = value; }
        int animationBaseFrameFor(int animation) const noexcept
        {
            return (animation >= 0 && animation < NO_ANIMATION) ? static_cast<int>(animationBaseFrame[animation]) : 0;
        }
        int animationFrameCountFor(int animation) const noexcept
        {
            return (animation >= 0 && animation < NO_ANIMATION) ? static_cast<int>(animationFrameCount[animation]) : 0;
        }

        bool hasAnimation12Content() const noexcept
        {
            return noAnimCadr[12] != 0 || sfx[12] != 0 || nChildVid[12] != 0;
        }

        DWORD spriteTypeId() const { return spriteType; }
        DWORD properties() const noexcept { return property; }
        void setProperties(DWORD value) noexcept { property = value; }
        DWORD movementMask() const noexcept { return moveMask; }
        float sizeX() const noexcept { return sizeXYZ.x; }
        float sizeY() const noexcept { return sizeXYZ.y; }
        float sizeZ() const noexcept { return sizeXYZ.z; }
        int maximumHp() const noexcept { return maxHp; }

        DWORD spriteClassId() const { return spriteClass; }

        float maxSpeedValue() const noexcept { return maxSpeed; }
        float maximumZSpeed() const noexcept { return maxZSpeed; }
        float accelerationValue() const noexcept { return acceleration; }
        float slowValue() const noexcept { return slow; }
        float rotationSpeedValue() const noexcept { return rotationSpeed; }
        float deathRangeValue() const noexcept { return deathRange; }
        std::int32_t deathDamageMinimumRawBits() const noexcept
        {
            std::int32_t value = 0;
            static_assert(sizeof(value) == sizeof(deathDamageMin), "VID+0x48 raw DWORD size mismatch");
            std::memcpy(&value, &deathDamageMin, sizeof(value));
            return value;
        }
        int linkedNvid() const noexcept { return nLinkVid; }
        float moveUpZ() const noexcept { return forMoveUpZ; }
        int directionCount() const noexcept { return noDir; }
        int directionQuantizationOffset() const noexcept { return directionQuantizationOffsetValue; }
        void setDirectionQuantizationOffset(int value) noexcept { directionQuantizationOffsetValue = value; }
        float halfSizeX() const noexcept { return halfSizeXY.x; }
        float halfSizeY() const noexcept { return halfSizeXY.y; }
        float calculateMoveUpZ(float verticalDelta, float projectedXYLength) const noexcept;
        int renderLayer() const { return layer; }
        void setRenderLayer(int value) noexcept { layer = value; }

        static constexpr int ScriptFunctionSlotCount = 18;
        static constexpr int BirthScriptFunctionIndex = 14;
        static constexpr int DestroyScriptFunctionIndex = 17;

        int scriptFunctionAt(int index) const
        {
            return (index >= 0 && index < _funcs_count) ? scriptFunction[index] : -1;
        }
        void setScriptFunctionAt(int index, int functionIndex)
        {
            if (index >= 0 && index < _funcs_count)
                scriptFunction[index] = functionIndex;
        }
        int birthScriptFunction() const { return scriptFunctionAt(BirthScriptFunctionIndex); }
        int destroyScriptFunction() const { return scriptFunctionAt(DestroyScriptFunctionIndex); }

        static constexpr int DamageScriptFunctionIndex = 7;
        int damageScriptFunction() const { return scriptFunctionAt(DamageScriptFunctionIndex); }

        VID* nextMirrorVid() const { return nextMirror; }
        VID* exchangedVidRef() const { return exchangedVid; }
        bool isMirrorChainOwner() const { return nextMirrorVid() == this; }

        bool movementTactEnabled() const noexcept
        {
            return movementTactEnabledValue != 0;
        }
        void setMovementTactEnabled(int value) noexcept { movementTactEnabledValue = value; }

        WEAPON* weaponRecord() const { return weapon; }
        void setWeaponRecord(WEAPON* value) { weapon = value; }
        int activeWeaponAmmoCapacity() const noexcept;

        STRING& scriptName() { return name; }
        const STRING& scriptName() const { return name; }
        STRING& sourceVidPath() { return vidName; }
        const STRING& sourceVidPath() const { return vidName; }

        static constexpr int SpriteCounterCount = 4;

        DWORD spriteCountForArmy(int index) const;
        DWORD spriteCountAcrossArmies() const;
        void incrementSpriteCountForArmy(int index);
        void decrementSpriteCountForArmy(int index);

        int killedUnitCounterValue(int bucket) const noexcept;
        void setKilledUnitCountForArmy(int bucket, int value) noexcept;
        void incrementKilledUnitCountForArmy(int bucket) noexcept;
        int recolorUnitCounterValue(int bucket) const noexcept;
        void setRecolorUnitCountForArmy(int bucket, int value) noexcept;
        int animationFrameDuration(int bucket) const noexcept;
        void setAnimationFrameDuration(int bucket, int value) noexcept;
        int deathChildNvid() const noexcept { return nChildVid[15]; }
        void setDeathChildNvid(int value) noexcept { nChildVid[15] = value; }
        VID* woundChildVid() const noexcept { return childVid[13]; }
        void setWoundChildVid(VID* value) noexcept { childVid[13] = value; }
        int woundChildNvid() const noexcept { return childVid[13] ? childVid[13]->nvid() : 0; }
        int hasDeath2ChildVid() const noexcept { return childVid[16] != nullptr; }

        int sfxForAnimation(int animationSlot) const noexcept
        {
            return (animationSlot >= 0 && animationSlot < NO_ANIMATION) ? sfx[animationSlot] : 0;
        }
        int constructorSfxId() const noexcept { return sfxForAnimation(14); }

        int damageSfxId() const noexcept { return sfxForAnimation(7); }

        int calculateLinkedContribution() const noexcept;
        int getWeaponValue24Scaled() const noexcept;
        int setLinkedPropertyBit400(int enabled) noexcept;
        VID* fightChildVid() const noexcept { return childVid[8]; }
        VID* birthChildVid() const noexcept { return childVid[14]; }
        VID* deathChildVid() const noexcept { return childVid[15]; }

        int hasHitChildVid() const noexcept { return childVid[7] != nullptr; }

        int fightNoChildValue() const noexcept { return noChild[8]; }
        int birthNoChildValue() const noexcept { return noChild[14]; }
        int deathNoChildValue() const noexcept { return noChild[15]; }
        void setDeathDamageMinimumRawBits(int value) noexcept { std::memcpy(&deathDamageMin, &value, sizeof(value)); }
        void setFightChildVid(VID* value) noexcept { childVid[8] = value; }
        void setBirthChildVid(VID* value) noexcept { childVid[14] = value; }
        void setDeathChildVid(VID* value) noexcept { childVid[15] = value; }
        void setFightNoChildValue(int value) noexcept { noChild[8] = value; }
        void setBirthNoChildValue(int value) noexcept { noChild[14] = value; }
        void setDeathNoChildValue(int value) noexcept { noChild[15] = value; }

        static constexpr int UnitLimitCount = 5;
        int unitLimit(int index) const noexcept
        {
            return unitLimits[static_cast<std::size_t>(index) % unitLimits.size()];
        }
        void setUnitLimit(int index, int value) noexcept
        {
            if (index == 255)
                unitLimits[0] = value;
            else
                unitLimits[static_cast<std::size_t>(index + 1)] = value;
        }

        std::uint32_t lastSpriteCountChangeTimestamp() const noexcept { return lastSpriteCountChangeTimestampMs; }
        void setLastSpriteCountChangeTimestamp(std::uint32_t value) noexcept { lastSpriteCountChangeTimestampMs = value; }
        void initializeRuntimeCountersAndCallbacks() noexcept;
        int setBucketFramePercent(int bucketIndex, int percentValue) noexcept;
        int setBucketFrameTime(int bucketIndex, int frameTimeValue) noexcept;

        const VECTOR& linkOffset() const noexcept { return linkXYZ; }
        VID* linkedVid() const noexcept { return linkVid; }
        void setLinkedVid(VID* value) noexcept { linkVid = value; }
        bool isNotCreateAsChild() const noexcept { return notCreateAsChildFlag != 0; }
        std::uint32_t weaponCount() const noexcept { return static_cast<std::uint32_t>(nWeapon); }
        void setWeaponCount(std::uint32_t value) noexcept { nWeapon = static_cast<int>(value); }
        std::uint32_t hasWeaponChildDescriptor() const noexcept { return childVid[8] != nullptr ? 1u : 0u; }
        void clearWeaponChildDescriptorIfZero(std::uint32_t value) noexcept { if (!value) childVid[8] = nullptr; }

        int CanFight() const noexcept;
        int frameTimeForBucket(int bucket) const noexcept;
        int spriteCountForBucket(int bucket) const noexcept;
        int totalSpriteCount() const noexcept;
        int killedUnitCountForArmy(int bucket) const noexcept;
        int totalKilledUnitCount() const noexcept;
        int recolorUnitCountForArmy(int bucket) const noexcept;
        int totalRecolorUnitCount() const noexcept;
        int notCreateAsChild() const noexcept;
        int setNotCreateAsChild(int value) noexcept;
        int PropBirthAsSmoke() const noexcept;
        int hasPropertyBit400() const noexcept;
        int noChildValueForDataCode(int type) const noexcept;
        void setNoChildValueForDataCode(int type, int value) noexcept;
        int childNvidForDataCode(int type) const noexcept;
        void setChildNvidForDataCode(int type, int value) noexcept;
        VID* childVidForDataCode(int type) const noexcept;
        void setChildVidForDataCode(int type, VID* value) noexcept;
        int actionAuxStateRequired() const noexcept { return actionAuxStateRequiredValue; }
        void setActionAuxStateRequired(int value) noexcept { actionAuxStateRequiredValue = value; }
        // Retail WEAPON record fields whose semantics are explicitly named by
        // Maps/EXPORT.LGC (VID_* constants) or by repeated AS1 owner usage.
        enum class WeaponFieldOffset : int
        {
            TypeMask = 0x00,
            Flags = 0x04,
            Radius = 0x08,
            DetectRange = 0x14,   // VID_DETECTRANGE
            BattleRange = 0x18,   // VID_BATTLERANGE
            Aim = 0x1C,           // VID_WEAPONAIM
            BuildTime = 0x24,     // VID_BUILDTIME
            Lifetime = 0x28,      // VID_LIFETIME
            AmmoCapacity = 0x2C,  // VID_AMMO
        };
        int weaponIntAt(int offset) const noexcept;
        float weaponFloatAt(int offset) const noexcept;
        void setWeaponIntAt(int offset, int value) noexcept;
        void setWeaponFloatAt(int offset, float value) noexcept;
        int weaponTypeMask() const noexcept { return weaponIntAt(static_cast<int>(WeaponFieldOffset::TypeMask)); }
        int weaponFlags() const noexcept { return weaponIntAt(static_cast<int>(WeaponFieldOffset::Flags)); }
        float weaponRadius() const noexcept { return weaponFloatAt(static_cast<int>(WeaponFieldOffset::Radius)); }
        float weaponDetectRange() const noexcept { return weaponFloatAt(static_cast<int>(WeaponFieldOffset::DetectRange)); }
        float weaponBattleRange() const noexcept { return weaponFloatAt(static_cast<int>(WeaponFieldOffset::BattleRange)); }
        float weaponAim() const noexcept { return weaponFloatAt(static_cast<int>(WeaponFieldOffset::Aim)); }
        int weaponBuildTime() const noexcept { return weaponIntAt(static_cast<int>(WeaponFieldOffset::BuildTime)); }
        int weaponLifetime() const noexcept { return weaponIntAt(static_cast<int>(WeaponFieldOffset::Lifetime)); }
        int weaponRecordAmmoCapacity() const noexcept { return weaponIntAt(static_cast<int>(WeaponFieldOffset::AmmoCapacity)); }
        void setWeaponAim(float value) noexcept { setWeaponFloatAt(static_cast<int>(WeaponFieldOffset::Aim), value); }
        void setWeaponBuildTime(int value) noexcept { setWeaponIntAt(static_cast<int>(WeaponFieldOffset::BuildTime), value); }
        void setWeaponLifetime(int value) noexcept { setWeaponIntAt(static_cast<int>(WeaponFieldOffset::Lifetime), value); }
        void setWeaponRecordAmmoCapacity(int value) noexcept { setWeaponIntAt(static_cast<int>(WeaponFieldOffset::AmmoCapacity), value); }
        void setWeaponDetectRange(float value) noexcept { setWeaponFloatAt(static_cast<int>(WeaponFieldOffset::DetectRange), value); }
        int nvid() const noexcept { return nVid; }
        void setMaxSpeedValue(float value) noexcept { maxSpeed = value; }
        void setMovementMask(DWORD value) noexcept { moveMask = value; }
        void setDefaultFrameSpeed(WORD value) noexcept { frameSpeedDefault = value; }

#if defined(_MSC_VER) && defined(_M_IX86)

        int     nVid;                                      // +0x004
        STRING  name;                                      // +0x008
        DWORD   spriteType;                                // +0x00C
        DWORD   spriteClass;                               // +0x010
        DWORD   property;                                  // +0x014
        DWORD   moveMask;                                  // +0x018
        VECTOR  sizeXYZ;                                   // +0x01C
        int     maxHp;                                     // +0x028
        float   maxSpeed;                                  // +0x02C
        float   maxZSpeed;                                 // +0x030
        float   acceleration;                              // +0x034
        float   slow;                                      // +0x038
        float   rotationSpeed;                             // +0x03C
        int     nWeapon;                                   // +0x040
        float   deathRange;                                // +0x044
        float   deathDamageMin;                            // +0x048 (raw DWORD consumer)
        VECTOR  linkXYZ;                                   // +0x04C
        int     nLinkVid;                                  // +0x058
        VID*    linkVid;                                   // +0x05C
        float   forMoveUpZ;                                // +0x060
        int     noDir;                                     // +0x064
        int     noAnimCadr[NO_ANIMATION];                  // +0x068
        int     sfx[NO_ANIMATION];                         // +0x0AC
        float   childX[NO_ANIMATION];                      // +0x0F0
        float   childY[NO_ANIMATION];                      // +0x134
        float   childZ[NO_ANIMATION];                      // +0x178
        int     nChildVid[NO_ANIMATION];                   // +0x1BC
        VID*    childVid[NO_ANIMATION];                    // +0x200
        int     noChild[NO_ANIMATION];                     // +0x244
        GammaRawPair gammaRaw;                             // +0x288
        VECTOR  scaleXYZ;                                  // +0x290
        STRING  vidName;                                   // +0x29C
        WORD    type;                                      // +0x2A0
        WORD    frameSpeedDefault;                         // +0x2A2
        short   noCadr;                                    // +0x2A4
        short   vidSizeX;                                  // +0x2A6
        short   vidSizeY;                                  // +0x2A8
        WORD    reservedHeaderPadding;                                 // +0x2AA unknown/pad
        int     animationBaseFrame[NO_ANIMATION];               // +0x2AC
        int     animationFrameCount[NO_ANIMATION];              // +0x2F0
        VECTOR2 halfSizeXY;                                // +0x334
        int     layer;                                     // +0x33C
        int     directionQuantizationOffsetValue;              // +0x340
        std::array<int, UnitLimitCount> unitLimits; // +0x344
        DWORD   spriteCountsByArmy[SpriteCounterCount]; // +0x358
        int     killedUnitCounters[4];                // +0x368
        int     recolorUnitCounters[4];                // +0x378
        int     animationFrameDurations[4];              // +0x388
        GammaRawPair altGammaRaw[4];                       // +0x398
        int     scriptFunction[_funcs_count];              // +0x3B8
        std::uint32_t lastSpriteCountChangeTimestampMs;            // +0x400
        WEAPON* weapon;                                    // +0x404
        VID*    nextMirror;                                // +0x408
        VID*    exchangedVid;                              // +0x40C
        int     movementTactEnabledValue;                   // +0x410
        int     actionAuxStateRequiredValue;                  // +0x414
        int     notCreateAsChildFlag;                       // +0x418
#else
        STRING  vidName;
        STRING  name;
        int actionAuxStateRequiredValue = 0;
        VID*    linkVid = nullptr;
        VECTOR* cadrLinkXYZ = nullptr;
        WEAPON* weapon = nullptr;
        VID*    nextMirror = nullptr;
        VID*    exchangedVid = nullptr;
        int     movementTactEnabledValue = 0;

        int     nVid = -1;
        DWORD   spriteType = 0;
        DWORD   spriteClass = 0;
        DWORD   property = 0;
        DWORD   moveMask = 0;
        VECTOR  sizeXYZ;
        VECTOR  scaleXYZ{1.0f, 1.0f, 1.0f};
        Gamma   gamma;
        GammaRawPair gammaRaw;
        int     maxHp = 0;
        int     maxArmorHp = 0;
        float   maxSpeed = 0.0f;
        float   randomSpeed = 0.0f;
        float   maxZSpeed = 0.0f;
        float   randomZSpeed = 0.0f;
        float   acceleration = 0.0f;
        float   slow = 0.0f;
        float   rotationSpeed = 0.0f;
        int     nWeapon = 0;
        float   deathRange = 0.0f;
        float   deathDamageMin = 0.0f;
        float   deathDamageMax = 0.0f;
        float   armorDamageMin = 0.0f;
        float   deathPush = 0.0f;
        VECTOR2 vidSizeXY;
        VECTOR  linkXYZ;
        int     nLinkVid = 0;
        float   topZ = 0.0f;
        float   forMoveUpZ = 0.0f;
        float   forMoveDownZ = 0.0f;
        DWORD   lifeTime = 0;
        DWORD   ext1Property = 0;
        DWORD   ext2Property = 0;

        int     noAnimCadr[NO_ANIMATION] = {};
        int     animationBaseFrame[NO_ANIMATION] = {};
        int     animationFrameCount[NO_ANIMATION] = {};
        int     nChildVid[NO_ANIMATION] = {};
        int     sfx[NO_ANIMATION] = {};
        int     frameSpeed[NO_ANIMATION] = {};
        float   childX[NO_ANIMATION] = {};
        float   childY[NO_ANIMATION] = {};
        float   childZ[NO_ANIMATION] = {};
        int     noChild[NO_ANIMATION] = {};
        VID*    childVid[NO_ANIMATION] = {};

        VECTOR2 halfSizeXY;
        VECTOR  realSizeXYZ;

        Gamma   altGamma[4];
        GammaRawPair altGammaRaw[4];
        int     scriptFunction[_funcs_count] = {};
        float   minXGridZ = 0.0f;
        float   maxXGridZ = 0.0f;
        float   minYGridZ = 0.0f;
        float   maxYGridZ = 0.0f;
        int     noGridZ = 0;
        VECTOR* gridZ = nullptr;
        int*    gridCadrShift = nullptr;
        int     maxHpForDeath2 = 0;
        float   moveEndDistance = 0.0f;

        WORD    type = 0;
        WORD    frameSpeedDefault = 0;
        int     noDir = 0;
        short   noCadr = 0;
        int     incDir = 0;
        short   vidSizeX = 0;
        short   vidSizeY = 0;
        int     layer = 0;
        int     directionQuantizationOffsetValue = 0;
        std::array<int, UnitLimitCount> unitLimits{};
        DWORD   spriteCountsByArmy[SpriteCounterCount] = {};
        int     killedUnitCounters[4] = {};
        int     recolorUnitCounters[4] = {};
        int     animationFrameDurations[4] = {};
        int     notCreateAsChildFlag = 0;
        std::uint32_t lastSpriteCountChangeTimestampMs = 0;
#endif


        Gamma& hostGammaMirrorStorage();
        const Gamma& hostGammaMirrorStorage() const;
        VECTOR2& hostVidSizeXYStorage();
        const VECTOR2& hostVidSizeXYStorage() const;
        float& hostTopZStorage();
        float hostTopZStorage() const;
        int& hostFrameSpeedStorage(int animation);
        int hostFrameSpeedStorage(int animation) const;
        VECTOR& hostRealSizeXYZStorage();


    protected:

        void clearDecodedVidData();
        void decodeDataSection(RESOURCE* globalRes);
        void decodeSurfaceSection(RESOURCE* globalRes);

        // Mutable access to the host-only sidecar.  These are ordinary methods,
        // not virtual slots and not physical VID fields.
        std::vector<PaletteEntry>& hostPaletteStorage();
        std::vector<DataFrame>& hostDataFramesStorage();
        std::vector<SurfacePage>& hostSurfacePagesStorage();
        std::vector<SurfaceRecord>& hostSurfaceRecordsStorage();
        std::vector<LightFrame>& hostLightFramesStorage();
        std::vector<FrameSurface>& hostFrameSurfacesStorage();
        std::vector<std::string>& hostDecodeWarningsStorage();
        std::vector<DWORD>& hostGammaPaletteBufferStorage();
        bool& hostCompressedSurfPresentStorage();
        size_t& hostCompressedSurfBytesStorage();

    private:
        void unlinkMirrorRing();
        void decodePaletteSection(RESOURCE* globalRes);
        void buildSoftwareFrameSurface(const DataFrame& frame);
        void buildHardwareFrameSurface(const SurfaceRecord& record);
        void buildLightFrameSurface(const LightFrame& frame);
        void finalizeFrameSurface(FrameSurface& surface);
        DWORD paletteIndexToBGRA(BYTE index) const;
        DWORD rgb565ToBGRA(WORD value) const;
        DWORD a4r4g4b4ToBGRA(WORD value) const;
        DWORD rgb444ToBGRA(WORD value) const;
        DWORD rgb444AlphaToBGRA(WORD value) const;
        bool dataPayloadHasZWords() const;
        bool dataPayloadUsesPaletteIndexes() const;
        bool dataPayloadUsesRgb565Words() const;
        bool dataPayloadUsesRgb444ZWords() const;
        bool dataPayloadIsLightColorTable() const;
        bool dataPayloadIsSurfaceRecordTable() const;
        void addDecodeWarning(const std::string& warning);

    };

#if defined(_MSC_VER) && defined(_M_IX86)
    struct VidRetailLayoutProbe
    {
        static constexpr std::size_t nVid = offsetof(VID, nVid);
        static constexpr std::size_t name = offsetof(VID, name);
        static constexpr std::size_t spriteType = offsetof(VID, spriteType);
        static constexpr std::size_t spriteClass = offsetof(VID, spriteClass);
        static constexpr std::size_t property = offsetof(VID, property);
        static constexpr std::size_t moveMask = offsetof(VID, moveMask);
        static constexpr std::size_t sizeXYZ = offsetof(VID, sizeXYZ);
        static constexpr std::size_t maxHp = offsetof(VID, maxHp);
        static constexpr std::size_t nWeapon = offsetof(VID, nWeapon);
        static constexpr std::size_t linkXYZ = offsetof(VID, linkXYZ);
        static constexpr std::size_t nLinkVid = offsetof(VID, nLinkVid);
        static constexpr std::size_t linkVid = offsetof(VID, linkVid);
        static constexpr std::size_t noAnimCadr = offsetof(VID, noAnimCadr);
        static constexpr std::size_t sfx = offsetof(VID, sfx);
        static constexpr std::size_t childX = offsetof(VID, childX);
        static constexpr std::size_t childY = offsetof(VID, childY);
        static constexpr std::size_t childZ = offsetof(VID, childZ);
        static constexpr std::size_t nChildVid = offsetof(VID, nChildVid);
        static constexpr std::size_t childVid = offsetof(VID, childVid);
        static constexpr std::size_t noChild = offsetof(VID, noChild);
        static constexpr std::size_t gammaRaw = offsetof(VID, gammaRaw);
        static constexpr std::size_t scaleXYZ = offsetof(VID, scaleXYZ);
        static constexpr std::size_t vidName = offsetof(VID, vidName);
        static constexpr std::size_t type = offsetof(VID, type);
        static constexpr std::size_t animationBaseFrame = offsetof(VID, animationBaseFrame);
        static constexpr std::size_t animationFrameCount = offsetof(VID, animationFrameCount);
        static constexpr std::size_t halfSizeXY = offsetof(VID, halfSizeXY);
        static constexpr std::size_t layer = offsetof(VID, layer);
        static constexpr std::size_t directionQuantizationOffsetValue = offsetof(VID, directionQuantizationOffsetValue);
        static constexpr std::size_t unitLimits = offsetof(VID, unitLimits);
        static constexpr std::size_t spriteCountsByArmy = offsetof(VID, spriteCountsByArmy);
        static constexpr std::size_t killedUnitCounters = offsetof(VID, killedUnitCounters);
        static constexpr std::size_t recolorUnitCounters = offsetof(VID, recolorUnitCounters);
        static constexpr std::size_t animationFrameDurations = offsetof(VID, animationFrameDurations);
        static constexpr std::size_t altGammaRaw = offsetof(VID, altGammaRaw);
        static constexpr std::size_t scriptFunction = offsetof(VID, scriptFunction);
        static constexpr std::size_t lastSpriteCountChangeTimestampMs = offsetof(VID, lastSpriteCountChangeTimestampMs);
        static constexpr std::size_t weapon = offsetof(VID, weapon);
        static constexpr std::size_t nextMirror = offsetof(VID, nextMirror);
        static constexpr std::size_t exchangedVid = offsetof(VID, exchangedVid);
        static constexpr std::size_t movementTactEnabledValue = offsetof(VID, movementTactEnabledValue);
        static constexpr std::size_t actionAuxStateRequiredValue = offsetof(VID, actionAuxStateRequiredValue);
        static constexpr std::size_t notCreateAsChildFlag = offsetof(VID, notCreateAsChildFlag);
    };
    static_assert(VidRetailLayoutProbe::nVid == 0x004, "VID +004 mismatch");
    static_assert(VidRetailLayoutProbe::name == 0x008, "VID +008 mismatch");
    static_assert(VidRetailLayoutProbe::spriteType == 0x00C, "VID +00C mismatch");
    static_assert(VidRetailLayoutProbe::spriteClass == 0x010, "VID +010 mismatch");
    static_assert(VidRetailLayoutProbe::property == 0x014, "VID +014 mismatch");
    static_assert(VidRetailLayoutProbe::moveMask == 0x018, "VID +018 mismatch");
    static_assert(VidRetailLayoutProbe::sizeXYZ == 0x01C, "VID +01C mismatch");
    static_assert(VidRetailLayoutProbe::maxHp == 0x028, "VID +028 mismatch");
    static_assert(VidRetailLayoutProbe::nWeapon == 0x040, "VID +040 mismatch");
    static_assert(VidRetailLayoutProbe::linkXYZ == 0x04C, "VID +04C mismatch");
    static_assert(VidRetailLayoutProbe::nLinkVid == 0x058, "VID +058 mismatch");
    static_assert(VidRetailLayoutProbe::linkVid == 0x05C, "VID +05C mismatch");
    static_assert(VidRetailLayoutProbe::noAnimCadr == 0x068, "VID +068 mismatch");
    static_assert(VidRetailLayoutProbe::sfx == 0x0AC, "VID +0AC mismatch");
    static_assert(VidRetailLayoutProbe::childX == 0x0F0, "VID +0F0 mismatch");
    static_assert(VidRetailLayoutProbe::childY == 0x134, "VID +134 mismatch");
    static_assert(VidRetailLayoutProbe::childZ == 0x178, "VID +178 mismatch");
    static_assert(VidRetailLayoutProbe::nChildVid == 0x1BC, "VID +1BC mismatch");
    static_assert(VidRetailLayoutProbe::childVid == 0x200, "VID +200 mismatch");
    static_assert(VidRetailLayoutProbe::noChild == 0x244, "VID +244 mismatch");
    static_assert(VidRetailLayoutProbe::gammaRaw == 0x288, "VID +288 mismatch");
    static_assert(VidRetailLayoutProbe::scaleXYZ == 0x290, "VID +290 mismatch");
    static_assert(VidRetailLayoutProbe::vidName == 0x29C, "VID +29C mismatch");
    static_assert(VidRetailLayoutProbe::type == 0x2A0, "VID +2A0 mismatch");
    static_assert(VidRetailLayoutProbe::animationBaseFrame == 0x2AC, "VID +2AC mismatch");
    static_assert(VidRetailLayoutProbe::animationFrameCount == 0x2F0, "VID +2F0 mismatch");
    static_assert(VidRetailLayoutProbe::halfSizeXY == 0x334, "VID +334 mismatch");
    static_assert(VidRetailLayoutProbe::layer == 0x33C, "VID +33C mismatch");
    static_assert(VidRetailLayoutProbe::directionQuantizationOffsetValue == 0x340, "VID +340 mismatch");
    static_assert(VidRetailLayoutProbe::unitLimits == 0x344, "VID +344 mismatch");
    static_assert(VidRetailLayoutProbe::spriteCountsByArmy == 0x358, "VID +358 mismatch");
    static_assert(VidRetailLayoutProbe::killedUnitCounters == 0x368, "VID +368 mismatch");
    static_assert(VidRetailLayoutProbe::recolorUnitCounters == 0x378, "VID +378 mismatch");
    static_assert(VidRetailLayoutProbe::animationFrameDurations == 0x388, "VID +388 mismatch");
    static_assert(VidRetailLayoutProbe::altGammaRaw == 0x398, "VID +398 mismatch");
    static_assert(VidRetailLayoutProbe::scriptFunction == 0x3B8, "VID +3B8 mismatch");
    static_assert(VidRetailLayoutProbe::lastSpriteCountChangeTimestampMs == 0x400, "VID +400 mismatch");
    static_assert(VidRetailLayoutProbe::weapon == 0x404, "VID +404 mismatch");
    static_assert(VidRetailLayoutProbe::nextMirror == 0x408, "VID +408 mismatch");
    static_assert(VidRetailLayoutProbe::exchangedVid == 0x40C, "VID +40C mismatch");
    static_assert(VidRetailLayoutProbe::movementTactEnabledValue == 0x410, "VID +410 mismatch");
    static_assert(VidRetailLayoutProbe::actionAuxStateRequiredValue == 0x414, "VID +414 mismatch");
    static_assert(VidRetailLayoutProbe::notCreateAsChildFlag == 0x418, "VID +418 mismatch");
    static_assert(sizeof(VID) == 0x41C, "retail base VID allocation must be exactly 0x41C on MSVC x86");
#endif

}

