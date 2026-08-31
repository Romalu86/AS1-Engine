#include "win/application_win.h"

#ifdef _WIN32
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <mmsystem.h>
#include <commdlg.h>
#include <objbase.h>
#include <filesystem>
#include <new>
#include <cmath>
#include <cstring>

#include "sprite.h"
#include "unit.h"
#include "rail.h"
#include "depo.h"
#include "building.h"
#include "avia.h"
#include "creature.h"
#include "civ_robot.h"
#include "engine.h"
#include "balloon.h"
#include "map.h"
#include "graph.h"
#include "graphics/base_texture.h"
#include "vid/vid.h"
#include "sprite_collector_hash.h"
#include "mouse.h"
#include "core/log.h"
#include "core/application.h"
#include "core/profile_p.h"
#include "core/file_logger.h"
#include "core/configuration.h"
#include "constant.h"
#include "sound/engine.h"
#include "win/resources/resource.h"
#include "win/main_sw.h"
#include "input/control_actions.h"
#include "game/startup.h"

namespace as1::win
{
    namespace
    {
        // byte [this+4] / dword [this+4] flags used by the original shell block.
        constexpr std::uint32_t kApplicationCleanupBusyFlag = 0x00000002u;
        constexpr std::uint32_t kApplicationInitializedFlag = 0x00000004u;
        constexpr std::uint32_t kApplicationFramePumpFlag = 0x00000008u;
        constexpr std::uint32_t kApplicationModalDispatchFlag = 0x00000010u;
        constexpr std::uint32_t kApplicationCommandLinePendingFlag = application_flags::PendingCommandOrLoad;
        constexpr std::uint32_t kApplicationRenderControlsFlag = 0x00000080u;

        constexpr std::uint32_t kShellLowNibbleMask = 0x0000000Fu;
        constexpr std::uint32_t kShellTogglePause = 0x00000001u;
        constexpr std::uint32_t kShellForceFrame = 0x00000002u;
        constexpr std::uint32_t kShellDrawLabels = 0x00000004u;
        constexpr std::uint32_t kShellDispatchOverlayList = 0x00000008u;

        constexpr std::uint32_t kFrameClampMs = 71u;
        constexpr std::uint32_t kDemoFrameToleranceMs = 20u;
        constexpr float kSub40A290Half = 0.5f;
        constexpr float kSub40A290Edge = 5.0f;
        constexpr float kSub40A290AccelX = 0.039999999f;
        constexpr float kSub40A290AccelY = 0.029999999f;
        constexpr float kSub40A290TargetScale = 0.001f;
        constexpr float kSub40A290PointerScale = -0.0040000002f;

        float dwordAsFloat(std::uint32_t value) noexcept
        {
            float out = 0.0f;
            std::memcpy(&out, &value, sizeof(out));
            return out;
        }

        bool cameraX87LessOrUnordered(float lhs, float rhs) noexcept
        {
            return std::isnan(lhs) || std::isnan(rhs) || lhs < rhs;
        }

        bool cameraX87LessEqualOrUnordered(float lhs, float rhs) noexcept
        {
            return std::isnan(lhs) || std::isnan(rhs) || lhs <= rhs;
        }

        bool cameraX87EqualOrUnordered(float lhs, float rhs) noexcept
        {
            return std::isnan(lhs) || std::isnan(rhs) || lhs == rhs;
        }

        std::int32_t cameraElapsedScaleFtolLow32(std::uint32_t elapsed, float scale) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            __int64 elapsed64 = static_cast<__int64>(elapsed);
            __int64 converted = 0;
            unsigned short oldControl = 0;
            unsigned short truncateControl = 0;
            __asm
            {
                fild qword ptr elapsed64
                fmul scale
                fstcw oldControl
                fwait
                mov ax, oldControl
                or ah, 0Ch
                mov truncateControl, ax
                fldcw truncateControl
                fistp qword ptr converted
                fldcw oldControl
            }
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(converted));
#else
            const long double d = static_cast<long double>(elapsed) * static_cast<long double>(scale);
            if (!std::isfinite(d) ||
                d < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                d > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(converted));
#endif
        }

        std::uint32_t frameElapsedScaleFtolLow32(std::uint32_t elapsed, float scale) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            __int64 elapsed64 = static_cast<__int64>(elapsed);
            __int64 converted = 0;
            unsigned short oldControl = 0;
            unsigned short truncateControl = 0;
            __asm
            {
                fild qword ptr elapsed64
                fmul scale
                fstcw oldControl
                fwait
                mov ax, oldControl
                or ah, 0Ch
                mov truncateControl, ax
                fldcw truncateControl
                fistp qword ptr converted
                fldcw oldControl
            }
            return static_cast<std::uint32_t>(converted);
#else
            const long double d = static_cast<long double>(elapsed) * static_cast<long double>(scale);
            if (!std::isfinite(d) || d >= 9223372036854775808.0L || d < -9223372036854775808.0L)
                return 0u;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<std::uint32_t>(converted);
#endif
        }

        bool fileExists(const std::filesystem::path& path)
        {
            std::error_code ec;
            return std::filesystem::exists(path, ec) && !ec;
        }

        std::filesystem::path inferResourceRootFromMapFile(const char* selectedPath)
        {
            if (!selectedPath || !*selectedPath)
                return {};

            std::filesystem::path mapPath(selectedPath);
            if (!mapPath.is_absolute())
                mapPath = std::filesystem::absolute(mapPath);

            const std::filesystem::path mapDir = mapPath.parent_path();
            std::filesystem::path root = mapDir;
            const std::string leaf = mapDir.filename().string();
            std::string lowerLeaf = leaf;
            std::transform(lowerLeaf.begin(), lowerLeaf.end(), lowerLeaf.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lowerLeaf == "maps")
                root = mapDir.parent_path();

            if (root.empty())
                return {};
            if (fileExists(root / "objects.res") || fileExists(root / "Objects.res"))
                return root;
            return {};
        }
        constexpr DWORD kMainWindowExStyle = 0x00040000u;
        constexpr DWORD kWindowedStyle = 0x90CA0000u;
        constexpr DWORD kFullscreenStyle = 0x80000000u;
        constexpr const char* kMenuResourceName = "AppMenu";
        constexpr const char* kIconResourceName = "AppIcon";
        constexpr const char* kAcceleratorResourceName = "AppAccel";

        std::uint32_t g_tooltipLastUpdateTime = 0;
        float g_tooltipLastClientX = 0.0f;
        float g_tooltipLastClientY = 0.0f;
        alignas(as1::STRING) unsigned char g_tooltipCachedTextStorage[sizeof(as1::STRING)];
        unsigned char g_tooltipInitFlags = 0u;

        void __cdecl cleanupCachedTooltipText()
        {

            as1::STRING& owner = *reinterpret_cast<as1::STRING*>(g_tooltipCachedTextStorage);
            char* const raw = const_cast<char*>(owner.c_str());
            if (raw != as1::STRING::SharedEmptyText())
                ::operator delete(raw);
        }

        as1::STRING& cachedTooltipText()
        {

            if ((g_tooltipInitFlags & 1u) == 0u)
            {
                new (g_tooltipCachedTextStorage) as1::STRING();
                g_tooltipInitFlags |= 1u;
                std::atexit(cleanupCachedTooltipText);
            }
            return *reinterpret_cast<as1::STRING*>(g_tooltipCachedTextStorage);
        }


        as1::input::InputMessageState& inputState(ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<as1::input::InputMessageState*>(
                reinterpret_cast<std::uint8_t*>(app) + core::retail_application_layout::InputState);
        }

        const as1::input::InputMessageState& inputState(const ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<const as1::input::InputMessageState*>(
                reinterpret_cast<const std::uint8_t*>(app) + core::retail_application_layout::InputState);
        }

        HINSTANCE& applicationInstanceHandle(ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<HINSTANCE*>(reinterpret_cast<std::uint8_t*>(app) + core::retail_application_layout::InstanceHandle);
        }

        HINSTANCE applicationInstanceHandle(const ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<HINSTANCE const*>(reinterpret_cast<const std::uint8_t*>(app) + core::retail_application_layout::InstanceHandle);
        }

        HWND& mainWindowHandle(ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<HWND*>(reinterpret_cast<std::uint8_t*>(app) + core::retail_application_layout::MainWindow);
        }

        HWND mainWindowHandle(const ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<HWND const*>(reinterpret_cast<const std::uint8_t*>(app) + core::retail_application_layout::MainWindow);
        }

        HACCEL& acceleratorHandle(ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<HACCEL*>(reinterpret_cast<std::uint8_t*>(app) + core::retail_application_layout::Accelerator);
        }

        HACCEL acceleratorHandle(const ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<HACCEL const*>(reinterpret_cast<const std::uint8_t*>(app) + core::retail_application_layout::Accelerator);
        }

        std::uint32_t& shellFlagsStorage(ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(app) + core::retail_application_layout::ShellFlags);
        }

        std::uint32_t shellFlagsStorage(const ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::uint8_t*>(app) + core::retail_application_layout::ShellFlags);
        }

        float& physicalFloatSlot(ApplicationWin* app, std::size_t offset) noexcept
        {
            return *reinterpret_cast<float*>(reinterpret_cast<std::uint8_t*>(app) + offset);
        }

        float physicalFloatSlot(const ApplicationWin* app, std::size_t offset) noexcept
        {
            return *reinterpret_cast<const float*>(reinterpret_cast<const std::uint8_t*>(app) + offset);
        }

        constexpr std::size_t kApplicationTitleOffset = core::retail_application_layout::ApplicationTitle;
        constexpr std::size_t kCurrentMapNameOffset = core::retail_application_layout::CurrentMapName;
        constexpr std::size_t kPendingCommandOffset = core::retail_application_layout::PendingCommand;
        constexpr std::size_t kRegistryPathOffset = core::retail_application_layout::RegistryPath;

        as1::STRING& applicationStringAt(ApplicationWin* app, std::size_t offset) noexcept
        {
            return *reinterpret_cast<as1::STRING*>(reinterpret_cast<std::uint8_t*>(app) + offset);
        }
        const as1::STRING& applicationStringAt(const ApplicationWin* app, std::size_t offset) noexcept
        {
            return *reinterpret_cast<const as1::STRING*>(reinterpret_cast<const std::uint8_t*>(app) + offset);
        }
        as1::STRING& applicationTitle(ApplicationWin* app) noexcept { return applicationStringAt(app, kApplicationTitleOffset); }
        const as1::STRING& applicationTitle(const ApplicationWin* app) noexcept { return applicationStringAt(app, kApplicationTitleOffset); }
        as1::STRING& currentMapName(ApplicationWin* app) noexcept { return applicationStringAt(app, kCurrentMapNameOffset); }
        const as1::STRING& currentMapName(const ApplicationWin* app) noexcept { return applicationStringAt(app, kCurrentMapNameOffset); }
        as1::STRING& pendingCommand(ApplicationWin* app) noexcept { return applicationStringAt(app, kPendingCommandOffset); }
        const as1::STRING& pendingCommand(const ApplicationWin* app) noexcept { return applicationStringAt(app, kPendingCommandOffset); }
        as1::STRING& registryPath(ApplicationWin* app) noexcept { return applicationStringAt(app, kRegistryPathOffset); }
        const as1::STRING& registryPath(const ApplicationWin* app) noexcept { return applicationStringAt(app, kRegistryPathOffset); }

        PLAYER*& playerPointerSlot(ApplicationWin* app, std::size_t offset) noexcept
        {
            return *reinterpret_cast<PLAYER**>(reinterpret_cast<std::uint8_t*>(app) + offset);
        }

        PLAYER* playerPointerSlot(const ApplicationWin* app, std::size_t offset) noexcept
        {
            return *reinterpret_cast<PLAYER* const*>(reinterpret_cast<const std::uint8_t*>(app) + offset);
        }

        constexpr std::size_t playerSlotOffset(int index) noexcept
        {
            return core::retail_application_layout::PlayerSlots +
                static_cast<std::size_t>(index & 3) * core::retail_application_layout::PlayerSlotStride;
        }

        // Retail releaseShellOwnedSpriteSlot is the canonical owner for this two-DWORD
        // vptr/object slot.  It does not know the concrete SPRITE class: the
        // owned object's vtable slot +0 is called with scalar-delete flag 1,
        // then +4 is cleared.  Keep the Win32/x86 route ABI-exact here instead
        // of adding MAP/container side effects to the owner itself.
        void deleteVidThroughRetailSlot04(VID* vid) noexcept
        {
            if (!vid)
                return;
#if defined(_MSC_VER) && defined(_M_IX86)

            __asm
            {
                mov ecx, vid
                mov edx, dword ptr [ecx]
                push 1
                call dword ptr [edx+4]
            }
#else
            // Portable syntax/runtime validation cannot reproduce MSVC x86
            // scalar-deleting-destructor codegen.  The host branch preserves
            // lifetime only; native acceptance is gated separately.
            delete vid;
#endif
        }

        void releaseShellOwnedSpriteSlot(void* ownerStorage) noexcept
        {
            auto** const objectSlot = reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(ownerStorage) + 4u);
            void* const object = *objectSlot;
            if (object)
            {
#if defined(_MSC_VER) && defined(_M_IX86)
                __asm
                {
                    mov ecx, object
                    mov eax, dword ptr [ecx]
                    push 1
                    call dword ptr [eax]
                }
#else
                // Portable validation has no MSVC scalar-deleting-destructor ABI.
                // The host ownership bridge is validation-only; the shipping x86
                // path above performs the exact retail virtual call shape.
                auto* const sprite = static_cast<SPRITE*>(object);
                if (MAP* const owner = sprite->mapOwner())
                    owner->ReleaseSpriteForScalarDeletingDestructor(sprite);
                delete sprite;
#endif
            }
            *objectSlot = nullptr;
        }

        std::uint32_t currentShellOwnedSpriteVtable() noexcept;

        void* shellOwnedSpriteScalarDeletingDestructor(void* ownerStorage, unsigned char flags) noexcept
        {

            void* const self = ownerStorage;
            const std::uint32_t vtable = currentShellOwnedSpriteVtable();
            std::memcpy(self, &vtable, sizeof(vtable));
            releaseShellOwnedSpriteSlot(self);
            if ((flags & 1u) != 0u)
                ::operator delete(self);
            return self;
        }

        class ShellOwnedSpriteVtableOwner final
        {
        public:
            virtual void* deletingDestructor(unsigned char flags) noexcept
            {
                return shellOwnedSpriteScalarDeletingDestructor(this, flags);
            }
        };

        std::uint32_t currentShellOwnedSpriteVtable() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            static ShellOwnedSpriteVtableOwner owner;
            return static_cast<std::uint32_t>(*reinterpret_cast<const std::uintptr_t*>(&owner));
