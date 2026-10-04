#include "vid/vid_light.h"
#include "graph.h"
#include "map.h"
#include "sprite.h"
#include "core/application.h"
#include "core/resource.h"
#include "core/log.h"
#include "core/file_logger.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <xmmintrin.h>

#include <new>

#include "d3d8.h"

namespace as1
{
    namespace
    {
        __forceinline
        int vidConvertFloatToInt32Light(float value) noexcept
        {
            return _mm_cvtt_ss2si(_mm_set_ss(value));
        }
    }


    VID_LIGHT::VID_LIGHT(const VID_LIGHT& other)
        : VID()
    {

        nextMirror = other.nextMirrorVid();
        const_cast<VID_LIGHT&>(other).nextMirror = this;
        layer = other.layer;
        type = other.formatFlags();
        noCadr = static_cast<short>(other.totalFrames());
        frameSpeedDefault = other.defaultFrameSpeed();
        setVidWidth(static_cast<short>(other.vidWidth()));
        setVidHeight(static_cast<short>(other.vidHeight()));
        m_lightData = other.m_lightData;
        m_lightDataBytes = other.m_lightDataBytes;
    }


    VID_LIGHT::~VID_LIGHT()
    {

        if (isMirrorChainOwner())
        {
            if (m_lightData)
                ::operator delete(m_lightData);
            m_lightData = nullptr;
            g_vidMemoryInUse -= static_cast<int>(m_lightDataBytes);
            m_lightDataBytes = 0;
        }
    }

    VID_LIGHT* VID_LIGHT::CreateMirror()
    {

        return new (std::nothrow) VID_LIGHT(*this);
    }

    void VID_LIGHT::SetLayer()
    {
        layer = 11;
    }


    void VID_LIGHT::Load(RESOURCE* resource)
    {


        if (resource->GoNext(RESOURCE::ResTypes::DATA) != 0)
            (void)logVidResourceError(5, "DATA", 0);

        setVidWidth(static_cast<short>(vidConvertFloatToInt32Light(sizeX())));
        setVidHeight(static_cast<short>(vidConvertFloatToInt32Light(sizeY())));

        void* loadedData = lightData();
        const DWORD byteCount = static_cast<DWORD>(resource->SubLoad(&loadedData, nullptr));
        setLightData(static_cast<BYTE*>(loadedData));
        setLightDataBytes(byteCount);
        if (byteCount == 0u)
            (void)logVidResourceError(5, "cadr", 0);

        g_vidMemoryInUse = static_cast<int>(
            static_cast<std::uint32_t>(g_vidMemoryInUse) + static_cast<std::uint32_t>(byteCount));
    }

    void VID_LIGHT::Draw(const SPRITE* sprite)
    {

        DWORD color = reinterpret_cast<const DWORD*>(m_lightData)
            [static_cast<std::size_t>(sprite->currentFrame())];

        if ((runtimeAuxFlags() & 0x40u) != 0u ||
            color == 0u || color == 0xFF000000u)
        {
            return;
        }

        IDirect3DDevice8* const device = static_cast<IDirect3DDevice8*>(Graph->deviceHandle());
        if ((properties() & P_DBLLIGHT) != 0u)
            device->SetTextureStageState(0u, D3DTSS_COLOROP, D3DTOP_MODULATE2X);

        Gamma selectedGamma{};
        selectedGamma = sprite->GetGamma();

        Gamma drawGamma{};
        drawGamma.setSaturatingAdd(baseGamma, selectedGamma);
        if ((properties() & P_GAMMA) == 0u)
            drawGamma.setSaturatingAdd(drawGamma, Graph->gammaPair());
        color = BlendGammaColor(drawGamma, color);

        const std::uint8_t* const application =
            static_cast<const std::uint8_t*>(core::ApplicationOwner());
        const float cameraShiftX = *reinterpret_cast<const float*>(
            application + core::application_layout::CameraShiftX);
        const float cameraShiftY = *reinterpret_cast<const float*>(
            application + core::application_layout::CameraShiftY);
        const VECTOR lightPosition{
            sprite->X() - cameraShiftX,
            (sprite->Y() - sprite->Z()) - cameraShiftY,
            sprite->Z()
        };

        Graph->DrawLightSource(
            lightPosition.x,
            lightPosition.y,
            lightPosition.z,
            sizeX(),
            sizeY(),
            color);

        if ((properties() & P_DBLLIGHT) != 0u)
            device->SetTextureStageState(0u, D3DTSS_COLOROP, D3DTOP_MODULATE);
    }

}
