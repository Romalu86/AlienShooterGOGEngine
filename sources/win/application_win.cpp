#include "win/application_win.h"

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <cstdio>
#include <mmsystem.h>
#include <commdlg.h>
#include <objbase.h>
#include <new>
#include <cmath>
#include <cstring>
#include <xmmintrin.h>

#include "sprite.h"
#include "player_arcade.h"
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
#include "menu.h"
#include "graph.h"
#include "graphics/base_texture.h"
#include "graphics/color.h"
#include "vid/vid.h"
#include "sprite_collector.h"
#include "mouse.h"
#include "core/log.h"
#include "core/application.h"
#include "core/weak_controller.h"
#include "core/profile_p.h"
#include "core/file_logger.h"
#include "core/configuration.h"
#include "constant.h"
#include "sound/sound_engine.h"
#include "win/resources/resource.h"
#include "win/main_sw.h"
#include "game/startup.h"

namespace as1 { namespace win
{
    namespace
    {
        constexpr std::uint32_t kApplicationCleanupBusyFlag = 0x00000002u;
        constexpr std::uint32_t kApplicationInitializedFlag = 0x00000004u;
        constexpr std::uint32_t kApplicationFramePumpFlag = 0x00000008u;
        constexpr std::uint32_t kApplicationModalDispatchFlag = 0x00000010u;
        constexpr std::uint32_t kApplicationCommandLinePendingFlag = application_flags::PendingCommandOrLoad;
        constexpr std::uint32_t kApplicationRenderControlsFlag = 0x00000080u;
        constexpr std::uint32_t kMapSelectSpriteUnderCursorFlag = 0x00100000u;
        constexpr std::uint32_t kApplicationStartupForcedFlags = 0x00111080u;


        constexpr std::uint32_t kShellLowNibbleMask = 0x0000000Fu;
        constexpr std::uint32_t kShellTogglePause = 0x00000001u;
        constexpr std::uint32_t kShellForceFrame = 0x00000002u;
        constexpr std::uint32_t kShellDrawLabels = 0x00000004u;
        constexpr std::uint32_t kShellDispatchOverlayList = 0x00000008u;


        constexpr std::uint32_t kDebugDrawTerrainGrid = 0x00000800u;
        constexpr std::uint32_t kDebugDrawCurrentSprite = 0x00001000u;
        constexpr std::uint32_t kDebugDrawAuxiliaryList = 0x00002000u;
        constexpr std::uint32_t kDebugDrawScrollBox = 0x00004000u;
        constexpr std::uint32_t kDebugDrawSpriteBuckets = 0x00008000u;
        constexpr std::uint32_t kDebugShowSoundCount = 0x00010000u;
        constexpr std::uint32_t kDebugShowFps = 0x00020000u;

        int decodeControlKeyName(STRING name)
        {
            char* const mutableText = const_cast<char*>(name.c_str());
            for (char* p = mutableText; p && *p; ++p)
            {
                if (*p >= 'a' && *p <= 'z')
                    *p = static_cast<char>(*p - ('a' - 'A'));
            }

            const char* const text = name.c_str();
            if (std::strcmp(text, "LBUTTON") == 0)
                return 1;
            if (std::strcmp(text, "RBUTTON") == 0)
                return 2;
            if (std::strcmp(text, "[") == 0)
                return 91;
            if (std::strcmp(text, "]") == 0)
                return 92;
            if (std::strcmp(text, "LEFT") == 0)
                return 37;
            if (std::strcmp(text, "RIGHT") == 0)
                return 39;
            if (std::strcmp(text, "UP") == 0)
                return 38;
            if (std::strcmp(text, "DOWN") == 0)
                return 40;
            if (std::strcmp(text, "INSERT") == 0)
                return 45;
            if (std::strcmp(text, "DELETE") == 0)
                return 46;
            if (std::strcmp(text, "HOME") == 0)
                return 36;
            if (std::strcmp(text, "END") == 0)
                return 35;
            if (std::strcmp(text, "PGUP") == 0)
                return 33;
            if (std::strcmp(text, "PGDN") == 0)
                return 34;
            if (std::strcmp(text, "SHIFT") == 0)
                return 16;
            if (std::strcmp(text, "CTRL") == 0)
                return 17;
            if (std::strcmp(text, "F1") == 0)
                return 112;
            if (std::strcmp(text, "F2") == 0)
                return 113;
            if (std::strcmp(text, "F3") == 0)
                return 114;
            if (std::strcmp(text, "F4") == 0)
                return 115;
            if (std::strcmp(text, "F5") == 0)
                return 116;
            if (std::strcmp(text, "F6") == 0)
                return 117;
            if (std::strcmp(text, "F7") == 0)
                return 118;
            if (std::strcmp(text, "F8") == 0)
                return 119;
            if (std::strcmp(text, "F9") == 0)
                return 120;
            if (std::strcmp(text, "F10") == 0)
                return 121;
            if (std::strcmp(text, "F11") == 0)
                return 122;
            if (std::strcmp(text, "F12") == 0)
                return 123;
            return static_cast<signed char>(text[0]);
        }

        std::uint32_t g_presentationVersionLastSpawnMs = 0u;


        constexpr std::uint32_t kFrameClampMs = 71u;
        constexpr std::uint32_t kDemoFrameToleranceMs = 20u;
        constexpr float kCameraEdgeThreshold = 5.0f;
        constexpr float kCameraAccelerationX = 0.039999999f;
        constexpr float kCameraAccelerationY = 0.029999999f;
        constexpr float kCameraTargetFollowScale = 0.001f;
        constexpr float kCameraPointerFollowScale = -0.0040000002f;

        float dwordAsFloat(std::uint32_t value) noexcept
        {
            float out = 0.0f;
            std::memcpy(&out, &value, sizeof(out));
            return out;
        }


        bool cameraLessEqualOrUnordered(float lhs, float rhs) noexcept
        {
            return std::isnan(lhs) || std::isnan(rhs) || lhs <= rhs;
        }

        bool cameraEqualOrUnordered(float lhs, float rhs) noexcept
        {
            return std::isnan(lhs) || std::isnan(rhs) || lhs == rhs;
        }


        std::int32_t convertCameraElapsedScaleToInt32(std::uint32_t elapsed, float scale) noexcept
        {


            const float value = static_cast<float>(elapsed) * scale;
            if (!std::isfinite(value) || value < -2147483648.0f || value >= 2147483648.0f)
                return static_cast<std::int32_t>(0x80000000u);
            return static_cast<std::int32_t>(value);
        }

        int applicationX87Int64Low32(long double value) noexcept
        {
            if (!std::isfinite(value) ||
                value < -9223372036854775808.0L ||
                value >= 9223372036854775808.0L)
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(value));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        int terrainGridDimension(float extent) noexcept
        {
            const int roundedExtent = applicationX87Int64Low32(
                static_cast<long double>(extent) + 7.0L);
            return (roundedExtent + (roundedExtent < 0 ? 7 : 0)) >> 3;
        }

        constexpr DWORD kMainWindowExStyle = 0x00040000u;
        constexpr DWORD kWindowedStyle = 0x90CA0000u;
        constexpr DWORD kBorderlessStyle = 0x90000000u;
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
            char* const ownedText = const_cast<char*>(owner.c_str());
            if (ownedText != as1::STRING::SharedEmptyText())
                ::operator delete(ownedText);
        }

        __forceinline as1::STRING& cachedTooltipText()
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
                reinterpret_cast<std::uint8_t*>(app) + core::application_layout::InputState);
        }

        const as1::input::InputMessageState& inputState(const ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<const as1::input::InputMessageState*>(
                reinterpret_cast<const std::uint8_t*>(app) + core::application_layout::InputState);
        }

        MENU& embeddedMenu(ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<MENU*>(
                reinterpret_cast<std::uint8_t*>(app) + core::application_layout::Menu);
        }

        const MENU& embeddedMenu(const ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<const MENU*>(
                reinterpret_cast<const std::uint8_t*>(app) + core::application_layout::Menu);
        }

        __forceinline
        BaseSpriteList<0>& embeddedFrameSpriteList(ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<BaseSpriteList<0>*>(
                reinterpret_cast<std::uint8_t*>(app) + core::application_layout::BaseSpriteList);
        }

        __forceinline
        const BaseSpriteList<0>& embeddedFrameSpriteList(const ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<const BaseSpriteList<0>*>(
                reinterpret_cast<const std::uint8_t*>(app) + core::application_layout::BaseSpriteList);
        }

        __forceinline
        core::ApplicationDrawPassBucket& embeddedDrawPassBucket(ApplicationWin* app, int pass) noexcept
        {
            return *reinterpret_cast<core::ApplicationDrawPassBucket*>(
                reinterpret_cast<std::uint8_t*>(app) +
                core::application_layout::DrawLayerOwners +
                static_cast<std::size_t>(pass) * core::application_layout::DrawLayerStride);
        }

        __forceinline
        const core::ApplicationDrawPassBucket& embeddedDrawPassBucket(const ApplicationWin* app, int pass) noexcept
        {
            return *reinterpret_cast<const core::ApplicationDrawPassBucket*>(
                reinterpret_cast<const std::uint8_t*>(app) +
                core::application_layout::DrawLayerOwners +
                static_cast<std::size_t>(pass) * core::application_layout::DrawLayerStride);
        }

        __forceinline
        int physicalIntSlot(const ApplicationWin* app, std::size_t offset) noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const std::uint8_t*>(app) + offset);
        }

        __forceinline
        std::uint32_t physicalDwordSlot(const ApplicationWin* app, std::size_t offset) noexcept
        {
            return *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::uint8_t*>(app) + offset);
        }

        __forceinline
        short* physicalTerrainGrid(const ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<short* const*>(
                reinterpret_cast<const std::uint8_t*>(app) + core::application_layout::TerrainGrid);
        }

        __forceinline
        int physicalVidCount(const ApplicationWin* app) noexcept
        {
            return physicalIntSlot(app, core::application_layout::VidCount);
        }

        __forceinline
        VID* physicalVidSlotUnchecked(const ApplicationWin* app, int index) noexcept
        {
            return *reinterpret_cast<VID* const*>(
                reinterpret_cast<const std::uint8_t*>(app) + core::application_layout::VidTable +
                static_cast<std::size_t>(index) * sizeof(std::uint32_t));
        }

        HINSTANCE& applicationInstanceHandle(ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<HINSTANCE*>(reinterpret_cast<std::uint8_t*>(app) + core::application_layout::InstanceHandle);
        }

        HINSTANCE applicationInstanceHandle(const ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<HINSTANCE const*>(reinterpret_cast<const std::uint8_t*>(app) + core::application_layout::InstanceHandle);
        }

        HWND& mainWindowHandle(ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<HWND*>(reinterpret_cast<std::uint8_t*>(app) + core::application_layout::MainWindow);
        }

        HWND mainWindowHandle(const ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<HWND const*>(reinterpret_cast<const std::uint8_t*>(app) + core::application_layout::MainWindow);
        }

        HACCEL& acceleratorHandle(ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<HACCEL*>(reinterpret_cast<std::uint8_t*>(app) + core::application_layout::Accelerator);
        }

        HACCEL acceleratorHandle(const ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<HACCEL const*>(reinterpret_cast<const std::uint8_t*>(app) + core::application_layout::Accelerator);
        }

        std::uint32_t& shellFlagsStorage(ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(app) + core::application_layout::ShellFlags);
        }

        std::uint32_t shellFlagsStorage(const ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::uint8_t*>(app) + core::application_layout::ShellFlags);
        }

        float& physicalFloatSlot(ApplicationWin* app, std::size_t offset) noexcept
        {
            return *reinterpret_cast<float*>(reinterpret_cast<std::uint8_t*>(app) + offset);
        }

        float physicalFloatSlot(const ApplicationWin* app, std::size_t offset) noexcept
        {
            return *reinterpret_cast<const float*>(reinterpret_cast<const std::uint8_t*>(app) + offset);
        }

        constexpr std::size_t kApplicationTitleOffset = core::application_layout::ApplicationTitle;
        constexpr std::size_t kCurrentMapNameOffset = core::application_layout::CurrentMapName;
        constexpr std::size_t kPendingCommandOffset = core::application_layout::PendingCommand;
        constexpr std::size_t kPreviousMapNameOffset = core::application_layout::PreviousMapName;
        constexpr std::size_t kResourceNameOffset = core::application_layout::ResourceName;

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
        as1::STRING& previousMapName(ApplicationWin* app) noexcept { return applicationStringAt(app, kPreviousMapNameOffset); }
        const as1::STRING& previousMapName(const ApplicationWin* app) noexcept { return applicationStringAt(app, kPreviousMapNameOffset); }
        as1::STRING& resourceName(ApplicationWin* app) noexcept { return applicationStringAt(app, kResourceNameOffset); }
        const as1::STRING& resourceName(const ApplicationWin* app) noexcept { return applicationStringAt(app, kResourceNameOffset); }

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
            return core::application_layout::PlayerSlots +
                static_cast<std::size_t>(index & 3) * core::application_layout::PlayerSlotStride;
        }


        class ApplicationSpriteOwner final
        {
        public:
            ApplicationSpriteOwner() noexcept : sprite_(nullptr) {}
            virtual ~ApplicationSpriteOwner()
            {
                reset();
            }

            __declspec(noinline) void reset() noexcept
            {
                SPRITE* const owned = sprite_;
                if (owned)
                    delete owned;
                sprite_ = nullptr;
            }

            void bind(SPRITE* sprite) noexcept { sprite_ = sprite; }
            __declspec(noinline) void clearIfMatches(SPRITE* sprite) noexcept
            {
                if (sprite_ == sprite)
                    sprite_ = nullptr;
            }
            bool empty() const noexcept { return sprite_ == nullptr; }

        private:
            SPRITE* sprite_;
        };

#if UINTPTR_MAX == 0xFFFFFFFFu
        static_assert(sizeof(ApplicationSpriteOwner) == 0x08u,
                      "Application sprite-owner subobject size");
