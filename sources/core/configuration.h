#pragma once

#include "core/as_string.h"

#include <cstddef>
#include <cstdint>

namespace as1
{


    class REGISTRY
    {
    public:
        REGISTRY() = default;
        explicit REGISTRY(const STRING& registryPath);

        __forceinline const STRING* Path() noexcept { return &m_path; }
        STRING& mutablePath() noexcept { return m_path; }

    private:
        STRING m_path;
    };
}

namespace as1 { namespace core
{
    constexpr std::uint32_t StartupVSyncFlag = 1u << 1;
    constexpr std::uint32_t StartupDialogFullscreenFlag = 1u << 2;

    struct VideoConfiguration
    {
        int device = 0;
        int screenX = 800;
        int screenY = 600;
        int colorDepth = 32;
        bool fullscreen = true;
        bool vsync = true;
        int windowPositionX = 0;
        int windowPositionY = 0;
    };

    struct FontConfiguration
    {
        STRING face;
        int sizeX = 7;
        int sizeY = 8;
    };

    struct SoundConfiguration
    {
        bool highQuality = false;
    };

    struct ControlConfiguration
    {
        STRING left{"A"};
        STRING right{"D"};
        STRING up{"W"};
        STRING down{"S"};
        STRING firstAction{"LBUTTON"};
        STRING secondAction{"RBUTTON"};
        STRING previousWeapon{"Q"};
        STRING nextWeapon{"E"};
        std::uint32_t relative = 0;
    };

    struct StartupSettingsBlock
    {
        char title[0x100];
        std::uint32_t allowedWidths[32];
        std::uint32_t allowedHeights[32];
        std::uint32_t allowedColorBits[8];
        std::uint32_t flags;
        std::int32_t device;
        std::int32_t screenWidth;
        std::int32_t screenHeight;
        std::int32_t colorDepth;
        std::int32_t fullscreen;
    };


    extern StartupSettingsBlock g_startupSettings;

    __forceinline StartupSettingsBlock& StartupSettings() noexcept { return g_startupSettings; }

    struct StartupConfiguration
    {
        STRING configPath;
        STRING stringsPath;
        STRING applicationName;
        STRING applicationTitle;
        STRING registryPath;
        STRING resourceRoot{"."};
        STRING objectsResource{"objects.res"};
        STRING startMap{"maps\\logo.map"};
        bool showStartDialog = false;
        bool startDialogFullscreen = false;
        int debugMode = 0;
        int drawFps = 0;
        int drawPresentation = 0;
        int noSysMenu = 0;
        unsigned int startupFlags = 0;
        VideoConfiguration video;
        FontConfiguration font;
        SoundConfiguration sound;
        ControlConfiguration control;
    };

    extern STRING* g_startupStringsIniPathOwner;
    extern REGISTRY* g_startupRegistryPathOwner;

} }