#else
            return 0u;
#endif
        }

        std::uint32_t& shellOwnedSpriteVtable(ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(app) + core::retail_application_layout::ShellOwnedSpriteVtable);
        }

        SPRITE*& shellOwnedSpritePointer(ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<SPRITE**>(reinterpret_cast<std::uint8_t*>(app) + core::retail_application_layout::ShellOwnedSpritePointer);
        }

        void initializeShellOwnedSpriteOwner(ApplicationWin* app) noexcept
        {
            shellOwnedSpriteVtable(app) = currentShellOwnedSpriteVtable();
            shellOwnedSpritePointer(app) = nullptr;
        }

        int releaseShellOwnedSpriteOwner(ApplicationWin* app)
        {
            shellOwnedSpriteVtable(app) = currentShellOwnedSpriteVtable();
            const bool hadObject = shellOwnedSpritePointer(app) != nullptr;
            releaseShellOwnedSpriteSlot(reinterpret_cast<std::uint8_t*>(app) + core::retail_application_layout::ShellOwnedSpriteVtable);
            return hadObject ? 1 : 0;
        }

        void bindShellOwnedSprite(ApplicationWin* app, SPRITE* object) noexcept
        {
            shellOwnedSpritePointer(app) = object;
        }

        void clearShellOwnedSpriteIfMatches(ApplicationWin* app, SPRITE* object) noexcept
        {
            if (shellOwnedSpritePointer(app) == object)
                shellOwnedSpritePointer(app) = nullptr;
        }

        bool shellOwnedSpriteEmpty(ApplicationWin* app) noexcept
        {
            return shellOwnedSpritePointer(app) == nullptr;
        }


        std::string copyCString(const char* text)
        {
            return (text && *text) ? std::string(text) : std::string();
        }

        void toggleFlag(std::uint32_t& flags, std::uint32_t mask) noexcept
        {
            flags ^= mask;
        }

        bool applicationInputGetWindowRect(std::uintptr_t hwndValue, as1::input::InputWindowRect& rect, void*)
        {
            RECT nativeRect;
            ::GetWindowRect(reinterpret_cast<HWND>(hwndValue), &nativeRect);
            rect.left = nativeRect.left;
            rect.top = nativeRect.top;
            rect.right = nativeRect.right;
            rect.bottom = nativeRect.bottom;
            return true;
        }


        std::unique_ptr<MAP> g_applicationMapCarrier;
        std::uintptr_t g_applicationWinDerivedVtable = 0;

        class ApplicationBaseVtableOwner
        {
        public:
            virtual ~ApplicationBaseVtableOwner()
            {
                reinterpret_cast<ApplicationWin*>(this)->destroyBaseApplicationState();
            }
            virtual bool dispatchWindowMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LRESULT& result)
            {
                return reinterpret_cast<ApplicationWin*>(this)->ApplicationWin::dispatchWindowMessage(hwnd, msg, wparam, lparam, result);
            }
            virtual void clearSpriteReferencesAcrossRuntime(SPRITE* sprite)
            {
                reinterpret_cast<ApplicationWin*>(this)->clearSpriteReferencesAcrossRuntime(sprite);
            }
            virtual bool runBaseFramePump()
            {
                return reinterpret_cast<ApplicationWin*>(this)->ApplicationWin::pumpOnce();
            }
            virtual void drawApplicationDebugOverlayPass()
            {
                reinterpret_cast<ApplicationWin*>(this)->drawApplicationDebugOverlayPass();
            }
            virtual void releaseWorldRuntime()
            {
                reinterpret_cast<ApplicationWin*>(this)->releaseWorldRuntime();
            }
            virtual void runCommandLineMap(char* ownedCommandLine)
            {
                reinterpret_cast<ApplicationWin*>(this)->runCommandLineMap(ownedCommandLine);
            }
            virtual void saveMap(const as1::STRING& outputName)
            {
                reinterpret_cast<ApplicationWin*>(this)->ApplicationWin::saveMap(outputName);
            }
            virtual std::unique_ptr<SPRITE> createSpriteViaApplicationFactory(const as1::core::ApplicationCreateSpriteRequest& request)
            {
                return as1::core::Application::CreateSprite(request);
            }
        };

        std::uintptr_t currentBaseApplicationVtable() noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            alignas(ApplicationBaseVtableOwner) static unsigned char storage[sizeof(ApplicationBaseVtableOwner)];
            static ApplicationBaseVtableOwner* owner = new (storage) ApplicationBaseVtableOwner();
            return *reinterpret_cast<std::uintptr_t*>(owner);
#else
            return 0;
#endif
        }

        void captureDerivedApplicationVtable(ApplicationWin* app) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            g_applicationWinDerivedVtable = *reinterpret_cast<std::uintptr_t*>(app);
#else
            (void)app;
#endif
        }

        void installBaseApplicationVtable(ApplicationWin* app) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            *reinterpret_cast<std::uintptr_t*>(app) = currentBaseApplicationVtable();
#else
            (void)app;
#endif
        }

        void installDerivedApplicationVtable(ApplicationWin* app) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            if (g_applicationWinDerivedVtable != 0)
                *reinterpret_cast<std::uintptr_t*>(app) = g_applicationWinDerivedVtable;
#else
            (void)app;