#endif

        __forceinline
        ApplicationSpriteOwner& shellOwnedSpriteOwner(ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<ApplicationSpriteOwner*>(
                reinterpret_cast<std::uint8_t*>(app) + core::application_layout::ShellOwnedSpriteVtable);
        }

        __forceinline
        const ApplicationSpriteOwner& shellOwnedSpriteOwner(const ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<const ApplicationSpriteOwner*>(
                reinterpret_cast<const std::uint8_t*>(app) + core::application_layout::ShellOwnedSpriteVtable);
        }

        __forceinline
        void initializeShellOwnedSpriteOwner(ApplicationWin* app) noexcept
        {
            new (reinterpret_cast<std::uint8_t*>(app) + core::application_layout::ShellOwnedSpriteVtable)
                ApplicationSpriteOwner();
        }

        __forceinline
        int releaseShellOwnedSpriteOwner(ApplicationWin* app) noexcept
        {
            ApplicationSpriteOwner& owner = shellOwnedSpriteOwner(app);
            const bool hadObject = !owner.empty();
            owner.reset();
            return hadObject ? 1 : 0;
        }

        __forceinline
        void destroyShellOwnedSpriteOwner(ApplicationWin* app) noexcept
        {
            shellOwnedSpriteOwner(app).~ApplicationSpriteOwner();
        }

        __forceinline
        void bindShellOwnedSprite(ApplicationWin* app, SPRITE* sprite) noexcept
        {
            shellOwnedSpriteOwner(app).bind(sprite);
        }

        __forceinline
        void clearShellOwnedSpriteIfMatches(ApplicationWin* app, SPRITE* sprite) noexcept
        {
            shellOwnedSpriteOwner(app).clearIfMatches(sprite);
        }

        __forceinline
        bool shellOwnedSpriteEmpty(ApplicationWin* app) noexcept
        {
            return shellOwnedSpriteOwner(app).empty();
        }


        std::string copyCString(const char* text)
        {
            return (text && *text) ? std::string(text) : std::string();
        }

        void toggleFlag(std::uint32_t& flags, std::uint32_t mask) noexcept
        {
            flags ^= mask;
        }


    }


    

    ApplicationWin::~ApplicationWin()
    {


        releaseShellOwnedSpriteOwner(this);
        deinitialize();
    }


    ApplicationWin* ApplicationWin::initializeDerivedApplicationStartup(HINSTANCE instance,
                                                     HINSTANCE previousInstance,
                                                     const char** commandLineOwner,
                                                     int showCmd,
                                                     as1::core::StartupSettingsBlock* startupSettings)
    {

        (void)initializeBaseApplicationStartup(instance, previousInstance, commandLineOwner, showCmd, startupSettings);


        if (!initialized())
            return this;

        shellFlagsStorage(this) &= ~kShellLowNibbleMask;
        as1::STRING startupCommand(pendingCommand(this).c_str());
        runCommandLineMap(startupCommand.DetachOwnedStorage());
        return this;
    }


    ApplicationWin* ApplicationWin::initializeBaseApplicationStartup(HINSTANCE instance,
                                    HINSTANCE previousInstance,
                                    const char** commandLineOwner,
                                    int showCmd,
                                    as1::core::StartupSettingsBlock* startupSettings)
    {


        const std::uint32_t startupFlags = startupSettings->flags;


        new (&applicationTitle(this)) as1::STRING();
        new (&currentMapName(this)) as1::STRING();
        new (&pendingCommand(this)) as1::STRING();
        new (&previousMapName(this)) as1::STRING();
        new (&resourceName(this)) as1::STRING();


        for (int pass = 0; pass < as1::core::ApplicationDrawDispatcherState::PassCount; ++pass)
        {
            void* const slot = reinterpret_cast<std::uint8_t*>(this) +
                core::application_layout::DrawLayerOwners +
                static_cast<std::size_t>(pass) * core::application_layout::DrawLayerStride;
            new (slot) core::ApplicationDrawPassBucket();
        }

        new (reinterpret_cast<std::uint8_t*>(this) + core::application_layout::ScriptRuntime) SCRIPT();
        new (reinterpret_cast<std::uint8_t*>(this) + core::application_layout::DemoResource) RESOURCE();
        new (reinterpret_cast<std::uint8_t*>(this) + core::application_layout::RelationTable) RelationTable();
        inputState(this).initializePreservingPersistentFlags();
        new (reinterpret_cast<std::uint8_t*>(this) + core::application_layout::Menu) MENU();
        new (reinterpret_cast<std::uint8_t*>(this) + core::application_layout::Groups) GROUPS();
        initializeShellOwnedSpriteOwner(this);
        *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(this) +
            core::application_layout::ShellFlags) = 0u;


        const char* const commandLine = *commandLineOwner;
        char startupCurrentDirectory[0x1000] = {};
        if (::GetCurrentDirectoryA(static_cast<DWORD>(sizeof(startupCurrentDirectory)), startupCurrentDirectory) == 0)
            startupCurrentDirectory[0] = '\0';


        as1::core::StartupConfiguration windowConfig;
        as1::core::StartupSettingsBlock& initialStartupSettings = as1::core::StartupSettings();
        windowConfig.startupFlags = initialStartupSettings.flags;
        windowConfig.video.device = initialStartupSettings.device;
        windowConfig.video.screenX = initialStartupSettings.screenWidth;
        windowConfig.video.screenY = initialStartupSettings.screenHeight;
        windowConfig.video.colorDepth = initialStartupSettings.colorDepth;
        windowConfig.video.fullscreen = initialStartupSettings.fullscreen != 0;

        as1::STRING executablePath;
        if (as1::g_executablePath && *as1::g_executablePath)
            ::new (static_cast<void*>(&(executablePath))) as1::STRING(as1::g_executablePath, static_cast<int>(std::strlen(as1::g_executablePath)));

        as1::STRING executableFileName;
        executableFileName.Assign(executablePath.AfterLast("\\"));
        windowConfig.applicationName.Assign(executableFileName.BeforeLast("."));
        windowConfig.applicationTitle = windowConfig.applicationName;

        as1::STRING defaultRegistryPath;
        ::new (static_cast<void*>(&(defaultRegistryPath))) as1::STRING("SOFTWARE\\Gromada\\", windowConfig.applicationName.c_str());
        windowConfig.registryPath = defaultRegistryPath;

        const as1::STRING currentDirectory(startupCurrentDirectory);
        const as1::STRING configDirectory = currentDirectory.isEmpty() ? as1::STRING(".") : currentDirectory;
        as1::STRING directoryPrefix;
        ::new (static_cast<void*>(&(directoryPrefix))) as1::STRING(configDirectory.c_str(), "\\");

        as1::STRING configBasePath;
        ::new (static_cast<void*>(&(configBasePath))) as1::STRING(directoryPrefix.c_str(), windowConfig.applicationName.c_str());
        as1::STRING resolvedConfigPath;
        ::new (static_cast<void*>(&(resolvedConfigPath))) as1::STRING(configBasePath.c_str(), ".cfg");

        if (std::strstr(commandLine, ".cfg"))
            ::new (static_cast<void*>(&(resolvedConfigPath))) as1::STRING(directoryPrefix.c_str(), commandLine);

        windowConfig.configPath = resolvedConfigPath;
        windowConfig.resourceRoot = configDirectory;


        as1::g_fileLogger = new (std::nothrow) as1::FileLogger(true);


        ::timeBeginPeriod(1u);
        const std::uint32_t initialRealTime = static_cast<std::uint32_t>(::timeGetTime());
        core::RealCurrentTime = initialRealTime;
        core::PrevRealCurrentTime = initialRealTime - 10u;
        const float kScale4096 = 4096.0f;
        const float kScaleRadians = 0.00017262212f;
        for (std::size_t i = 0; i < 256u; ++i)
        {
            const float sourceSin = SPRITE::directionSinValue(static_cast<int>(i));
            const float sourceCosWindow = SPRITE::directionCosValue(static_cast<int>(i));
            const float derivedSin = static_cast<float>(
                static_cast<long double>(sourceSin) *
                static_cast<long double>(kScale4096) *
                static_cast<long double>(kScaleRadians));
            const float derivedCosWindow = static_cast<float>(
                static_cast<long double>(sourceCosWindow) *
                static_cast<long double>(kScale4096) *
                static_cast<long double>(kScaleRadians));
            std::memcpy(&g_directionTrigWindow[512u + i], &derivedSin, sizeof(derivedSin));
            std::memcpy(&g_directionTrigWindow[768u + i], &derivedCosWindow, sizeof(derivedCosWindow));
        }

        std::uint32_t targetFlags = flags() & 0xFFF7FFFDu;
        setFlags(targetFlags);
        targetFlags =
            ((targetFlags & 0xFFFD1082u) ^ (startupFlags & 1u)) |
            0x00111080u;
        setFlags(targetFlags);


        *reinterpret_cast<short**>(reinterpret_cast<std::uint8_t*>(this) + core::application_layout::TerrainGrid) = nullptr;
        *reinterpret_cast<short**>(reinterpret_cast<std::uint8_t*>(this) + core::application_layout::TempTerrainGrid) = nullptr;
        *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(this) + core::application_layout::TerrainGridWidth) = 0;
        *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(this) + core::application_layout::TerrainGridHeight) = 0;
        *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(this) + core::application_layout::WeaponCount) = 0;
        *reinterpret_cast<WEAPON**>(reinterpret_cast<std::uint8_t*>(this) + core::application_layout::WeaponTable) = nullptr;
        *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(this) + core::application_layout::VidCount) = 0;
        physicalFloatSlot(this, core::application_layout::MapExtentX) = 640.0f;
        physicalFloatSlot(this, core::application_layout::MapExtentY) = 480.0f;
        *reinterpret_cast<std::uint32_t*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::ActivePlayerIndex) = 0u;
        physicalFloatSlot(this, core::application_layout::CameraShiftX) = 0.0f;
        physicalFloatSlot(this, core::application_layout::CameraShiftY) = 1.0f;
        *reinterpret_cast<std::uint32_t*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::WeaponTable) = 0u;
        *reinterpret_cast<std::uint32_t*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::WeaponCount) = 0u;
        *reinterpret_cast<std::uint32_t*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::VidCount) = 0u;
        *reinterpret_cast<std::uint32_t*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::Fps) = 0u;
        *reinterpret_cast<std::uint32_t*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::FpsCounter) = 0u;
        *reinterpret_cast<std::uint32_t*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::WorldFrameCounter) = 0u;
        *reinterpret_cast<std::uint32_t*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::WorldStartTime) =
            initialRealTime;
        physicalFloatSlot(this, core::application_layout::TickScale) = 1.0f;
        *reinterpret_cast<std::uint32_t*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::ScrollType) = 1u;
        std::memset(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::VidTable,
            0,
            core::application_layout::VidTableBytes);
        playerPointerSlot(this, playerSlotOffset(0)) = nullptr;
        playerPointerSlot(this, playerSlotOffset(1)) = nullptr;
        playerPointerSlot(this, playerSlotOffset(2)) = nullptr;
        playerPointerSlot(this, playerSlotOffset(3)) = nullptr;
        applicationInstanceHandle(this) = instance;
        {
            short*& grid = *reinterpret_cast<short**>(
                reinterpret_cast<std::uint8_t*>(this) + core::application_layout::TerrainGrid);
            short*& temporaryTerrainGrid = *reinterpret_cast<short**>(
                reinterpret_cast<std::uint8_t*>(this) + core::application_layout::TempTerrainGrid);
            if (grid)
                ::operator delete(static_cast<void*>(grid));
            if (temporaryTerrainGrid)
                ::operator delete(static_cast<void*>(temporaryTerrainGrid));

            const int gridX = terrainGridDimension(
                physicalFloatSlot(this, core::application_layout::MapExtentX));
            const int gridY = terrainGridDimension(
                physicalFloatSlot(this, core::application_layout::MapExtentY));
            *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(this) +
                core::application_layout::TerrainGridWidth) = gridX;
            *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(this) +
                core::application_layout::TerrainGridHeight) = gridY;

            const std::size_t gridBytes =
                2u * static_cast<std::size_t>(gridX) * static_cast<std::size_t>(gridY);
            grid = static_cast<short*>(::operator new(gridBytes));
            temporaryTerrainGrid = static_cast<short*>(::operator new(gridBytes));
            std::memset(grid, 0, gridBytes);
            std::memset(temporaryTerrainGrid, 0, gridBytes);
        }

        const HRESULT comInitializeResult = ::CoInitialize(nullptr);
        if (FAILED(comInitializeResult))
        {
            as1::logFileLoggerResourceError(g_fileLogger, "MAP", 12, "COM", static_cast<int>(comInitializeResult));
            return this;
        }


        as1::core::g_startupStringsIniPathOwner = new (std::nothrow) as1::STRING();
        if (as1::core::g_startupStringsIniPathOwner)
        {
            const char* const rootText = windowConfig.resourceRoot.isEmpty()
                ? "."
                : windowConfig.resourceRoot.c_str();
            as1::STRING directoryPath(rootText);
            as1::STRING stringsIniPath;
            ::new (static_cast<void*>(&(stringsIniPath))) as1::STRING(directoryPath.c_str(), "\\Strings.ini");
            (*as1::core::g_startupStringsIniPathOwner).ResetAndAssign(stringsIniPath);
            windowConfig.stringsPath = *as1::core::g_startupStringsIniPathOwner;
        }

        {
            as1::STRING configuredTitle;
            as1::core::profile_p::readProfileStringInto(
                configuredTitle,
                windowConfig.configPath,
                as1::STRING("common"),
                as1::STRING("Title"),
                windowConfig.applicationName);
            (windowConfig.applicationTitle).Assign(configuredTitle);
        }

        as1::core::g_startupRegistryPathOwner = new (std::nothrow) as1::REGISTRY();
        if (as1::core::g_startupRegistryPathOwner)
        {
            as1::STRING defaultRegistryPath;
            ::new (static_cast<void*>(&(defaultRegistryPath))) as1::STRING("SOFTWARE\\Gromada\\", windowConfig.applicationName.c_str());
            as1::STRING configuredRegistryPath;
            as1::core::profile_p::readProfileStringInto(
                configuredRegistryPath,
                windowConfig.configPath,
                as1::STRING("common"),
                as1::STRING("RegPath"),
                defaultRegistryPath);
            (as1::core::g_startupRegistryPathOwner->mutablePath()).Assign(configuredRegistryPath);
            windowConfig.registryPath = *as1::core::g_startupRegistryPathOwner->Path();
        }

        as1::core::StartupSettingsBlock& startupProfileSettings = as1::core::StartupSettings();
        windowConfig.startupFlags = startupProfileSettings.flags;
        int startupProfileValue = static_cast<int>(as1::core::profile_p::readProfileIntValue(
            windowConfig.configPath, as1::STRING("graph"), as1::STRING("VSync"), 1));
        startupProfileSettings.flags |= startupProfileValue ? as1::core::StartupVSyncFlag : 0u;
        windowConfig.startupFlags = startupProfileSettings.flags;
        windowConfig.video.vsync = (windowConfig.startupFlags & as1::core::StartupVSyncFlag) != 0u;

        startupProfileValue = static_cast<int>(as1::core::profile_p::readProfileIntValue(
            windowConfig.configPath, as1::STRING("game"), as1::STRING("StartDialogIsFull"), 0));
        startupProfileSettings.flags |= startupProfileValue ? as1::core::StartupDialogFullscreenFlag : 0u;
        windowConfig.startupFlags = startupProfileSettings.flags;
        windowConfig.startDialogFullscreen =
            (windowConfig.startupFlags & as1::core::StartupDialogFullscreenFlag) != 0u;

        windowConfig.noSysMenu = 0;

        if (as1::core::g_startupRegistryPathOwner)
        {
            const as1::STRING* const registryPath = as1::core::g_startupRegistryPathOwner->Path();
            startupProfileSettings.device = registryPath->ReadRegistryInt(as1::STRING("Device"), startupProfileSettings.device);
            startupProfileSettings.screenWidth = registryPath->ReadRegistryInt(as1::STRING("ScreenX"), startupProfileSettings.screenWidth);
            startupProfileSettings.screenHeight = registryPath->ReadRegistryInt(as1::STRING("ScreenY"), startupProfileSettings.screenHeight);
            startupProfileSettings.colorDepth = registryPath->ReadRegistryInt(as1::STRING("BPP"), startupProfileSettings.colorDepth);
            startupProfileSettings.fullscreen = registryPath->ReadRegistryInt(as1::STRING("FullScreen"), startupProfileSettings.fullscreen);
        }
        if (startupProfileSettings.colorDepth != 16 && startupProfileSettings.colorDepth != 32)
            startupProfileSettings.colorDepth = 32;
        windowConfig.video.device = startupProfileSettings.device;
        windowConfig.video.screenX = startupProfileSettings.screenWidth;
        windowConfig.video.screenY = startupProfileSettings.screenHeight;
        windowConfig.video.colorDepth = startupProfileSettings.colorDepth;
        windowConfig.video.fullscreen = startupProfileSettings.fullscreen != 0;

        (applicationTitle(this)).Assign(windowConfig.applicationTitle);


        void* const graphStorage = ::operator new(0x0E30u, std::nothrow);
        if (!graphStorage)
        {


            as1::Graph = nullptr;
            return this;
        }
        as1::GRAPH* const graph = new (graphStorage) as1::GRAPH();

        std::strcpy(startupSettings->title, applicationTitle(this).c_str());
        graph->initializeGraphState(*startupSettings);

        as1::Graph = graph;

        windowConfig.showStartDialog = as1::core::profile_p::readProfileIntValue(
            windowConfig.configPath, as1::STRING("game"), as1::STRING("StartDialog"), 1) != 0u;
        if (windowConfig.showStartDialog)
        {


            if (::DialogBoxParamA(instance, "START_DIALOG", nullptr, as1::win::DialogFunc, 0) == 0)
                return this;
        }

        if (!previousInstance)
        {
            WNDCLASSA wc{};
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = AppWndProc;
            wc.hInstance = applicationInstanceHandle(this);
            wc.hIcon = LoadIconA(applicationInstanceHandle(this), kIconResourceName);
            wc.hCursor = nullptr;
            wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(4));
            wc.lpszClassName = windowConfig.applicationName.c_str();
            wc.lpszMenuName = (flags() & 1u) != 0u ? kMenuResourceName : nullptr;
            if (RegisterClassA(&wc) == 0)
                return this;
        }

        const bool fullscreenWindow = graph->fullscreenRequested();
        if (fullscreenWindow)
        {
            windowConfig.video.windowPositionX = 0;
            windowConfig.video.windowPositionY = 0;
        }
        else
        {


            const as1::STRING* const registryPath = as1::core::g_startupRegistryPathOwner
                ? as1::core::g_startupRegistryPathOwner->Path() : nullptr;
            windowConfig.video.windowPositionX = registryPath
                ? registryPath->ReadRegistryInt(as1::STRING("WindowPositionX"), 0) : 0;
            windowConfig.video.windowPositionY = registryPath
                ? registryPath->ReadRegistryInt(as1::STRING("WindowPositionY"), 0) : 0;
        }

        const DWORD windowStyle = (!fullscreenWindow && windowConfig.noSysMenu == 0)
            ? kWindowedStyle
            : kBorderlessStyle;
        as1::Map = reinterpret_cast<as1::MAP*>(this);
        HWND hwnd = CreateWindowExA(
            kMainWindowExStyle,
            windowConfig.applicationName.c_str(),
            applicationTitle(this).c_str(),
            windowStyle,
            fullscreenWindow ? 0 : windowConfig.video.windowPositionX,
            fullscreenWindow ? 0 : windowConfig.video.windowPositionY,
            applicationX87Int64Low32(static_cast<long double>(graph->SizeX())),
            applicationX87Int64Low32(static_cast<long double>(graph->SizeY())),
            nullptr,
            nullptr,
            applicationInstanceHandle(this),
            nullptr);
        mainWindowHandle(this) = hwnd;
        ShowWindow(hwnd, showCmd);
        UpdateWindow(hwnd);
        HACCEL accel = LoadAcceleratorsA(applicationInstanceHandle(this), kAcceleratorResourceName);
        acceleratorHandle(this) = accel;
        ShowCursor(FALSE);

        if (graph->initializeWindowDevice(hwnd) != 0)
            return this;
        windowConfig.font.sizeY = static_cast<int>(as1::core::profile_p::readProfileIntValue(
            windowConfig.configPath, as1::STRING("graph"), as1::STRING("FontSizeY"), 8));
        windowConfig.font.sizeX = static_cast<int>(as1::core::profile_p::readProfileIntValue(
            windowConfig.configPath, as1::STRING("graph"), as1::STRING("FontSizeX"), 0));
        {
            as1::STRING configuredFontFace;
            as1::core::profile_p::readProfileStringInto(
                configuredFontFace,
                windowConfig.configPath,
                as1::STRING("graph"),
                as1::STRING("Font"),
                as1::STRING("Arial"));
            (windowConfig.font.face).Assign(configuredFontFace);
        }
        graph->rebuildTextFont(windowConfig.font.face, windowConfig.font.sizeX, windowConfig.font.sizeY);

        as1::RESOURCE startupObjectsResource;
        {
            as1::STRING configuredResourceName;
            as1::core::profile_p::readProfileStringInto(
                configuredResourceName,
                windowConfig.configPath,
                as1::STRING("game"),
                as1::STRING("Resource"),
                as1::STRING("objects.res"));
            (windowConfig.objectsResource).Assign(configuredResourceName);
        }
        (resourceName(this)).Assign(windowConfig.objectsResource);
        if (startupObjectsResource.openFile(&windowConfig.objectsResource,
                                               as1::RESOURCE::ResTypes::DATA) != 0)
        {
            as1::logFileLoggerResourceError(g_fileLogger, "%s", 7, "resource file", 0, "");
            return this;
        }

        if (as1::core::g_startupRegistryPathOwner)
            windowConfig.sound.highQuality =
                as1::core::g_startupRegistryPathOwner->Path()->ReadRegistryInt(
                    as1::STRING("SoundHighQuality"), 1) != 0;
        else
            windowConfig.sound.highQuality = true;

        void* const soundStorage = ::operator new(sizeof(as1::sound::Engine), std::nothrow);
        as1::sound::Engine* const soundEngine = static_cast<as1::sound::Engine*>(soundStorage);
        if (soundEngine)
        {
            as1::sound::g_globalSoundEngine = soundEngine->initializeSoundState(
                hwnd, &startupObjectsResource, windowConfig.sound.highQuality ? 1 : 0);
        }
        else
        {
            as1::sound::g_globalSoundEngine = nullptr;
        }

        as1::CONSTANT* baseConstants = new (std::nothrow) as1::CONSTANT;
        if (baseConstants)
            as1::g_baseConstants = baseConstants ? baseConstants->Load(&startupObjectsResource) : nullptr;
        else
            as1::g_baseConstants = nullptr;

        windowConfig.debugMode = static_cast<int>(as1::core::profile_p::readProfileIntValue(
            windowConfig.configPath, as1::STRING("game"), as1::STRING("DebugMode"), 0));
        windowConfig.drawFps = static_cast<int>(as1::core::profile_p::readProfileIntValue(
            windowConfig.configPath, as1::STRING("game"), as1::STRING("DrawFPS"), 0));
        windowConfig.drawPresentation = static_cast<int>(as1::core::profile_p::readProfileIntValue(
            windowConfig.configPath, as1::STRING("game"), as1::STRING("DrawPresentation"), 0));
        if (as1::g_baseConstants)
            as1::g_baseConstants->entries[10] = static_cast<DWORD>(windowConfig.debugMode);

        std::uint32_t startupApplicationFlags = flags();
        startupApplicationFlags = (startupApplicationFlags & ~0x00020000u) |
            (windowConfig.drawFps ? 0x00020000u : 0u);
        startupApplicationFlags = (startupApplicationFlags & ~0x00040000u) |
            (windowConfig.drawPresentation ? 0x00040000u : 0u);
        setFlags(startupApplicationFlags);

        as1::MAP& map = *as1::Map;
        reinterpret_cast<core::Application*>(this)->LoadVid(&startupObjectsResource);
        ::ShowCursor(TRUE);


        {
            as1::core::ApplicationVidTable& appVidTable = as1::core::GlobalApplicationVidTable();
            appVidTable.setWeaponSentinel(map.weaponTable());
            as1::EmptyVid->setWeaponRecord(appVidTable.weaponSentinel());
            void* const storage = ::operator new(0x44u, std::nothrow);
            as1::SPRITE_COLLECTOR* const created = storage
                ? new (storage) as1::SPRITE_COLLECTOR(as1::core::ApplicationMapWidth(),
                                                              as1::core::ApplicationMapHeight(),
                                                              appVidTable.slotData(),
                                                              appVidTable.count())
                : nullptr;
            as1::SetGlobalSpriteCollector(created);
        }
        startupObjectsResource.close();
        ::SetCursor(nullptr);

        as1::Mouse = new (std::nothrow) as1::MOUSE(
            as1::EmptyVid,
            graph->screenWidth() * 0.5f,
            graph->screenHeight() * 0.5f,
            0.0f,
            0,
            nullptr);
        as1::Mouse->Enable();
        const auto readControlString = [&windowConfig](const char* keyName, const as1::STRING& defaultValue) {
            as1::STRING configuredText;
            as1::core::profile_p::readProfileStringInto(
                configuredText,
                windowConfig.configPath,
                as1::STRING("control"),
                as1::STRING(keyName),
                defaultValue);
            return configuredText;
        };
        {
            const char leftBytes[2] = { static_cast<char>(0x25), '\0' };
            const char upBytes[2] = { static_cast<char>(0x26), '\0' };
            const char rightBytes[2] = { static_cast<char>(0x27), '\0' };
            const unsigned char downBytes[2] = { 0x04u, 0x38u };
            as1::STRING leftDefault(leftBytes);
            as1::STRING upDefault(upBytes);
            as1::STRING rightDefault(rightBytes);
            as1::STRING downDefault(reinterpret_cast<const char*>(downBytes), 2);
            windowConfig.control.left = readControlString("Left", leftDefault);
            windowConfig.control.up = readControlString("Up", upDefault);
            windowConfig.control.right = readControlString("Right", rightDefault);
            windowConfig.control.down = readControlString("Down", downDefault);
            windowConfig.control.relative = as1::core::profile_p::readProfileIntValue(
                windowConfig.configPath, as1::STRING("control"), as1::STRING("Relative"), 0);
            windowConfig.control.firstAction = readControlString("First", as1::STRING("LBUTTON"));
            windowConfig.control.secondAction = readControlString("Second", as1::STRING("RBUTTON"));
            windowConfig.control.previousWeapon = readControlString("Prev", as1::STRING("["));
            windowConfig.control.nextWeapon = readControlString("Next", as1::STRING("]"));
        }


        as1::input::InputControlKeys& controlKeys = as1::input::g_inputControlKeys;
        controlKeys.leftAlternate = static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<signed char>(windowConfig.control.left.c_str()[0])));
        controlKeys.upAlternate = static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<signed char>(windowConfig.control.up.c_str()[0])));
        controlKeys.rightAlternate = static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<signed char>(windowConfig.control.right.c_str()[0])));
        controlKeys.downAlternate = static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<signed char>(windowConfig.control.down.c_str()[0])));
        as1::input::g_relativeControlEnabled = windowConfig.control.relative;
        controlKeys.firstActionPrimary = static_cast<std::uint32_t>(
            decodeControlKeyName(windowConfig.control.firstAction));
        controlKeys.secondActionPrimary = static_cast<std::uint32_t>(
            decodeControlKeyName(windowConfig.control.secondAction));
        controlKeys.previousWeapon = static_cast<std::uint32_t>(
            decodeControlKeyName(windowConfig.control.previousWeapon));
        controlKeys.nextWeapon = static_cast<std::uint32_t>(
            decodeControlKeyName(windowConfig.control.nextWeapon));

        PLAYER_ARCADE* const player0 = new (std::nothrow) PLAYER_ARCADE(1, 0);
        playerPointerSlot(this, playerSlotOffset(0)) = player0;
        PLAYER_ARCADE* const player2 = new (std::nothrow) PLAYER_ARCADE(0, 2);
        playerPointerSlot(this, playerSlotOffset(2)) = player2;
        PLAYER_ARCADE* const player1 = new (std::nothrow) PLAYER_ARCADE(2, 1);
        playerPointerSlot(this, playerSlotOffset(1)) = player1;
        PLAYER_ARCADE* const player3 = new (std::nothrow) PLAYER_ARCADE(0, 3);
        playerPointerSlot(this, playerSlotOffset(3)) = player3;

        physicalFloatSlot(this, core::application_layout::ScrollMaxX) = as1::core::ApplicationMapWidth();
        physicalFloatSlot(this, core::application_layout::ScrollMaxY) = as1::core::ApplicationMapHeight();
        physicalFloatSlot(this, core::application_layout::ScrollMinX) = 0.0f;
        physicalFloatSlot(this, core::application_layout::ScrollMinY) = 0.0f;

        const as1::STRING startupCommandLine(commandLine);
        if (commandLine[0] != '\0' && std::strstr(commandLine, ".cfg") == nullptr)
        {
            (windowConfig.startMap).Assign(startupCommandLine);
        }
        else
        {
            as1::STRING configuredMapName;
            as1::core::profile_p::readProfileStringInto(
                configuredMapName,
                windowConfig.configPath,
                as1::STRING("game"),
                as1::STRING("StartMap"),
                as1::STRING("maps\\logo.map"));
            (windowConfig.startMap).Assign(configuredMapName);
        }

        (pendingCommand(this)).Assign(windowConfig.startMap);
        setFlags(flags() | 0x00000004u);
        return this;
    }


    

