#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "base_sprite_list.h"
#include "input.h"
#include "player.h"
#include "core/application.h"
#include "core/configuration.h"

namespace as1
{
    class GRAPH;
    class MAP;
    class RESOURCE;
    class VID;
}

namespace as1 { namespace win
{

    struct ApplicationWinInit
    {
        HINSTANCE hInstance = nullptr;
        HINSTANCE previousInstance = nullptr;
        const char* commandLine = nullptr;
        int showCmd = SW_SHOWDEFAULT;
        const char* shellString = "";
        std::uint32_t startupFlags = 0u;
    };

    class ApplicationWin : public as1::core::Application
    {
        friend class as1::core::Application;
    public:
        explicit ApplicationWin(const ApplicationWinInit& init) { (void)init; }
        ~ApplicationWin() override;

        ApplicationWin(const ApplicationWin&) = delete;
        ApplicationWin& operator=(const ApplicationWin&) = delete;

        void transferFrom(SPRITE* sprite) override;


        bool pumpOnce() override;
        __forceinline int pumpFrame() { return pumpOnce() ? 1 : 0; }

        void drawShellOverlays() override;

        void deinitialize() override;

        ApplicationWin* initializeDerivedApplicationStartup(HINSTANCE instance, HINSTANCE previousInstance, const char** commandLineOwner, int showCmd,
                                   as1::core::StartupSettingsBlock* startupSettings);
        ApplicationWin* initializeBaseApplicationStartup(HINSTANCE instance, HINSTANCE previousInstance, const char** commandLineOwner, int showCmd,
                       as1::core::StartupSettingsBlock* startupSettings);

        SPRITE* CreateSprite(VID* vid, VECTOR xyz, ANGLE direction, SPRITE* parent) override;

        __forceinline bool initialized() const noexcept
        {
            return (flags() & 0x00000004u) != 0u;
        }

        __forceinline HINSTANCE instance() const noexcept
        {
            return *reinterpret_cast<const HINSTANCE*>(
                reinterpret_cast<const std::uint8_t*>(this) + as1::core::application_layout::InstanceHandle);
        }

        __forceinline HWND nativeWindow() const noexcept
        {
            return *reinterpret_cast<const HWND*>(
                reinterpret_cast<const std::uint8_t*>(this) + as1::core::application_layout::MainWindow);
        }

        __forceinline HACCEL accelerator() const noexcept
        {
            return *reinterpret_cast<const HACCEL*>(
                reinterpret_cast<const std::uint8_t*>(this) + as1::core::application_layout::Accelerator);
        }

        __forceinline std::uint32_t flags() const noexcept
        {
            return *reinterpret_cast<const std::uint32_t*>(
                reinterpret_cast<const std::uint8_t*>(this) + as1::core::application_layout::Flags);
        }

        __forceinline void setFlags(std::uint32_t value) noexcept
        {


            *reinterpret_cast<std::uint32_t*>(
                reinterpret_cast<std::uint8_t*>(this) + as1::core::application_layout::Flags) = value;
        }

        __forceinline void setWorldStartTime(std::uint32_t value) noexcept
        {


            *reinterpret_cast<std::uint32_t*>(
                reinterpret_cast<std::uint8_t*>(this) +
                as1::core::application_layout::WorldStartTime) = value;
        }



        float mapExtentX() const noexcept;
        float mapExtentY() const noexcept;
        PLAYER* playerSlotByIndex(int index) const noexcept;

        __forceinline PLAYER* startupPlayerSlotByIndex(int index) const noexcept
        {
            return playerSlotByIndex(index);
        }

        __forceinline std::uint32_t activeStartupPlayerIndex() const noexcept
        {


            return *reinterpret_cast<const std::uint32_t*>(
                reinterpret_cast<const std::uint8_t*>(this) +
                as1::core::application_layout::ActivePlayerIndex);
        }


        as1::STRING* buildMouseTipText(as1::STRING* out);
        as1::STRING selectFile(bool save, const char* filter);
    private:
        __forceinline void destroyBaseApplicationStateImpl();
        __forceinline bool dispatchWindowMessageImpl(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
        __forceinline void drawApplicationDebugPass();
        __forceinline void runCommandLineMap(char* ownedCommandLine);
        __forceinline void saveMapImpl(const as1::STRING& outputName);
        bool pumpNativeMessages();
        int processDemoFrame();
        SPRITE* activeAuxiliarySprite() const noexcept;
        void updateCameraFromInput();

    };

#if UINTPTR_MAX == 0xFFFFFFFFu
    static_assert(sizeof(as1::core::Application) == 0x04u, "Application polymorphic base size");
    static_assert(sizeof(ApplicationWin) == 0x04u, "ApplicationWin adds no physical fields before application storage");
#endif

    LRESULT CALLBACK AppWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    __forceinline ApplicationWin* applicationWinInstance() noexcept
    {
        return static_cast<ApplicationWin*>(as1::core::ApplicationOwner());
    }
} }