#endif
        }

        std::string startupCurrentDirectoryText()
        {
            char buffer[MAX_PATH] = {};
            const DWORD count = ::GetCurrentDirectoryA(static_cast<DWORD>(sizeof(buffer)), buffer);
            if (count == 0 || count >= sizeof(buffer))
                return ".";
            return buffer;
        }
    }

    ApplicationWin* applicationWinInstance() noexcept
    {

        return static_cast<ApplicationWin*>(as1::core::ApplicationPhysicalOwner());
    }

    ApplicationWin* CreateApplicationWin(const ApplicationWinInit& init)
    {

        static_assert(sizeof(ApplicationWin) <= 0x22A0u,
                      "ApplicationWin source representation exceeds retail 0x22A0 allocation");
#if UINTPTR_MAX == 0xFFFFFFFFu

        static_assert(sizeof(ApplicationWin) <= 0x204u,
                      "ApplicationWin convenience state overlaps retail Application+0x204 physical owner region");
#endif
        void* const storage = ::operator new(0x22A0u, std::nothrow);
        if (!storage)
            return nullptr;
        return new (storage) ApplicationWin(init);
    }

    void DestroyApplicationWin(ApplicationWin* app)
    {
        if (!app)
            return;

#if defined(_MSC_VER) && defined(_M_IX86)
        __asm
        {
            mov ecx, app
            mov eax, [ecx]
            push 1
            call dword ptr [eax]
        }
#else
        app->~ApplicationWin();
        ::operator delete(app);
#endif
    }

    void ReleaseApplicationWinHostMapCarrier() noexcept
    {

        g_applicationMapCarrier.reset();
    }

    ApplicationWin::ApplicationWin(const ApplicationWinInit& init)
    {

        captureDerivedApplicationVtable(this);
        (void)init;
    }

    ApplicationWin::~ApplicationWin()
    {
        destroyDerivedApplicationState();
    }

    void ApplicationWin::destroyDerivedApplicationState()
    {

        installDerivedApplicationVtable(this);

        releaseShellOwnedSpriteOwner(this);
        deinitialize();
        destroyBaseApplicationState();
    }

    void ApplicationWin::initializeBase(const ApplicationWinInit& init)
    {

        new (&applicationTitle(this)) as1::STRING();
        new (&currentMapName(this)) as1::STRING();
        new (&pendingCommand(this)) as1::STRING();
        new (&registryPath(this)) as1::STRING();

        // initializePointerListRecord x16 at Application+0x4C..+0x13C.
        as1::core::InitializeApplicationPhysicalDrawStorage(this);

        // Embedded Application owners in exact construction order.
        new (reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::ScriptRuntime) SCRIPT();
        new (reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::DemoResource) RESOURCE();
        new (reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::RelationTable) RelationTable();
        inputState(this).initializePreservingPersistentFlags();
        new (reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::BaseSpriteList) SPRITE_LIST();
        new (reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::Groups) GROUPS();
        initializeShellOwnedSpriteOwner(this);

        installBaseApplicationVtable(this);

        SPRITE::initializeRetailStartupTrigTables();

        const std::uint32_t maskedIncomingFlags = flags() & 0xFFFDFFFDu;
        setFlags(maskedIncomingFlags);
        physicalFloatSlot(this, core::retail_application_layout::MapExtentX) = 640.0f;
        physicalFloatSlot(this, core::retail_application_layout::MapExtentY) = 480.0f;
        physicalFloatSlot(this, core::retail_application_layout::CameraShiftY) = 1.0f;

        std::uint32_t startupFlags = maskedIncomingFlags;
        startupFlags ^= (startupFlags ^ init.startupFlags) & 1u;
        startupFlags = (startupFlags & 0xFFFF1083u) | 0x00051080u;

        *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::TerrainGrid) = 0u;
        *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::ActivePlayerIndex) = 0u;
        setFlags(startupFlags);
        physicalFloatSlot(this, core::retail_application_layout::CameraShiftX) = 0.0f;
        *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::WeaponTable) = 0u;
        *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::WeaponCount) = 0u;
        *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::VidCount) = 0u;
        *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::WorldFrameCounter) = 0u;

        *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::WorldStartTime) =
            static_cast<std::uint32_t>(::timeGetTime());
        physicalFloatSlot(this, core::retail_application_layout::TickScale) = 1.0f;
        *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::ScrollType) = 1u;
        std::memset(reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::VidTable, 0, core::retail_application_layout::VidTableBytes);
        playerPointerSlot(this, playerSlotOffset(0)) = nullptr;
        playerPointerSlot(this, playerSlotOffset(1)) = nullptr;
        playerPointerSlot(this, playerSlotOffset(2)) = nullptr;
        playerPointerSlot(this, playerSlotOffset(3)) = nullptr;
        applicationInstanceHandle(this) = init.hInstance;

        (void)rebuildTerrainGrid();
    }


    int ApplicationWin::rebuildTerrainGrid()
    {

        short*& grid = *reinterpret_cast<short**>(reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::TerrainGrid);
        if (grid)
            ::operator delete(static_cast<void*>(grid));
        const int gridX = static_cast<int>(physicalFloatSlot(this, core::retail_application_layout::MapExtentX)) / 8;
        const int gridY = static_cast<int>(physicalFloatSlot(this, core::retail_application_layout::MapExtentY)) / 8;
        *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::TerrainGridWidth) = gridX;
        *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::TerrainGridHeight) = gridY;
        const std::size_t bytes = 2u * static_cast<std::size_t>(gridX) * static_cast<std::size_t>(gridY);
        grid = static_cast<short*>(::operator new(bytes));
        std::memset(grid, 0, bytes);
        return 0;
    }

    ApplicationWin* ApplicationWin::initializeDerivedApplicationStartup(HINSTANCE instance,
                                                     HINSTANCE previousInstance,
                                                     const char** commandLineOwner,
                                                     int showCmd,
                                                     as1::core::StartupSettingsBlock* startupSettings)
    {

        (void)initializeBaseApplicationStartup(instance, previousInstance, commandLineOwner, showCmd, startupSettings);
        runPostStartupHandoff();
        return this;
    }

    ApplicationWin* ApplicationWin::initializeBaseApplicationStartup(HINSTANCE instance,
                                    HINSTANCE previousInstance,
                                    const char** commandLineOwner,
                                    int showCmd,
                                    as1::core::StartupSettingsBlock* startupSettings)
    {

        ApplicationWinInit baseInit{};
        baseInit.hInstance = instance;
        baseInit.previousInstance = previousInstance;
        baseInit.commandLine = (commandLineOwner && *commandLineOwner) ? *commandLineOwner : "";
        baseInit.showCmd = showCmd;
        baseInit.startupFlags = startupSettings ? startupSettings->flags : 0u;
        initializeBase(baseInit);

        as1::InitializeGlobalFileLoggerOwner(true);

        const char* const commandLine = (commandLineOwner && *commandLineOwner) ? *commandLineOwner : "";
        as1::core::StartupConfiguration windowConfig =
            as1::core::Configuration::LoadStartupConfiguration(as1::STRING(),
                                                                as1::STRING(commandLine),
                                                                as1::STRING(startupCurrentDirectoryText()));

        const HRESULT comInitializeResult = ::CoInitialize(nullptr);
        if (FAILED(comInitializeResult))
        {
            as1::LOG::ResourceError("MAP", 12, "COM", static_cast<int>(comInitializeResult));
            return this;
        }

        as1::core::initializePostComProfileOwners(windowConfig);
        setApplicationTitle(windowConfig.applicationTitle);

#if defined(_MSC_VER) && defined(_M_IX86)
        static_assert(sizeof(as1::GRAPH) == 0x0E24u,
                      "GRAPH Win32/x86 source representation must equal retail 0xE24 allocation");
#endif
        void* const graphStorage = ::operator new(0x0E24u, std::nothrow);
        if (!graphStorage)
            return this;
        as1::GRAPH* const graph = new (graphStorage) as1::GRAPH();

        std::strcpy(startupSettings->title, applicationTitle(this).c_str());
        graph->initializeRetailGraphState(*startupSettings);

        as1::GRAPH::BindCurrentGraph(graph);

        as1::core::readStartupStartDialogProfileBlock(windowConfig);
        if (windowConfig.showStartDialog)
        {
            if (!as1::win::ShowStartDialog(instance))
                return this;
        }

        if (!registerMainWindowClass(windowConfig, previousInstance))
            return this;

        as1::core::readStartupWindowPositionRegistryBlock(
            windowConfig, graph->GraphFlag34Bit7());
        HWND hwnd = createMainWindow(windowConfig);
        showMainWindow(showCmd);
        HACCEL accel = loadMainAccelerators();
        bindNativeWindow(hwnd, accel);

        if (graph->initializeWindowDevice(hwnd) != 0)
            return this;
        as1::core::readStartupGraphFontProfileBlock(windowConfig);
        graph->rebuildTextFont(windowConfig.font.face, windowConfig.font.sizeX, windowConfig.font.sizeY);

        as1::RESOURCE startupObjectsResource;
        as1::initializeResourceState(startupObjectsResource);
        as1::core::readStartupResourceProfileBlock(windowConfig);
        setObjectsResourceName(windowConfig.objectsResource);
        if (as1::openResourceFileForRead(startupObjectsResource,
                            windowConfig.objectsResource,
                            as1::RESOURCE::ResTypes::DATA) != 0)
        {
            as1::LOG::ResourceError("%s", 7, "resource file", 0, "");
            return this;
        }

        as1::core::readStartupSoundHighQualityRegistryBlock(windowConfig);
        as1::sound::Engine* soundEngine = new (std::nothrow) as1::sound::Engine;
        if (soundEngine)
        {
            as1::sound::EngineStartup soundStartup{};
            soundStartup.window = hwnd;
            soundStartup.resource = &startupObjectsResource;
            soundStartup.highQuality = windowConfig.sound.highQuality ? 1 : 0;
            as1::sound::BindGlobalSoundEngine(soundEngine->initializeSoundState(soundStartup));
        }
        else
        {
            as1::sound::BindGlobalSoundEngine(nullptr);
        }

        as1::BASE_CONSTANTS* baseConstants = new (std::nothrow) as1::BASE_CONSTANTS();
        if (baseConstants)
            as1::BindGlobalBaseConstants(as1::loadBaseConstantsFromResource(baseConstants, &startupObjectsResource));
        else
            as1::BindGlobalBaseConstants(nullptr);

        as1::core::readStartupDebugModeProfileBlock(windowConfig);
        if (as1::GlobalBaseConstants())
            as1::GlobalBaseConstants()->raw[10] = static_cast<DWORD>(windowConfig.debugMode);

        g_applicationMapCarrier.reset(new (std::nothrow) as1::MAP(graph));
        if (!g_applicationMapCarrier)
            return this;
        as1::MAP& map = *g_applicationMapCarrier;

        (void)loadVidDepot(&startupObjectsResource);
        ::ShowCursor(TRUE);
        map.createStartupSpriteHashMap();
        as1::closeResourceOwner(startupObjectsResource);
        ::SetCursor(nullptr);

#if defined(_MSC_VER) && defined(_M_IX86)
        static_assert(sizeof(as1::MOUSE) == 0x108u,
                      "MOUSE Win32/x86 source representation must equal retail 0x108 allocation");
#endif
        as1::Mouse = new (std::nothrow) as1::MOUSE(
            as1::MAP::NullVid(),
            graph->screenWidth() * 0.5f,
            graph->screenHeight() * 0.5f,
            0.0f,
            0,
            nullptr);
        as1::Mouse->HardwareOn();
        as1::core::readStartupControlProfileBlock(windowConfig);
        as1::input::applyStartupControlProfileBlock(windowConfig.control);
        allocateStartupPlayerControls();

        // Retail first writes +0x34/+0x38/+0x3C/+0x40 here, after PLAYER
        // construction, not during the initial embedded-owner setup.
        copyStartupShellMapFields();
        as1::core::readStartupStartMapProfileBlock(windowConfig, as1::STRING(commandLine));
        as1::StartupOptions startupOptions = as1::core::Configuration::BuildStartupOptions(windowConfig);
        startupOptions.loadGameResources = false;
        map.setResourceRoot(startupOptions.resourceRoot);
        map.setObjectsResource(startupOptions.objectsResource);

        assignStringFromString(currentMapName(this), windowConfig.startMap);
        as1::destroyResourceOwner(startupObjectsResource);
        setFlags(flags() | 0x00000004u);
        return this;
    }

    void ApplicationWin::setFlags(std::uint32_t value) noexcept
    {

        *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::Flags) = value;
        if (as1::core::ApplicationPhysicalOwner() == this)
            as1::core::SetApplicationFlags(value);
    }

    void ApplicationWin::setPendingCommand(const as1::STRING& value)
    {

        assignStringFromString(pendingCommand(this), value);
    }

    void ApplicationWin::setCurrentMapName(const as1::STRING& value)
    {

        assignStringFromString(currentMapName(this), value);
    }

    void ApplicationWin::setApplicationTitle(const as1::STRING& value)
    {

        assignStringFromString(applicationTitle(this), value);
    }

    void ApplicationWin::setObjectsResourceName(const as1::STRING& value)
    {

        assignStringFromString(registryPath(this), value);
    }

    void ApplicationWin::addFlags(std::uint32_t mask) noexcept
    {
        setFlags(flags() | mask);
    }

    void ApplicationWin::removeFlags(std::uint32_t mask) noexcept
    {
        setFlags(flags() & ~mask);
    }

    void ApplicationWin::toggleFlags(std::uint32_t mask) noexcept
    {
        setFlags(flags() ^ mask);
    }


    void ApplicationWin::transferFrom(SPRITE* sprite)
    {

        clearShellOwnedSpriteIfMatches(this, sprite);
        clearSpriteReferencesAcrossRuntime(sprite);
    }

    void ApplicationWin::clearSpriteReferencesAcrossRuntime(SPRITE* sprite)
    {

        PLAYER* const players[4] = {
            playerPointerSlot(this, playerSlotOffset(0)),
            playerPointerSlot(this, playerSlotOffset(1)),
            playerPointerSlot(this, playerSlotOffset(2)),
            playerPointerSlot(this, playerSlotOffset(3))
        };
        for (PLAYER* const player : players)
            (void)player->clearSpriteReferenceViaVtable(sprite);

        core::ApplicationScriptRuntime()->clearSpriteReferencesFromExecutionStack(sprite);
        reinterpret_cast<GROUPS*>(reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::Groups)->removeSpriteReferences(sprite);

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

        // Application+0x58: 16 BaseSpriteList traversal owners, stride 0x10.
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
    }

    int ApplicationWin::pumpFrame()
    {

        return pumpOnce() ? 1 : 0;
    }

    bool ApplicationWin::pumpOnce()
    {
        // the native message-pump helper updates the global frame clock, compacts
        // deferred object lists, dispatches pending Win32 messages with accelerators,
        // and returns 1 only when WM_QUIT is seen.
        if (pumpNativeMessages())
            return true;

        const bool debugMode = as1::GlobalBaseConstants() && as1::GlobalBaseConstants()->raw[10] != 0;
        if (debugMode)
            dispatchShellCommandKey();

        const bool waitForMessage = shouldWaitForMessageGate();
        if (waitForMessage)
        {
            waitForMessageGate();
            return false;
        }

        const bool abortBeforeFrame = shouldAbortBeforeFrameCounter();
        if (abortBeforeFrame)
        {
            return false;
        }

        const bool worldTick = incrementFrameCounterAndBuildWorldTick();
        frameDispatchPrologue(worldTick);
        dispatchPrimaryControlMovementMask();
        dispatchAuxiliaryControlMovementMask();
        graphFrameDispatch(worldTick);
        clearControlMovementMasksAfterGraph();
        dispatchSpriteFrameBuckets(worldTick);
        prepareInputAndDispatchControls(worldTick);
        conditionalOverlayDrawAndPresent(worldTick);
        soundTick();
        return false;
    }

    void ApplicationWin::drawApplicationDebugOverlayPass()
    {

        drawApplicationDebugPass();
    }

    void ApplicationWin::drawShellOverlays()
    {

        drawApplicationDebugPass();

        if (shellFlagsStorage(this) & kShellDrawLabels)
            MAP::Current()->DrawSpriteNumberLabels(*GRAPH::CurrentGraph());

        if (shellFlagsStorage(this) & kShellDispatchOverlayList)
            MAP::Current()->DrawOverlaySpriteList();
    }

    void ApplicationWin::deinitialize()
    {

        reportNoVidReleaseResidue();
        removeFlags(kApplicationCleanupBusyFlag);
        g_spriteWorkList.releaseRepeatedReferencesRetail();
        releaseShellOwnedSpriteOwner(this);
        releaseBaseApplicationRuntime();
    }

    void ApplicationWin::destroyBaseApplicationState()
    {

        installBaseApplicationVtable(this);

        core::ApplicationDrawDispatcherState& drawState =
            core::GlobalApplicationDrawDispatcherState();
        for (int pass = 0; pass < core::ApplicationDrawDispatcherState::PassCount; ++pass)
            drawState.drawPassBucket(pass).list.releaseRepeatedReferencesRetail();

        if (Mouse)
        {
            MOUSE* const mouse = Mouse;

            (void)mouse->scalarDeletingDestructor(1u);
        }

        for (const std::size_t offset : {0x220u, 0x224u, 0x228u, 0x22Cu})
        {
            PLAYER*& player = playerPointerSlot(this, offset);
            if (player)
            {

                PLAYER* const owned = player;
                (void)owned->scalarDeletingDestructorActivePlayer(1u);
            }
        }

        DestroyGlobalSpriteHashMapForApplicationDestructor();

        // g_startupStringsIniPathOwner.
        core::DestroyStartupStringsIniPathOwnerForApplicationDestructor();

        if (as1::sound::Engine* const soundEngine = as1::sound::GlobalSoundEngine())
            delete soundEngine;

        if (BASE_CONSTANTS* const constants = GlobalBaseConstants())
            delete constants;
        // g_startupRegistryPathOwner.
        core::DestroyStartupRegistryPathOwnerForApplicationDestructor();

        // Retail Application+0x290/+0x294 reverse VID deleting-destructor loop.
        // The raw pointer/count cells live in this same 0x22A0 Application
        // allocation. MAP::m_vids is only a host C++ lifetime carrier, so its
        // matching unique_ptr is released immediately before the raw delete.
        core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
        for (int index = appVidTable.count() - 1; index >= 0; --index)
        {
            VID* const vid = appVidTable.slot(index);
            if (!vid)
                continue;
            if (MAP* const map = MAP::Current())
                (void)map->ReleaseVidForScalarDeletingDestructor(vid);
            deleteVidThroughRetailSlot04(vid);
            appVidTable.setSlotCell(index, nullptr);
        }
        appVidTable.setStoredCount(0);

        LOG::Write("Vid release %i %i",
                   static_cast<int>(BASE_TEXTURE::RuntimeGlobals().nativeTextureBytes),
                   g_vidAllocatedBytes);

        if (GRAPH* const graph = GRAPH::CurrentGraph())
        {
            graph->~GRAPH();
            ::operator delete(static_cast<void*>(graph));
        }

        // Retail raw Application storage follows GRAPH teardown: +0x204 is the
        // signed-WORD terrain grid and +0x28C is the 0x44-stride WEAPON table.
        if (short* const grid = core::ApplicationTerrainGrid())
            ::operator delete(static_cast<void*>(grid));

        if (WEAPON* const weapons = core::ApplicationWeaponTable())
            ::operator delete(static_cast<void*>(weapons));

        DestroyGlobalFileLoggerOwnerForApplicationDestructor();

        ::CoUninitialize();

        // Retail late embedded tail: +0x2294 owned sprite, then the +0x264
        // circular GROUPS sentinel, then +0x24C frame list.
        releaseShellOwnedSpriteOwner(this);
        reinterpret_cast<GROUPS*>(reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::Groups)->unlinkAndReleaseStorage();
        applicationFrameSpriteList().destroyBaseSpriteListRecord(false);
        reinterpret_cast<RelationTable*>(reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::RelationTable)->~RelationTable();
        reinterpret_cast<RESOURCE*>(reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::DemoResource)->~RESOURCE();
        reinterpret_cast<SCRIPT*>(reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::ScriptRuntime)->~SCRIPT();

        as1::core::DestroyApplicationPhysicalDrawStorage(this);

        registryPath(this).~STRING();
        pendingCommand(this).~STRING();
        currentMapName(this).~STRING();
        applicationTitle(this).~STRING();
    }


    void ApplicationWin::drawApplicationDebugPass()
    {

        as1::core::ApplicationDebugPassState debugPassState{};
        debugPassState.flags = flags();

        as1::core::ApplicationDebugPassContext context{};
        context.graph = GRAPH::CurrentGraph();
        context.map = MAP::Current();
        context.selectedSprite = controlledSpriteForPlayer(static_cast<int>(as1::core::ActivePlayerIndex()));
        if (!context.selectedSprite)
            context.selectedSprite = activeAuxiliarySprite();
        as1::core::Application::DrawDebugPass(debugPassState, context);
    }

    void ApplicationWin::runCommandLineMap(char* ownedCommandLine)
    {

        STRING loadName;
        loadName.AdoptOwnedStorage(ownedCommandLine);

        addFlags(application_flags::MapLoading);
        if (loadName.isEmpty())
            return;

        MAP* const map = MAP::Current();
        if (!map)
            return;

        if (core::ApplicationMapWidth() != 0.0f ||
            core::ApplicationMapHeight() != 0.0f)
        {
            deinitialize();
        }

        if (!map->demoResource().isOpen() &&
            map->demoResource().openFile(loadName, RESOURCE::ResTypes::DEMO))
        {
            readStringLineFromStream(loadName, &map->demoResource());
            addFlags(application_flags::DemoUseResource);
        }

        RESOURCE mapResource;
        const std::filesystem::path mapPath = map->resolveGameFile(loadName);
        if (!mapResource.openFile(STRING(mapPath.string()), RESOURCE::ResTypes::MAP))
        {
            LOG::Write("!!!ERROR!!!LOAD: Invalid map file %s", loadName.c_str());
            return;
        }

        if (Mouse)
            Mouse->HardwareOff();

        assignStringFromString(currentMapName(this), loadName);
        map->m_fileName = loadName; // host-only mirror used to resolve sibling .lgc
        map->clearLoadedMapRuntime();

        std::uint32_t demoStart = static_cast<std::uint32_t>(::timeGetTime());
        if ((flags() & application_flags::DemoUseResource) != 0u)
            map->demoResource().read(&demoStart, sizeof(demoStart));
        core::SetDemoStartTimestampMilliseconds(demoStart);

        (void)loadVidDepot(&mapResource);
        if (!map->loadMapResourceSections(mapResource))
            return;

        removeFlags(application_flags::MapLoading);

        mapResource.close();
        map->relationTable().clear();

        LOG::Write("Vid release %i %i",
                   static_cast<int>(BASE_TEXTURE::RuntimeGlobals().nativeTextureBytes),
                   g_vidAllocatedBytes);
        map->installScriptNativeContext();
        map->loadScript();
        map->runRetailPostLoadScriptPasses();
    }

    bool ApplicationWin::loadVidDepot(RESOURCE* resource)
    {

        MAP* const map = MAP::Current();
        if (!map || !resource)
            return false;
        return map->hostLoadVidDepot(resource);
    }

    SPRITE* ApplicationWin::loadSpriteFromMapResource(RESOURCE* resource, int version)
    {

        std::int32_t oldAddress = -1;
        resource->read(&oldAddress, 4);
        if (oldAddress == -1)
            return reinterpret_cast<SPRITE*>(static_cast<std::uintptr_t>(0xFFFFFFFFu));

        std::int32_t nvid = -1;
        resource->read(&nvid, 4);

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if (version > 9)
        {
            resource->read(&x, 4);
            resource->read(&y, 4);
            resource->read(&z, 4);
        }
        else
        {
            std::int32_t value = 0;
            resource->read(&value, 4); x = static_cast<float>(value);
            resource->read(&value, 4); y = static_cast<float>(value);
            resource->read(&value, 4); z = static_cast<float>(value);
        }

        ANGLE direction;
        resource->read(&direction.value, 4);
        std::int32_t army = 0;
        resource->read(&army, 4);

        SPRITE* sprite = nullptr;
        core::ApplicationVidTable& vids = core::GlobalApplicationVidTable();
        if (nvid >= 0 && nvid < vids.count())
        {
            if (VID* const vid = vids.slot(nvid))
            {
                if (MAP* const map = MAP::Current())
                    sprite = map->CreateSpriteViaFactory(
                        vid, VECTOR(x, y, z), direction, nullptr, false);
            }
        }
        if (!sprite && (nvid < 0 || nvid >= vids.count() || vids.slot(nvid) == nullptr))
            LOG::ResourceError("%s", 3, "sprite, this vid not exist", nvid, "");

        reinterpret_cast<RelationTable*>(reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::RelationTable)->append(oldAddress, sprite);
        if (sprite)
            sprite->changeArmyBucket(army);
        return sprite;
    }

    VID* ApplicationWin::createVidFromResource(RESOURCE* resource, int nvid)
    {

        MAP* const map = MAP::Current();
        if (!map || !resource)
            return nullptr;
        return map->hostCreateVid(resource, nvid);
    }

    void ApplicationWin::releaseLinkVidRuntime()
    {

        if (MAP* const map = MAP::Current())
            map->hostReleaseLinkVidRuntime();
    }

    void ApplicationWin::releaseWorldRuntime()
    {

        if (MAP* const map = MAP::Current())
        {
            map->hostReleaseWorldRuntime();
            return;
        }

        // Process teardown can reach this owner after the host MAP carrier has
        // disappeared.  Only the directly proven GRAPH prefix is available in
        // that state; do not invent MAP-owned cleanup.
        if (GRAPH* const graph = GRAPH::CurrentGraph())
            graph->resetMapRenderRuntimeState();
        as1::DeleteGlobalSpriteHashMap();
    }

    void ApplicationWin::runCommandLine(char* ownedCommandLine)
    {
        runCommandLineMap(ownedCommandLine);
    }

    void ApplicationWin::runPostStartupHandoff()
    {

        installDerivedApplicationVtable(this);
        if (!initialized())
            return;

        shellFlagsStorage(this) &= ~kShellLowNibbleMask;
        const char* const startupText = currentMapName(this).c_str();
        char* ownedStartupText = as1::STRING::SharedEmptyText();
        if (*startupText != '\0')
        {
            const std::size_t length = std::strlen(startupText);
            ownedStartupText = static_cast<char*>(::operator new(length + 1u));
            std::memcpy(ownedStartupText, startupText, length);
            ownedStartupText[length] = '\0';
        }
        runCommandLineMap(ownedStartupText);
    }


    std::unique_ptr<SPRITE> ApplicationWin::CreateSprite(const as1::core::ApplicationCreateSpriteRequest& request)
    {

        if (!request.vid)
            return nullptr;

        VID* selectedVid = request.vid;
        if ((selectedVid->properties() & P_NOTCHANGELINKERCOOR) != 0u)
            selectedVid = resolveRegionMappedVid(selectedVid, request.xyz.x, request.xyz.y, request.xyz.z);

        const int liveLimit = selectedVid->unitLimit(0);
        if (liveLimit >= 0 && static_cast<int>(selectedVid->spriteCountAcrossArmies()) >= liveLimit)
            return nullptr;

        bool usedWinOverride = false;
        auto noteWinRoute = [&]()
        {
            usedWinOverride = true;
        };

        std::unique_ptr<SPRITE> sprite;
        switch (selectedVid->spriteClassId())
        {
        case B_TERRAIN:
        case B_OBJECT:
            noteWinRoute();
            sprite = std::make_unique<TERRAIN>(request.owner, selectedVid, request.xyz, request.direction, request.parent);
            break;
        case B_BUILDING:
            noteWinRoute();
            sprite = std::make_unique<BUILDING>(request.owner, selectedVid, request.xyz, request.direction, request.parent);
            break;
        case B_RAIL:
            noteWinRoute();
            sprite = std::make_unique<RAIL>(request.owner, selectedVid, request.xyz, request.direction, request.parent);
            break;
        case B_DEPO:
            noteWinRoute();
            sprite = std::make_unique<DEPO>(request.owner, selectedVid, request.xyz, request.direction, request.parent);
            break;
        case B_CIV_ROBOT:
            noteWinRoute();
            sprite = std::make_unique<CIV_ROBOT>(request.owner, selectedVid, request.xyz, request.direction, request.parent);
            break;
        case B_ENGINE:
            noteWinRoute();
            sprite = std::make_unique<ENGINE>(request.owner, selectedVid, request.xyz, request.direction, request.parent);
            break;
        case B_CREATURE:
            noteWinRoute();
            sprite = std::make_unique<CREATURE>(request.owner, selectedVid, request.xyz, request.direction, request.parent);
            break;
        case B_BALLOON:
            noteWinRoute();
            sprite = std::make_unique<BALLOON>(request.owner, selectedVid, request.xyz, request.direction, request.parent);
            break;
        default:
        {
            as1::core::ApplicationCreateSpriteRequest resolvedRequest = request;
            resolvedRequest.vid = selectedVid;
            sprite = as1::core::Application::CreateSprite(resolvedRequest);
            break;
        }
        }

        if (sprite && usedWinOverride &&
            (as1::core::ApplicationFlags() & application_flags::MapLoading) == 0u)
        {
            const int functionIndex = sprite->Vid()->birthScriptFunction();
            if (functionIndex >= 0)
            {
                const int spriteArg = static_cast<int>(static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(sprite.get())));
                as1::core::Application::callScriptFunction(functionIndex, spriteArg, 0);
            }
        }
        return sprite;
    }


    bool ApplicationWin::registerMainWindowClass(const as1::core::StartupConfiguration& config, HINSTANCE previousInstance)
    {

        if (previousInstance)
            return true;

        WNDCLASSA wc{};
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = applicationWindowProc;
        wc.hInstance = applicationInstanceHandle(this);
        wc.hIcon = LoadIconA(applicationInstanceHandle(this), kIconResourceName);
        wc.hCursor = nullptr;
        wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(4));
        wc.lpszClassName = config.applicationName.c_str();
        wc.lpszMenuName = (flags() & 1u) != 0u ? kMenuResourceName : nullptr;
        return RegisterClassA(&wc) != 0;
    }

    HWND ApplicationWin::createMainWindow(const as1::core::StartupConfiguration& config)
    {
        const char* const className = config.applicationName.c_str();

        const char* const title = applicationTitle(this).c_str();
        GRAPH* const graph = GRAPH::CurrentGraph();
        const bool fullscreenWindow = graph->GraphFlag34Bit7();
        const DWORD style = fullscreenWindow ? kFullscreenStyle : kWindowedStyle;
        const int x = fullscreenWindow ? 0 : config.video.windowPositionX;
        const int y = fullscreenWindow ? 0 : config.video.windowPositionY;
        const int width = graph->SizeX();
        const int height = graph->SizeY();

        as1::core::BindApplicationPhysicalOwner(this);
        mainWindowHandle(this) = CreateWindowExA(kMainWindowExStyle,
                                className,
                                title,
                                style,
                                x,
                                y,
                                width,
                                height,
                                nullptr,
                                nullptr,
                                applicationInstanceHandle(this),
                                nullptr);
        return mainWindowHandle(this);
    }

    HACCEL ApplicationWin::loadMainAccelerators()
    {

        acceleratorHandle(this) = LoadAcceleratorsA(applicationInstanceHandle(this), kAcceleratorResourceName);
        ShowCursor(FALSE);
        return acceleratorHandle(this);
    }

    void ApplicationWin::showMainWindow(int nShowCmd)
    {

        ShowWindow(mainWindowHandle(this), nShowCmd);
        UpdateWindow(mainWindowHandle(this));
    }

    bool ApplicationWin::dispatchWindowMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LRESULT& result)
    {

        result = 0;

        if ((flags() & kApplicationFramePumpFlag) != 0u)
        {
            as1::input::InputWindowMessageContext inputContext{};
            inputContext.getWindowRect = applicationInputGetWindowRect;
            GRAPH* const graph = GRAPH::CurrentGraph();
            const GraphViewportState& viewport = graph->viewportState();
            inputContext.viewportMinX = viewport.left;
            inputContext.viewportMaxX = viewport.right;
            inputContext.viewportMinY = viewport.top;
            inputContext.viewportMaxY = viewport.bottom;
            inputContext.worldOffsetX = as1::core::GlobalApplicationDrawDispatcherState().cameraShiftX();
            inputContext.worldOffsetY = as1::core::GlobalApplicationDrawDispatcherState().cameraShiftY();
            inputContext.hasViewport = true;
            const int inputHandled = as1::input::HandleInputWindowMessage(
                inputState(this),
                reinterpret_cast<std::uintptr_t>(hwnd),
                static_cast<std::uint32_t>(msg),
                static_cast<std::uint32_t>(wparam),
                static_cast<std::uint32_t>(lparam),
                &inputContext);
            if (inputHandled != 0)
            {
                result = 1;
                return true;
            }
        }

        GRAPH* const graph = GRAPH::CurrentGraph();

        if (msg > 0x211u)
        {
            if (msg == WM_EXITMENULOOP || msg == WM_EXITSIZEMOVE)
            {
                graph->leaveModalRenderState();
                as1::sound::GlobalSoundEngine()->resumeAllPlayback();
            }
            else if (msg == WM_ENTERSIZEMOVE)
            {
                graph->enterModalRenderState();
                as1::sound::GlobalSoundEngine()->pauseAllPlayback();
            }
            return false;
        }

        if (msg == WM_ENTERMENULOOP)
        {
            graph->enterModalRenderState();
            as1::sound::GlobalSoundEngine()->pauseAllPlayback();
            return false;
        }

        if (msg > WM_ACTIVATEAPP)
        {
            if (msg == WM_SYSCOMMAND)
            {
                const std::uintptr_t command = static_cast<std::uintptr_t>(wparam);
                if ((command == 0xF000u || command == 0xF010u || command == 0xF030u || command == 0xF170u) &&
                    graph->GraphFlag34Bit7())
                {
                    result = 1;
                    return true;
                }
            }
            return false;
        }

        if (msg == WM_ACTIVATEAPP)
        {
            const std::uint32_t activationFlags =
                (flags() & 0xFFFFFFF7u) | (wparam != 0 ? kApplicationFramePumpFlag : 0u);
            setFlags(activationFlags);
            if (as1::sound::GlobalSoundEngine() && Mouse)
            {
                if ((flags() & kApplicationFramePumpFlag) != 0u)
                {
                    as1::sound::GlobalSoundEngine()->resumeAllPlayback();
                    Mouse->HardwareOn();
                }
                else
                {
                    as1::sound::GlobalSoundEngine()->pauseAllPlayback();
                    Mouse->HardwareOff();
                }
            }
            return false;
        }

        if (msg == WM_DESTROY)
        {
            if (!graph->GraphFlag34Bit7())
            {
                RECT rect;
                ::GetWindowRect(mainWindowHandle(this), &rect);
                as1::STRING nameX("WindowPositionX");
                as1::core::StartupRegistryPath().WriteRegistryInt(nameX, rect.left);
                as1::STRING nameY("WindowPositionY");
                as1::core::StartupRegistryPath().WriteRegistryInt(nameY, rect.top);
            }
            mainWindowHandle(this) = nullptr;
            ::PostQuitMessage(0);
            return false;
        }

        if (msg == WM_PAINT)
        {
            LOG::Write("WM_PAINT");
            return false;
        }

        return false;
    }

    LRESULT CALLBACK applicationWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
    {

        ApplicationWin* const app = applicationWinInstance();
        LRESULT handledResult = 0;
        if (app->dispatchWindowMessage(hwnd, msg, wparam, lparam, handledResult))
        {
            return 1;
        }

        if (msg != WM_COMMAND)
        {
            const LRESULT defResult = ::DefWindowProcA(hwnd, msg, wparam, lparam);
            return defResult;
        }

        switch (static_cast<unsigned int>(LOWORD(wparam)))
        {
        case IDM_FILE_LOAD:
            app->commandLoadMapFiles();
            return 0;
        case IDM_FILE_SAVE:
            app->commandSaveCurrent();
            return 0;
        case IDM_FILE_EXIT:
            app->commandRequestExit(hwnd);
            return 0;
        case IDM_OPTIONS_ACCEL:
            app->commandWriteScreenshot();
            return 0;
        default:
            return 0;
        }
    }

    void ApplicationWin::selectMapFilePath(as1::STRING& outPath, bool saveDialog, const char* filter)
    {

        char fileName[4096] = {};
        OPENFILENAMEA ofn{};
        ofn.lStructSize = 76u;
        ofn.hwndOwner = mainWindowHandle(this);
        ofn.hInstance = applicationInstanceHandle(this);
        ofn.lpstrFilter = filter;
        ofn.lpstrCustomFilter = nullptr;
        ofn.nMaxCustFilter = 0u;
        ofn.nFilterIndex = 1u;
        ofn.lpstrFile = fileName;
        ofn.nMaxFile = 4096u;
        ofn.lpstrFileTitle = nullptr;
        ofn.nMaxFileTitle = 0u;
        ofn.lpstrInitialDir = "maps";
        ofn.lpstrTitle = nullptr;
        ofn.Flags = saveDialog ? 0x0008080Cu : 0x0008180Cu;
        ofn.nFileOffset = 0u;
        ofn.nFileExtension = 0u;
        ofn.lpstrDefExt = "map";
        ofn.lpfnHook = nullptr;
        ofn.lpTemplateName = nullptr;

        GRAPH* const graph = GRAPH::CurrentGraph();

        graph->enterModalRenderState();
        const BOOL selected = saveDialog ? ::GetSaveFileNameA(&ofn) : ::GetOpenFileNameA(&ofn);
        graph->leaveModalRenderState();

        outPath = selected ? fileName : "";
    }

    void ApplicationWin::commandLoadMapFiles()
    {

        static const char mapOpenFilter[] =
            "Map Files\0*.map\0"
            "Save Files\0*.sav\0"
            "Demo Files\0*.dem\0"
            "All Files\0*.*\0\0";

        STRING selected;
        selectMapFilePath(selected, false, mapOpenFilter);
        runCommandLineMap(selected.DetachOwnedStorage());
    }

    void ApplicationWin::saveMap(const as1::STRING& outputName)
    {

        RESOURCE output;
        if (std::strcmp(outputName.c_str(), STRING::SharedEmptyText()) == 0)
            return;

        static const char kTemporaryMapName[] = "tmp_del!.map";
        STRING& activeMapName = currentMapName(this);
        const bool replacingCurrentMap = std::strcmp(outputName.c_str(), activeMapName.c_str()) == 0;
        if (replacingCurrentMap)
        {
            std::rename(activeMapName.c_str(), kTemporaryMapName);
            assignStringFromCString(activeMapName, kTemporaryMapName);
        }

        if (!output.openFileForWrite(outputName, RESOURCE::ResTypes::MAP))
        {
            LOG::Write("Can't open file %s", outputName.c_str());
            return;
        }

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
            if (previous.openFile(activeMapName, RESOURCE::ResTypes::MAP))
            {
                output.CopySectionTypeFrom(previous, RESOURCE::ResTypes::WEAPON);
                output.CopySectionTypeFrom(previous, RESOURCE::ResTypes::OBJECT);
                previous.clear();
            }
            else
            {
                LOG::Write("Can't open file %s", activeMapName.c_str());
            }
        }

        output.BeginSection(RESOURCE::ResTypes::GRAPH);
        GRAPH::CurrentGraph()->saveGraphParameters(&output);
        output.EndSection();

        output.BeginSection(RESOURCE::ResTypes::HEAD);
        const float sizeX = core::ApplicationMapWidth();
        const float sizeY = core::ApplicationMapHeight();
        const float shiftX = core::GlobalApplicationDrawDispatcherState().cameraShiftX();
        const float shiftY = core::GlobalApplicationDrawDispatcherState().cameraShiftY();
        output.write(&sizeX, 4u);
        output.write(&sizeY, 4u);
        output.write(&shiftX, 4u);
        output.write(&shiftY, 4u);
        const std::uint32_t now = core::CurrentTimeMilliseconds();
        output.write(&now, 4u);
        const int retailVersion = 10;
        output.write(&retailVersion, 4u);
        output.EndSection();

        output.BeginSection(RESOURCE::ResTypes::GRID);
        if (const short* const grid = core::ApplicationTerrainGrid())
        {
            const unsigned bytes = static_cast<unsigned>(
                2u * static_cast<unsigned>(core::ApplicationTerrainGridWidth()) *
                static_cast<unsigned>(core::ApplicationTerrainGridHeight()));
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
                    if (!sprite || sprite->childBacklink() || frameList.contains(sprite))
                        continue;
                    fn(sprite);
                }
            }
        };

        output.BeginSection(RESOURCE::ResTypes::SPRITE);
        forEachRetailSaveSprite([&](SPRITE* sprite) { sprite->serializeSpriteRecord(&output); });
        const std::int32_t spriteTerminator = -1;
        output.write(&spriteTerminator, 4u);
        output.EndSection();

        forEachRetailSaveSprite([&](SPRITE* sprite)
        {
            const std::size_t begin = output.position();
            output.BeginSection(RESOURCE::ResTypes::SPRITEDATA);
            const std::uint32_t spritePointerValue = static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(sprite) & 0xFFFFFFFFu);
            output.write(&spritePointerValue, 4u);
            sprite->Action(static_cast<int>(ActionCode::ACT_SAVE), reinterpret_cast<std::intptr_t>(&output), 0, 0);
            if (output.position() > begin + 5u)
                output.EndSection();
        });
        output.BeginSection(RESOURCE::ResTypes::SPRITEDATA);
        output.write(&spriteTerminator, 4u);
        output.EndSection();

        output.BeginSection(RESOURCE::ResTypes::PLAY);
        PLAYER* const players[4] = {
            playerPointerSlot(this, playerSlotOffset(0)),
            playerPointerSlot(this, playerSlotOffset(1)),
            playerPointerSlot(this, playerSlotOffset(2)),
            playerPointerSlot(this, playerSlotOffset(3))
        };
        for (PLAYER* const player : players)
            player->saveControlledSpriteReference(&output);
        output.EndSection();

        output.BeginSection(RESOURCE::ResTypes::GROUP);
        reinterpret_cast<GROUPS*>(reinterpret_cast<std::uint8_t*>(this) + core::retail_application_layout::Groups)->Save(&output);
        output.EndSection();
        output.clear();

        if (std::strcmp(activeMapName.c_str(), kTemporaryMapName) == 0)
        {
            std::remove(kTemporaryMapName);
            assignStringFromString(activeMapName, outputName);
        }
    }

    void ApplicationWin::commandSaveCurrent()
    {

        saveMap(STRING("current.sav"));
    }

    void ApplicationWin::commandWriteScreenshot()
    {

        STRING date;
        constructCurrentDateString(date);
        STRING screensAndDate;
        constructConcatenatedString(screensAndDate, "Screens\\", date.c_str());

        STRING datedPrefix;
        constructConcatenatedString(datedPrefix, screensAndDate.c_str(), " ");

        STRING time;
        constructCurrentTimeString(time);
        STRING dateAndTime;
        constructConcatenatedString(dateAndTime, datedPrefix.c_str(), time.c_str());

        STRING outputPath;
        constructConcatenatedString(outputPath, dateAndTime.c_str(), ".tga");
        replaceStringFirst(outputPath, ":", "h");
        replaceStringFirst(outputPath, ":", "m");
        replaceStringFirst(outputPath, ":", "s");

        GRAPH* const graph = GRAPH::CurrentGraph();
        graph->captureBackBufferRegionTga(outputPath,
                          0,
                          0,
                          static_cast<int>(graph->screenWidth()),
                          static_cast<int>(graph->screenHeight()));
    }

    void ApplicationWin::commandRequestExit(HWND hwnd)
    {
        PostMessageA(hwnd, WM_CLOSE, 0, 0);
    }

    void ApplicationWin::bindNativeWindow(HWND hwnd, HACCEL accelerator) noexcept
    {

        mainWindowHandle(this) = hwnd;
        acceleratorHandle(this) = accelerator;
    }

    void ApplicationWin::setFramePumpEnabled(bool enabled) noexcept
    {
        if (enabled)
            addFlags(kApplicationFramePumpFlag);
        else
            removeFlags(kApplicationFramePumpFlag);
    }

    void ApplicationWin::setRenderControlsEnabled(bool enabled) noexcept
    {
        if (enabled)
            addFlags(kApplicationRenderControlsFlag);
        else
            removeFlags(kApplicationRenderControlsFlag);
    }

    void ApplicationWin::storeInputState(const as1::input::InputMessageState& state) noexcept
    {

        inputState(this) = state;
    }

    void ApplicationWin::allocateStartupPlayerControls()
    {

        PLAYER* player0 = new (std::nothrow) PLAYER();
        playerPointerSlot(this, playerSlotOffset(0)) = player0 ? player0->initializeActivePlayerState(1, 0) : nullptr;

        PLAYER* player2 = new (std::nothrow) PLAYER();
        playerPointerSlot(this, playerSlotOffset(2)) = player2 ? player2->initializeActivePlayerState(0, 2) : nullptr;

        PLAYER* player1 = new (std::nothrow) PLAYER();
        playerPointerSlot(this, playerSlotOffset(1)) = player1 ? player1->initializeActivePlayerState(2, 1) : nullptr;

        PLAYER* player3 = new (std::nothrow) PLAYER();
        playerPointerSlot(this, playerSlotOffset(3)) = player3 ? player3->initializeActivePlayerState(0, 3) : nullptr;
    }

    void ApplicationWin::copyStartupShellMapFields() noexcept
    {

        physicalFloatSlot(this, core::retail_application_layout::ScrollMaxX) = as1::core::ApplicationMapWidth();
        physicalFloatSlot(this, core::retail_application_layout::ScrollMaxY) = as1::core::ApplicationMapHeight();
        physicalFloatSlot(this, core::retail_application_layout::ScrollMinX) = 0.0f;
        physicalFloatSlot(this, core::retail_application_layout::ScrollMinY) = 0.0f;
    }

    const as1::input::InputMessageState& ApplicationWin::inputStateSnapshot() const noexcept
    {
        return inputState(this);
    }

    std::uint32_t ApplicationWin::activeStartupPlayerIndex() const noexcept { return as1::core::ActivePlayerIndex(); }

    bool ApplicationWin::initialized() const noexcept
    {
        return (flags() & kApplicationInitializedFlag) != 0;
    }

    bool ApplicationWin::wantsStartupPump() const noexcept
    {
        return initialized() && mainWindowHandle(this) != nullptr;
    }

    void ApplicationWin::clearStartupShellNibble() noexcept
    {
        shellFlagsStorage(this) &= ~kShellLowNibbleMask;
    }

    HINSTANCE ApplicationWin::instance() const noexcept
    {
        return applicationInstanceHandle(this);
    }

    HWND ApplicationWin::nativeWindow() const noexcept
    {
        return mainWindowHandle(this);
    }

    HACCEL ApplicationWin::accelerator() const noexcept
    {
        return acceleratorHandle(this);
    }

    std::string ApplicationWin::commandLine() const
    {
        return pendingCommand(this).str();
    }

    std::string ApplicationWin::shellString() const
    {
        return currentMapName(this).str();
    }

    std::uint32_t ApplicationWin::flags() const noexcept
    {
        return *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::uint8_t*>(this) + core::retail_application_layout::Flags);
    }

    std::uint32_t ApplicationWin::shellFlags() const noexcept
    {
        return shellFlagsStorage(this);
    }

    std::uint32_t ApplicationWin::frameCounter() const noexcept
    {
        return as1::core::ApplicationWorldFrameCounter();
    }

    void ApplicationWin::resetWorldFrameCounter() noexcept
    {

        as1::core::SetApplicationWorldFrameCounter(0u);
    }

    void ApplicationWin::resetTickScale() noexcept
    {

        as1::core::SetApplicationTickScale(1.0f);
    }

    void ApplicationWin::setWorldStartTime(std::uint32_t value) noexcept
    {
        as1::core::SetApplicationWorldStartTime(value);
    }

    std::uint32_t ApplicationWin::worldStartTime() const noexcept
    {
        return as1::core::ApplicationWorldStartTime();
    }

    std::uint32_t ApplicationWin::tickAccumulator() const noexcept
    {
        return as1::core::CurrentTimeMilliseconds();
    }

    int ApplicationWin::lastVirtualKey() const noexcept
    {
        return static_cast<int>(inputState(this).lastCode);
    }

    bool ApplicationWin::pumpNativeMessages()
    {
        updateFrameClock();
        inputState(this).resetFrameState();
        compactDeferredObjectListsIfNeeded();

        if (flags() & kApplicationCommandLinePendingFlag)
        {

            setFlags(flags() & ~kApplicationCommandLinePendingFlag);
            STRING pendingCopy(pendingCommand(this));
            runCommandLine(pendingCopy.DetachOwnedStorage());
        }

        MSG msg{};
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                return true;
            }

            const bool translatedByAccelerator = mainWindowHandle(this) &&
                TranslateAcceleratorA(mainWindowHandle(this), acceleratorHandle(this), &msg) != 0;
            if (translatedByAccelerator)
                continue;

            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        return false;
    }

    void ApplicationWin::updateFrameClock()
    {

        const std::uint32_t sampleForDelta = static_cast<std::uint32_t>(::timeGetTime());
        std::uint32_t elapsed = sampleForDelta - as1::core::RealTimeMilliseconds();
        as1::core::SetRealTimeMilliseconds(static_cast<std::uint32_t>(::timeGetTime()));

        const std::uint32_t current = as1::core::CurrentTimeMilliseconds();
        as1::core::SetPreviousWorldTimeMilliseconds(current);
        if (elapsed > kFrameClampMs)
            elapsed = kFrameClampMs;

        const std::uint32_t increment = frameElapsedScaleFtolLow32(
            elapsed, as1::core::ApplicationTickScale());
        as1::core::SetCurrentTimeMilliseconds(current + increment);
    }

    void ApplicationWin::compactDeferredObjectListsIfNeeded()
    {

        if ((as1::core::CurrentTimeMilliseconds() & 3u) != 0u)
            return;
        as1::core::ApplicationDrawDispatcherState& state =
            as1::core::GlobalApplicationDrawDispatcherState();
        for (int pass = 0; pass < as1::core::ApplicationDrawDispatcherState::PassCount; ++pass)
            state.drawPassBucket(pass).compactSparse();
    }

    void ApplicationWin::reportNoVidReleaseResidue()
    {

        const auto& vidTable = as1::core::GlobalApplicationVidTable();
        std::uint32_t residueCount = 0;
        const int noVid = vidTable.count();
        for (int index = 0; index < noVid && static_cast<std::size_t>(index) < vidTable.capacity(); ++index)
        {
            const VID* vid = vidTable.slot(index);
            if (!vid)
                continue;

            const DWORD usage0 = vid->spriteCountForArmy(0);
            const DWORD usage1 = vid->spriteCountForArmy(1);
            const DWORD usage2 = vid->spriteCountForArmy(2);
            const DWORD usage3 = vid->spriteCountForArmy(3);
            const DWORD total = usage0 + usage1 + usage2 + usage3;
            if (total == 0)
                continue;

            ++residueCount;
            LOG::Write("NoVid[%3i]=%i\t%i %i %i %s Layer=%i %s",
                       index,
                       static_cast<int>(usage0),
                       static_cast<int>(usage1),
                       static_cast<int>(usage2),
                       static_cast<int>(usage3),
                       vid->scriptName().c_str(),
                       vid->renderLayer(),
                       vid->sourceVidPath().c_str());
        }
        (void)residueCount;
    }

    void ApplicationWin::releaseBaseApplicationRuntime()
    {

        releaseWorldRuntime();
    }

    void ApplicationWin::dispatchShellCommandKey()
    {
        const std::uint32_t lastCode = inputState(this).lastCode;
        if (!lastCode)
            return;

        switch (lastCode)
        {
        case VK_F15:
            MAP::Current()->reloadGameResourceParameters();
            break;
        case 'O':

            toggleFlags(0x00008800u);
            break;
        case 'P':
            toggleFlag(shellFlagsStorage(this), kShellTogglePause);
            break;
        case 'I':
            toggleFlags(0x00001000u);
            break;
        case 'H':
            toggleFlag(shellFlagsStorage(this), kShellDrawLabels);
            break;
        case 'R':
            toggleFlags(0x00002000u);
            break;
        case 'G':
            toggleFlag(shellFlagsStorage(this), kShellDispatchOverlayList);
            break;
        default:
            break;
        }
    }