void ApplicationWin::transferFrom(SPRITE* sprite)
    {

        clearShellOwnedSpriteIfMatches(this, sprite);
        core::Application::transferFrom(sprite);
    }

    bool ApplicationWin::pumpOnce()
    {


        if (pumpNativeMessages())
            return true;

        if (as1::g_baseConstants->entries[10] != 0u)
        {
            const std::uint32_t lastCode = inputState(this).lastCode;
            switch (lastCode)
            {
            case 'O': setFlags(flags() ^ 0x00008800u); break;
            case 'P': toggleFlag(shellFlagsStorage(this), kShellTogglePause); break;
            case 'I': setFlags(flags() ^ 0x00001000u); break;
            case 'H': toggleFlag(shellFlagsStorage(this), kShellDrawLabels); break;
            case 'R': setFlags(flags() ^ 0x00002000u); break;
            case 'G': toggleFlag(shellFlagsStorage(this), kShellDispatchOverlayList); break;
            case '~': reinterpret_cast<core::Application*>(this)->ReloadVid(); break;
            default: break;
            }
        }

        bool waitForMessage = true;
        if ((flags() & kApplicationFramePumpFlag) || (shellFlagsStorage(this) & kShellForceFrame))
        {
            GRAPH* const waitGraph = Graph;
            waitForMessage = waitGraph->isModalRenderStateActive();
            if (!waitForMessage && as1::g_baseConstants->entries[10] != 0u &&
                (shellFlagsStorage(this) & kShellTogglePause) && inputState(this).lastCode != 0x70u)
                waitForMessage = true;
        }
        if (waitForMessage)
        {
            as1::core::SetCurrentTimeMilliseconds(as1::core::PreviousWorldTimeMilliseconds());
            WaitMessage();
            return false;
        }

        GRAPH* const preFrameGraph = Graph;


        const std::uint32_t preFrameGraphFlags =
            *reinterpret_cast<const std::uint32_t*>(
                reinterpret_cast<const std::uint8_t*>(preFrameGraph) + 0x34u);
        const bool sceneRequested =
            ((flags() & kApplicationFramePumpFlag) != 0u) ||
            ((preFrameGraphFlags & 0x80u) == 0u);
        bool sceneActive = sceneRequested && preFrameGraph->movieComObject(0) == nullptr;
        if (sceneActive && preFrameGraph->preTact() != 0)
            return false;

        as1::core::SetApplicationWorldFrameCounter(
            as1::core::ApplicationWorldFrameCounter() + 1u);
        const int demoResult = processDemoFrame();
        GRAPH* const activeGraph = Graph;
        if (demoResult == 999999)
        {
            activeGraph->PostTact(1);
            return false;
        }
        bool worldTick = sceneActive && demoResult != 0;

        (void)embeddedMenu(this).processInput(&inputState(this));

        (void)reinterpret_cast<as1::core::Application*>(this)->callScriptFunctionInternal(
            -1, 0, 0, 0);

        GRAPH* const postScriptGraph = Graph;
        if (worldTick && postScriptGraph->movieComObject(0) != nullptr)
        {
            worldTick = false;
            sceneActive = false;
        }

        {
            SPRITE* target = reinterpret_cast<MAP*>(this)->Flagman(static_cast<int>(activeStartupPlayerIndex()));
            if (target && target->armyBits() == 0u)
            {
                const CONSTANT* const constants = as1::g_baseConstants;
                if (constants->entries[20] != 0u)
                {
                    const std::uint32_t packed = constants->entries[20];
                    std::uint32_t subtractive = 0;
                    std::uint32_t additive = 0;
                    const std::uint32_t gammaMasks[4] = {0x0000007Fu, 0x00007F80u, 0x007F8000u, 0xFF800000u};
                    const std::uint32_t gammaSignBits[4] = {0x00000080u, 0x00008000u, 0x00800000u, 0x80000000u};
                    for (int gammaIndex = 0; gammaIndex < 4; ++gammaIndex)
                    {
                        if (packed & gammaSignBits[gammaIndex])
                            additive |= ((~packed) & gammaMasks[gammaIndex]) << 1;
                        else
                            subtractive |= (packed & gammaMasks[gammaIndex]) << 1;
                    }
                    const std::uint32_t baseGamma[2] = {subtractive, additive};
                    target->SetGamma(Gamma{baseGamma[0], baseGamma[1]});
                    if (SPRITE* child = target->childChain())
                    {
                        VID* const targetVid = target->Vid();
                        if (child->Vid() == targetVid->linkedVid())
                            child->SetGamma(Gamma{baseGamma[0], baseGamma[1]});
                    }
                }
            }
        }

        {
            SPRITE* target = activeAuxiliarySprite();
            const CONSTANT* const constants = as1::g_baseConstants;
            if (target && (flags() & kMapSelectSpriteUnderCursorFlag) != 0u &&
                (constants->entries[21] != 0u || constants->entries[22] != 0u))
            {
                const std::uint32_t maskedFlags = target->armyBits();
                const std::uint32_t packed = (maskedFlags == 0x00001000u)
                    ? constants->entries[21]
                    : constants->entries[22];
                std::uint32_t subtractive = 0;
                std::uint32_t additive = 0;
                const std::uint32_t gammaMasks[4] = {0x0000007Fu, 0x00007F80u, 0x007F8000u, 0xFF800000u};
                const std::uint32_t gammaSignBits[4] = {0x00000080u, 0x00008000u, 0x00800000u, 0x80000000u};
                for (int gammaIndex = 0; gammaIndex < 4; ++gammaIndex)
                {
                    if (packed & gammaSignBits[gammaIndex])
                        additive |= ((~packed) & gammaMasks[gammaIndex]) << 1;
                    else
                        subtractive |= (packed & gammaMasks[gammaIndex]) << 1;
                }
                const std::uint32_t baseGamma[2] = {subtractive, additive};
                target->SetGamma(Gamma{baseGamma[0], baseGamma[1]});
                if (SPRITE* child = target->childChain())
                {
                    VID* const targetVid = target->Vid();
                    if (child->Vid() == targetVid->linkedVid())
                        child->SetGamma(Gamma{baseGamma[0], baseGamma[1]});
                }
            }
        }

        Graph->Tact(worldTick ? 1 : 0);

        {
            const std::uint32_t neutralGamma[2] = {0u, 0u};
            const CONSTANT* const constants = as1::g_baseConstants;

            SPRITE* primary = reinterpret_cast<MAP*>(this)->Flagman(static_cast<int>(activeStartupPlayerIndex()));
            if (primary && constants->entries[20] != 0u)
            {
                primary = reinterpret_cast<MAP*>(this)->Flagman(static_cast<int>(activeStartupPlayerIndex()));
                if (primary->armyBits() == 0u)
                {
                    primary = reinterpret_cast<MAP*>(this)->Flagman(static_cast<int>(activeStartupPlayerIndex()));
                    primary->SetGamma(Gamma{neutralGamma[0], neutralGamma[1]});

                    primary = reinterpret_cast<MAP*>(this)->Flagman(static_cast<int>(activeStartupPlayerIndex()));
                    if (SPRITE* child = primary ? primary->childChain() : nullptr)
                    {
                        VID* const targetVid = primary->Vid();
                        if (child->Vid() == targetVid->linkedVid())
                            child->SetGamma(Gamma{neutralGamma[0], neutralGamma[1]});
                    }
                }
            }

            if (constants->entries[21] != 0u || constants->entries[22] != 0u)
            {
                SPRITE* auxiliary = activeAuxiliarySprite();
                if (auxiliary)
                {
                    auxiliary = activeAuxiliarySprite();
                    auxiliary->SetGamma(Gamma{neutralGamma[0], neutralGamma[1]});

                    auxiliary = activeAuxiliarySprite();
                    if (SPRITE* child = auxiliary ? auxiliary->childChain() : nullptr)
                    {
                        VID* const targetVid = auxiliary->Vid();
                        if (child->Vid() == targetVid->linkedVid())
                            child->SetGamma(Gamma{neutralGamma[0], neutralGamma[1]});
                    }
                }
            }
        }
        if ((flags() & kApplicationModalDispatchFlag) != 0u)
        {
            BaseSpriteList<0>& list = embeddedFrameSpriteList(this);
            for (int cursor = list.activeCount() - 1; cursor >= 0; --cursor)
            {
                if (SPRITE* const sprite = list.at(static_cast<std::size_t>(cursor)))
                    sprite->Tact();
            }
        }
        else
        {
            for (int pass = 0; pass < core::ApplicationDrawDispatcherState::PassCount; ++pass)
            {
                const as1::core::ApplicationDrawPassBucket& bucket = embeddedDrawPassBucket(this, pass);
                for (int cursor = bucket.count() - 1; cursor >= 0; --cursor)
                {
                    if (SPRITE* const sprite = bucket.spriteAt(cursor))
                        sprite->Tact();
                }
            }
        }

        const auto updateTooltip = [&]() noexcept
        {
            (void)cachedTooltipText();
            as1::input::InputMessageState& input = inputState(this);
            const std::uint32_t now = as1::core::RealCurrentTime;
            if (g_tooltipLastClientX != input.clientX ||
                g_tooltipLastClientY != input.clientY)
            {
                g_tooltipLastUpdateTime = now;
                g_tooltipLastClientX = input.clientX;
                g_tooltipLastClientY = input.clientY;
            }

            const CONSTANT* const constants = as1::g_baseConstants;
            const std::uint32_t idleThreshold = constants->entries[0x34u / sizeof(DWORD)];
            if (now - g_tooltipLastUpdateTime <= idleThreshold ||
                (input.flags & 1u) != 0u ||
                input.lastCode != 0u ||
                (applicationMenu().controlFlags() & 1u) != 0u ||
                (flags() & kMapSelectSpriteUnderCursorFlag) == 0u)
            {
                releaseShellOwnedSpriteOwner(this);
                return;
            }

            if (shellOwnedSpriteEmpty(this))
            {
                as1::core::ApplicationVidTable& table = as1::core::GlobalApplicationVidTable();
                VID* tooltipVid = EmptyVid;
                if (table.count() > 6)
                {
                    if (VID* const tooltipTemplateVid = table.slot(6))
                        tooltipVid = tooltipTemplateVid;
                }

                as1::STRING text;
                buildMouseTipText(&text);
                ::new (static_cast<void*>(&(cachedTooltipText()))) as1::STRING(text);
                if (tooltipVid == EmptyVid || cachedTooltipText().isEmpty())
                    return;

                float x = input.clientX + 5.0f;
                float y = input.clientY - tooltipVid->sizeY() + 3000.0f - 10.0f;
                GRAPH* const graph = Graph;
                const float halfHeight = tooltipVid->sizeY() * 0.5f;
                if (!(static_cast<float>(graph->ViewYMin()) < (halfHeight + y - 3000.0f)))
                    y = halfHeight + input.clientY + 3010.0f;

                const std::size_t glyphCount = std::strlen(cachedTooltipText().c_str()) + 2u;
                const float lineRight = static_cast<float>(glyphCount) * tooltipVid->sizeX() + x;
                if (lineRight > static_cast<float>(graph->ViewXMax()))
                    x = static_cast<float>(graph->ViewXMax()) -
                        static_cast<float>(glyphCount) * tooltipVid->sizeX();

                SPRITE* const created = reinterpret_cast<core::Application*>(core::ApplicationOwner())->CreateSprite(
                    tooltipVid, VECTOR{x, y, 3000.0f}, ANGLE(static_cast<unsigned char>(0)), nullptr);
                bindShellOwnedSprite(this, created);
                if (!created)
                    return;

                as1::STRING open;
                ::new (static_cast<void*>(&(open))) as1::STRING("{", cachedTooltipText().c_str());
                as1::STRING wrapped;
                ::new (static_cast<void*>(&(wrapped))) as1::STRING(open.c_str(), "}");
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
        
        };
        updateTooltip();
        updateCameraFromInput();

        const std::uint32_t applicationFlags = flags();
        const bool controlPath = ((applicationFlags & kApplicationModalDispatchFlag) == 0u) &&
                                 ((applicationFlags & kApplicationRenderControlsFlag) != 0u);
        if (controlPath && (applicationFlags & 0x00040000u) != 0u)
        {
            const std::uint32_t now = as1::core::RealCurrentTime;
            if (now - g_presentationVersionLastSpawnMs > 2000u)
            {
                GRAPH* const graph = Graph;
                as1::core::ApplicationVidTable& vidTable =
                    as1::core::GlobalApplicationVidTable();
                VID* presentationVid = EmptyVid;
                if (vidTable.count() > 2)
                {
                    if (VID* const presentationFallbackVid = vidTable.slot(2))
                        presentationVid = presentationFallbackVid;
                }

                SPRITE* const presentation = CreateSprite(
                    presentationVid,
                    VECTOR(graph->screenWidth() * 0.5f, 32.0f, 0.0f),
                    ANGLE(static_cast<unsigned char>(0)),
                    nullptr);
                STRING presentationText("Presentation version. Not for sale!");
                g_presentationVersionLastSpawnMs = now;
                presentation->Action(0x5F, 1, 0, 0);
                presentation->Action(0x78,
                    reinterpret_cast<std::intptr_t>(&presentationText), 0, 0);
                presentation->Action(0x28, 1000, 0, 0);
                presentation->ActionStack()->insertAt(0u, ACT{15u, 0u, 0u, 0u});
            }
        }

        if ((flags() & kApplicationModalDispatchFlag) == 0u &&
            (flags() & kApplicationRenderControlsFlag) != 0u)
        {
            for (int index = 0; index < 4; ++index)
            {
                PLAYER* const player = playerPointerSlot(this, playerSlotOffset(index));
                player->processInput(&inputState(this));
            }
        }

        if (worldTick)
            drawShellOverlays();

        if (sceneActive)
            Graph->PostTact(1);

        as1::sound::g_globalSoundEngine->updateSoundRequestQueue();
        return false;
    }


    void ApplicationWin::drawShellOverlays()
    {
        core::Application::drawShellOverlays();


        if (shellFlagsStorage(this) & kShellDrawLabels)
        {
            SPRITE_COLLECTOR* const hash = GlobalSpriteCollector();
            core::List<SPRITE*>& list = hash->mutableOverflowList();
            int* const cursor = hash->reverseCursorAddress();
            for (SPRITE* sprite = list.BeginIterate(cursor);
                 sprite;
                 sprite = list.NextIterate(cursor))
            {
                if (sprite->childBacklink() != nullptr)
                    continue;

                const core::ApplicationDrawDispatcherState& drawState =
                    core::GlobalApplicationDrawDispatcherState();
                Graph->PrintfXY(
                    sprite->X() - drawState.cameraShiftX(),
                    sprite->Y() - sprite->Z() - drawState.cameraShiftY(),
                    "%i", sprite->Hp());
            }
        }


        if (shellFlagsStorage(this) & kShellDispatchOverlayList)
        {
            SPRITE_COLLECTOR* const hash = GlobalSpriteCollector();
            core::List<SPRITE*>& list = hash->mutableOverflowList();
            int* const cursor = hash->reverseCursorAddress();
            for (SPRITE* sprite = list.BeginIterate(cursor);
                 sprite;
                 sprite = list.NextIterate(cursor))
            {
                sprite->DrawRelationDebugOverlay();
            }
        }
    }


    void ApplicationWin::deinitialize()
    {
        setFlags(flags() & ~kApplicationCleanupBusyFlag);
        g_spriteWorkList.deleteAllSprites();
        releaseShellOwnedSpriteOwner(this);
        core::Application::deinitialize();
    }


    __forceinline void ApplicationWin::destroyBaseApplicationStateImpl()
    {
        ApplicationWin* const self = this;

        core::ApplicationDrawDispatcherState& drawState =
            core::GlobalApplicationDrawDispatcherState();
        for (int pass = 0; pass < core::ApplicationDrawDispatcherState::PassCount; ++pass)
            drawState.drawPassBucket(pass).list.deleteAllSprites();

        if (Mouse)
        {
            MOUSE* const mouse = Mouse;


            delete mouse;
        }

        for (int index = 0; index < 4; ++index)
        {
            PLAYER*& player = playerPointerSlot(self, playerSlotOffset(index));
            if (player)
            {

                PLAYER* const owned = player;


                (void)owned->scalarDeletingDestructor(1u);
            }
        }

        if (SPRITE_COLLECTOR* const collector = g_spriteCollector)
        {
            collector->DeleteAll();
            ::operator delete(collector);
        }


        delete core::g_startupStringsIniPathOwner;

        if (as1::sound::Engine* const soundEngine = as1::sound::g_globalSoundEngine)
        {


            soundEngine->Destroy();
            ::operator delete(soundEngine);
        }

        if (CONSTANT* const constants = g_baseConstants)
            delete constants;

        delete core::g_startupRegistryPathOwner;

        core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
        for (int index = appVidTable.count() - 1; index >= 0; --index)
        {
            VID* const vid = appVidTable.slot(index);
            if (!vid)
                continue;
            delete vid;
            appVidTable.setSlotCell(index, nullptr);
        }
        appVidTable.setStoredCount(0);

        writeLogLine(g_fileLogger, "Vid    release %i %i",
                   static_cast<int>(g_textureMemoryBytes),
                   g_vidMemoryInUse);

        if (GRAPH* const graph = Graph)
        {
            graph->~GRAPH();
            ::operator delete(static_cast<void*>(graph));
        }

        if (short* const grid = core::ApplicationTerrainGrid())
            ::operator delete(static_cast<void*>(grid));
        if (short* const temporaryTerrainGrid = core::ApplicationTempTerrainGrid())
            ::operator delete(static_cast<void*>(temporaryTerrainGrid));

        if (WEAPON* const weapons = core::ApplicationWeaponTable())
            ::operator delete(static_cast<void*>(weapons));

        delete as1::g_fileLogger;


        ::CoUninitialize();
        ::timeEndPeriod(1u);

        destroyShellOwnedSpriteOwner(self);
        reinterpret_cast<GROUPS*>(reinterpret_cast<std::uint8_t*>(self) + core::application_layout::Groups)->~GROUPS();


        reinterpret_cast<MENU*>(
            reinterpret_cast<std::uint8_t*>(self) + core::application_layout::Menu)->~MENU();
        reinterpret_cast<RelationTable*>(reinterpret_cast<std::uint8_t*>(self) + core::application_layout::RelationTable)->~RelationTable();
        reinterpret_cast<RESOURCE*>(reinterpret_cast<std::uint8_t*>(self) + core::application_layout::DemoResource)->~RESOURCE();
        reinterpret_cast<SCRIPT*>(reinterpret_cast<std::uint8_t*>(self) + core::application_layout::ScriptRuntime)->~SCRIPT();


        for (int pass = as1::core::ApplicationDrawDispatcherState::PassCount - 1; pass >= 0; --pass)
        {
            auto* const bucket = reinterpret_cast<core::ApplicationDrawPassBucket*>(
                reinterpret_cast<std::uint8_t*>(self) + core::application_layout::DrawLayerOwners +
                static_cast<std::size_t>(pass) * core::application_layout::DrawLayerStride);
            bucket->~ApplicationDrawPassBucket();
        }

        resourceName(self).~STRING();
        previousMapName(self).~STRING();
        pendingCommand(self).~STRING();
        currentMapName(self).~STRING();
        applicationTitle(self).~STRING();
    }


    __forceinline void ApplicationWin::drawApplicationDebugPass()
    {
        GRAPH* const graph = Graph;
        const std::uint32_t applicationFlags = flags();

        if (applicationFlags & kDebugShowFps)
        {
            graph->PrintfXY(graph->ViewXMin(),
                            graph->ViewYMin() + 1.0f,
                            "%i",
                            static_cast<int>(as1::core::DisplayedFramesPerSecond()));
        }


        if (!(as1::g_baseConstants && as1::g_baseConstants->entries[10] != 0u))
            return;

        if (applicationFlags & kDebugShowSoundCount)
        {
            const as1::sound::Engine* const engine = as1::sound::g_globalSoundEngine;
            const int playing = engine->playingSoundCount();
            graph->PrintfXY(graph->ViewXMax() - 20.0f,
                            graph->ViewYMin() + 1.0f,
                            "%2i", playing);
        }

        if (applicationFlags & kDebugDrawTerrainGrid)
        {


            const short* const permanent = core::ApplicationTerrainGrid();
            const short* const temporary = core::ApplicationTempTerrainGrid();
            const int gridX = core::ApplicationTerrainGridWidth();
            const int gridY = core::ApplicationTerrainGridHeight();
            if (gridX > 1 && gridY > 1 && permanent)
            {
                const core::ApplicationDrawDispatcherState& drawState =
                    core::GlobalApplicationDrawDispatcherState();
                const float cameraX = drawState.cameraShiftX();
                const float cameraY = drawState.cameraShiftY();
                const float viewLeft = static_cast<float>(graph->ViewXMin());
                const float viewRight = static_cast<float>(graph->ViewXMax());
                const float viewTop = static_cast<float>(graph->ViewYMin());
                const float viewBottom = static_cast<float>(graph->ViewYMax());

                for (int y = 1; y < gridY; ++y)
                {
                    for (int x = 1; x < gridX; ++x)
                    {
                        const std::size_t index =
                            static_cast<std::size_t>(x + y * gridX);
                        int z1 = static_cast<int>(permanent[index]);
                        int z0 = static_cast<int>(permanent[index - 1u]);
                        const int currentTemporaryHeight = static_cast<int>(temporary[index]);
                        const int previousTemporaryHeight = static_cast<int>(temporary[index - 1u]);
                        if (currentTemporaryHeight > z1) z1 = currentTemporaryHeight;
                        if (previousTemporaryHeight > z0) z0 = previousTemporaryHeight;

                        const float x1 = static_cast<float>(8 * x + 4) - cameraX;
                        const float y1 = static_cast<float>(8 * y + 4 - z1) - cameraY;
                        const float x0 = static_cast<float>(8 * x - 4) - cameraX;
                        const float y0 = static_cast<float>(8 * y + 4 - z0) - cameraY;

                        const bool firstVisible =
                            x1 >= viewLeft && x1 < viewRight &&
                            y1 >= viewTop && y1 < viewBottom;
                        const bool secondVisible =
                            x0 >= viewLeft && x0 < viewRight &&
                            y0 >= viewTop && y0 < viewBottom;
                        if (firstVisible || secondVisible)
                            graph->Line(x1, y1, x0, y0, g_colorGray.color);
                    }
                }
            }
        }

        if (applicationFlags & kDebugDrawSpriteBuckets)
        {


            for (int pass = 0; pass < core::ApplicationDrawDispatcherState::PassCount - 1; ++pass)
            {
                const as1::core::ApplicationDrawPassBucket& bucket =
                    embeddedDrawPassBucket(this, pass);
                for (int index = bucket.count() - 1; index >= 0; --index)
                {
                    SPRITE* const sprite = bucket.spriteAt(index);
                    if (sprite)
                        sprite->DrawRectangle();
                }
            }
        }

        if (applicationFlags & kDebugDrawCurrentSprite)
        {


            PLAYER* const player = playerSlotByIndex(
                static_cast<int>(activeStartupPlayerIndex()));
            SPRITE* selectedSprite = player ? player->Flagman() : nullptr;
            if (!selectedSprite && player)
                selectedSprite = player->auxiliarySprite();
            if (!selectedSprite)
                selectedSprite = embeddedMenu(this).selectedSprite();
            if (selectedSprite)
                selectedSprite->DrawDebugOverlay();
        }

        if (applicationFlags & kDebugDrawScrollBox)
        {
            reinterpret_cast<GROUPS*>(
                reinterpret_cast<std::uint8_t*>(this) + core::application_layout::Groups)->DrawNumber();
        }

        if (applicationFlags & kDebugDrawAuxiliaryList)
        {


            core::g_rMap.DebugDraw();
        }
    }


    __forceinline void ApplicationWin::runCommandLineMap(char* ownedCommandLine)
    {

        STRING loadName;
        loadName.AdoptOwnedStorage(ownedCommandLine);

        setFlags(flags() | application_flags::MapLoading);
        if (loadName.isEmpty())
            return;

        MAP* const map = Map;
        if (!map)
            return;

        if (core::ApplicationMapWidth() != 0.0f ||
            core::ApplicationMapHeight() != 0.0f)
        {
            if ((as1::g_baseConstants && as1::g_baseConstants->entries[10] != 0u))
            {
                if (GRAPH* const graph = Graph)
                    DrawDebugText(graph, "Release previous map");
            }
            reinterpret_cast<core::Application*>(map)->core::Application::deinitialize();
        }

        if (!map->demoResource().isOpen() &&
            map->demoResource().openFile(&loadName, RESOURCE::ResTypes::DEMO) == 0)
        {
            (loadName).Read(&map->demoResource());
            setFlags(flags() | application_flags::DemoUseResource);
        }

        RESOURCE mapResource;


        const GamePath mapPath = loadName;
        if (mapResource.openFile(&mapPath, RESOURCE::ResTypes::MAP) != 0)
        {
            logAndShowError(g_fileLogger, "!!!ERROR!!!LOAD: Invalid map file %s", loadName.c_str());
            return;
        }

        if (Mouse)
            Mouse->Disable();

        (previousMapName(this)).Assign(currentMapName(this));
        (currentMapName(this)).Assign(loadName);

        bool usesCompactSpriteRecords = false;
        BYTE* const mapOwner = reinterpret_cast<BYTE*>(map);
        float& mapWidth = *reinterpret_cast<float*>(mapOwner + core::application_layout::MapExtentX);
        float& mapHeight = *reinterpret_cast<float*>(mapOwner + core::application_layout::MapExtentY);
        float& mapShiftX = *reinterpret_cast<float*>(mapOwner + core::application_layout::CameraShiftX);
        float& mapShiftY = *reinterpret_cast<float*>(mapOwner + core::application_layout::CameraShiftY);
        int mapVersion = 0;

        std::uint32_t demoStart = static_cast<std::uint32_t>(::timeGetTime());
        if ((flags() & application_flags::DemoUseResource) != 0u)
            map->demoResource().read(&demoStart, sizeof(demoStart));
        std::srand(static_cast<unsigned int>(demoStart));

        if ((as1::g_baseConstants && as1::g_baseConstants->entries[10] != 0u))
        {
            if (GRAPH* const graph = Graph)
                DrawDebugText(graph, "Load extra vid");
        }

        reinterpret_cast<core::Application*>(this)->LoadVid(&mapResource);


        SPRITE* const endSpriteMarker = reinterpret_cast<SPRITE*>(~static_cast<std::uintptr_t>(0));


        const bool hasGraphSection = (mapResource.GoBegin(RESOURCE::ResTypes::GRAPH) == 0);
        usesCompactSpriteRecords = !hasGraphSection;
        if (hasGraphSection)
        {
            if ((as1::g_baseConstants && as1::g_baseConstants->entries[10] != 0u) && Graph)
                DrawDebugText(Graph, "Load graph parameters");
            if (Graph)
                Graph->LoadParameters(&mapResource);
        }

        const int headResult = usesCompactSpriteRecords
            ? mapResource.GoBegin(RESOURCE::ResTypes::HEAD)
            : mapResource.GoNext(RESOURCE::ResTypes::HEAD);
        if (headResult != 0)
        {
            logFileLoggerResourceError(g_fileLogger, "MAP", 11, "HEAD", 0);
            return;
        }

        if (!usesCompactSpriteRecords)
        {


            mapResource.read(&mapWidth, 4);
            mapResource.read(&mapHeight, 4);
            mapResource.read(&mapShiftX, 4);
            mapResource.read(&mapShiftY, 4);
            mapResource.read(&core::g_currentTimeMilliseconds, 4);
        }
        else
        {
            int ix = 0;
            int iy = 0;
            std::int16_t sx = 0;
            std::int16_t sy = 0;
            mapResource.read(&ix, 4);
            mapResource.read(&iy, 4);
            mapResource.read(&sx, 2);
            mapResource.read(&sy, 2);

            mapWidth = static_cast<float>(ix);
            mapHeight = static_cast<float>(iy);
            mapShiftX = static_cast<float>(sx);
            mapShiftY = static_cast<float>(sy);

            mapResource.read(&core::g_currentTimeMilliseconds, 4);
            setWorldStartTime(core::CurrentTimeMilliseconds());
            core::SetPreviousWorldTimeMilliseconds(core::CurrentTimeMilliseconds());
            mapResource.read(&mapVersion, 4);
            if (Graph)
                Graph->OldLoadParameters(&mapResource);
            usesCompactSpriteRecords = true;
        }

        if (!usesCompactSpriteRecords)
        {
            core::SetPreviousWorldTimeMilliseconds(1u);
            core::SetCurrentTimeMilliseconds(10u);

            std::uint32_t loadFlags = flags();
            if ((loadFlags & application_flags::DemoUseResource) == 0u && map->demoResource().isOpen())
            {
                loadFlags |= application_flags::DemoWriteToResource;
                setFlags(loadFlags);
                map->demoResource().BeginSection(RESOURCE::ResTypes::DEMO);
                const unsigned mapNameBytes = static_cast<unsigned>(std::strlen(loadName.c_str()) + 1u);
                map->demoResource().write(loadName.c_str(), mapNameBytes);
                map->demoResource().write(&demoStart, sizeof(demoStart));
                const std::uint32_t worldClock = core::CurrentTimeMilliseconds();
                map->demoResource().write(&worldClock, sizeof(worldClock));
            }

            if ((loadFlags & application_flags::DemoUseResource) != 0u)
            {
                std::uint32_t playbackTime = core::CurrentTimeMilliseconds();
                map->demoResource().read(&playbackTime, sizeof(playbackTime));
                core::SetCurrentTimeMilliseconds(playbackTime);
            }


            setWorldStartTime(core::CurrentTimeMilliseconds());
            mapResource.read(&mapVersion, 4);
            if (mapVersion <= 9)
            {
                int ix = 0, iy = 0, sx = 0, sy = 0;
                std::memcpy(&ix, &mapWidth, 4);
                std::memcpy(&iy, &mapHeight, 4);
                std::memcpy(&sx, &mapShiftX, 4);
                std::memcpy(&sy, &mapShiftY, 4);
                mapWidth = static_cast<float>(ix);
                mapHeight = static_cast<float>(iy);
                mapShiftX = static_cast<float>(sx);
                mapShiftY = static_cast<float>(sy);
            }
        }
        if (!usesCompactSpriteRecords)
        {
            writeLogLine(g_fileLogger, "CurrentTime   =%-15u   sizeof(SPRITE)=%-8i Map version   =%i",
                       core::CurrentTimeMilliseconds(), static_cast<int>(sizeof(SPRITE)), mapVersion);
        }

        map->setTerrainGridDimensions(terrainGridDimension(mapWidth),
                                      terrainGridDimension(mapHeight));

        if (!usesCompactSpriteRecords && (as1::g_baseConstants && as1::g_baseConstants->entries[10] != 0u) && Graph)
            DrawDebugText(Graph, "Create new hash table");
        {
            core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
            if (SPRITE_COLLECTOR* const collector = g_spriteCollector)
            {
                collector->DeleteAll();
                ::operator delete(collector);
            }
            void* const storage = ::operator new(sizeof(SPRITE_COLLECTOR), std::nothrow);
            g_spriteCollector = storage
                ? new (storage) SPRITE_COLLECTOR(core::ApplicationMapWidth(),
                                                core::ApplicationMapHeight(),
                                                appVidTable.slotData(),
                                                appVidTable.count())
                : nullptr;
        }

        {
            const float scrollMaxX = mapWidth;
            *reinterpret_cast<float*>(mapOwner + core::application_layout::ScrollMinX) = 0.0f;
            *reinterpret_cast<float*>(mapOwner + core::application_layout::ScrollMaxX) = scrollMaxX;
            const float scrollMaxY = mapHeight;
            *reinterpret_cast<float*>(mapOwner + core::application_layout::ScrollMinY) = 0.0f;
            *reinterpret_cast<float*>(mapOwner + core::application_layout::ScrollMaxY) = scrollMaxY;
        }

        if (Graph && !usesCompactSpriteRecords)
        {
            map->SetShiftCoor(mapShiftX + Graph->screenWidth() * 0.5f,
                              mapShiftY + Graph->screenHeight() * 0.5f,
                              0);
        }


        if (!usesCompactSpriteRecords && (as1::g_baseConstants && as1::g_baseConstants->entries[10] != 0u) && Graph)
            DrawDebugText(Graph, "Load gridZ");
        map->ResetGroundZ();
        const int gridX = map->terrainGridWidth();
        const int gridY = map->terrainGridHeight();
        if (mapResource.GoNext(RESOURCE::ResTypes::GRID))
        {
            (void)mapResource.GoBegin(RESOURCE::ResTypes::ANY);
        }
        else
        {
            const int expectedBytes = 2 * gridX * gridY;
            if (short* const oldGrid = core::ApplicationTerrainGrid())
                ::operator delete(static_cast<void*>(oldGrid));
            core::SetApplicationTerrainGrid(nullptr);
            void* loadedGrid = nullptr;
            const int actualBytes = mapResource.SubLoad(&loadedGrid, nullptr);
            core::SetApplicationTerrainGrid(static_cast<short*>(loadedGrid));
            if (actualBytes != expectedBytes)
            {
                logFileLoggerResourceError(g_fileLogger, "MAP", 4, "grid", actualBytes);
                map->ResetGroundZ();
            }
        }



        if (!usesCompactSpriteRecords && (as1::g_baseConstants && as1::g_baseConstants->entries[10] != 0u) && Graph)
            DrawDebugText(Graph, "Load hardware terrain");
        if (mapResource.GoNext(RESOURCE::ResTypes::SPRITE))
        {
            logFileLoggerResourceError(g_fileLogger, "MAP", 11, "SPR ", 0);
            return;
        }
        int previousLayer = 0;
        while (true)
        {
            SPRITE* sprite = usesCompactSpriteRecords
                ? map->OldLoadSprite(&mapResource)
                : map->LoadSprite(&mapResource, mapVersion);
            if (sprite == endSpriteMarker)
                break;

            if (!usesCompactSpriteRecords)
            {
                if ((as1::g_baseConstants && as1::g_baseConstants->entries[10] != 0u) && sprite && Graph)
                {
                    if (VID* const vid = sprite->Vid())
                    {
                        const int layer = vid->renderLayer();
                        if (layer != previousLayer)
                        {
                            previousLayer = layer;
                            const char* stage = "Load sprites";
                            if (layer == 0)
                                stage = "Load hardware terrain";
                            else if (layer == 1)
                                stage = "Build sprites in terrain";
                            else if (layer == 2)
                                stage = "Build sprites with alpha in terrain";
                            DrawDebugText(Graph, stage);
                        }
                    }
                }
                if (mapResource.CurrentResourceSize() > 1000u &&
                    Graph->DrawLoadBar(core::GlobalApplicationVidTable().slot(0)))
                {
                    (void)0;
                }
            }
        }

        as1::core::g_rMap.CreateAdditionalDots();


        if (!usesCompactSpriteRecords && (as1::g_baseConstants && as1::g_baseConstants->entries[10] != 0u) && Graph)
            DrawDebugText(Graph, "Load data for sprite");
        if (mapResource.GoNext(RESOURCE::ResTypes::SPRITEDATA))
        {
            logFileLoggerResourceError(g_fileLogger, "MAP", 11, "SPRD", 0);
            return;
        }
        const int restoreOpcode = usesCompactSpriteRecords
            ? SpriteActConst::ACT_RESTORE_OLD_MAP
            : SpriteActConst::ACT_RESTORE;
        while (true)
        {
            SPRITE* const sprite = map->ReadPointer(&mapResource);
            if (sprite == endSpriteMarker)
                break;

            if (!usesCompactSpriteRecords &&
                mapResource.CurrentResourceSize() > 1000u &&
                Graph->DrawLoadBar(core::GlobalApplicationVidTable().slot(0)))
            {
                (void)0;
            }

            if (sprite)
            {
                sprite->dispatchVirtualAction(
                    static_cast<std::uint32_t>(restoreOpcode),
                    static_cast<int>(reinterpret_cast<std::uintptr_t>(&mapResource)),
                    mapVersion,
                    0);
            }
            if (mapResource.GoNextSub(RESOURCE::ResTypes::SPRITEDATA))
                break;
        }

        if (!usesCompactSpriteRecords)
        {
            if ((as1::g_baseConstants && as1::g_baseConstants->entries[10] != 0u) && Graph)
                DrawDebugText(Graph, "Load players info");
            if (mapResource.GoNext(RESOURCE::ResTypes::PLAY))
            {
                logFileLoggerResourceError(g_fileLogger, "MAP", 11, "PLAY", 0);
                return;
            }
            PLAYER* const players[4] = {
                playerSlotByIndex(0), playerSlotByIndex(1),
                playerSlotByIndex(2), playerSlotByIndex(3)
            };
            for (PLAYER* const player : players)
            {
                (void)player->loadControlledSpriteReference(&mapResource);
            }

            if ((as1::g_baseConstants && as1::g_baseConstants->entries[10] != 0u) && Graph)
                DrawDebugText(Graph, "Load groups info");
            if (mapResource.GoNext(RESOURCE::ResTypes::GROUP))
            {
                logFileLoggerResourceError(g_fileLogger, "MAP", 11, "GROU", 0);
                return;
            }
            map->groupOwner().Load(&mapResource);
        }

        setFlags(flags() & ~application_flags::MapLoading);
        mapResource.close();
        map->relationTable().clear();

        writeLogLine(g_fileLogger, "Vid    release %i %i",
                   static_cast<int>(g_textureMemoryBytes),
                   g_vidMemoryInUse);

        if ((as1::g_baseConstants && as1::g_baseConstants->entries[10] != 0u) && Graph)
            DrawDebugText(Graph, "Load script file");


        const STRING scriptBase = loadName.BeforeLast(".");
        STRING requestedLgc = scriptBase + ".lgc";
        STRING requestedLgd = scriptBase + ".lgd";
        STRING chosenPath(requestedLgc);
        std::FILE* probe = std::fopen(requestedLgc.c_str(), "rb");
        const bool hasLgc = (probe != nullptr);
        if (probe)
            std::fclose(probe);
        if (!hasLgc)
        {
            probe = std::fopen(requestedLgd.c_str(), "rb");
            if (probe)
            {
                std::fclose(probe);
                (chosenPath).Assign(requestedLgd);
            }
        }
        (void)map->getScript().Load(chosenPath);

        if ((as1::g_baseConstants && as1::g_baseConstants->entries[10] != 0u) && Graph)
            DrawDebugText(Graph, "Connect script function with VID");


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

        const int functionCount = map->getScript().functionCount();
        for (int functionIndex = 0; functionIndex < functionCount; ++functionIndex)
        {
            const script::LogicFunctionRecord* const fnPtr = map->getScript().functionRecordAt(functionIndex);
            if (!fnPtr)
                continue;
            const script::LogicFunctionRecord& fn = *fnPtr;
            if (fn.flags != 3)
                continue;

            const std::string name = std::string(fn.name.c_str());
            for (std::size_t globalIndex = 0;
                 globalIndex < (sizeof(kGlobalFunctionNames) / sizeof(kGlobalFunctionNames[0]));
                 ++globalIndex)
            {
                if (name == kGlobalFunctionNames[globalIndex])
                    core::EvFunctionNumber[globalIndex] = functionIndex;
            }

            if (name.size() < 4 || name[0] != 'F' ||
                !std::isdigit(static_cast<unsigned char>(name[1])) ||
                !std::isdigit(static_cast<unsigned char>(name[2])) ||
                !std::isdigit(static_cast<unsigned char>(name[3])))
                continue;

            const int threeDigitNvid = (name[1] - '0') * 100 + (name[2] - '0') * 10 + (name[3] - '0');
            int nVid = threeDigitNvid;
            std::size_t suffix = 0;
            bool threeDigitVidName = false;
            if (name.size() >= 5 && name[4] == '_')
            {

                threeDigitVidName = true;
                suffix = 5;
            }
            else if (name.size() >= 6 &&
                     std::isdigit(static_cast<unsigned char>(name[4])) &&
                     name[5] == '_')
            {
                nVid = threeDigitNvid * 10 + (name[4] - '0');
                suffix = 6;
            }
            else
            {
                continue;
            }

            const std::string callbackName = name.substr(suffix);
            if (threeDigitVidName && callbackName == "DAMAGE")
            {
                if (fn.itemCount != 3)
                {
                    STRING detail("no parameters in functions '");
                    detail += fn.name;
                    detail += "'";
                    logFileLoggerResourceError(g_fileLogger, "MAP", 4, detail.c_str(), fn.itemCount);
                    continue;
                }
                if (map->ValidateVid(nVid))
                    map->Vid(nVid)->setDamageInterceptScriptFunction(functionIndex);
                continue;
            }
            if (threeDigitVidName && callbackName == "DESTROY")
            {
                if (fn.itemCount != 1)
                {
                    STRING detail("no parameters in functions '");
                    detail += fn.name;
                    detail += "'";
                    logFileLoggerResourceError(g_fileLogger, "MAP", 4, detail.c_str(), fn.itemCount);
                    continue;
                }
                if (map->ValidateVid(nVid))
                    map->Vid(nVid)->setScriptFunctionAt(VID::DestroyScriptFunctionIndex, functionIndex);
                continue;
            }
            if (threeDigitVidName && callbackName == "COLLISION")
            {
                if (fn.itemCount != 2)
                {
                    STRING detail("no parameters in functions '");
                    detail += fn.name;
                    detail += "'";
                    logFileLoggerResourceError(g_fileLogger, "MAP", 4, detail.c_str(), fn.itemCount);
                    continue;
                }
                if (map->ValidateVid(nVid))
                    map->Vid(nVid)->setCollisionScriptFunction(functionIndex);
                continue;
            }

            if (callbackName.empty() ||
                !std::isdigit(static_cast<unsigned char>(callbackName[0])))
                continue;

            int nAnim = callbackName[0] - '0';
            if (callbackName.size() >= 2 &&
                std::isdigit(static_cast<unsigned char>(callbackName[1])))
                nAnim = nAnim * 10 + (callbackName[1] - '0');

            if (fn.itemCount != 1)
            {
                STRING detail("no parameters in functions '");
                detail += fn.name;
                detail += "'";
                logFileLoggerResourceError(g_fileLogger, "MAP", 4, detail.c_str(), fn.itemCount);
                continue;
            }
            if (map->ValidateVid(nVid) && nAnim < 0x11)
                map->Vid(nVid)->setScriptFunctionAt(nAnim, functionIndex);
        }

        const std::uint32_t stackFlags = flags();
        if ((stackFlags & application_flags::DemoUseResource) != 0u)
            map->getScript().readExecutionStackFromStream(&map->demoResource());
        else if ((stackFlags & application_flags::DemoWriteToResource) != 0u)
            map->getScript().writeExecutionStackToStream(&map->demoResource());

        core::RealCurrentTime = static_cast<std::uint32_t>(::timeGetTime());
        if ((as1::g_baseConstants && as1::g_baseConstants->entries[10] != 0u) && Graph)
            DrawDebugText(Graph, "Run scripts for create sprites");

        core::ApplicationDrawDispatcherState& drawState = core::GlobalApplicationDrawDispatcherState();
        if ((flags() & 0x00000001u) == 0u)
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
                    if (functionIndex >= 0 &&
                        (flags() & application_flags::ScriptCallbacksDisabled) == 0u)
                    {
                        const int spriteArg = static_cast<int>(static_cast<std::uint32_t>(
                            reinterpret_cast<std::uintptr_t>(sprite)));
                        (void)core::Application::callScriptFunction(functionIndex, spriteArg, 0, 0);
                    }
                }
            }
        }

        for (int pass = 0; pass < core::ApplicationDrawDispatcherState::PassCount; ++pass)
        {
            const core::ApplicationDrawPassBucket& bucket = embeddedDrawPassBucket(this, pass);
            for (int cursor = bucket.count() - 1; cursor >= 0; --cursor)
            {
                SPRITE* const sprite = bucket.spriteAt(cursor);
                if (!sprite)
                    continue;
                if (sprite->Vid()->spriteClassId() == 21u)
                    static_cast<ENGINE*>(sprite)->RepairReciprocalChainLinks();
            }
        }

        if ((flags() & application_flags::DemoUseResource) == 0u && Mouse)
            Mouse->Enable();
        if ((as1::g_baseConstants && as1::g_baseConstants->entries[10] != 0u) && Graph)
            DrawDebugText(Graph, "");
    }


    SPRITE* ApplicationWin::CreateSprite(VID* vid, VECTOR xyz, ANGLE direction, SPRITE* parent)
    {
        if (!vid)
            return nullptr;

        VID* selectedVid = vid;
        if ((selectedVid->runtimeAuxFlags() & 0x10u) != 0u)
            selectedVid = resolveRegionMappedVid(selectedVid, xyz.x, xyz.y, xyz.z);

        const int liveLimit = selectedVid->unitLimit(0);
        if (liveLimit >= 0 && static_cast<int>(selectedVid->NoSprites()) >= liveLimit)
            return nullptr;

        SPRITE* sprite = nullptr;
        switch (selectedVid->spriteClassId())
        {
        case B_TERRAIN:
        case B_OBJECT:
            sprite = new TERRAIN(selectedVid, xyz.x, xyz.y, xyz.z, direction, parent);
            break;
        case B_BUILDING:
            sprite = new BUILDING(selectedVid, xyz.x, xyz.y, xyz.z, direction, parent);
            break;
        case B_RAIL:
            sprite = new RAIL(selectedVid, xyz.x, xyz.y, xyz.z, direction, parent);
            break;
        case B_DEPO:
            sprite = new DEPO(selectedVid, xyz.x, xyz.y, xyz.z, direction, parent);
            break;
        case B_CIV_ROBOT:
            sprite = new CIV_ROBOT(selectedVid, xyz.x, xyz.y, xyz.z, direction, parent);
            break;
        case B_ENGINE:
            sprite = new ENGINE(selectedVid, xyz.x, xyz.y, xyz.z, direction, parent);
            break;
        case B_CREATURE:
            sprite = new CREATURE(selectedVid, xyz.x, xyz.y, xyz.z, direction, parent);
            break;
        case B_BALLOON:
            sprite = new BALLOON(selectedVid, xyz.x, xyz.y, xyz.z, direction, parent);
            break;
        default:
            return core::Application::CreateSprite(selectedVid, xyz, direction, parent);
        }

        const std::uint32_t appFlags = *reinterpret_cast<const std::uint32_t*>(
            reinterpret_cast<const std::uint8_t*>(this) + as1::core::application_layout::Flags);
        if (sprite && (appFlags & application_flags::MapLoading) == 0u)
        {
            const int functionIndex = sprite->Vid()->birthScriptFunction();
            if (functionIndex >= 0)
            {
                const int spriteArg = static_cast<int>(static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(sprite)));
                reinterpret_cast<as1::core::Application*>(this)->callScriptFunctionInternal(
                    functionIndex, spriteArg, 0);
            }
        }
        return sprite;
    }


    __forceinline bool ApplicationWin::dispatchWindowMessageImpl(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
    {


        if ((flags() & kApplicationFramePumpFlag) != 0u)
        {


            const int inputHandled = inputState(this).handleWindowMessage(
                reinterpret_cast<std::uintptr_t>(hwnd),
                static_cast<std::uint32_t>(msg),
                static_cast<std::uint32_t>(wparam),
                static_cast<std::uint32_t>(lparam));
            if (inputHandled != 0)
            {
                return true;
            }
        }

        GRAPH* const graph = Graph;

        if (msg > 0x211u)
        {
            if (msg == WM_EXITMENULOOP || msg == WM_EXITSIZEMOVE)
            {
                graph->EndPause();
                as1::sound::g_globalSoundEngine->resumeMusicAndRestoreActiveBuffers();
            }
            else if (msg == WM_ENTERSIZEMOVE)
            {
                graph->BeginPause();
                as1::sound::g_globalSoundEngine->pauseMusicAndStopActiveBuffers();
            }
            return false;
        }

        if (msg == WM_ENTERMENULOOP)
        {
            graph->BeginPause();
            as1::sound::g_globalSoundEngine->pauseMusicAndStopActiveBuffers();
            return false;
        }

        if (msg > WM_ACTIVATEAPP)
        {
            if (msg == WM_SYSCOMMAND)
            {
                const std::uintptr_t command = static_cast<std::uintptr_t>(wparam);
                if ((command == 0xF000u || command == 0xF010u || command == 0xF030u || command == 0xF170u) &&
                    graph->fullscreenRequested())
                {
                    return true;
                }
            }
            return false;
        }

        if (msg == WM_ACTIVATEAPP)
        {
            const std::uint32_t oldFlags = flags();
            const bool becomesActive = wparam != 0;

            setFlags((oldFlags & ~kApplicationFramePumpFlag) |
                     (becomesActive ? kApplicationFramePumpFlag : 0u));

            if (as1::sound::g_globalSoundEngine && Mouse)
            {
                if (becomesActive)
                {
                    as1::sound::g_globalSoundEngine->resumeMusicAndRestoreActiveBuffers();
                    Mouse->Enable();
                }
                else
                {
                    as1::sound::g_globalSoundEngine->pauseMusicAndStopActiveBuffers();
                    Mouse->Disable();
                }
            }
            return false;
        }

        if (msg == WM_DESTROY)
        {
            if (!graph->fullscreenRequested())
            {
                RECT rect;
                ::GetWindowRect(mainWindowHandle(this), &rect);
                if (as1::core::g_startupRegistryPathOwner)
                {
                    const as1::STRING* const registryPath = as1::core::g_startupRegistryPathOwner->Path();
                    registryPath->WriteRegistryInt(as1::STRING("WindowPositionX"), rect.left);
                    registryPath->WriteRegistryInt(as1::STRING("WindowPositionY"), rect.top);
                }
            }
            mainWindowHandle(this) = nullptr;
            ::PostQuitMessage(0);
            return false;
        }

        if (msg == WM_PAINT)
        {
            writeLogLine(g_fileLogger, "WM_PAINT");
            return false;
        }

        return false;
    }

    as1::STRING ApplicationWin::selectFile(bool save, const char* filter)
    {
        char fileName[4096] = {};
        OPENFILENAMEA ofn{};
        ofn.lStructSize = 76u;
        ofn.hwndOwner = mainWindowHandle(this);
        ofn.hInstance = applicationInstanceHandle(this);
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = 1u;
        ofn.lpstrFile = fileName;
        ofn.nMaxFile = 4096u;
        ofn.lpstrInitialDir = "maps";
        ofn.Flags = save ? 0x0008080Cu : 0x0008180Cu;
        ofn.lpstrDefExt = "map";

        GRAPH* const graph = Graph;
        graph->BeginPause();
        const BOOL selected = save ? ::GetSaveFileNameA(&ofn) : ::GetOpenFileNameA(&ofn);
        graph->EndPause();

        return selected ? as1::STRING(fileName) : as1::STRING();
    }


    LRESULT CALLBACK AppWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
    {

        ApplicationWin* const app = applicationWinInstance();
        if (app->dispatchWindowMessage(hwnd, msg, wparam, lparam))
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
            if ((as1::g_baseConstants && as1::g_baseConstants->entries[10] != 0u))
            {
                static const char mapOpenFilter[] =
                    "Map Files\0*.map\0"
                    "Save Files\0*.sav\0"
                    "Demo Files\0*.dem\0"
                    "All Files\0*.*\0\0";

                STRING selected = app->selectFile(false, mapOpenFilter);
                app->runCommandLine(selected.DetachOwnedStorage());
            }
            return 0;
        case IDM_FILE_SAVE:
            if ((as1::g_baseConstants && as1::g_baseConstants->entries[10] != 0u))
                app->saveMap(STRING("current.sav"));
            return 0;
        case IDM_FILE_EXIT:
            ::PostMessageA(hwnd, WM_CLOSE, 0, 0);
            return 0;
        case IDM_OPTIONS_ACCEL:
        {
            STRING date;
            constructCurrentDateString(date);
            STRING screensAndDate;
            ::new (static_cast<void*>(&(screensAndDate))) as1::STRING("Screens\\", date.c_str());

            STRING datedPrefix;
            ::new (static_cast<void*>(&(datedPrefix))) as1::STRING(screensAndDate.c_str(), " ");

            STRING time;
            constructCurrentTimeString(time);
            STRING dateAndTime;
            ::new (static_cast<void*>(&(dateAndTime))) as1::STRING(datedPrefix.c_str(), time.c_str());

            STRING outputPath;
            ::new (static_cast<void*>(&(outputPath))) as1::STRING(dateAndTime.c_str(), ".tga");
            outputPath.Replace(":", "h");
            outputPath.Replace(":", "m");
            outputPath.Replace(":", "s");

            GRAPH* const graph = Graph;
            graph->SaveTGA(&outputPath,
                           0,
                           0,
                           static_cast<int>(graph->screenWidth()),
                           static_cast<int>(graph->screenHeight()));
            return 0;
        }
        default:
            return 0;
        }
    }


    __forceinline void ApplicationWin::saveMapImpl(const as1::STRING& outputName)
    {

        RESOURCE output;
        if (std::strcmp(outputName.c_str(), STRING::SharedEmptyText()) == 0)
            return;

        static const char kTemporaryMapName[] = "tmp_del!.map";
        STRING& activeMapName = currentMapName(this);
        const bool replacingCurrentMap = std::strcmp(outputName.c_str(), activeMapName.c_str()) == 0;
        if (replacingCurrentMap)
        {


            ::MoveFileA(activeMapName.c_str(), kTemporaryMapName);
            (activeMapName).Assign(kTemporaryMapName);
        }

        if (output.OpenForWrite(&outputName, RESOURCE::ResTypes::MAP) != 0)
        {
            writeLogLine(g_fileLogger, "Can't open file %s", outputName.c_str());
            return;
        }

        bool copyObjectSections = false;
        for (int i = 0; i < physicalVidCount(this); ++i)
        {
            VID* const vid = physicalVidSlotUnchecked(this, i);
            if (vid && (vid->formatFlags() & 0x0200u) != 0u)
            {
                copyObjectSections = true;
                break;
            }
        }
        if (copyObjectSections)
        {
            RESOURCE previous;
            if (previous.openFile(&activeMapName, RESOURCE::ResTypes::MAP) == 0)
            {
                output.Copy(&previous, RESOURCE::ResTypes::WEAPON);
                output.Copy(&previous, RESOURCE::ResTypes::OBJECT);
                previous.close();
            }
            else
            {
                writeLogLine(g_fileLogger, "Can't open file '%s', needed for save map", activeMapName.c_str());
            }
        }

        output.BeginSection(RESOURCE::ResTypes::GRAPH);
        Graph->SaveParameters(&output);
        output.EndSection();

        output.BeginSection(RESOURCE::ResTypes::HEAD);
        const float sizeX = physicalFloatSlot(this, core::application_layout::MapExtentX);
        const float sizeY = physicalFloatSlot(this, core::application_layout::MapExtentY);
        const float shiftX = physicalFloatSlot(this, core::application_layout::CameraShiftX);
        const float shiftY = physicalFloatSlot(this, core::application_layout::CameraShiftY);
        output.write(&sizeX, 4u);
        output.write(&sizeY, 4u);
        output.write(&shiftX, 4u);
        output.write(&shiftY, 4u);
        const std::uint32_t now = core::CurrentTimeMilliseconds();
        output.write(&now, 4u);

        const int storedVersion = 12;
        output.write(&storedVersion, 4u);
        output.EndSection();


        if (const short* const grid = physicalTerrainGrid(this))
        {
            const int gridWidth = physicalIntSlot(this, core::application_layout::TerrainGridWidth);
            const int gridHeight = physicalIntSlot(this, core::application_layout::TerrainGridHeight);
            bool anyGridValue = false;
            const int cells = gridWidth * gridHeight;
            for (int i = 0; i < cells; ++i)
            {
                if (grid[i] != 0)
                {
                    anyGridValue = true;
                    break;
                }
            }
            if (anyGridValue)
            {
                output.BeginSection(RESOURCE::ResTypes::GRID);
                output.write(grid, static_cast<unsigned>(2u * static_cast<unsigned>(cells)));
                output.EndSection();
            }
        }

        const BaseSpriteList<0>& frameList = embeddedFrameSpriteList(this);
        auto forEachSaveSprite = [&](const auto& fn)
        {
            for (int pass = 0; pass < core::ApplicationDrawDispatcherState::PassCount; ++pass)
            {
                const core::ApplicationDrawPassBucket& bucket = embeddedDrawPassBucket(this, pass);
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
        forEachSaveSprite([&](SPRITE* sprite) { sprite->serializeSpriteRecord(&output); });
        const std::int32_t spriteTerminator = -1;
        output.write(&spriteTerminator, 4u);
        output.EndSection();

        forEachSaveSprite([&](SPRITE* sprite)
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
        reinterpret_cast<GROUPS*>(reinterpret_cast<std::uint8_t*>(this) + core::application_layout::Groups)->Save(&output);
        output.EndSection();
        output.close();

        if (std::strcmp(activeMapName.c_str(), kTemporaryMapName) == 0)
        {

            ::DeleteFileA(kTemporaryMapName);
            (activeMapName).Assign(outputName);
        }
    }


    bool ApplicationWin::pumpNativeMessages()
    {


        const std::uint32_t previousRealTime = as1::core::RealCurrentTime;
        std::uint32_t sampleForDelta = 0;
        do
        {
            sampleForDelta = static_cast<std::uint32_t>(::timeGetTime());
        }
        while (sampleForDelta == previousRealTime);

        std::uint32_t elapsed = sampleForDelta - previousRealTime;
        as1::core::PrevRealCurrentTime = previousRealTime;
        as1::core::RealCurrentTime = static_cast<std::uint32_t>(::timeGetTime());

        const std::uint32_t current = as1::core::CurrentTimeMilliseconds();
        as1::core::SetPreviousWorldTimeMilliseconds(current);
        if (elapsed > kFrameClampMs)
            elapsed = kFrameClampMs;

        const float tickScale = physicalFloatSlot(this, core::application_layout::TickScale);
        const long double scaled = static_cast<long double>(elapsed) * static_cast<long double>(tickScale);
        const std::int64_t converted = static_cast<std::int64_t>(std::trunc(scaled));
        const std::uint32_t increment = static_cast<std::uint32_t>(converted);
        as1::core::SetCurrentTimeMilliseconds(current + increment);


        auto* const selfBytes = reinterpret_cast<std::uint8_t*>(this);
        std::uint32_t& fpsCounter = *reinterpret_cast<std::uint32_t*>(
            selfBytes + core::application_layout::FpsCounter);
        const std::uint32_t framesThisWindow = ++fpsCounter;
        const std::uint32_t fpsNow = as1::core::RealCurrentTime;
        std::uint32_t& lastFpsSample = as1::prev_second_time;
        if (fpsNow - lastFpsSample >= 1000u)
        {
            lastFpsSample = fpsNow;
            *reinterpret_cast<std::uint32_t*>(
                selfBytes + core::application_layout::Fps) = framesThisWindow;
            fpsCounter = 0u;
        }

        inputState(this).resetFrameState();

        if ((as1::core::CurrentTimeMilliseconds() & 3u) == 0u)
        {
            for (int pass = 0; pass < as1::core::ApplicationDrawDispatcherState::PassCount; ++pass)
                embeddedDrawPassBucket(this, pass).compactSparse();
        }

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
                return true;

            const bool translatedByAccelerator = mainWindowHandle(this) &&
                TranslateAcceleratorA(mainWindowHandle(this), acceleratorHandle(this), &msg) != 0;
            if (translatedByAccelerator)
                continue;

            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        return false;
    }


    int ApplicationWin::processDemoFrame()
    {

        int result = 1;
        MAP* const map = Map;
        RESOURCE& demo = map->demoResource();

        if ((flags() & application_flags::DemoUseResource) != 0u)
        {
            std::int32_t frameTime = -1;
            demo.read(&frameTime, sizeof(frameTime));

            if (frameTime != -1 && inputState(this).lastCode == 0u &&
                (inputState(this).flags & 0x05u) == 0u)
            {
                inputState(this).readSerializedState(&demo);

                const std::uint32_t recorded = static_cast<std::uint32_t>(frameTime);
                std::uint32_t baseRecorded = as1::core::g_demoRecordedTimeBaseMilliseconds;
                if (recorded - baseRecorded > kFrameClampMs)
                {
                    baseRecorded = recorded - kFrameClampMs;
                    as1::core::g_demoRecordedTimeBaseMilliseconds = baseRecorded;
                }

                const std::uint32_t realBase = as1::core::g_demoRealTimeBaseMilliseconds;
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
                as1::core::g_demoRecordedTimeBaseMilliseconds = recorded;
                as1::core::g_demoRealTimeBaseMilliseconds = static_cast<std::uint32_t>(::timeGetTime());
            }
            else
            {
                STRING nextMap;

                (void)demo.GoNext(RESOURCE::ResTypes::DEMO);
                demo.shift(static_cast<int>(demo.CurrentResourceSize()));
                (nextMap).Read(&demo);

                if (nextMap.isEmpty())
                {
                    PostMessageA(mainWindowHandle(this), WM_CLOSE, 0, 0);
                }
                else
                {
                    demo.close();
                    Mouse->Enable();
                    setFlags(flags() & ~application_flags::DemoUseResource);
                    setFlags(flags() | kApplicationCommandLinePendingFlag);
                    (pendingCommand(this)).Assign(nextMap);
                }
                return 0;
            }
        }

        if ((flags() & application_flags::DemoWriteToResource) != 0u)
        {
            const std::uint32_t worldTime = as1::core::CurrentTimeMilliseconds();
            demo.write(&worldTime, sizeof(worldTime));
            inputState(this).writeSerializedState(&demo);
        }
        return result;
    }

    float ApplicationWin::mapExtentX() const noexcept
    {

        return physicalFloatSlot(this, core::application_layout::MapExtentX);
    }

    float ApplicationWin::mapExtentY() const noexcept
    {

        return physicalFloatSlot(this, core::application_layout::MapExtentY);
    }

    PLAYER* ApplicationWin::playerSlotByIndex(int index) const noexcept
    {

        return playerPointerSlot(this, playerSlotOffset(index));
    }


    SPRITE* ApplicationWin::activeAuxiliarySprite() const noexcept
    {


        const std::uint32_t index = *reinterpret_cast<const std::uint32_t*>(
            reinterpret_cast<const std::uint8_t*>(this) +
            core::application_layout::ActivePlayerIndex);
        PLAYER* const player = *reinterpret_cast<PLAYER* const*>(
            reinterpret_cast<const std::uint8_t*>(this) +
            core::application_layout::PlayerSlots +
            static_cast<std::size_t>(index & 3u) * core::application_layout::PlayerSlotStride);
        return *reinterpret_cast<SPRITE* const*>(
            reinterpret_cast<const std::uint8_t*>(player) + 0x24u);
    }


    as1::STRING* ApplicationWin::buildMouseTipText(as1::STRING* out)
    {

        as1::STRING text;
        if ((flags() & kApplicationModalDispatchFlag) == 0u)
        {
            PLAYER* const player = startupPlayerSlotByIndex(
                static_cast<int>(activeStartupPlayerIndex()));
            as1::STRING playerText;
            player->getAuxiliaryUnitName(&playerText);
            ::new (static_cast<void*>(&(text))) as1::STRING(playerText);
        }

        MENU& list = embeddedMenu(this);
        if (text.isEmpty() && list.selectedSprite())
        {
            char number[0x80]{};
            _itoa(list.NVidUnderCursor(), number, 10);

            as1::STRING nvidText;
            ::new (static_cast<void*>(&nvidText)) as1::STRING(number);
            as1::STRING menuVid;
            ::new (static_cast<void*>(&(menuVid))) as1::STRING("MenuVid", nvidText.c_str());

            as1::STRING defaultValue;
            as1::STRING section("MouseTips");
            as1::STRING allDirKey;
            ::new (static_cast<void*>(&(allDirKey))) as1::STRING(menuVid.c_str(), "AllDir");
            as1::STRING profileValue;
            as1::core::profile_p::readProfileStringInto(
                profileValue,
                *as1::core::g_startupStringsIniPathOwner,
                section,
                allDirKey,
                defaultValue);
            ::new (static_cast<void*>(&(text))) as1::STRING(profileValue);

            if (text.isEmpty())
            {
                _itoa(list.NDirUnderCursor(), number, 10);
                as1::STRING directionText;
                ::new (static_cast<void*>(&directionText)) as1::STRING(number);
                as1::STRING dirPrefix;
                ::new (static_cast<void*>(&(dirPrefix))) as1::STRING(menuVid.c_str(), "Dir");
                as1::STRING dirKey;
                ::new (static_cast<void*>(&(dirKey))) as1::STRING(dirPrefix.c_str(), directionText.c_str());
                as1::STRING dirProfileValue;
                as1::core::profile_p::readProfileStringInto(
                    dirProfileValue,
                    *as1::core::g_startupStringsIniPathOwner,
                    section,
                    dirKey,
                    defaultValue);
                ::new (static_cast<void*>(&(text))) as1::STRING(dirProfileValue);
            }
        }

        ::new (static_cast<void*>(&(*out))) as1::STRING(text);
        return out;
    }


    void ApplicationWin::updateCameraFromInput()
    {

        GRAPH* graph = Graph;
        MAP* map = Map;

        const CONSTANT* constants = as1::g_baseConstants;
        const float maxX = dwordAsFloat(constants->entries[0]);
        const float maxY = dwordAsFloat(constants->entries[1]);
        const float graphWidth = graph->screenWidth();
        const float graphHeight = graph->screenHeight();
                const float graphRight = graph->viewportRight();
        const float graphBottom = graph->viewportBottom();

        const float cameraShiftX = physicalFloatSlot(this, core::application_layout::CameraShiftX);
        const float cameraShiftY = physicalFloatSlot(this, core::application_layout::CameraShiftY);

        const std::uint32_t mode = physicalDwordSlot(this, core::application_layout::ScrollType);
        float& velocityX = as1::core::g_shiftSpeedX;
        float& velocityY = as1::core::g_shiftSpeedY;
        const std::uint32_t inputFlags = inputState(this).flags;
        const float clientX = inputState(this).clientX;
        const float clientY = inputState(this).clientY;

        if ((mode & 0x21u) != 0)
        {
            if (cameraLessEqualOrUnordered(clientX, kCameraEdgeThreshold) && (mode & 0x01u) != 0)
            {
                if (!(-maxX >= velocityX))
                    velocityX -= kCameraAccelerationX;
            }
            else if ((graphRight - kCameraEdgeThreshold) > clientX && (mode & 0x01u) != 0)
            {

                if ((inputFlags & 0x80u) != 0 && (mode & 0x20u) != 0)
                {
                    if (!(-maxX >= velocityX))
                        velocityX -= kCameraAccelerationX;
                }
                else if ((inputFlags & 0x0100u) != 0 && (mode & 0x20u) != 0)
                {
                    if (!(velocityX >= maxX))
                        velocityX += kCameraAccelerationX;
                }
                else
                    velocityX = 0.0f;
            }
            else if ((mode & 0x01u) != 0)
            {
                if (!(velocityX >= maxX))
                    velocityX += kCameraAccelerationX;
            }
            else if ((inputFlags & 0x80u) != 0 && (mode & 0x20u) != 0)
            {
                if (!(-maxX >= velocityX))
                    velocityX -= kCameraAccelerationX;
            }
            else if ((inputFlags & 0x0100u) != 0 && (mode & 0x20u) != 0)
            {
                if (!(velocityX >= maxX))
                    velocityX += kCameraAccelerationX;
            }
            else
                velocityX = 0.0f;

            if (cameraLessEqualOrUnordered(clientY, kCameraEdgeThreshold) && (mode & 0x01u) != 0)
            {
                if (!(-maxY >= velocityY))
                    velocityY -= kCameraAccelerationY;
            }
            else if ((graphBottom - kCameraEdgeThreshold) > clientY && (mode & 0x01u) != 0)
            {
                if ((inputFlags & 0x0400u) != 0 && (mode & 0x20u) != 0)
                {
                    if (!(-maxY >= velocityY))
                        velocityY -= kCameraAccelerationY;
                }
                else if ((inputFlags & 0x0200u) != 0 && (mode & 0x20u) != 0)
                {
                    if (!(velocityY >= maxY))
                        velocityY += kCameraAccelerationY;
                }
                else
                    velocityY = 0.0f;
            }
            else if ((mode & 0x01u) != 0)
            {
                if (!(velocityY >= maxY))
                    velocityY += kCameraAccelerationY;
            }
            else if ((inputFlags & 0x0400u) != 0 && (mode & 0x20u) != 0)
            {
                if (!(-maxY >= velocityY))
                    velocityY -= kCameraAccelerationY;
            }
            else if ((inputFlags & 0x0200u) != 0 && (mode & 0x20u) != 0)
            {
                if (!(velocityY >= maxY))
                    velocityY += kCameraAccelerationY;
            }
            else
                velocityY = 0.0f;
        }
        else
        {
            velocityX = 0.0f;
            velocityY = 0.0f;
        }

        SPRITE* target = reinterpret_cast<MAP*>(this)->Flagman(static_cast<int>(activeStartupPlayerIndex()));


        const bool noVelocity = velocityX == 0.0f && velocityY == 0.0f;
        if (target && (mode & 0x04u) != 0 && noVelocity)
        {
            float targetDeltaX = target->X() - cameraShiftX;
            targetDeltaX -= graphWidth * 0.5f;
            velocityX = targetDeltaX / 1000.0f;

            float targetDeltaY = target->Y() - target->Z();
            targetDeltaY -= cameraShiftY;
            targetDeltaY -= graphHeight * 0.5f;
            velocityY = targetDeltaY / 1000.0f;
        }
        else if (target && (mode & 0x08u) != 0 && noVelocity)
        {


            float pointerX = graphWidth - 640.0f;
            if (!(pointerX > clientX))
                pointerX = (640.0f < clientX ? 640.0f : clientX);
            float pointerY = graphHeight - 480.0f;
            if (!(pointerY > clientY))
                pointerY = (480.0f < clientY ? 480.0f : clientY);

            float pointerTargetX = target->X() - cameraShiftX;
            pointerTargetX += pointerX;
            pointerTargetX *= 0.5f;
            float pointerTargetY = target->Y() - target->Z();
            pointerTargetY -= cameraShiftY;
            pointerTargetY += pointerY;
            pointerTargetY *= 0.5f;

            velocityX = graphWidth * 0.5f - pointerTargetX;
            velocityX /= -1000.0f;
            velocityX *= 4.0f;
            velocityY = graphHeight * 0.5f - pointerTargetY;
            velocityY /= -1000.0f;
            velocityY *= 4.0f;
        }
        else if (target && (mode & 0x50u) == 0x50u && noVelocity)
        {


            map->SetShiftCoor(target->X(),
                              target->Y() - target->Z(),
                              0);
            return;
        }
        else if (target && (mode & 0x10u) != 0 && noVelocity)
        {


            map->SetShiftCoor(target->X(), cameraShiftY, 0);
            return;
        }
        else if (target && (mode & 0x40u) != 0 && noVelocity)
        {

            map->SetShiftCoor(cameraShiftX, target->Y() - target->Z(), 0);
            return;
        }

        const std::uint32_t current = as1::core::CurrentTimeMilliseconds();
        const std::uint32_t previous = as1::core::PreviousWorldTimeMilliseconds();
        const std::uint32_t deltaMs = current - previous;
        const std::int32_t dx = convertCameraElapsedScaleToInt32(deltaMs, velocityX);
        const std::int32_t dy = convertCameraElapsedScaleToInt32(deltaMs, velocityY);
        float centerX = graphWidth * 0.5f;
        centerX += cameraShiftX;
        centerX += static_cast<float>(dx);
        float centerY = graphHeight * 0.5f;
        centerY += cameraShiftY;
        centerY += static_cast<float>(dy);
        map->SetShiftCoor(centerX, centerY, 0);
    }

} }


namespace as1 { namespace core
{
    Application::~Application()
    {
        static_cast<as1::win::ApplicationWin*>(this)->destroyBaseApplicationStateImpl();
    }

    bool Application::dispatchWindowMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
    {
        return static_cast<as1::win::ApplicationWin*>(this)->dispatchWindowMessageImpl(
            hwnd, msg, wparam, lparam);
    }

    bool Application::pumpOnce()
    {
        return false;
    }

    void Application::drawShellOverlays()
    {
        static_cast<as1::win::ApplicationWin*>(this)->drawApplicationDebugPass();
    }

    void Application::runCommandLine(char* ownedCommandLine)
    {
        static_cast<as1::win::ApplicationWin*>(this)->runCommandLineMap(ownedCommandLine);
    }

    void Application::saveMap(const STRING& outputName)
    {
        static_cast<as1::win::ApplicationWin*>(this)->saveMapImpl(outputName);
    }

} }