bool ApplicationWin::shouldWaitForMessageGate() const noexcept
    {
        const bool debugMode = as1::GlobalBaseConstants() && as1::GlobalBaseConstants()->raw[10] != 0;
        return shouldWaitForMessageForDebugGate(debugMode);
    }
bool ApplicationWin::shouldWaitForMessage() const noexcept
    {
        const bool debugMode = as1::GlobalBaseConstants() && as1::GlobalBaseConstants()->raw[10] != 0;
        return shouldWaitForMessageForDebugGate(debugMode);
    }

    bool ApplicationWin::shouldWaitForMessageForDebugGate(bool debugMode) const noexcept
    {
        if ((flags() & kApplicationFramePumpFlag) || (shellFlagsStorage(this) & kShellForceFrame))
        {
            GRAPH* const graph = GRAPH::CurrentGraph();

            if (graph && graph->GraphFlag34Bit0())
                return true;
            if (debugMode && (shellFlagsStorage(this) & kShellTogglePause) && inputState(this).lastCode != 0x70u)
                return true;
            return false;
        }
        return true;
    }

    bool ApplicationWin::shouldAbortBeforeFrameCounter() const noexcept
    {

        if ((flags() & kApplicationFramePumpFlag) == 0)
            return false;
        GRAPH* graph = GRAPH::CurrentGraph();
        if (!graph)
            return true;
        return graph->beginSceneWithDeviceRecovery() != 0;
    }

    bool ApplicationWin::incrementFrameCounterAndBuildWorldTick()
    {

        as1::core::SetApplicationWorldFrameCounter(
            as1::core::ApplicationWorldFrameCounter() + 1u);
        return (flags() & kApplicationFramePumpFlag) && processDemoFrame() != 0;
    }

    int ApplicationWin::processDemoFrame()
    {

        int result = 1;
        MAP* const map = MAP::Current();
        RESOURCE& demo = map->demoResource();

        if ((flags() & application_flags::DemoUseResource) != 0u)
        {
            std::int32_t frameTime = -1;
            demo.read(&frameTime, sizeof(frameTime));

            if (frameTime != -1 && inputState(this).lastCode == 0u &&
                (inputState(this).flags & 0x05u) == 0u)
            {
                inputState(this).readRawState(&demo);

                const std::uint32_t recorded = static_cast<std::uint32_t>(frameTime);
                std::uint32_t baseRecorded = as1::core::DemoRecordedTimeBaseMilliseconds();
                if (recorded - baseRecorded > kFrameClampMs)
                {
                    baseRecorded = recorded - kFrameClampMs;
                    as1::core::SetDemoRecordedTimeBaseMilliseconds(baseRecorded);
                }

                const std::uint32_t realBase = as1::core::DemoRealTimeBaseMilliseconds();
                std::uint32_t realElapsed = static_cast<std::uint32_t>(::timeGetTime()) - realBase;
                const std::uint32_t recordedElapsed = recorded - baseRecorded;
                if (recordedElapsed <= realElapsed)
                {
                    result = (as1::core::CurrentTimeMilliseconds() - recorded) <= kDemoFrameToleranceMs ? 1 : 0;
                }
                else
                {
                    do
                    {
                        realElapsed = static_cast<std::uint32_t>(::timeGetTime()) - realBase;
                    } while (recordedElapsed >= realElapsed);
                }

                as1::core::SetCurrentTimeMilliseconds(recorded);
                as1::core::SetDemoRecordedTimeBaseMilliseconds(recorded);
                as1::core::SetDemoRealTimeBaseMilliseconds(static_cast<std::uint32_t>(::timeGetTime()));
            }
            else
            {
                STRING nextMap;

                (void)demo.GoNext(RESOURCE::ResTypes::DEMO);
                demo.shift(static_cast<int>(demo.CurrentResourceSize()));
                readStringLineFromStream(nextMap, &demo);

                if (nextMap.isEmpty())
                {
                    PostMessageA(mainWindowHandle(this), WM_CLOSE, 0, 0);
                }
                else
                {
                    closeResourceOwner(demo);
                    Mouse->HardwareOn();
                    removeFlags(application_flags::DemoUseResource);
                    addFlags(kApplicationCommandLinePendingFlag);
                    assignStringFromString(pendingCommand(this), nextMap);
                }
                return 0;
            }
        }

        if ((flags() & application_flags::DemoWriteToResource) != 0u)
        {
            const std::uint32_t worldTime = as1::core::CurrentTimeMilliseconds();
            demo.write(&worldTime, sizeof(worldTime));
            inputState(this).writeRawState(&demo);
        }
        return result;
    }

    void ApplicationWin::frameDispatchPrologue(bool worldTick)
    {
        (void)worldTick;

        (void)as1::core::Application::callScriptFunction(-1, 0, 0);
    }

    float ApplicationWin::mapExtentX() const noexcept
    {

        return physicalFloatSlot(this, core::retail_application_layout::MapExtentX);
    }

    float ApplicationWin::mapExtentY() const noexcept
    {

        return physicalFloatSlot(this, core::retail_application_layout::MapExtentY);
    }

    PLAYER* ApplicationWin::playerSlotByIndex(int index) const noexcept
    {

        return playerPointerSlot(this, playerSlotOffset(index));
    }

    PLAYER* ApplicationWin::startupPlayerSlotByIndex(int index) const noexcept
    {
        return playerSlotByIndex(index);
    }

    SPRITE* ApplicationWin::controlledSpriteForPlayer(int index) const noexcept
    {

        PLAYER* const player = startupPlayerSlotByIndex(index);
        return player->controlledSprite();
    }

    SPRITE* ApplicationWin::activeAuxiliarySprite() const noexcept
    {

        PLAYER* const player = startupPlayerSlotByIndex(static_cast<int>(as1::core::ActivePlayerIndex()));
        return player->auxiliarySprite();
    }

    void ApplicationWin::dispatchSpriteCommandMask(SPRITE* target, const std::uint32_t* commandMask) const noexcept
    {
        if (target)
            target->dispatchSpriteCommandMask(commandMask);
    }

    SPRITE* ApplicationWin::childCommandTargetByVidSlot5C(SPRITE* target) const noexcept
    {

        if (!target)
            return nullptr;
        SPRITE* child = target->childChain();
        if (!child)
            return nullptr;
        // Retail dereferences target+0x1C unconditionally after the target and
        // child gates.  Do not insert a null-VID recovery path here: a null
        // target VID is an original fault boundary and must not silently skip
        // the child command.
        VID* const targetVid = target->Vid();
        return (child->Vid() == targetVid->linkedVid()) ? child : nullptr;
    }

    void ApplicationWin::dispatchChildCommandIfVidSlot5CMatches(SPRITE* target, const std::uint32_t* commandMask) const noexcept
    {
        if (SPRITE* child = childCommandTargetByVidSlot5C(target))
            dispatchSpriteCommandMask(child, commandMask);
    }

    void ApplicationWin::dispatchPrimaryControlMovementMask()
    {

        SPRITE* target = controlledSpriteForPlayer(static_cast<int>(as1::core::ActivePlayerIndex()));
        if (!target || (target->armyBits()) != 0)
            return;

        const BASE_CONSTANTS* constants = as1::GlobalBaseConstants();
        const std::uint32_t packed = constants ? constants->raw[20] : 0;
        std::uint32_t positive = 0;
        std::uint32_t negative = 0;
        splitPackedDirectionalMask(packed, positive, negative);
        const std::uint32_t commandMask[2] = {positive, negative};
        dispatchSpriteCommandMask(target, commandMask);
        dispatchChildCommandIfVidSlot5CMatches(target, commandMask);
    }

    void ApplicationWin::dispatchAuxiliaryControlMovementMask()
    {

        SPRITE* target = activeAuxiliarySprite();
        const BASE_CONSTANTS* constants = as1::GlobalBaseConstants();
        if (!target || !constants || (flags() & 0x00040000u) == 0)
            return;

        const std::uint32_t maskedFlags = target->armyBits();
        const std::uint32_t packed = (maskedFlags == 0x00000400u) ? constants->raw[21] : constants->raw[22];
        std::uint32_t positive = 0;
        std::uint32_t negative = 0;
        splitPackedDirectionalMask(packed, positive, negative);
        const std::uint32_t commandMask[2] = {positive, negative};
        dispatchSpriteCommandMask(target, commandMask);
        dispatchChildCommandIfVidSlot5CMatches(target, commandMask);
    }

    void ApplicationWin::graphFrameDispatch(bool worldTick)
    {

        if (GRAPH* graph = GRAPH::CurrentGraph())
            graph->runFrameService(worldTick ? 1 : 0);
    }

    void ApplicationWin::clearControlMovementMasksAfterGraph()
    {

        const std::uint32_t zeroMask[2] = {0u, 0u};

        SPRITE* primary = controlledSpriteForPlayer(static_cast<int>(as1::core::ActivePlayerIndex()));
        if (primary)
        {
            primary = controlledSpriteForPlayer(static_cast<int>(as1::core::ActivePlayerIndex()));
            if ((primary->armyBits()) == 0u)
            {
                primary = controlledSpriteForPlayer(static_cast<int>(as1::core::ActivePlayerIndex()));
                dispatchSpriteCommandMask(primary, zeroMask);

                primary = controlledSpriteForPlayer(static_cast<int>(as1::core::ActivePlayerIndex()));
                if (childCommandTargetByVidSlot5C(primary))
                {
                    primary = controlledSpriteForPlayer(static_cast<int>(as1::core::ActivePlayerIndex()));
                    dispatchSpriteCommandMask(primary->childChain(), zeroMask);
                }
            }
        }

        SPRITE* auxiliary = activeAuxiliarySprite();
        if (!auxiliary)
            return;

        auxiliary = activeAuxiliarySprite();
        dispatchSpriteCommandMask(auxiliary, zeroMask);

        auxiliary = activeAuxiliarySprite();
        if (childCommandTargetByVidSlot5C(auxiliary))
        {
            auxiliary = activeAuxiliarySprite();
            dispatchSpriteCommandMask(auxiliary->childChain(), zeroMask);
        }
    }

    void ApplicationWin::dispatchSpriteFrameBuckets(bool worldTick)
    {
        (void)worldTick;

        if ((flags() & kApplicationModalDispatchFlag) != 0u)
        {
            SPRITE_LIST& list = applicationFrameSpriteList();
            for (int cursor = list.activeCount() - 1; cursor >= 0; --cursor)
            {
                if (SPRITE* const sprite = list.at(static_cast<std::size_t>(cursor)))
                    sprite->Tact();
            }
            return;
        }

        as1::core::ApplicationDrawDispatcherState& state =
            as1::core::GlobalApplicationDrawDispatcherState();
        for (int pass = 0; pass < 13; ++pass)
        {
            const as1::core::ApplicationDrawPassBucket& bucket = state.drawPassBucket(pass);
            for (int cursor = bucket.count() - 1; cursor >= 0; --cursor)
            {
                if (SPRITE* const sprite = bucket.spriteAt(cursor))
                    sprite->Tact();
            }
        }
    }

    void ApplicationWin::prepareInputAndDispatchControls(bool worldTick)
    {

        dispatchInputControls(worldTick);
    }

    void ApplicationWin::conditionalOverlayDrawAndPresent(bool worldTick)
    {

        if (!worldTick)
            return;

        const std::uint32_t applicationFlags = flags();
        const bool controlPath = ((applicationFlags & kApplicationModalDispatchFlag) == 0u) &&
                                 ((applicationFlags & kApplicationRenderControlsFlag) != 0u);
        if (controlPath || (applicationFlags & 0x00008000u) != 0u)
            drawShellOverlays();

        if (GRAPH* graph = GRAPH::CurrentGraph())
            graph->endSceneAndPresentRetail(1);
    }

    void ApplicationWin::soundTick()
    {

        if (as1::sound::Engine* engine = as1::sound::GlobalSoundEngine())
            engine->updateSoundRequests();
    }

    int ApplicationWin::selectFrameSprite() noexcept
    {
        // Retail caller: ECX = Application+0x24C BaseSpriteList, argument =
        // Application+0x230 InputMessageState.
        SPRITE_LIST::SelectionInputRoute route{};
        route.flags = inputState(this).flags;
        route.probeX = inputState(this).worldX;
        route.probeY = inputState(this).worldY;
        route.user = &inputState(this);
        route.clearInputTransient = [](void* user, int buttonIndex)
        {
            auto* input = static_cast<as1::input::InputMessageState*>(user);
            if (!input)
                return;
            if (buttonIndex == 0)
                input->clearFirstButtonTransient();
            else
                input->clearSecondButtonTransient();
        };
        route.changeSpriteAnimation = [](void*, SPRITE* sprite, int animation)
        {
            if (sprite)
                sprite->ChangeAnimation(animation);
        };
        return applicationFrameSpriteList().selectSpriteByInput(route);
    }

    as1::STRING* ApplicationWin::buildMouseTipText(as1::STRING* out)
    {

        as1::STRING text;
        if ((flags() & kApplicationModalDispatchFlag) == 0u)
        {
            PLAYER* const player = startupPlayerSlotByIndex(
                static_cast<int>(as1::core::ActivePlayerIndex()));
            as1::STRING playerText;
            player->getAuxiliaryUnitName(&playerText);
            copyConstructString(text, playerText);
        }

        SPRITE_LIST& list = applicationFrameSpriteList();
        if (text.isEmpty() && list.selectedSprite())
        {
            char number[0x80]{};
            _itoa(list.selectedSpriteNvid(), number, 10);

            as1::STRING nvidText;
            constructStringFromCString(nvidText, number);
            as1::STRING menuVid;
            constructConcatenatedString(menuVid, "MenuVid", nvidText.c_str());

            as1::STRING defaultValue;
            as1::STRING section("MouseTips");
            as1::STRING allDirKey;
            constructConcatenatedString(allDirKey, menuVid.c_str(), "AllDir");
            as1::STRING profileValue;
            as1::core::profile_p::readProfileStringInto(
                profileValue,
                as1::core::StartupStringsIniPath(),
                section,
                allDirKey,
                defaultValue);
            copyConstructString(text, profileValue);

            if (text.isEmpty())
            {
                _itoa(list.selectedSpriteDirectionFrame(), number, 10);
                as1::STRING directionText;
                constructStringFromCString(directionText, number);
                as1::STRING dirPrefix;
                constructConcatenatedString(dirPrefix, menuVid.c_str(), "Dir");
                as1::STRING dirKey;
                constructConcatenatedString(dirKey, dirPrefix.c_str(), directionText.c_str());
                as1::STRING dirProfileValue;
                as1::core::profile_p::readProfileStringInto(
                    dirProfileValue,
                    as1::core::StartupStringsIniPath(),
                    section,
                    dirKey,
                    defaultValue);
                copyConstructString(text, dirProfileValue);
            }
        }

        copyConstructString(*out, text);
        return out;
    }

    void ApplicationWin::dispatchInputOwner2294() noexcept
    {

        (void)cachedTooltipText();
        const std::uint32_t now = as1::core::RealTimeMilliseconds();
        if (g_tooltipLastClientX != inputState(this).clientX ||
            g_tooltipLastClientY != inputState(this).clientY)
        {
            g_tooltipLastUpdateTime = now;
            g_tooltipLastClientX = inputState(this).clientX;
            g_tooltipLastClientY = inputState(this).clientY;
        }

        const BASE_CONSTANTS* const constants = as1::GlobalBaseConstants();
        const std::uint32_t idleThreshold = constants->raw[0x34u / sizeof(DWORD)];
        if (now - g_tooltipLastUpdateTime <= idleThreshold ||
            (inputState(this).flags & 1u) != 0u ||
            inputState(this).lastCode != 0u ||
            (applicationFrameSpriteList().selectionFlags() & 1u) != 0u ||
            (flags() & 0x00040000u) == 0u)
        {
            releaseShellOwnedSpriteOwner(this);
            return;
        }

        if (shellOwnedSpriteEmpty(this))
        {
            as1::core::ApplicationVidTable& table = as1::core::GlobalApplicationVidTable();
            VID* tooltipVid = MAP::NullVid();
            if (table.count() > 6)
            {
                if (VID* const tooltipTemplateVid = table.slot(6))
                    tooltipVid = tooltipTemplateVid;
            }

            as1::STRING text;
            buildMouseTipText(&text);
            copyConstructString(cachedTooltipText(), text);
            if (tooltipVid == MAP::NullVid() || cachedTooltipText().isEmpty())
                return;

            float x = inputState(this).clientX + 5.0f;
            float y = inputState(this).clientY - tooltipVid->sizeY() + 3000.0f - 10.0f;
            GRAPH* const graph = GRAPH::CurrentGraph();
            const float halfHeight = tooltipVid->sizeY() * 0.5f;
            if (!(static_cast<float>(graph->getViewportTop()) < (halfHeight + y - 3000.0f)))
                y = halfHeight + inputState(this).clientY + 3010.0f;

            const std::size_t glyphCount = std::strlen(cachedTooltipText().c_str()) + 2u;
            const float lineRight = static_cast<float>(glyphCount) * tooltipVid->sizeX() + x;
            if (lineRight > static_cast<float>(graph->getViewportRight()))
                x = static_cast<float>(graph->getViewportRight()) -
                    static_cast<float>(glyphCount) * tooltipVid->sizeX();

            MAP* const map = MAP::Current();
            SPRITE* const created = map->CreateSpriteViaFactory(
                tooltipVid, VECTOR{x, y, 3000.0f}, ANGLE(0), nullptr, false);
            bindShellOwnedSprite(this, created);
            if (!created)
                return;

            as1::STRING open;
            constructConcatenatedString(open, "{", cachedTooltipText().c_str());
            as1::STRING wrapped;
            constructConcatenatedString(wrapped, open.c_str(), "}");
            created->dispatchVirtualAction(ActionCode::ACT_SET_TEXT,
                static_cast<int>(reinterpret_cast<std::uintptr_t>(&wrapped) & 0xFFFFFFFFu),
                0,
                0);
            return;
        }

        if (now - g_tooltipLastUpdateTime > idleThreshold + 500u)
        {
            g_tooltipLastUpdateTime += 500u;
            as1::STRING current;
            buildMouseTipText(&current);
            if (std::strcmp(current.c_str(), cachedTooltipText().c_str()) != 0)
                releaseShellOwnedSpriteOwner(this);
        }
    }

    bool ApplicationWin::cameraOwnerReady() const noexcept
    {
        return GRAPH::CurrentGraph() != nullptr && MAP::Current() != nullptr;
    }

    void ApplicationWin::updateCameraFromInput()
    {

        GRAPH* graph = GRAPH::CurrentGraph();
        MAP* map = MAP::Current();

        const BASE_CONSTANTS* constants = as1::GlobalBaseConstants();
        const float maxX = dwordAsFloat(constants->raw[0]);
        const float maxY = dwordAsFloat(constants->raw[1]);
        const float graphWidth = graph->screenWidth();
        const float graphHeight = graph->screenHeight();
        const auto& viewport = graph->viewportState();
        const float graphRight = viewport.right;
        const float graphBottom = viewport.bottom;

        const as1::core::ApplicationDrawDispatcherState& appDraw =
            as1::core::GlobalApplicationDrawDispatcherState();
        const float cameraShiftX = appDraw.cameraShiftX();
        const float cameraShiftY = appDraw.cameraShiftY();

        const std::uint32_t mode = as1::core::ApplicationScrollType();
        float& velocityX = as1::core::ApplicationScrollVelocityX();
        float& velocityY = as1::core::ApplicationScrollVelocityY();
        const std::uint32_t inputFlags = inputState(this).flags;
        const float clientX = inputState(this).clientX;
        const float clientY = inputState(this).clientY;

        if ((mode & 0x21u) != 0)
        {
            if (cameraX87LessEqualOrUnordered(clientX, kSub40A290Edge) && (mode & 0x01u) != 0)
            {
                if (cameraX87LessOrUnordered(-maxX, velocityX))
                    velocityX -= kSub40A290AccelX;
            }
            else if ((graphRight - kSub40A290Edge) > clientX && (mode & 0x01u) != 0)
            {

                if ((inputFlags & 0x80u) != 0 && (mode & 0x20u) != 0)
                {
                    if (cameraX87LessOrUnordered(-maxX, velocityX))
                        velocityX -= kSub40A290AccelX;
                }
                else if ((inputFlags & 0x0100u) != 0 && (mode & 0x20u) != 0)
                {
                    if (cameraX87LessOrUnordered(velocityX, maxX))
                        velocityX += kSub40A290AccelX;
                }
                else
                    velocityX = 0.0f;
            }
            else if ((mode & 0x01u) != 0)
            {
                if (cameraX87LessOrUnordered(velocityX, maxX))
                    velocityX += kSub40A290AccelX;
            }
            else if ((inputFlags & 0x80u) != 0 && (mode & 0x20u) != 0)
            {
                if (cameraX87LessOrUnordered(-maxX, velocityX))
                    velocityX -= kSub40A290AccelX;
            }
            else if ((inputFlags & 0x0100u) != 0 && (mode & 0x20u) != 0)
            {
                if (cameraX87LessOrUnordered(velocityX, maxX))
                    velocityX += kSub40A290AccelX;
            }
            else
                velocityX = 0.0f;

            if (cameraX87LessEqualOrUnordered(clientY, kSub40A290Edge) && (mode & 0x01u) != 0)
            {
                if (cameraX87LessOrUnordered(-maxY, velocityY))
                    velocityY -= kSub40A290AccelY;
            }
            else if ((graphBottom - kSub40A290Edge) > clientY && (mode & 0x01u) != 0)
            {
                if ((inputFlags & 0x0400u) != 0 && (mode & 0x20u) != 0)
                {
                    if (cameraX87LessOrUnordered(-maxY, velocityY))
                        velocityY -= kSub40A290AccelY;
                }
                else if ((inputFlags & 0x0200u) != 0 && (mode & 0x20u) != 0)
                {
                    if (cameraX87LessOrUnordered(velocityY, maxY))
                        velocityY += kSub40A290AccelY;
                }
                else
                    velocityY = 0.0f;
            }
            else if ((mode & 0x01u) != 0)
            {
                if (cameraX87LessOrUnordered(velocityY, maxY))
                    velocityY += kSub40A290AccelY;
            }
            else if ((inputFlags & 0x0400u) != 0 && (mode & 0x20u) != 0)
            {
                if (cameraX87LessOrUnordered(-maxY, velocityY))
                    velocityY -= kSub40A290AccelY;
            }
            else if ((inputFlags & 0x0200u) != 0 && (mode & 0x20u) != 0)
            {
                if (cameraX87LessOrUnordered(velocityY, maxY))
                    velocityY += kSub40A290AccelY;
            }
            else
                velocityY = 0.0f;
        }
        else
        {
            velocityX = 0.0f;
            velocityY = 0.0f;
        }

        SPRITE* target = controlledSpriteForPlayer(static_cast<int>(as1::core::ActivePlayerIndex()));
        const bool noVelocity =
            cameraX87EqualOrUnordered(velocityX, 0.0f) &&
            cameraX87EqualOrUnordered(velocityY, 0.0f);
        if (target && (mode & 0x04u) != 0 && noVelocity)
        {
            velocityX = static_cast<float>(
                ((static_cast<double>(target->X()) - static_cast<double>(cameraShiftX)) -
                 static_cast<double>(graphWidth) * 0.5) *
                static_cast<double>(kSub40A290TargetScale));
            velocityY = static_cast<float>(
                (((static_cast<double>(target->Y()) - static_cast<double>(target->Z())) -
                  static_cast<double>(cameraShiftY)) -
                 static_cast<double>(graphHeight) * 0.5) *
                static_cast<double>(kSub40A290TargetScale));
        }
        else if (target && (mode & 0x08u) != 0 && noVelocity)
        {
            // The two target expressions are explicitly FSTP m32 locals in
            // retail before the viewport-center subtraction.
            const float pointerTargetX = static_cast<float>(
                (static_cast<double>(target->X()) - static_cast<double>(cameraShiftX) +
                 static_cast<double>(clientX)) * 0.5);
            const float pointerTargetY = static_cast<float>(
                ((static_cast<double>(target->Y()) - static_cast<double>(target->Z()) -
                  static_cast<double>(cameraShiftY) + static_cast<double>(clientY)) * 0.5));
            velocityX = static_cast<float>(
                (static_cast<double>(graphWidth) * 0.5 - static_cast<double>(pointerTargetX)) *
                static_cast<double>(kSub40A290PointerScale));
            velocityY = static_cast<float>(
                (static_cast<double>(graphHeight) * 0.5 - static_cast<double>(pointerTargetY)) *
                static_cast<double>(kSub40A290PointerScale));
        }
        else if (target && (mode & 0x10u) != 0 && noVelocity)
        {
            map->SetShiftCoor(target->X(),
                              target->Y() - target->Z(),
                              0);
            return;
        }

        const std::uint32_t current = as1::core::CurrentTimeMilliseconds();
        const std::uint32_t previous = as1::core::PreviousWorldTimeMilliseconds();
        const std::uint32_t deltaMs = current - previous;
        // Retail FILDs the unsigned 32-bit delta through a zero-extended qword,
        // multiplies in x87, then calls __ftol.  Do not round delta/product to
        // float before conversion.  The returned low DWORD is reloaded with
        // FILD dword (signed) before adding viewport half + camera shift.
        const std::int32_t dx = cameraElapsedScaleFtolLow32(deltaMs, velocityX);
        const std::int32_t dy = cameraElapsedScaleFtolLow32(deltaMs, velocityY);
        const float centerX = static_cast<float>(
            static_cast<double>(dx) + static_cast<double>(graphWidth) * 0.5 +
            static_cast<double>(cameraShiftX));
        const float centerY = static_cast<float>(
            static_cast<double>(dy) + static_cast<double>(graphHeight) * 0.5 +
            static_cast<double>(cameraShiftY));
        map->SetShiftCoor(centerX, centerY, 0);
    }

    void ApplicationWin::dispatchStartupPlayerControls()
    {

        for (const std::size_t offset : {0x220u, 0x224u, 0x228u, 0x22Cu})
        {
            PLAYER* const player = playerPointerSlot(this, offset);
            // Retail has no null guard here: all four PLAYER owners exist once
            // bit 0x80 enables this path.
            player->processInput(&inputState(this));
        }
    }

    void ApplicationWin::dispatchInputControls(bool worldTick)
    {
        (void)worldTick;
        (void)selectFrameSprite();
        dispatchInputOwner2294();
        updateCameraFromInput();

        if ((flags() & kApplicationModalDispatchFlag) != 0u)
            return;
        if ((flags() & kApplicationRenderControlsFlag) == 0u)
            return;

        dispatchStartupPlayerControls();
    }

    void ApplicationWin::waitForMessageGate()
    {

        as1::core::SetCurrentTimeMilliseconds(as1::core::PreviousWorldTimeMilliseconds());
        WaitMessage();
    }

    void ApplicationWin::splitPackedDirectionalMask(std::uint32_t packed, std::uint32_t& positive, std::uint32_t& negative) noexcept
    {

        positive = 0;
        negative = 0;
        const std::uint32_t masks[4] = {0x0000007Fu, 0x00007F80u, 0x007F8000u, 0xFF800000u};
        const std::uint32_t signBits[4] = {0x00000080u, 0x00008000u, 0x00800000u, 0x80000000u};
        for (int i = 0; i < 4; ++i)
        {
            if (packed & signBits[i])
                negative |= ((~packed) & masks[i]) << 1;
            else
                positive |= (packed & masks[i]) << 1;
        }
    }
}
#endif
