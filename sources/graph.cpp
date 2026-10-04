#include "graph.h"

#include "core/application.h"
#include "core/configuration.h"
#include "core/resource.h"
#include "map.h"
#include "mouse.h"
#include "sprite.h"
#include "sprite_collector.h"
#include "vid/vid.h"
#include "vid/vid_font.h"
#include "vid/vid_software.h"
#include "vid/vid_software16.h"
#include "vid/vid_surface.h"
#include "vid/vid_texcoor.h"
#include "graphics/base_texture.h"
#include "graphics/gamma.h"
#include "graphics/color.h"
#include "images/picture.h"
#include "core/log.h"
#include "core/file_logger.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>
#include <memory>
#include <new>
#include <utility>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <xmmintrin.h>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dshow.h>
#include "win/application_win.h"
#include "d3d8.h"
#include "win/dialog_item.h"


namespace as1
{
    __forceinline int graphConvertFloatToInt32(float value) noexcept;

#define m_d3d8PresentParameters (*reinterpret_cast<D3DPRESENT_PARAMETERS8*>(m_presentParameters))
    DWORD g_color16RedMask = 0xF800u;
    DWORD g_color16GreenMask = 0x07E0u;
    DWORD g_color16RedShift = 8u;
    DWORD g_color16GreenShift = 3u;
    DWORD g_packedSoftwareDepth = 0u;
    int g_softwareClipLeft = 0;
    int g_softwareClipRight = 0;
    int g_softwareClipTop = 0;
    int g_softwareClipBottom = 0;


    GRAPH* Graph = nullptr;

    namespace
    {

        std::uint32_t start_squall = 0u;
        float old_w_speed = -1.0f;

        std::uint32_t g_groundSnowStart = 0u;
        std::uint32_t g_groundSnowAmount = 0u;


        std::uint32_t g_lightBufferToggle = 0u;

        DWORD graphFloatBits(float value)
        {
            DWORD bits = 0u;

            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
        }


        struct GraphWeatherVertex
        {
            float x;
            float y;
            float z;
            float rhw;
            DWORD color;
        };


        struct GraphWeatherLineParticle
        {
            GraphWeatherVertex vertex[2];
        };


        struct GraphWeatherCrossParticle
        {
            GraphWeatherVertex vertex[6];
        };


        std::array<GraphWeatherLineParticle, 250> g_lineParticles{};
        std::array<GraphWeatherCrossParticle, 1000> g_crossParticles{};
        int g_lineParticleCount = 0;
        int g_crossParticleCount = 0;
        std::uint8_t g_lineParticleWindDirection = 0u;
        float g_lineParticleWindSpeed = 99999.0f;
        std::uint8_t g_crossParticleWindDirection = 0u;
        float g_crossParticleWindSpeed = 99999.0f;
        float g_lineParticleWindX = 0.0f;
        float g_crossParticleWindX = 0.0f;
        float g_crossParticlePreviousNegatedCameraX = 0.0f;
        float g_crossParticlePreviousNegatedCameraY = 0.0f;

        constexpr float kGraphRand32767 = 0.000030518509f;
        constexpr float kGraphRand268435456 = 0.000000003725404f;
        constexpr float kGraphOneOver8192 = 0.00012207031f;
        constexpr float kGraphOneOver819_2 = 0.0012207031f;
        constexpr float kGraphOneOver200 = 0.0049999999f;

        int clampGraphByte(int value)
        {
            if (value < 0)
                return 0;
            if (value > 255)
                return 255;
            return value;
        }

        std::uint16_t packGraphIntensityWord(int high, int green, int low, bool r5g6b5)
        {
            high = clampGraphByte(high);
            green = clampGraphByte(green);
            low = clampGraphByte(low);
            const int highShift = r5g6b5 ? 8 : 7;
            const int greenShift = r5g6b5 ? 3 : 2;
            const int greenMask = r5g6b5 ? 0x07E0 : 0x03E0;
            return static_cast<std::uint16_t>(
                ((high & 0xF8) << highShift) |
                ((green << greenShift) & greenMask) |
                ((low >> 3) & 0x1F));
        }

        void buildGraphIntensityPalette(std::array<std::uint16_t, 256>& table, bool r5g6b5)
        {

            for (int v = 8, base = 0; base < 256; v += 8, base += 8)
            {
                table[base + 0] = packGraphIntensityWord(v - 8, v - 8, v - 8, r5g6b5);
                table[base + 1] = packGraphIntensityWord(v - 7, v - 7, v, r5g6b5);
                table[base + 2] = packGraphIntensityWord(v, v - 6, v - 6, r5g6b5);
                table[base + 3] = packGraphIntensityWord(v, v - 5, v, r5g6b5);

                table[base + 4] = packGraphIntensityWord(v - 4, v, v - 4, r5g6b5);
                table[base + 5] = packGraphIntensityWord(v - 3, v, v, r5g6b5);
                table[base + 6] = packGraphIntensityWord(v, v, v - 2, r5g6b5);
                table[base + 7] = packGraphIntensityWord(v, v, v, r5g6b5);
            }
        }

        bool graphSnowEdgeIntensity(std::uint16_t center, std::uint16_t neighbor, std::uint32_t& intensity)
        {
            const int delta = std::abs(static_cast<int>(center) - static_cast<int>(neighbor));
            if (delta > 6)
                return false;
            intensity = (g_groundSnowAmount * static_cast<std::uint32_t>(6 - delta)) >> 3u;
            return true;
        }

        int signedHalfTowardZero(int value)
        {
            const int sign = value < 0 ? -1 : 0;
            return (value - sign) >> 1;
        }

        int signedHalfFromFloatTowardZero(float value)
        {
            return signedHalfTowardZero(graphConvertFloatToInt32(value));
        }

        DWORD floatBits(float value)
        {
            DWORD bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
        }

        float floatFromBits(DWORD bits)
        {
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        DWORD graphGrayRgb(DWORD value)
        {


            return value | ((value | (value << 8u)) << 8u);
        }

        DWORD graphScaleRgb(DWORD color, DWORD scale256)
        {

            const DWORD redProductBits = static_cast<DWORD>(
                static_cast<std::uint64_t>(color) * static_cast<std::uint64_t>(scale256));
            std::int32_t redProductSigned = 0;
            std::memcpy(&redProductSigned, &redProductBits, sizeof(redProductSigned));
            const DWORD red = static_cast<DWORD>(redProductSigned >> 8) & 0xFF0000u;
            const DWORD green = (((color & 0xFF00u) * scale256) >> 8u) & 0xFF00u;
            const DWORD blue = ((color & 0xFFu) * scale256) >> 8u;
            return red + green + blue;
        }

        void logMovieError(int errorCode, const char* detailText, int detailValue)
        {
            logFileLoggerResourceError(g_fileLogger, "%s", errorCode, detailText, detailValue, "MOVIE");
        }

        IDirect3D8* graphD3D(void* p) { return static_cast<IDirect3D8*>(p); }
        IDirect3DDevice8* graphDevice(void* p) { return static_cast<IDirect3DDevice8*>(p); }

        void appendTextureArgumentName(char* destination, DWORD value)
        {
            const unsigned char low = static_cast<unsigned char>(value);
            if ((low & 0x20u) != 0u)
                std::strcat(destination, "alp-");
            if ((low & 0x10u) != 0u)
                std::strcat(destination, "inv-");

            switch (low & 0x0Fu)
            {
            case 2u: std::strcat(destination, "tex"); break;
            case 0u: std::strcat(destination, "dif"); break;
            case 4u: std::strcat(destination, "spec"); break;
            case 1u: std::strcat(destination, "cur"); break;
            case 3u: std::strcat(destination, "tfac"); break;
            default: break;
            }
        }

        const char* buildTextureStageDebugText()
        {
            static char text[0x40C];
            IDirect3DDevice8* const device = graphDevice(Graph ? Graph->deviceHandle() : nullptr);
            DWORD value;

            std::strcpy(text, "Op=");
            device->GetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(1u), &value);
            switch (value)
            {
            case 1u: std::strcat(text, "dis"); break;
            case 2u: std::strcat(text, "sel1"); break;
            case 3u: std::strcat(text, "sel2"); break;
            case 4u: std::strcat(text, "mod"); break;
            case 13u: std::strcat(text, "tex_alpha"); break;
            default: std::strcat(text, "unknown"); break;
            }

            std::strcat(text, " Arg1=");
            device->GetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(2u), &value);
            appendTextureArgumentName(text, value);

            std::strcat(text, " Arg2=");
            device->GetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(3u), &value);
            appendTextureArgumentName(text, value);

            std::strcat(text, " AOp=");
            device->GetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(4u), &value);
            switch (value)
            {
            case 1u: std::strcat(text, "dis"); break;
            case 2u: std::strcat(text, "sel1"); break;
            case 3u: std::strcat(text, "sel2"); break;
            case 4u: std::strcat(text, "mod"); break;
            case 13u: std::strcat(text, "tex_alpha"); break;
            default: std::strcat(text, "unknown"); break;
            }

            std::strcat(text, " Arg1=");
            device->GetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(5u), &value);
            appendTextureArgumentName(text, value);

            std::strcat(text, " Arg2=");
            device->GetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(6u), &value);
            appendTextureArgumentName(text, value);
            return text;
        }

        IDirect3DSurface8* graphSurface(void* p) { return static_cast<IDirect3DSurface8*>(p); }
        IDirect3DTexture8* graphTexture(void* p) { return static_cast<IDirect3DTexture8*>(p); }

        int nextPowerOfTwo(int v)
        {
            int out = 1;
            while (out < v && out < 4096)
                out <<= 1;
            return out;
        }

        struct TextureVertex
        {
            float x, y, z, rhw;
            DWORD color;
            float u, v;
        };

        constexpr DWORD kTextureVertexFVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;


        bool isAllowedDisplayMode(const as1::core::StartupSettingsBlock& startupSettings,
                         DWORD width, DWORD height, DWORD colorBits)
        {

            const std::uint32_t* colorTable = startupSettings.allowedColorBits;
            int index = 0;
            DWORD value = colorTable[0];
            while (value != 0u)
            {
                if (colorBits == value)
                    break;
                ++index;
                value = colorTable[index];
            }

            if (startupSettings.allowedColorBits[index] == 0u || startupSettings.allowedWidths[0] == 0u)
                return false;

            index = 0;
            value = startupSettings.allowedWidths[0];
            while (value != 0u)
            {
                if (width == value && height == startupSettings.allowedHeights[index])
                    return true;
                ++index;
                value = startupSettings.allowedWidths[index];
            }
            return false;
        }


        DWORD chooseDepthStencilFormat(IDirect3D8* d3d, UINT adapter, D3DFORMAT format)
        {
            const D3DFORMAT depthFormats[] = {
                D3DFMT_D16,
                D3DFMT_D32,
                D3DFMT_D24X8,
                D3DFMT_D24S8
            };
            for (D3DFORMAT depth : depthFormats)
            {
                if (d3d->CheckDepthStencilMatch(adapter, D3DDEVTYPE_HAL, format, format, depth) == D3D_OK)
                    return static_cast<DWORD>(depth);
            }
            return 0;
        }

        float effectiveRenderZForStaticDraw(DWORD property, float spriteZ, float groundZ)
        {
            (void)property;
            (void)groundZ;
            return spriteZ;
        }
    }


    __forceinline int graphConvertFloatToInt32(float value) noexcept
    {


        const double extended = static_cast<double>(value);
        if (!(extended >= -9223372036854775808.0 && extended < 9223372036854775808.0))
            return 0;
        const std::int64_t integer = static_cast<std::int64_t>(value);
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(integer));
    }

    __forceinline bool floatEqualOrUnordered(float lhs, float rhs) noexcept
    {

        return lhs == rhs || std::isnan(lhs) || std::isnan(rhs);
    }

    __forceinline bool floatLessOrUnordered(float lhs, float rhs) noexcept
    {

        return lhs < rhs || std::isnan(lhs) || std::isnan(rhs);
    }

    struct GraphTextGlyph
    {
        float left;
        float top;
        float right;
        float bottom;
    };

    namespace
    {
        constexpr DWORD kTextVertexFVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
        constexpr float kTextVertexZ = 0.899999976f;
        constexpr float kTextVertexRhw = 1.0f;
        constexpr UINT kTextVertexBufferBytes = 0x20D0u;
        constexpr UINT kTextVertexStride = 0x1Cu;
        constexpr UINT kTextVertexBufferUsage = 0x208u;
        constexpr DWORD kTextVertexLockFlags = 0x2000u;

        struct GraphTextVertex
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = kTextVertexZ;
            float rhw = kTextVertexRhw;
            DWORD color = 0xFFFFFFFFu;
            float u = 0.0f;
            float v = 0.0f;
        };
    }


    class CD3DFont
    {
    public:

        CD3DFont(const STRING& face, int sizeX, int sizeY, DWORD flags)
        {
            std::strcpy(m_face, face.c_str());
            m_sizeY = sizeY;
            m_sizeX = sizeX;
            m_flags = flags;
            m_device = nullptr;
            m_texture = nullptr;
            m_vertexBuffer = nullptr;
            m_savedStateBlock = 0u;
            m_textStateBlock = 0u;
        }


        __declspec(noinline) ~CD3DFont()
        {
            (void)InvalidateDeviceObjects();
            (void)DeleteDeviceObjects();
        }

        const char* face() const { return m_face; }
        int sizeX() const { return m_sizeX; }
        int sizeY() const { return m_sizeY; }
        DWORD flags() const { return m_flags; }
        bool isReady() const
        {
            return m_vertexBuffer != nullptr;
        }


        int InvalidateDeviceObjects()
        {
            if (m_vertexBuffer)
            {
                m_vertexBuffer->Release();
                m_vertexBuffer = nullptr;
            }
            if (m_device)
            {
                if (m_savedStateBlock)
                    m_device->DeleteStateBlock(m_savedStateBlock);
                if (m_textStateBlock)
                    m_device->DeleteStateBlock(m_textStateBlock);
            }
            m_savedStateBlock = 0u;
            m_textStateBlock = 0u;
            return 0;
        }


        __declspec(noinline) int DeleteDeviceObjects()
        {
            if (m_texture)
            {
                m_texture->Release();
                m_texture = nullptr;
            }
            m_device = nullptr;
            return 0;
        }

        int InitDeviceObjects(IDirect3DDevice8* device)
        {
            m_device = device;

            m_textureWidth = atlasSizeForHeight(m_sizeY);
            m_textureHeight = m_textureWidth;
            m_scale = 1.0f;

            D3DCAPS8 caps;
            device->GetDeviceCaps(&caps);
            if (static_cast<DWORD>(m_textureWidth) > caps.MaxTextureWidth)
            {
                m_scale = static_cast<float>(caps.MaxTextureWidth) / static_cast<float>(m_textureWidth);
                m_textureWidth = static_cast<int>(caps.MaxTextureWidth);
                m_textureHeight = static_cast<int>(caps.MaxTextureWidth);
            }

            HRESULT hr = device->CreateTexture(
                static_cast<UINT>(m_textureWidth),
                static_cast<UINT>(m_textureHeight),
                1,
                0,
                D3DFMT_A4R4G4B4,
                D3DPOOL_MANAGED,
                &m_texture);
            if (FAILED(hr))
            {
                return static_cast<int>(hr);
            }

            if (!buildAtlas())
                return static_cast<int>(E_FAIL);

            return 0;
        }


        int RestoreDeviceObjects()
        {
            const HRESULT result = m_device->CreateVertexBuffer(
                kTextVertexBufferBytes,
                kTextVertexBufferUsage,
                0,
                D3DPOOL_DEFAULT,
                &m_vertexBuffer);
            if (FAILED(result))
                return static_cast<int>(result);

            for (unsigned block = 0; block < 2u; ++block)
            {
                m_device->BeginStateBlock();
                m_device->SetTexture(0, m_texture);
                m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(7), (m_flags & 4u) != 0u ? 1u : 0u);
                m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(27), 1u);
                m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(19), 5u);
                m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(20), 6u);
                m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(15), 1u);
                m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(24), 8u);
                m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(25), 7u);
                m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(8), 3u);
                m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(22), 3u);
                m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(52), 0u);
                m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(136), 1u);
                m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(40), 0u);
                m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(152), 0u);
                m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(151), 0u);
                m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(167), 0u);
                m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(28), 0u);
    
                m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(1), 4u);
                m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(2), 2u);
                m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(3), 0u);
                m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(4), 4u);
                m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(5), 2u);
                m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(6), 0u);
                m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(17), 1u);
                m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(16), 1u);
                m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(18), 0u);
                m_device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0u);
                m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(24), 0u);
                m_device->SetTextureStageState(1, static_cast<D3DTEXTURESTAGESTATETYPE>(1), 1u);
                m_device->SetTextureStageState(1, static_cast<D3DTEXTURESTAGESTATETYPE>(4), 1u);
                m_device->EndStateBlock(block == 0u ? &m_savedStateBlock : &m_textStateBlock);
            }
            return 0;
        }

        int DrawText(float x, float y, DWORD color, const char* text, DWORD flags)
        {
            if (!m_device)
                return static_cast<int>(E_FAIL);
            if (m_savedStateBlock)
                m_device->CaptureStateBlock(m_savedStateBlock);
            if (m_textStateBlock)
                m_device->ApplyStateBlock(m_textStateBlock);

            m_device->SetVertexShader(kTextVertexFVF);
            m_device->SetPixelShader(0u);
            m_device->SetStreamSource(0, m_vertexBuffer, kTextVertexStride);
            m_device->SetTexture(0, m_texture);
            if ((flags & 4u) != 0)
            {
                D3D8SetSamplerState(m_device, 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
                D3D8SetSamplerState(m_device, 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            }

            void* vertexData = nullptr;
            HRESULT hr = m_vertexBuffer->Lock(0, 0, &vertexData, kTextVertexLockFlags);
            if (FAILED(hr) || !vertexData)
            {
                return static_cast<int>(E_FAIL);
            }

            GraphTextVertex* vertices = reinterpret_cast<GraphTextVertex*>(vertexData);
            UINT primitiveCount = 0;
            UINT vertexCount = 0;
            bool ok = true;
            float cursorX = x;
            float cursorY = y;
            const float lineStep = (m_glyphs[0].bottom - m_glyphs[0].top) * static_cast<float>(m_textureHeight);

            for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p)
            {
                const unsigned char ch = *p;
                if (ch == '\n')
                {
                    cursorX = x;
                    cursorY += lineStep;
                }
                const GraphTextGlyph& g = m_glyphs[ch];
                const float glyphW = (g.right - g.left) * static_cast<float>(m_textureWidth) / m_scale;
                const float glyphH = (g.bottom - g.top) * static_cast<float>(m_textureHeight) / m_scale;
                if (ch == ' ')
                {
                    cursorX += glyphW;
                    continue;
                }

                const float x0 = cursorX - 0.5f;
                const float y0 = cursorY - 0.5f;
                const float x1 = cursorX + glyphW - 0.5f;
                const float y1 = cursorY + glyphH - 0.5f;
                const float u0 = g.left;
                const float v0 = g.top;
                const float u1 = g.right;
                const float v1 = g.bottom;
                GraphTextVertex quad[6] = {
                    {x0, y0, kTextVertexZ, kTextVertexRhw, color, u0, v0},
                    {x1, y0, kTextVertexZ, kTextVertexRhw, color, u1, v0},
                    {x1, y1, kTextVertexZ, kTextVertexRhw, color, u1, v1},
                    {x0, y0, kTextVertexZ, kTextVertexRhw, color, u0, v0},
                    {x1, y1, kTextVertexZ, kTextVertexRhw, color, u1, v1},
                    {x0, y1, kTextVertexZ, kTextVertexRhw, color, u0, v1},
                };
                std::copy(quad, quad + 6, vertices + vertexCount);
                vertexCount += 6u;
                primitiveCount += 2u;
                if (primitiveCount > 98u)
                {
                    hr = flush(primitiveCount);
                    if (FAILED(hr))
                    {
                        ok = false;
                        break;
                    }
                    vertexData = nullptr;
                    hr = m_vertexBuffer->Lock(0, 0, &vertexData, kTextVertexLockFlags);
                    if (FAILED(hr) || !vertexData)
                    {
                        ok = false;
                        break;
                    }
                    vertices = reinterpret_cast<GraphTextVertex*>(vertexData);
                    primitiveCount = 0;
                    vertexCount = 0;
                }
                cursorX += glyphW;
            }

            HRESULT unlockHr = m_vertexBuffer->Unlock();
            if (FAILED(unlockHr) && ok)
            {
                ok = false;
            }
            if (ok && primitiveCount > 0)
            {
                hr = m_device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, primitiveCount);
                if (FAILED(hr))
                {
                    ok = false;
                }
            }

            if (m_savedStateBlock)
                m_device->ApplyStateBlock(m_savedStateBlock);
            return ok ? 0 : static_cast<int>(E_FAIL);
        }

    private:
        static __forceinline int atlasSizeForHeight(int sizeY)
        {
            if (sizeY > 40)
                return 1024;
            if (sizeY > 20)
                return 512;
            return 256;
        }

        __forceinline bool buildAtlas()
        {
            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = m_textureWidth;
            bmi.bmiHeader.biHeight = -m_textureHeight;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            void* bits = nullptr;
            HDC dc = CreateCompatibleDC(nullptr);
            HBITMAP bitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);

            SelectObject(dc, bitmap);
            SetMapMode(dc, MM_TEXT);
            const int dpiY = GetDeviceCaps(dc, LOGPIXELSY);
            const int dpiX = GetDeviceCaps(dc, LOGPIXELSX);
            const int fontHeight = -MulDiv(m_sizeY, static_cast<int>(static_cast<float>(dpiY) * m_scale), 72);
            const int fontWidth = -MulDiv(m_sizeX, static_cast<int>(static_cast<float>(dpiX) * m_scale), 72);
            const int weight = (m_flags & 1u) ? FW_BOLD : FW_NORMAL;
            const BOOL italic = (m_flags & 2u) ? TRUE : FALSE;
            const DWORD pitchAndFamily = 2u - ((m_flags & 8u) != 0u ? 1u : 0u);
            HFONT font = CreateFontA(fontHeight, fontWidth, 0, 0, weight, italic, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     ANTIALIASED_QUALITY, pitchAndFamily, m_face);
            if (!font)
                return false;
            SelectObject(dc, font);
            SetTextColor(dc, RGB(255, 255, 255));
            SetBkColor(dc, RGB(0, 0, 0));
            SetTextAlign(dc, TA_TOP | TA_LEFT);

            int penX = 0;
            int penY = 0;
            for (int ch = 0; ch < 256; ++ch)
            {
                char c = static_cast<char>(ch);
                SIZE extent{};
                GetTextExtentPoint32A(dc, &c, 1, &extent);
                if (penX + extent.cx + 1 > m_textureWidth)
                {
                    penX = 0;
                    penY += extent.cy + 1;
                }
                ExtTextOutA(dc, penX, penY, ETO_OPAQUE, nullptr, &c, 1, nullptr);
                GraphTextGlyph& glyph = m_glyphs[static_cast<unsigned char>(ch)];
                glyph.left = static_cast<float>(penX) / static_cast<float>(m_textureWidth);
                glyph.top = static_cast<float>(penY) / static_cast<float>(m_textureHeight);
                glyph.right = static_cast<float>(penX + extent.cx) / static_cast<float>(m_textureWidth);
                glyph.bottom = static_cast<float>(penY + extent.cy) / static_cast<float>(m_textureHeight);
                penX += extent.cx + 1;
            }

            D3DLOCKED_RECT locked;
            m_texture->LockRect(0, &locked, nullptr, 0);
            const DWORD* src = static_cast<const DWORD*>(bits);
            for (int y = 0; y < m_textureHeight; ++y)
            {
                std::uint16_t* dst = reinterpret_cast<std::uint16_t*>(static_cast<BYTE*>(locked.pBits) + y * locked.Pitch);
                for (int x = 0; x < m_textureWidth; ++x)
                {
                    const DWORD pixel = src[static_cast<std::size_t>(y) * static_cast<std::size_t>(m_textureWidth) + static_cast<std::size_t>(x)];
                    const BYTE alpha = static_cast<BYTE>((pixel >> 4) & 0x0Fu);
                    dst[x] = alpha ? static_cast<std::uint16_t>((alpha << 12) | 0x0FFFu) : 0;
                }
            }
            m_texture->UnlockRect(0);

            DeleteObject(bitmap);
            DeleteDC(dc);
            DeleteObject(font);
            return true;
        }

        

        

        __forceinline HRESULT flush(UINT primitiveCount)
        {
            HRESULT unlockHr = m_vertexBuffer->Unlock();
            if (FAILED(unlockHr))
            {
                return unlockHr;
            }
            HRESULT drawHr = S_OK;
            if (primitiveCount > 0)
                drawHr = m_device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, primitiveCount);
            return drawHr;
        }

        char m_face[80];
        int m_sizeY;
        int m_sizeX;
        DWORD m_flags;
        IDirect3DDevice8* m_device;
        IDirect3DTexture8* m_texture;
        IDirect3DVertexBuffer8* m_vertexBuffer;
        int m_textureWidth;
        int m_textureHeight;
        float m_scale;
        GraphTextGlyph m_glyphs[256];
        DWORD m_savedStateBlock;
        DWORD m_textStateBlock;
    };


    namespace
    {
        void destroyCD3DFont(CD3DFont*& font)
        {
            if (!font)
                return;
            font->~CD3DFont();
            ::operator delete(font);
            font = nullptr;
        }

        __forceinline int displayFormatBits(DWORD format) noexcept;
    }

    VID_FONT::~VID_FONT()
    {
        if (isMirrorChainOwner())
        {
            if (m_fontOwner)
            {
                m_fontOwner->~CD3DFont();
                ::operator delete(m_fontOwner);
                m_fontOwner = nullptr;
            }
        }
        else
        {
            m_fontOwner = nullptr;
        }
    }

    void VID_FONT::Load(RESOURCE*)
    {
        const int sizeX = static_cast<int>(sizeXYZ.x);
        const int sizeY = static_cast<int>(sizeXYZ.y);

        frameSpeedDefault = 71u;
        noCadr = 256;
        setVidWidth(static_cast<short>(sizeX));
        setVidHeight(static_cast<short>(sizeY));
        type = static_cast<WORD>(VID_TYPE_FONT);

        void* const storage = ::operator new(0x107Cu);
        m_fontOwner = storage ? new (storage) CD3DFont(vidName, sizeX, sizeY, 8u) : nullptr;
        IDirect3DDevice8* const device = graphDevice(Graph->deviceHandle());
        (void)m_fontOwner->InitDeviceObjects(device);
        (void)m_fontOwner->RestoreDeviceObjects();
    }

    void VID_FONT::Draw(const SPRITE* sprite)
    {
        if (!m_fontOwner)
            return;

        const Gamma selected = sprite->GetGamma();
        const std::uint8_t* const application =
            static_cast<const std::uint8_t*>(core::ApplicationOwner());
        const float x = sprite->X() - *reinterpret_cast<const float*>(
            application + core::application_layout::CameraShiftX);
        const float y = (sprite->Y() - sprite->Z()) - *reinterpret_cast<const float*>(
            application + core::application_layout::CameraShiftY);

        (void)m_fontOwner->DrawText(x, y, ~selected.first, "", 0u);
    }

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4715)
#endif
    int VID_FONT::InvalidateDeviceObjects() noexcept
    {
        if (m_fontOwner)
            return m_fontOwner->InvalidateDeviceObjects();
    }

    int VID_FONT::RestoreDeviceObjects() noexcept
    {
        if (m_fontOwner)
            return m_fontOwner->RestoreDeviceObjects();
    }
#if defined(_MSC_VER)
#pragma warning(pop)
#endif


    GRAPH* GRAPH::initializeGraphState(const as1::core::StartupSettingsBlock& startupSettings)
    {


        for (DD_DRIVER& record : m_adapterRecords)
        {
            record.displayModeCount = 0u;
            record.description[0] = '\0';
            record.capabilityFlags &= 0xFFFFFFF0u;
        }

        m_effectGammaPair.first = 0u;
        m_effectGammaPair.second = 0u;
        m_gammaPair.first = 0u;
        m_gammaPair.second = 0u;

        DWORD flags = m_graphFlags;
        flags = (flags & ~0x00000400u) |
                ((startupSettings.flags & 0x1u) != 0u ? 0x00000400u : 0u);
        flags = (flags & ~0x00000101u) |
                ((startupSettings.flags & 0x2u) != 0u ? 0x00000100u : 0u);
        flags &= ~0x00010000u;
        m_graphFlags = flags;

        m_direct3D = nullptr;
        m_device = nullptr;
        m_textFont = nullptr;
        m_lightBuffer = nullptr;
        m_hiBuffer = nullptr;
        m_alphaBuffer = nullptr;
        m_tempBuffer = nullptr;
        m_backBuffer = nullptr;
        m_softwareDepthBuffer = nullptr;
        m_lockedBackBufferPixels = nullptr;
        m_renderFlags = 0u;
        m_deviceLifecycleState = 0u;
        m_windDirection = 0xDCu;
        m_windSpeed = 20.0f;


        Effect(0, 0, 0, 0);

        m_direct3D = Direct3DCreate8(D3D8_SDK_VERSION);
        if (!m_direct3D)
        {
            logAndShowError(g_fileLogger, "Can't create Direct3D8");
            return this;
        }

        IDirect3D8* const d3d = graphD3D(m_direct3D);
        m_adapterCount = 0u;
        const UINT adapterCount = d3d->GetAdapterCount();
        for (UINT adapter = 0; adapter < adapterCount; ++adapter)
        {


            buildAdapterRecord(m_adapterRecords[adapter], d3d,
                               static_cast<int>(adapter), startupSettings);
            ++m_adapterCount;
        }

        m_selectedAdapterIndex = 0;
        m_sizeX = static_cast<float>(static_cast<std::uint32_t>(startupSettings.screenWidth));
        m_sizeY = static_cast<float>(static_cast<std::uint32_t>(startupSettings.screenHeight));

        flags = m_graphFlags;
        flags = (flags & ~0x00000002u) |
                (startupSettings.colorDepth == 32 ? 0x00000002u : 0u);
        flags = (flags & ~0x00000080u) |
                (startupSettings.fullscreen != 0 ? 0x00000080u : 0u);
        m_graphFlags = flags;

        if (m_adapterCount == 0u)
            m_selectedAdapterIndex = 0;
        return this;
    }


    int GRAPH::rebuildTextFont(const STRING& face, int sizeX, int sizeY)
    {

        CD3DFont* const oldFont = m_textFont;
        if (oldFont)
        {
            oldFont->~CD3DFont();
            ::operator delete(oldFont);
        }

        void* const storage = ::operator new(0x107Cu);
        CD3DFont* const replacement = storage ? new (storage) CD3DFont(face, sizeX, sizeY, 8u) : nullptr;
        m_textFont = replacement;
        (void)m_textFont->InitDeviceObjects(graphDevice(m_device));
        return m_textFont->RestoreDeviceObjects();
    }


    STRING GRAPH::D3DFormatToString(DWORD format)
    {


        switch (format)
        {
        case 20: return STRING("R8G8B8");
        case 21: return STRING("A8R8G8B8");
        case 22: return STRING("X8R8G8B8");
        case 23: return STRING("R5G6B5");
        case 24: return STRING("X1R5G5B5");
        case 25: return STRING("A1R5G5B5");
        case 26: return STRING("A4R4G4B4");
        case 28: return STRING("A8");
        case 40: return STRING("A8P8");
        case 41: return STRING("P8");
        case 50: return STRING("L8");
        case 51: return STRING("A8L8");
        case 52: return STRING("A4L4");
        case 60: return STRING("V8U8");
        case 70: return STRING("D16_LOCKABLE");
        case 71: return STRING("D32");
        case 73: return STRING("D15S1");
        case 75: return STRING("D24S8");
        case 77: return STRING("D24X8");
        case 80: return STRING("D16");
        case 100: return STRING("VERTEXDATA");
        case 101: return STRING("INDEX16");
        case 102: return STRING("INDEX32");
        case 0x31545844u: return STRING("DXT1");
        case 0x32545844u: return STRING("DXT2");
        case 0x33545844u: return STRING("DXT3");
        case 0x34545844u: return STRING("DXT4");
        case 0x35545844u: return STRING("DXT5");
        default: return STRING("Unknown");
        }
    }

    namespace
    {
        __forceinline int displayFormatBits(DWORD format) noexcept
        {

            switch (format)
            {
            case 0x15u:
            case 0x16u:
                return 32;
            case 0x17u:
            case 0x18u:
            case 0x19u:
            case 0x1Au:
                return 16;
            case 0x14u:
                return 24;
            case 0x29u:
                return 8;
            case 0x31545844u:
                return 4;
            case 0x33545844u:
            case 0x35545844u:
                return 8;
            default:
                return 0;
            }
        }
    }


    int DD_DRIVER::GetMode(int width, int height, int bitsPerPixel) const noexcept
    {

        for (DWORD index = 0; index < displayModeCount; ++index)
        {
            if (static_cast<int>(displayModeWidths[index]) == width &&
                static_cast<int>(displayModeHeights[index]) == height &&
                displayFormatBits(displayModeFormats[index]) == bitsPerPixel)
            {
                return static_cast<int>(index);
            }
        }
        return -1;
    }


    STRING DD_DRIVER::GetModeDesctription(int modeIndex) const
    {
        bool multipleFormats = false;
        if (displayModeCount > 1u)
        {
            const DWORD firstFormat = displayModeFormats[0];
            for (DWORD index = displayModeCount - 1u; index >= 1u; --index)
            {
                if (displayModeFormats[index] != firstFormat)
                {
                    multipleFormats = true;
                    break;
                }
                if (index == 1u)
                    break;
            }
        }

        const DWORD index = static_cast<DWORD>(modeIndex);
        STRING description;
        if (!multipleFormats)
        {
            constructFormattedString(description, "%i x %i",
                static_cast<int>(displayModeWidths[index]), static_cast<int>(displayModeHeights[index]));
        }
        else
        {
            constructFormattedString(description, "%i x %i x %ibpp",
                static_cast<int>(displayModeWidths[index]),
                static_cast<int>(displayModeHeights[index]),
                displayFormatBits(displayModeFormats[index]));
        }
        return description;
    }


    int GRAPH::syncDisplayModeDialog(const win::DialogItemRef& deviceRef,
                          const win::DialogItemRef& modeRef,
                          const win::DialogItemRef* fullscreenRef)
    {
        if (deviceRef.sendControlMessage(CB_GETCOUNT, 0, 0) != 0)
        {
            const LRESULT selection = deviceRef.currentSelection();
            m_selectedAdapterIndex = static_cast<int>(deviceRef.sendControlMessage(CB_GETITEMDATA, static_cast<WPARAM>(selection), 0));
        }
        else
        {
            if (fullscreenRef)
                fullscreenRef->sendControlMessage(BM_SETCHECK, (m_graphFlags >> 7u) & 1u, 0);

            deviceRef.sendControlMessage(CB_RESETCONTENT, 0, 0);
            for (DWORD adapter = 0; adapter < m_adapterCount; ++adapter)
            {
                const DD_DRIVER& record = m_adapterRecords[adapter];
                const LRESULT item = deviceRef.sendControlMessage(CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(record.description));
                if (item != CB_ERR)
                    deviceRef.sendControlMessage(CB_SETITEMDATA, static_cast<WPARAM>(item), static_cast<LPARAM>(adapter));
                if (static_cast<int>(adapter) == m_selectedAdapterIndex)
                    deviceRef.sendControlMessage(CB_SETCURSEL, static_cast<WPARAM>(item), 0);
            }
        }

        DD_DRIVER& record = selectedAdapterRecord();

        if (modeRef.sendControlMessage(CB_GETCOUNT, 0, 0) != 0)
        {
            const LRESULT selection = modeRef.currentSelection();
            const DWORD packed = static_cast<DWORD>(modeRef.sendControlMessage(CB_GETITEMDATA, static_cast<WPARAM>(selection), 0));
            m_sizeX = static_cast<float>(packed & 0x7FFFu);
            m_sizeY = static_cast<float>(static_cast<std::int32_t>(packed) >> 16u);
            m_graphFlags = (m_graphFlags & ~0x2u) | ((packed >> 14u) & 0x2u);
        }

        const int currentWidth = static_cast<int>(m_sizeX);
        const int currentHeight = static_cast<int>(m_sizeY);
        int currentMode = record.GetMode(currentWidth, currentHeight, (m_graphFlags & 0x2u) != 0u ? 32 : 16);
        if (currentMode < 0)
        {
            m_sizeX = static_cast<float>(record.displayModeWidths[0]);
            m_sizeY = static_cast<float>(record.displayModeHeights[0]);
            m_graphFlags = (m_graphFlags & ~0x2u) |
                           (displayFormatBits(record.displayModeFormats[0]) == 32 ? 0x2u : 0u);
        }

        if (fullscreenRef)
        {
            bool fullscreenAvailable = false;
            if ((record.capabilityFlags & 0x1u) != 0u &&
                ((m_graphFlags >> 1u) & 1u) == (displayFormatBits(record.desktopDisplayFormat) == 32))
            {
                fullscreenAvailable = static_cast<float>(GetSystemMetrics(SM_CXSCREEN)) >= m_sizeX &&
                                      static_cast<float>(GetSystemMetrics(SM_CYSCREEN)) >= m_sizeY;
            }

            if (fullscreenAvailable)
            {
                EnableWindow(GetDlgItem(fullscreenRef->dialog, fullscreenRef->controlId), TRUE);
            }
            else
            {
                fullscreenRef->sendControlMessage(BM_SETCHECK, BST_CHECKED, 0);
                EnableWindow(GetDlgItem(fullscreenRef->dialog, fullscreenRef->controlId), FALSE);
            }

            m_graphFlags = fullscreenRef->isChecked() ? (m_graphFlags | 0x80u) : (m_graphFlags & ~0x80u);
        }

        modeRef.sendControlMessage(CB_RESETCONTENT, 0, 0);
        for (DWORD index = 0; index < record.displayModeCount; ++index)
        {
            STRING text = record.GetModeDesctription(static_cast<int>(index));
            const int bits = displayFormatBits(record.displayModeFormats[index]);
            const DWORD packed = (bits == 32 ? 0x8000u : 0u) |
                                 (record.displayModeWidths[index] & 0x7FFFu) |
                                 ((record.displayModeHeights[index] & 0xFFFFu) << 16u);
            const LRESULT item = modeRef.sendControlMessage(CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
            if (item != CB_ERR)
                modeRef.sendControlMessage(CB_SETITEMDATA, static_cast<WPARAM>(item), static_cast<LPARAM>(packed));
            if (static_cast<float>(record.displayModeWidths[index]) == m_sizeX &&
                static_cast<float>(record.displayModeHeights[index]) == m_sizeY &&
                ((m_graphFlags >> 1u) & 1u) == (bits == 32))
            {
                modeRef.sendControlMessage(CB_SETCURSEL, static_cast<WPARAM>(item), 0);
            }
        }

        return m_selectedAdapterIndex;
    }


    void GRAPH::buildAdapterRecord(DD_DRIVER& record, void* direct3D, int adapter,
                                   const as1::core::StartupSettingsBlock& startupSettings)
    {
        IDirect3D8* d3d = graphD3D(direct3D);

        D3DADAPTER_IDENTIFIER8 identifier;
        d3d->GetAdapterIdentifier(static_cast<UINT>(adapter), 0u, &identifier);
        D3DDISPLAYMODE desktop;
        d3d->GetAdapterDisplayMode(static_cast<UINT>(adapter), &desktop);
        std::memcpy(record.description, identifier.Description, 0x28u);
        record.desktopDisplayFormat = static_cast<DWORD>(desktop.Format);

        record.displayModeCount = 0u;
        const UINT count = d3d->GetAdapterModeCount(static_cast<UINT>(adapter));
        for (UINT index = count; index > 0u; --index)
        {
            D3DDISPLAYMODE mode;
            d3d->EnumAdapterModes(static_cast<UINT>(adapter), index - 1u, &mode);

            const DWORD format = static_cast<DWORD>(mode.Format);
            DWORD colorBits = 0u;
            if (format == 21u || format == 22u)
                colorBits = 32u;
            else if (format == 23u || format == 24u)
                colorBits = 16u;
            else
                continue;

            if (!isAllowedDisplayMode(startupSettings, mode.Width, mode.Height, colorBits))
                continue;
            if (record.GetMode(static_cast<int>(mode.Width), static_cast<int>(mode.Height), static_cast<int>(colorBits)) >= 0)
                continue;

            const DWORD slot = record.displayModeCount;
            record.displayModeWidths[slot] = mode.Width;
            record.displayModeHeights[slot] = mode.Height;
            record.displayModeFormats[slot] = format;
            record.depthStencilFormats[slot] = chooseDepthStencilFormat(d3d, static_cast<UINT>(adapter), mode.Format);
            record.displayModeCount = slot + 1u;
            const STRING depthName = D3DFormatToString(record.depthStencilFormats[slot]);
            const STRING colorName = D3DFormatToString(format);
            writeLogLine(g_fileLogger, "   Enum display modes %ix%i %s %s",
                       static_cast<int>(mode.Width),
                       static_cast<int>(mode.Height),
                       colorName.c_str(), depthName.c_str());
        }

        D3DCAPS8 caps{};
        (void)d3d->GetDeviceCaps(static_cast<UINT>(adapter), D3DDEVTYPE_HAL, &caps);

        record.videoMemoryBudgetBytes = 0x007A1200u;


        const DWORD capabilityBits =
            ((caps.Caps2 >> 19u) & 0x1u) | ((caps.Caps2 & 0x2u) << 2u);
        record.capabilityFlags = (record.capabilityFlags & 0xFFFFFFF0u) | capabilityBits;
    }

    

    


    int GRAPH::SetAlphaBlend(DWORD srcBlend, DWORD dstBlend)
    {
        IDirect3DDevice8* const device = graphDevice(m_device);
        if ((m_graphFlags & 0x00004000u) == 0u)
        {
            (void)device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x1Bu), 1u);
            m_graphFlags |= 0x00004000u;
        }
        (void)device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x13u), srcBlend);
        return static_cast<int>(device->SetRenderState(
            static_cast<D3DRENDERSTATETYPE>(0x14u), dstBlend));
    }

    void GRAPH::reloadPaletteLightBuffer()
    {

        if (!m_lightBuffer || m_lightBuffer->format() != 41u)
            return;

        delete m_lightBuffer;
        m_lightBuffer = new BASE_TEXTURE(256, 256, 41u, 0u);
        if (!m_lightBuffer->isLoaded())
            logFileLoggerResourceError(g_fileLogger, "%s", 3, "Light at RelodPalette()", 0, "GRAPH");

        if (m_lightBuffer->format() == 41u)
        {
            std::array<DWORD, 256> palette{};
            for (DWORD value = 0; value < 256u; ++value)
                palette[value] = 0xFF000000u | value | (value << 8u) | (value << 16u);
            m_lightBuffer->createPaletteSlot(palette.data());
        }
        writeLogLine(g_fileLogger, "ReloadPalettes");
    }

    void GRAPH::drawBackBufferPixel2x2(float x, float y, DWORD color)
    {

        Lock();

        if (!(x >= static_cast<double>(m_viewportLeft)) ||
            !(y >= static_cast<double>(m_viewportTop)) ||
            !(x < static_cast<double>(m_viewportRight) - 1.0) ||
            !(y < static_cast<double>(m_viewportBottom) - 1.0))
            return;

        const int iy = graphConvertFloatToInt32(y);
        const int ix = graphConvertFloatToInt32(x);
        if ((m_graphFlags & 2u) != 0u)
        {
            DWORD* const pixels = static_cast<DWORD*>(m_lockedBackBufferPixels);
            pixels[ix + (iy + 1) * m_backBufferPitchPixels + 1] = color;
            pixels[ix + iy * m_backBufferPitchPixels + 1] = color;
            pixels[ix + (iy + 1) * m_backBufferPitchPixels] = color;
            pixels[ix + iy * m_backBufferPitchPixels] = color;
            return;
        }

        WORD* const pixels = static_cast<WORD*>(m_lockedBackBufferPixels);
        const WORD packed = static_cast<WORD>(
            ((color >> 3u) & 0x1Fu) |
            (g_color16RedMask & (color >> ((16u - g_color16RedShift) & 31u))) |
            (g_color16GreenMask & (color >> ((8u - g_color16GreenShift) & 31u))));
        pixels[ix + (iy + 1) * m_backBufferPitchPixels + 1] = packed;
        pixels[ix + iy * m_backBufferPitchPixels + 1] = pixels[ix + (iy + 1) * m_backBufferPitchPixels + 1];
        pixels[ix + (iy + 1) * m_backBufferPitchPixels] = pixels[ix + iy * m_backBufferPitchPixels + 1];
        pixels[ix + iy * m_backBufferPitchPixels] = pixels[ix + (iy + 1) * m_backBufferPitchPixels];
    }

    DWORD* GRAPH::sampleBackBufferPixel(DWORD* colorOut, float x, float y)
    {

        Lock();
        if (!(x >= static_cast<double>(m_viewportLeft) &&
              x < static_cast<double>(m_viewportRight) &&
              y >= static_cast<double>(m_viewportTop) &&
              y < static_cast<double>(m_viewportBottom)))
        {
            *colorOut = 0xFF000000u;
            return colorOut;
        }

        const int ix = graphConvertFloatToInt32(x);
        const int iy = graphConvertFloatToInt32(y);
        const std::ptrdiff_t index = static_cast<std::ptrdiff_t>(ix) +
                                     static_cast<std::ptrdiff_t>(iy) * static_cast<std::ptrdiff_t>(m_backBufferPitchPixels);
        if ((m_graphFlags & 2u) != 0)
        {
            *colorOut = static_cast<const DWORD*>(m_lockedBackBufferPixels)[index];
        }
        else
        {
            const WORD pixel565 = static_cast<const WORD*>(m_lockedBackBufferPixels)[index];
            *colorOut = 8u * (pixel565 & 0x1Fu) |
                        ((static_cast<DWORD>(pixel565) << (8u - g_color16GreenShift)) & 0x0000FF00u) |
                        ((static_cast<DWORD>(pixel565) << (16u - g_color16RedShift)) & 0x00FF0000u);
        }
        return colorOut;
    }


    void GRAPH::SaveTGA(const STRING* outputPath, int x, int y, int width, int height)
    {
        images::PICTURE_RESOURCE pictureResource(width, height, 1);
        images::PICTURE* picture = pictureResource.picture();

        for (int py = 0; py < height; ++py)
        {
            for (int px = 0; px < width; ++px)
            {
                DWORD color = 0;
                sampleBackBufferPixel(&color, static_cast<float>(px + x), static_cast<float>(py + y));
                pictureResource.writePictureResourcePixel(px, py, color);
            }
        }
        (void)picture->saveTGA(*outputPath, 0, 0, -1, -1);
    }


    int GRAPH::DrawLoadBar(VID* drawVid)
    {
        static DWORD previousFrameTime = 0u;
        static int frame = 0;

        const DWORD sampledTime = ::timeGetTime();
        core::RealCurrentTime = sampledTime;

        if (!drawVid ||
            sampledTime - previousFrameTime <= static_cast<DWORD>(drawVid->defaultFrameSpeed()))
        {
            return 0;
        }

        (void)preTact();
        (void)clearFrameBuffers(0xFF000000u);

        drawVidFrame(
            drawVid,
            frame,
            m_sizeX * 0.5f,
            m_sizeY * 0.5f + 1.0f,
            1.0f);

        DrawEffect(1);
        (void)drawStringColored(
            200.0f,
            ViewYMin() + 5.0f,
            m_loadingPresentationText,
            g_colorGreen.color);
        (void)PostTact(1);

        previousFrameTime = sampledTime;
        ++frame;
        if (frame >= static_cast<int>(static_cast<std::int16_t>(drawVid->totalFrames())))
            frame = 0;

        return 1;
    }


    void GRAPH::drawVidFrame(VID* vid, int cadr, float x, float y, float z)
    {

        if (!vid || vid == EmptyVid)
            return;

        const int frameCount = static_cast<int>(static_cast<std::int16_t>(vid->totalFrames()));
        if (cadr < 0 || cadr >= frameCount)
        {
            logFileLoggerResourceError(g_fileLogger, "GRAPH", 4, "ncadr in DrawVid", cadr);
            return;
        }

        const bool wasLocked = m_lockedBackBufferPixels != nullptr;
        const int vidLayer = vid->renderLayer();
        if (vidLayer == 5 || vidLayer == 6 || vidLayer == 7)
        {
            if (!m_lockedBackBufferPixels)
            {

                D3DLOCKED_RECT locked;
                IDirect3DSurface8* const surface = graphSurface(m_backBuffer);
                const HRESULT lockResult = surface->LockRect(&locked, nullptr, 0);
                if (FAILED(lockResult))
                    logGraphResourceError(0, "backBuffer", 0);
                m_lockedBackBufferPixels = locked.pBits;
                const int divisor = (m_graphFlags & 2u) != 0u ? 4 : 2;
                m_backBufferPitchPixels = locked.Pitch / divisor;
            }
        }
        else if (m_lockedBackBufferPixels)
        {
            graphSurface(m_backBuffer)->UnlockRect();
            m_lockedBackBufferPixels = nullptr;
        }

        if (vidLayer > 8)
        {
            SetRenderState(14u, 0u);
            SetRenderState(27u, 1u);
            SetRenderState(23u, 7u);
        }
        else
        {
            SetRenderState(27u, 0u);
            SetRenderState(14u, 1u);
        }

        const core::ApplicationDrawDispatcherState& appDraw =
            core::GlobalApplicationDrawDispatcherState();
        SPRITE sprite(
            vid,
            x + appDraw.cameraShiftX(),
            y + appDraw.cameraShiftY(),
            z,
            ANGLE{},
            nullptr);
        sprite.setCurrentFrameDirect(cadr);
        vid->Draw(&sprite);

        if (!wasLocked)
        {
            if (m_lockedBackBufferPixels)
            {
                graphSurface(m_backBuffer)->UnlockRect();
                m_lockedBackBufferPixels = nullptr;
            }
        }
        else if (!m_lockedBackBufferPixels)
        {

            D3DLOCKED_RECT locked;
            IDirect3DSurface8* const surface = graphSurface(m_backBuffer);
            const HRESULT lockResult = surface->LockRect(&locked, nullptr, 0);
            if (FAILED(lockResult))
                logFileLoggerResourceError(g_fileLogger, "GRAPH", 0, "backBuffer", 0);
            m_lockedBackBufferPixels = locked.pBits;
            const int divisor = (m_graphFlags & 2u) != 0u ? 4 : 2;
            m_backBufferPitchPixels = locked.Pitch / divisor;
        }
    }


    int GRAPH::logGraphResourceError(int errorCode, const char* message, int detailValue)
    {

        return static_cast<int>(logFileLoggerResourceError(g_fileLogger, "GRAPH", errorCode, message, detailValue));
    }


    void GRAPH::SaveParameters(RESOURCE* stream)
    {
        (void)stream->write(&m_renderFlags, 4);
        (void)stream->write(&m_gammaPair.first, 4);
        (void)stream->write(&m_gammaPair.second, 4);
        const DWORD direction = m_windDirection & 0xFFu;
        (void)stream->write(&direction, 4);
        (void)stream->write(&m_windSpeed, 4);
    }

    void GRAPH::invalidateDeviceObjects() noexcept
    {
        unlockBackBufferIfLocked();

        if (m_textFont)
            m_textFont->InvalidateDeviceObjects();
        if (MAP* const map = Map)
            map->invalidateFontVidDeviceObjects();


        if (m_backBuffer)
        {
            const ULONG releaseResult = graphSurface(m_backBuffer)->Release();
            m_backBuffer = nullptr;
            rewriteLogLine(g_fileLogger, "backBuffer release %i", static_cast<int>(releaseResult));
        }
    }


    int GRAPH::preTact()
    {

        if ((m_graphFlags & 0x00010000u) != 0u)
            return 0;

        const HRESULT cooperative = graphDevice(m_device)->TestCooperativeLevel();
        if (cooperative == D3DERR_DEVICELOST)
        {
            reloadPaletteLightBuffer();
            if (g_fileLogger)
                logFileLoggerResourceError(g_fileLogger, "GRAPH", 10, "device lost", 0);
            return 1;
        }

        if (cooperative == D3DERR_DEVICENOTRESET)
        {
            invalidateDeviceObjects();

            D3DPRESENT_PARAMETERS8& pp = m_d3d8PresentParameters;
            const HRESULT resetResult = graphDevice(m_device)->Reset(&pp);
            if (g_fileLogger)
                logFileLoggerResourceError(g_fileLogger, "GRAPH", 4, "device notreset", static_cast<int>(resetResult));
            if (FAILED(resetResult))
                return 2;

            if (m_textFont)
                (void)m_textFont->RestoreDeviceObjects();
            if (MAP* const map = Map)
                map->restoreFontVidDeviceObjects();

            const HRESULT backBufferResult = graphDevice(m_device)->GetBackBuffer(
                0u, D3DBACKBUFFER_TYPE_MONO, reinterpret_cast<IDirect3DSurface8**>(&m_backBuffer));
            if (backBufferResult != D3D_OK)
            {
                if (g_fileLogger)
                    logFileLoggerResourceError(g_fileLogger, "GRAPH", 9, "BackBuffer", static_cast<int>(backBufferResult));
                return 2;
            }
        }
        else if (cooperative != D3D_OK)
        {
            return 3;
        }

        const HRESULT beginResult = graphDevice(m_device)->BeginScene();
        if (beginResult != D3D_OK)
        {
            if (g_fileLogger)
                logFileLoggerResourceError(g_fileLogger, "GRAPH", 10, "3dBeginScene for PreTact", static_cast<int>(beginResult));
            return 4;
        }
        m_graphFlags |= 0x00010000u;
        return 0;
    }

    int GRAPH::PostTact(int presentFlag)
    {

        if ((m_graphFlags & 0x00010000u) == 0u)
            return 0;

        unlockBackBufferIfLocked();
        IDirect3DDevice8* const device = graphDevice(m_device);
        const HRESULT endResult = device->EndScene();
        if (endResult != D3D_OK)
            logFileLoggerResourceError(g_fileLogger, "%s", 10, "3dEndScene for PostTact", static_cast<int>(endResult), "GRAPH");

        int result = 0;
        if (presentFlag && m_effectStartTimes[6] == 0u && m_effectStartTimes[7] == 0u)
        {
            RECT sourceRect{};
            sourceRect.left = static_cast<LONG>(m_viewportLeft);
            sourceRect.top = static_cast<LONG>(m_viewportTop);
            sourceRect.right = static_cast<LONG>(m_viewportRight);
            sourceRect.bottom = static_cast<LONG>(m_viewportBottom);
            RECT destinationRect = sourceRect;

            if (!fullscreenRequested())
            {
                RECT clientRect{};
                RECT windowRect{};
                ::GetClientRect(static_cast<HWND>(m_windowHandle), &clientRect);
                ::ClientToScreen(static_cast<HWND>(m_windowHandle), reinterpret_cast<POINT*>(&clientRect));
                ::GetWindowRect(static_cast<HWND>(m_windowHandle), &windowRect);
                const LONG offsetX = windowRect.left - clientRect.left;
                const LONG offsetY = windowRect.top - clientRect.top;
                destinationRect.left += offsetX;
                destinationRect.right += offsetX;
                destinationRect.top += offsetY;
                destinationRect.bottom += offsetY;
            }

            const HRESULT presentResult = fullscreenRequested()
                ? device->Present(nullptr, nullptr, nullptr, nullptr)
                : device->Present(&sourceRect, &destinationRect, nullptr, nullptr);
            result = static_cast<int>(presentResult);
            if (presentResult != D3D_OK)
            {
                result = static_cast<int>(logFileLoggerResourceError(
                    g_fileLogger, "%s", 4, "Present", static_cast<int>(presentResult), "GRAPH"));
            }
        }
        else if (m_effectStartTimes[6] != 0u)
        {
            result = static_cast<int>(m_effectStartTimes[6]);
        }
        else if (m_effectStartTimes[7] != 0u)
        {
            result = static_cast<int>(m_effectStartTimes[7]);
        }

        m_graphFlags &= ~0x00010000u;
        return result;
    }


    void GRAPH::FlipToGDI()
    {
        if ((m_graphFlags & 0x00000080u) == 0u)
            return;

        (void)PostTact(1);

        HWND hwnd = static_cast<HWND>(m_windowHandle);
        if (win::applicationWinInstance())
            hwnd = win::applicationWinInstance()->nativeWindow();

        HDC const dc = ::GetDC(hwnd);
        if (!dc)
            (void)logFileLoggerResourceError(g_fileLogger, "GRAPH", 9, "DC in FlipToGDI", 0);

        if (::SetPixel(dc, 0, 0, 0x00FFFFFFu) == CLR_INVALID)
            (void)logFileLoggerResourceError(g_fileLogger, "GRAPH", 8, "pixel in FlipToGDI", 0);

        ::Sleep(150u);
        for (int i = 0; ::GetPixel(dc, 0, 0) == 0x00FFFFFFu; ++i)
        {
            if (i >= 8)
                break;
            (void)preTact();
            (void)PostTact(1);
            ::Sleep(150u);
        }
        (void)::ReleaseDC(hwnd, dc);
    }


    int GRAPH::clearFrameBuffers(DWORD color)
    {

        unlockBackBufferIfLocked();


        graphDevice(m_device)->Clear(0u, nullptr, D3DCLEAR_TARGET, color, 0.0f, 0u);
        const std::uint32_t heightValue = static_cast<std::uint32_t>(graphConvertFloatToInt32(m_sizeY));
        std::uint32_t wordCount = heightValue * static_cast<std::uint32_t>(m_softwareDepthPitch);
        std::uint16_t* out = m_softwareDepthBuffer;
        if ((wordCount & 1u) != 0u)
        {
            *out++ = static_cast<std::uint16_t>(0x03FFu);
            --wordCount;
        }
        const std::uint32_t dwordCount = wordCount >> 1u;
        const std::uint32_t fill = 0x03FF03FFu;
        for (std::uint32_t i = 0; i < dwordCount; ++i)
        {
            std::memcpy(out, &fill, sizeof(fill));
            out += 2;
        }
        return static_cast<int>(0x03FF03FFu);
    }


    void GRAPH::DrawSquall()
    {

        if ((core::ApplicationFlags() & application_flags::BucketTimingActive) != 0u)
            return;

        const std::uint32_t now = core::CurrentTimeMilliseconds();
        if ((m_renderFlags & 0x80u) != 0u)
        {
            if (floatEqualOrUnordered(old_w_speed, -1.0f))
                old_w_speed = m_windSpeed;

            const std::uint32_t elapsed = now - start_squall;
            std::uint32_t triangularTime = 0u;
            if (elapsed <= 0x800u)
            {
                triangularTime = elapsed;
            }
            else if (elapsed <= 0x1000u)
            {
                triangularTime = 0x1000u - elapsed;
            }
            else
            {
                m_renderFlags &= 0xFFFFFF7Fu;
                m_windSpeed = old_w_speed;
                old_w_speed = -1.0f;
                return;
            }

            m_windSpeed =
                static_cast<float>(triangularTime) * old_w_speed * 0.001953125f +
                old_w_speed;
            return;
        }

        start_squall = now;
        if (floatEqualOrUnordered(old_w_speed, -1.0f))
        {
            old_w_speed = -1.0f;
            return;
        }

        m_windSpeed = old_w_speed;
        old_w_speed = -1.0f;
    }

    void GRAPH::drawFogBufferOverlay(float left, float top, float right, float bottom,
                              int fogStart, int fogEnd, DWORD colorMask, const WORD* ramp,
                              int baseDepth, int blendFlag)
    {

        int clippedLeft = graphConvertFloatToInt32(left);
        int clippedTop = graphConvertFloatToInt32(top);
        int clippedRight = graphConvertFloatToInt32(right);
        int clippedBottom = graphConvertFloatToInt32(bottom);

        if ((m_graphFlags & 0x20u) != 0u || ramp == nullptr ||
            floatLessOrUnordered(right, m_viewportLeft) ||
            !floatLessOrUnordered(left, m_viewportRight) ||
            floatLessOrUnordered(bottom, m_viewportTop) ||
            !floatLessOrUnordered(top, m_viewportBottom))
            return;
        if (floatLessOrUnordered(left, m_viewportLeft))
            clippedLeft = graphConvertFloatToInt32(m_viewportLeft);
        if (floatLessOrUnordered(top, m_viewportTop))
            clippedTop = graphConvertFloatToInt32(m_viewportTop);
        if (!floatLessOrUnordered(right, m_viewportRight))
            clippedRight = graphConvertFloatToInt32(m_viewportRight);
        if (!floatLessOrUnordered(bottom, m_viewportBottom))
            clippedBottom = graphConvertFloatToInt32(m_viewportBottom);

        const int width = clippedRight - clippedLeft;
        const int height = clippedBottom - clippedTop;
        if (width < 4 || height < 4)
            return;

        RECTI sourceRect{0, 0, width / 4, height / 4};
        RECTI destinationRect{clippedLeft, clippedTop, clippedRight, clippedBottom};
        const std::uint32_t lowerDepthBits =
            static_cast<std::uint32_t>(baseDepth) +
            ((static_cast<std::uint32_t>(fogStart) - static_cast<std::uint32_t>(fogEnd)) << 3u);
        const int lowerDepth = static_cast<std::int32_t>(lowerDepthBits);

        int texturePitchBytes = 0;
        std::uint16_t* const locked = m_lightBuffer->lock16(&texturePitchBytes, &sourceRect);
        if (!locked)
        {
            if (g_fileLogger)
                logFileLoggerResourceError(g_fileLogger, "%s", 0, "fog buffer", 0, "GRAPH");
            return;
        }

        const std::uint16_t* const depth = m_softwareDepthBuffer;
        int depthIndex = clippedLeft + m_softwareDepthPitch * clippedTop;
        if (m_lightBuffer->format() == 41u)
        {
            std::uint8_t* output = reinterpret_cast<std::uint8_t*>(locked);
            std::uint8_t previous = 0u;
            const unsigned rowCount = static_cast<unsigned>(height + 3) >> 2u;
            for (unsigned row = 0; row < rowCount; ++row)
            {
                const unsigned columnCount = static_cast<unsigned>(width + 3) >> 2u;
                for (unsigned column = 0; column < columnCount; ++column)
                {
                    const std::uint16_t first = depth[depthIndex];
                    const std::uint16_t second = depth[depthIndex + 3];
                    const int depthValue = static_cast<int>(first < second ? first : second) - 1024;
                    if (depthValue > baseDepth)
                    {
                        const int upperFadeDepth = static_cast<std::int32_t>(
                            static_cast<std::uint32_t>(baseDepth) + 10u);
                        if (depthValue <= upperFadeDepth)
                        {
                            previous = 0u;
                            *output = 0u;
                        }
                        else
                        {
                            *output = previous;
                        }
                    }
                    else if (depthValue > lowerDepth)
                    {
                        previous = reinterpret_cast<const std::uint8_t*>(ramp)[2 * (baseDepth - depthValue)];
                        *output = previous;
                    }
                    else
                    {
                        previous = 0xFFu;
                        *output = 0xFFu;
                    }
                    ++output;
                    depthIndex += 4;
                }
                const int quarterWidth = (width + 3) / 4;
                depthIndex += 4 * (m_softwareDepthPitch - quarterWidth);
                output += texturePitchBytes - quarterWidth;
            }
        }
        else
        {
            std::uint16_t* output = locked;
            std::uint16_t previous = 0u;
            const unsigned rowCount = static_cast<unsigned>(height + 3) >> 2u;
            for (unsigned row = 0; row < rowCount; ++row)
            {
                const unsigned columnCount = static_cast<unsigned>(width + 3) >> 2u;
                for (unsigned column = 0; column < columnCount; ++column)
                {
                    const std::uint16_t first = depth[depthIndex];
                    const std::uint16_t second = depth[depthIndex + 3];
                    const int depthValue = static_cast<int>(first < second ? first : second) - 1024;
                    if (depthValue > baseDepth)
                    {
                        const int upperFadeDepth = static_cast<std::int32_t>(
                            static_cast<std::uint32_t>(baseDepth) + 10u);
                        if (depthValue <= upperFadeDepth)
                        {
                            previous = 0u;
                            *output = 0u;
                        }
                        else
                        {
                            *output = previous;
                        }
                    }
                    else if (depthValue > lowerDepth)
                    {
                        previous = ramp[baseDepth - depthValue];
                        *output = previous;
                    }
                    else
                    {
                        previous = ramp[baseDepth - lowerDepth];
                        *output = previous;
                    }
                    ++output;
                    depthIndex += 4;
                }
                const int quarterWidth = (width + 3) / 4;
                depthIndex += 4 * (m_softwareDepthPitch - quarterWidth);
                output += texturePitchBytes / 2 - quarterWidth;
            }
        }

        m_lightBuffer->unlock();
        SetAlphaBlend(2u - (blendFlag != 0 ? 1u : 0u), 4u);
        const int zDepth = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(baseDepth) + 0x3FEu);
        const float z = static_cast<float>(zDepth) * 0.000015258789f;
        const Gamma colors(~colorMask, Color(0, 0, 0).color);
        const DWORD depthBits = floatBits(z);
        m_lightBuffer->DrawDepthRectangle(
            depthBits, depthBits, destinationRect, sourceRect, reinterpret_cast<const DWORD*>(&colors));
    }

    void GRAPH::drawSnowLightBuffer()
    {

        const core::ApplicationDrawDispatcherState& appDraw = core::GlobalApplicationDrawDispatcherState();
        const int shiftX = graphConvertFloatToInt32(appDraw.cameraShiftX());
        const int shiftY = graphConvertFloatToInt32(appDraw.cameraShiftY());
        const int startX = (-(shiftX & 3)) & 3;
        const int startY = (-(shiftY & 3)) & 3;

        if ((m_graphFlags & 0x20u) != 0u ||
            (core::ApplicationFlags() & application_flags::BucketTimingActive) != 0u)
            return;

        const std::uint32_t now = core::CurrentTimeMilliseconds();
        if ((m_renderFlags & 0x40u) == 0u)
        {
            g_groundSnowStart = now;
            g_groundSnowAmount = 0u;
            return;
        }

        if (g_groundSnowAmount < 0x100u)
            g_groundSnowAmount = (now - g_groundSnowStart) >> 7u;

        const int screenWidth = graphConvertFloatToInt32(m_sizeX);
        const int screenHeight = graphConvertFloatToInt32(m_sizeY);
        RECTI textureRect{0, 0, screenWidth / 4, screenHeight / 4};
        RECTI screenRect{0, 0, screenWidth, screenHeight};
        int texturePitchBytes = 0;
        std::uint16_t* const locked = m_lightBuffer->lock16(&texturePitchBytes, &textureRect);
        if (!locked)
        {
            if (g_fileLogger)
                logFileLoggerResourceError(g_fileLogger, "%s", 0, "snow buffer", 0, "GRAPH");
            return;
        }

        const std::uint16_t* const depth = m_softwareDepthBuffer;
        const int depthPitch = m_softwareDepthPitch;
        const bool paletteTexture = m_lightBuffer->format() == 0x29u;
        if (paletteTexture)
        {
            std::uint8_t* const output = reinterpret_cast<std::uint8_t*>(locked);
            for (int y = startY; floatLessOrUnordered(static_cast<float>(y), m_sizeY); y += 4)
            {
                int depthIndex = startX + y * depthPitch;
                for (int x = startX; floatLessOrUnordered(static_cast<float>(x), m_sizeX); x += 4, depthIndex += 4)
                {
                    std::uint32_t intensity = 0u;
                    bool accepted = false;
                    if (y > 3)
                        accepted = graphSnowEdgeIntensity(depth[depthIndex], depth[depthIndex - 4 * depthPitch], intensity);
                    if (!accepted)
                    {
                        intensity = 0u;
                        graphSnowEdgeIntensity(depth[depthIndex], depth[depthIndex + 4 * depthPitch], intensity);
                    }
                    output[(y / 4) * texturePitchBytes + (x / 4)] = static_cast<std::uint8_t>(intensity);
                }
            }
        }
        else
        {
            std::uint16_t* const output = locked;
            const int texturePitchWords = texturePitchBytes / 2;
            for (int y = startY; floatLessOrUnordered(static_cast<float>(y), m_sizeY); y += 4)
            {
                int depthIndex = startX + y * depthPitch;
                for (int x = startX; floatLessOrUnordered(static_cast<float>(x), m_sizeX); x += 4, depthIndex += 4)
                {
                    std::uint32_t intensity = 0u;
                    bool accepted = false;
                    if (y > 3)
                        accepted = graphSnowEdgeIntensity(depth[depthIndex], depth[depthIndex - 4 * depthPitch], intensity);
                    if (!accepted)
                    {
                        intensity = 0u;
                        graphSnowEdgeIntensity(depth[depthIndex], depth[depthIndex + 4 * depthPitch], intensity);
                    }
                    output[(y / 4) * texturePitchWords + (x / 4)] =
                        m_intensityPalette16[static_cast<std::size_t>(intensity)];
                }
            }
        }

        m_lightBuffer->unlock();
        SetRenderState(0x1Du, 0u);
        SetAlphaBlend(2u, 4u);
        const DWORD colors[2] = {0u, 0u};
        m_lightBuffer->DrawFixedDepthRectangle(screenRect, textureRect, colors);
    }


    int GRAPH::drawLineParticles()
    {

        int result = static_cast<int>(m_renderFlags);
        if ((m_renderFlags & 0x00000C00u) == 0u)
        {
            g_lineParticleCount = 0;
            return result;
        }
        if ((m_graphFlags & 0x20u) != 0u)
            return result;
        void* const applicationOwner = core::ApplicationOwner();
        result = static_cast<int>(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(applicationOwner)));
        if ((core::ApplicationFlags() & application_flags::BucketTimingActive) != 0u)
            return result;

        const std::uint8_t direction = static_cast<std::uint8_t>(m_windDirection);
        if (g_lineParticleWindDirection != direction ||
            !floatEqualOrUnordered(g_lineParticleWindSpeed, m_windSpeed))
        {
            const float oldWindX = g_lineParticleWindX;
            g_lineParticleWindX =
                SPRITE::directionSinUncheckedValue(direction) * m_windSpeed * 1000.0f;
            if (g_lineParticleCount > 0)
            {
                const float windDifference = g_lineParticleWindX - oldWindX;
                for (int index = 0; index < g_lineParticleCount; ++index)
                {
                    GraphWeatherLineParticle& particle = g_lineParticles[static_cast<std::size_t>(index)];
                    particle.vertex[1].x +=
                        (particle.vertex[1].y - particle.vertex[0].y) * windDifference * kGraphOneOver200;
                }
            }
            g_lineParticleWindDirection = direction;
            g_lineParticleWindSpeed = m_windSpeed;
        }

        const DWORD mode = m_renderFlags & 0x00000C00u;
        switch (mode)
        {
        case 0x00000C00u:
            g_lineParticleCount = 250;
            break;
        case 0x00000800u:
            if (g_lineParticleCount >= 250)
                SetEnvironment(0x00000C00u);
            else
                ++g_lineParticleCount;
            break;
        case 0x00000400u:
            if (g_lineParticleCount <= 0)
                m_renderFlags &= 0xFFFFF3FFu;
            else
                --g_lineParticleCount;
            break;
        default:
            break;
        }

        if (g_lineParticleCount > 0)
        {
            const std::uint32_t deltaMilliseconds =
                core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
            int respawned = 0;
            for (int index = 0; index < g_lineParticleCount; ++index)
            {
                GraphWeatherLineParticle& particle = g_lineParticles[static_cast<std::size_t>(index)];
                GraphWeatherVertex& first = particle.vertex[0];
                GraphWeatherVertex& second = particle.vertex[1];
                const bool active =
                    first.color != 0u &&
                    first.y <= m_viewportBottom &&
                    first.z >= 0.015625f;

                if (active)
                {
                    const float travel =
                        (second.z - first.z) * static_cast<float>(deltaMilliseconds) * 200.0f;
                    float windTravel = g_lineParticleWindX * travel * kGraphOneOver200;
                    if (floatLessOrUnordered(windTravel + first.x, 0.0f))
                        windTravel += static_cast<float>(m_sizeX);
                    if (windTravel + first.x > static_cast<float>(m_sizeX))
                        windTravel -= static_cast<float>(m_sizeX);
                    const float depthTravel = travel * kGraphOneOver8192;
                    first.x += windTravel;
                    first.y += travel;
                    first.z -= depthTravel;
                    second.x += windTravel;
                    second.y += travel;
                    second.z -= depthTravel;
                }
                else
                {
                    ++respawned;
                    const float length = static_cast<float>((std::rand() % 51) + 15);
                    const float windTail = g_lineParticleWindX * length * -kGraphOneOver200;
                    const float x = static_cast<float>(std::rand()) *
                        (m_viewportRight - 1.0f) * kGraphRand32767;
                    const float yLimit = respawned >= 50 ? m_viewportBottom : 40.0f;
                    const float y = static_cast<float>(std::rand()) * yLimit * kGraphRand32767;
                    const float z = (length + 10.0f) * kGraphOneOver819_2 + 0.015625f;
                    first = GraphWeatherVertex{x, y, z, 1.0f, 0x70E0E0FFu};
                    second = GraphWeatherVertex{
                        x + windTail,
                        y - length,
                        z + length * kGraphOneOver8192,
                        1.0f,
                        0x308080FFu};
                }
            }
        }

        IDirect3DDevice8* const device = graphDevice(m_device);
        device->SetTexture(0u, nullptr);
        SetAlphaBlend(5u, 6u);
        result = drawPrimitiveUp(
            2u,
            0x44u,
            g_lineParticles.data(),
            static_cast<DWORD>(sizeof(GraphWeatherVertex)),
            2 * g_lineParticleCount);
        return result;
    }


    int GRAPH::drawCrossParticles()
    {

        int result = static_cast<int>(m_renderFlags);
        if ((m_renderFlags & 0x0000C000u) == 0u)
        {
            g_crossParticleCount = 0;
            return result;
        }
        if ((m_graphFlags & 0x20u) != 0u)
            return result;
        void* const applicationOwner = core::ApplicationOwner();
        result = static_cast<int>(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(applicationOwner)));
        if ((core::ApplicationFlags() & application_flags::BucketTimingActive) != 0u)
            return result;

        const std::uint8_t direction = static_cast<std::uint8_t>(m_windDirection);
        if (g_crossParticleWindDirection != direction ||
            !floatEqualOrUnordered(g_crossParticleWindSpeed, m_windSpeed))
        {
            g_crossParticleWindX =
                SPRITE::directionSinUncheckedValue(direction) * m_windSpeed * 1000.0f;
            g_crossParticleWindDirection = direction;
            g_crossParticleWindSpeed = m_windSpeed;
        }

        const DWORD mode = m_renderFlags & 0x0000C000u;
        switch (mode)
        {
        case 0x0000C000u:
            g_crossParticleCount = 1000;
            break;
        case 0x00008000u:
            if (g_crossParticleCount >= 1000)
            {
                g_crossParticleCount = 1000;
                SetEnvironment(0x0000C000u);
            }
            else
            {
                ++g_crossParticleCount;
            }
            break;
        case 0x00004000u:
            if (g_crossParticleCount <= 0)
                m_renderFlags &= 0xFFFF3FFFu;
            else
                --g_crossParticleCount;
            break;
        default:
            break;
        }

        int count = g_crossParticleCount;
        if (count > 0)
        {
            const std::uint32_t deltaMilliseconds =
                core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
            const core::ApplicationDrawDispatcherState& appDraw = core::GlobalApplicationDrawDispatcherState();
            const float cameraX = appDraw.cameraShiftX();
            const float cameraY = appDraw.cameraShiftY();
            int respawned = 0;
            for (int index = 0; index < count; ++index)
            {
                GraphWeatherCrossParticle& particle = g_crossParticles[static_cast<std::size_t>(index)];
                GraphWeatherVertex& first = particle.vertex[0];
                const bool active = first.color != 0u && first.z >= 0.015625f;
                if (active)
                {
                    const float travel =
                        (particle.vertex[1].z - first.z - kGraphOneOver8192) *
                        static_cast<float>(deltaMilliseconds) * 150.0f;
                    const float xTravel =
                        (g_crossParticleWindX * travel + 50.0f - static_cast<float>(std::rand() % 101)) * 0.02f -
                        (g_crossParticlePreviousNegatedCameraX - -cameraX);
                    const float yTravel =
                        travel - (g_crossParticlePreviousNegatedCameraY - -cameraY);

                    float adjustedXTravel = xTravel;
                    float adjustedYTravel = yTravel;
                    const float movedX = adjustedXTravel + first.x;
                    if (!floatLessOrUnordered(m_viewportLeft - 30.0f, movedX) &&
                        !floatEqualOrUnordered(m_viewportLeft - 30.0f, movedX))
                        adjustedXTravel += m_viewportRight - m_viewportLeft;
                    const float movedXAfterLeft = adjustedXTravel + first.x;
                    if (floatLessOrUnordered(m_viewportRight + 30.0f, movedXAfterLeft))
                        adjustedXTravel -= m_viewportRight - m_viewportLeft;

                    const float movedY = adjustedYTravel + first.y;
                    if (!floatLessOrUnordered(m_viewportTop - 30.0f, movedY) &&
                        !floatEqualOrUnordered(m_viewportTop - 30.0f, movedY))
                        adjustedYTravel += m_viewportBottom - m_viewportTop;
                    const float movedYAfterTop = adjustedYTravel + first.y;
                    if (floatLessOrUnordered(m_viewportBottom + 30.0f, movedYAfterTop))
                        adjustedYTravel -= m_viewportBottom - m_viewportTop;

                    const float depthTravel = travel * kGraphOneOver8192;
                    for (GraphWeatherVertex& vertex : particle.vertex)
                    {
                        vertex.x += adjustedXTravel;
                        vertex.y += adjustedYTravel;
                        vertex.z -= depthTravel;
                    }
                }
                else
                {
                    ++respawned;
                    const float radius = static_cast<float>((std::rand() % 3) + 2);
                    const float centerX = static_cast<float>(std::rand()) *
                        (m_viewportRight - 1.0f) * kGraphRand32767;
                    const float yLimit = respawned >= 50 ? m_viewportBottom : 40.0f;
                    const float centerY = static_cast<float>(std::rand()) * yLimit * kGraphRand32767;
                    const float z = static_cast<float>(std::rand()) *
                        (m_viewportBottom + 50.0f) * kGraphRand268435456 + 0.015625f;
                    const float halfRadius = radius * 0.5f;
                    const float zTail = z + radius * kGraphOneOver8192;

                    particle.vertex[0] = GraphWeatherVertex{centerX - radius, centerY, z, 1.0f, 0xFFFFFFFFu};
                    particle.vertex[1] = GraphWeatherVertex{centerX + radius, centerY, zTail, 1.0f, 0xFFFFFFFFu};
                    particle.vertex[2] = GraphWeatherVertex{centerX - halfRadius, centerY - radius, z, 1.0f, 0xFFFFFFFFu};
                    particle.vertex[3] = GraphWeatherVertex{centerX + halfRadius, centerY + radius, z, 1.0f, 0xFFFFFFFFu};
                    particle.vertex[4] = GraphWeatherVertex{centerX - halfRadius, centerY + radius, z, 1.0f, 0xFFFFFFFFu};
                    particle.vertex[5] = GraphWeatherVertex{centerX + halfRadius, centerY - radius, z, 1.0f, 0xFFFFFFFFu};
                }
            }

            for (int index = 0; index < count; ++index)
            {
                if ((std::rand() % 5) == 0)
                {
                    GraphWeatherCrossParticle& particle = g_crossParticles[static_cast<std::size_t>(index)];
                    const float middleX = (particle.vertex[1].x + particle.vertex[0].x) * 0.5f;
                    const float middleY = (particle.vertex[1].y + particle.vertex[0].y) * 0.5f;
                    for (GraphWeatherVertex& vertex : particle.vertex)
                    {
                        const float oldX = vertex.x;
                        const float oldY = vertex.y;
                        vertex.x = middleX + oldY - middleY;
                        vertex.y = oldX + middleY - middleX;
                    }
                }
            }
        }

        const core::ApplicationDrawDispatcherState& appDrawForShiftCache =
            core::GlobalApplicationDrawDispatcherState();
        g_crossParticlePreviousNegatedCameraX = -appDrawForShiftCache.cameraShiftX();
        g_crossParticlePreviousNegatedCameraY = -appDrawForShiftCache.cameraShiftY();

        IDirect3DDevice8* const device = graphDevice(m_device);
        device->SetTexture(0u, nullptr);
        SetAlphaBlend(5u, 6u);
        result = drawPrimitiveUp(
            2u,
            0x44u,
            g_crossParticles.data(),
            static_cast<DWORD>(sizeof(GraphWeatherVertex)),
            2 * g_crossParticleCount);
        return result;
    }

    void GRAPH::Tact(int worldTickFlag)
    {

        core::Application* const application = reinterpret_cast<core::Application*>(core::ApplicationOwner());
        MAP& map = *Map;


        float preSquallCameraX = 0.0f;
        float preSquallCameraY = 0.0f;
        const bool squallShift =
            (m_renderFlags & 4u) != 0u &&
            (core::ApplicationFlags() & application_flags::BucketTimingActive) == 0u;
        if (squallShift)
        {
            preSquallCameraX = application->cameraShiftX();
            preSquallCameraY = application->cameraShiftY();
            const int jitterY = std::rand() % 9;
            const int jitterX = std::rand() % 9;
            map.SetShiftCoor(
                preSquallCameraX + static_cast<float>(m_sizeX) * 0.5f + 4.0f - static_cast<float>(jitterX),
                preSquallCameraY + static_cast<float>(m_sizeY) * 0.5f + 4.0f - static_cast<float>(jitterY),
                0);
        }

        auto setStage0MagFilter = [this](DWORD value) -> DWORD
        {
            return static_cast<DWORD>(D3D8SetSamplerState(graphDevice(m_device),
                0u, D3DSAMP_MAGFILTER, value));
        };

        if (worldTickFlag)
        {


            bool clearBackBuffer = true;
            if (application->vidCount() > 0x400 && application->vidAt(0x400) != nullptr)
            {
                const float cameraX = application->cameraShiftX();
                const float cameraY = application->cameraShiftY();
                if (cameraX >= 0.0f &&
                    m_viewportRight + cameraX - m_viewportLeft <= application->mapExtentX() &&
                    cameraY >= 0.0f &&
                    m_viewportBottom + cameraY - m_viewportTop <= application->mapExtentY())
                {
                    clearBackBuffer = false;
                }
            }
            if (clearBackBuffer)
                clearFrameBuffers(Color(0, 0, 0).color);


            SetRenderState(0x1Bu, 0u);
            SetRenderState(0x17u, 8u);
            SetRenderState(0x0Eu, 1u);
            setStage0MagFilter(1u);

            unlockBackBufferIfLocked();
            application->drawSpritePass(0);
            graphDevice(m_device)->SetTexture(0u, nullptr);
            Lock();
            application->drawSpritePass(1);
            application->drawSpritePass(2);
            application->drawSpritePass(3);

            unlockBackBufferIfLocked();
            application->drawSpritePass(4);

            Lock();
            application->drawSpritePass(5);
            application->drawSpritePass(6);
            application->drawSpritePass(7);

            unlockBackBufferIfLocked();


            SetRenderState(0x17u, 7u);
            application->drawSpritePass(8);


            SetRenderState(0x0Eu, 0u);
            SetRenderState(0x1Bu, 1u);
            setStage0MagFilter(1u);
            application->drawSpritePass(9);
            application->drawSpritePass(10);

            setStage0MagFilter(2u);
            application->drawSpritePass(11);
            drawSnowLightBuffer();
            drawCrossParticles();

            setStage0MagFilter(1u);
            application->drawSpritePass(12);
            drawLineParticles();

            SetRenderState(0x0Eu, 1u);
            setStage0MagFilter(1u);

            Lock();
            application->drawSpritePass(13);
            unlockBackBufferIfLocked();

            application->drawSpritePass(14);

            Lock();
            application->drawSpritePass(15);

            if (Mouse && Mouse->hardwareCursorEnabled() == 0)
            {
                SPRITE* node = mouseSprite();
                VID* const mouseVid = node ? node->Vid() : nullptr;
                if (mouseVid && (mouseVid->properties() & 0x00008000u) != 0u)
                {
                    while (node)
                    {
                        if (!node->isDrawSuppressed())
                            node->Draw();
                        node = node->childChain();
                    }
                }
            }
        }

        DrawSquall();
        if (squallShift)
        {
            map.SetShiftCoor(
                preSquallCameraX + static_cast<float>(m_sizeX) * 0.5f,
                preSquallCameraY + static_cast<float>(m_sizeY) * 0.5f,
                0);
        }
        unlockBackBufferIfLocked();

        if (m_movie.pGraph && m_movie.Update())
            m_movie.Release();

        SetRenderState(0x1Du, 0u);
        DrawEffect(worldTickFlag);
        unlockBackBufferIfLocked();
    }


    int GRAPH::SetRenderState(DWORD renderState, DWORD value)
    {
        if (renderState == 0x17u || renderState == 0x0Eu || renderState == 7u)
            return 0;

        if (renderState == 0x1Du)
        {
            const DWORD oldEnabled = (m_graphFlags >> 13u) & 1u;
            if (oldEnabled == value)
                return 0;
            m_graphFlags = (m_graphFlags & ~0x00002000u) |
                           ((value != 0u ? 1u : 0u) << 13u);
        }
        else if (renderState == 0x1Bu)
        {
            const DWORD oldEnabled = (m_graphFlags >> 14u) & 1u;
            if (oldEnabled == value)
                return 0;
            m_graphFlags = (m_graphFlags & ~0x00004000u) |
                           ((value != 0u ? 1u : 0u) << 14u);
        }

        return static_cast<int>(graphDevice(m_device)->SetRenderState(
            static_cast<D3DRENDERSTATETYPE>(renderState), value));
    }


    void GRAPH::PlayMovie(const STRING* moviePath)
    {
        const int centerY = signedHalfFromFloatTowardZero(m_sizeY);
        const int centerX = signedHalfFromFloatTowardZero(m_sizeX);
        m_movie.Open(moviePath, centerX, centerY);
    }


    void MOVIE::Open(const STRING* moviePath, int centerX, int centerY)
    {
        (void)centerX;
        (void)centerY;
        Release();
        Graph->FlipToGDI();

        HRESULT hr = ::CoCreateInstance(
            CLSID_FilterGraph, nullptr, 1u, IID_IGraphBuilder, &pGraph);
        if (hr < 0)
        {
            if (g_fileLogger)
                (void)logFileLoggerResourceError(g_fileLogger, "MOVIE", 3, "GraphBuilder", static_cast<int>(hr));
            return;
        }

        hr = static_cast<IGraphBuilder*>(pGraph)->QueryInterface(
            IID_IMediaControl, &pMediaControl);
        if (hr < 0)
        {
            if (g_fileLogger)
                (void)logFileLoggerResourceError(g_fileLogger, "MOVIE", 3, "MediaControl", static_cast<int>(hr));
            Release();
            return;
        }

        (void)static_cast<IGraphBuilder*>(pGraph)->QueryInterface(IID_IMediaEvent, &pEvent);
        wchar_t widePath[1024];
        (void)moviePath->ToWideChar(widePath, 1024);
        hr = static_cast<IGraphBuilder*>(pGraph)->RenderFile(widePath, nullptr);
        if (hr < 0)
        {
            if (g_fileLogger)
                (void)logFileLoggerResourceError(g_fileLogger, "MOVIE", 4, "RenderFile", static_cast<int>(hr));
            Release();
            return;
        }

        (void)static_cast<IGraphBuilder*>(pGraph)->QueryInterface(IID_IVideoWindow, &pVidWin);
        static_cast<IVideoWindow*>(pVidWin)->put_Owner(
            reinterpret_cast<OAHWND>(win::applicationWinInstance()->nativeWindow()));
        static_cast<IVideoWindow*>(pVidWin)->put_WindowStyle(0x44000000L);

        GRAPH* const viewportGraph = Graph;
        const int top = graphConvertFloatToInt32(viewportGraph->ViewYMin());
        const int left = graphConvertFloatToInt32(viewportGraph->ViewXMin());
        IVideoWindow* const videoWindow = static_cast<IVideoWindow*>(pVidWin);
        const int bottom = graphConvertFloatToInt32(viewportGraph->ViewYMax());
        const int height = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(bottom) - static_cast<std::uint32_t>(top) + 1u);
        const int right = graphConvertFloatToInt32(viewportGraph->ViewXMax());
        const int width = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(right) - static_cast<std::uint32_t>(left) + 1u);
        videoWindow->SetWindowPosition(left, top, width, height);

        (void)static_cast<IMediaControl*>(pMediaControl)->Run();
        ::SetCapture(win::applicationWinInstance()->nativeWindow());
        GRAPH* const cursorGraph = Graph;
        const int cursorY = graphConvertFloatToInt32(cursorGraph->screenHeight());
        const int cursorX = graphConvertFloatToInt32(cursorGraph->screenWidth());
        (void)::SetCursorPos(cursorX, cursorY);
    }


    int MOVIE::Update() const noexcept
    {
        if (!pEvent)
            return 1;
        long eventCode = 0;
        static_cast<IMediaEvent*>(pEvent)->WaitForCompletion(0, &eventCode);
        return eventCode == 1 ? 1 : 0;
    }


    void MOVIE::Pause() noexcept
    {
        if (pMediaControl)
            (void)static_cast<IMediaControl*>(pMediaControl)->Pause();
    }

    void MOVIE::Resume() noexcept
    {
        if (pMediaControl)
            (void)static_cast<IMediaControl*>(pMediaControl)->Run();
    }


    void GRAPH::BeginPause()
    {
        m_graphFlags |= 0x1u;
        if (m_lockedBackBufferPixels)
        {
            graphSurface(m_backBuffer)->UnlockRect();
            m_lockedBackBufferPixels = nullptr;
        }
        FlipToGDI();
        ::DrawMenuBar(static_cast<HWND>(m_windowHandle));
        ::RedrawWindow(static_cast<HWND>(m_windowHandle), nullptr, nullptr, 0x400u);
        m_movie.Pause();
    }


    void GRAPH::EndPause()
    {
        m_graphFlags &= ~0x1u;
        m_movie.Resume();
    }

    int GRAPH::Lock()
    {

        if (m_lockedBackBufferPixels)
        {
            return static_cast<int>(
                static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(m_lockedBackBufferPixels)));
        }
        IDirect3DSurface8* const surface = graphSurface(m_backBuffer);
        D3DLOCKED_RECT locked;
        const HRESULT hr = surface->LockRect(&locked, nullptr, 0);
        if (FAILED(hr))
            logFileLoggerResourceError(g_fileLogger, "%s", 0, "backBuffer", 0, "GRAPH");
        m_lockedBackBufferPixels = locked.pBits;
        const int divisor = (m_graphFlags & 2u) != 0u ? 4 : 2;
        m_backBufferPitchPixels = locked.Pitch / divisor;
        return m_backBufferPitchPixels;
    }

    namespace
    {
        struct GraphOverlayVertex
        {
            DWORD x;
            DWORD y;
            DWORD z;
            DWORD rhw;
            DWORD diffuse;
            DWORD specular;
        };
    }


    void GRAPH::ShadowBar(float x, float y, float x1, float y1, int shadow)
    {
        const DWORD left = floatBits(x);
        const DWORD top = floatBits(y);
        const DWORD right = floatBits(x1);
        const DWORD bottom = floatBits(y1);
        const DWORD color = static_cast<DWORD>(shadow);
        const GraphOverlayVertex vertices[4] = {
            {left,  top,    0x3F7FFFFEu, 0x3F800000u, color, 0xFFFFFFFFu},
            {right, top,    0x3F7FFFFEu, 0x3F800000u, color, 0xFFFFFFFFu},
            {right, bottom, 0x3F7FFFFEu, 0x3F800000u, color, 0xFFFFFFFFu},
            {left,  bottom, 0x3F7FFFFEu, 0x3F800000u, color, 0xFFFFFFFFu},
        };

        unlockBackBufferIfLocked();
        graphDevice(m_device)->SetTexture(0, nullptr);
        (void)SetAlphaBlend(1u, 4u);
        (void)SetRenderState(0x0Eu, 0u);
        (void)drawPrimitiveUp(6u, 0xC4u, vertices, 24u, 4);
        (void)SetRenderState(0x0Eu, 1u);
    }


    void GRAPH::LightBar(float x, float y, float x1, float y1, DWORD bright)
    {
        const DWORD left = floatBits(x);
        const DWORD top = floatBits(y);
        const DWORD right = floatBits(x1);
        const DWORD bottom = floatBits(y1);
        const GraphOverlayVertex vertices[4] = {
            {left,  top,    0x3F7FFFFEu, 0x3F800000u, bright, 0xFFFFFFFFu},
            {right, top,    0x3F7FFFFEu, 0x3F800000u, bright, 0xFFFFFFFFu},
            {right, bottom, 0x3F7FFFFEu, 0x3F800000u, bright, 0xFFFFFFFFu},
            {left,  bottom, 0x3F7FFFFEu, 0x3F800000u, bright, 0xFFFFFFFFu},
        };

        unlockBackBufferIfLocked();
        graphDevice(m_device)->SetTexture(0, nullptr);
        (void)SetRenderState(0x1Du, 0u);
        (void)SetAlphaBlend(9u, 2u);
        (void)SetRenderState(0x0Eu, 0u);
        (void)drawPrimitiveUp(6u, 0xC4u, vertices, 24u, 4);
        (void)SetRenderState(0x0Eu, 1u);
    }


    void GRAPH::bar(float x, float y, float x1, float y1, DWORD color)
    {
        const DWORD left = floatBits(x);
        const DWORD top = floatBits(y);
        const DWORD right = floatBits(x1);
        const DWORD bottom = floatBits(y1);
        const GraphOverlayVertex vertices[4] = {
            {left,  top,    0x3F7FFFFEu, 0x3F800000u, color, 0xFFFFFFFFu},
            {right, top,    0x3F7FFFFEu, 0x3F800000u, color, 0xFFFFFFFFu},
            {right, bottom, 0x3F7FFFFEu, 0x3F800000u, color, 0xFFFFFFFFu},
            {left,  bottom, 0x3F7FFFFEu, 0x3F800000u, color, 0xFFFFFFFFu},
        };

        unlockBackBufferIfLocked();
        graphDevice(m_device)->SetTexture(0, nullptr);
        if ((color & 0xFF000000u) == 0xFF000000u)
            (void)SetRenderState(0x1Bu, 0u);
        else
            (void)SetAlphaBlend(5u, 6u);
        (void)SetRenderState(0x0Eu, 0u);
        (void)drawPrimitiveUp(6u, 0xC4u, vertices, 24u, 4);
        (void)SetRenderState(0x0Eu, 1u);
    }


    void DrawDebugText(GRAPH* graph, const char* text)
    {
        graph->m_loadingPresentationText.Assign(text);

        (void)graph->preTact();
        const float top = graph->ViewYMin();
        graph->bar(200.0f, top + 5.0f, 800.0f, top + 30.0f, 0xFF000000u);
        (void)graph->drawStringColored(
            200.0f,
            graph->ViewYMin() + 5.0f,
            graph->m_loadingPresentationText,
            g_colorGreen.color);
        (void)graph->PostTact(1);
    }



    void MOVIE::Release() noexcept
    {
        ::ReleaseCapture();
        void** const slots[4] = { &pVidWin, &pMediaControl, &pEvent, &pGraph };
        for (void** slot : slots)
        {
            void*& object = *slot;
            if (object)
                static_cast<IUnknown*>(object)->Release();
            object = nullptr;
        }
    }


    void GRAPH::SetEnvironment(DWORD env)
    {
        if ((env & 0x80000000u) != 0u)
        {
            m_renderFlags &= ~env;
            return;
        }

        if (env == 1u || env == 2u)
            m_renderFlags &= 0xFFFFFFFCu;
        if ((env & 0x00000C00u) != 0u)
            m_renderFlags &= 0xFFFFF3FFu;
        if ((env & 0x0000C000u) != 0u)
            m_renderFlags &= 0xFFFF3FFFu;
        m_renderFlags |= env;
    }

    void GRAPH::deinit()
    {

        m_movie.Release();

        if (m_lockedBackBufferPixels)
        {
            graphSurface(m_backBuffer)->UnlockRect();
            m_lockedBackBufferPixels = nullptr;
        }

        if (m_device)
            graphDevice(m_device)->SetStreamSource(0, nullptr, 0u);

        destroyCD3DFont(m_textFont);

        if (m_softwareDepthBuffer)
        {
            ::operator delete(m_softwareDepthBuffer);
            m_softwareDepthBuffer = nullptr;
        }

        if (m_tempBuffer)
        {
            graphSurface(m_tempBuffer)->Release();
            m_tempBuffer = nullptr;
        }

        delete m_alphaBuffer;
        m_alphaBuffer = nullptr;
        delete m_lightBuffer;
        m_lightBuffer = nullptr;
        delete m_hiBuffer;
        m_hiBuffer = nullptr;

        if (m_device)
        {
            const ULONG result = graphDevice(m_device)->Release();
            m_device = nullptr;
            writeLogLine(g_fileLogger, "d3dDevice release %i", static_cast<int>(result));
        }
        if (m_backBuffer)
        {
            const ULONG result = graphSurface(m_backBuffer)->Release();
            m_backBuffer = nullptr;
            rewriteLogLine(g_fileLogger, "backBuffer release %i", static_cast<int>(result));
        }
        if (m_direct3D)
        {
            const ULONG result = graphD3D(m_direct3D)->Release();
            m_direct3D = nullptr;
            writeLogLine(g_fileLogger, "d3d release %i", static_cast<int>(result));
        }

        m_movie.Release();
    }


    void GRAPH::PrintfXY(float x, float y, const char* format, ...)
    {
        char text[1024];
        va_list args;
        va_start(args, format);
        std::vsprintf(text, format, args);
        va_end(args);

        (void)drawTextColored(x, y, text, g_colorGreen.color);
    }




    int GRAPH::drawTextColored(float x, float y, const char* text, DWORD color)
    {

        if (!m_textFont)
            return 0;
        unlockBackBufferIfLocked();
        return m_textFont->DrawText(x, y, color, text, 0u);
    }


    int GRAPH::drawStringColored(float x, float y, const STRING& text, DWORD color)
    {

        return drawTextColored(x, y, text.c_str(), color);
    }

    void GRAPH::drawBackBufferPixel(float x, float y, DWORD color)
    {

        if (!(x >= m_viewportLeft) || x >= m_viewportRight ||
            !(y >= m_viewportTop) || y >= m_viewportBottom)
            return;

        (void)Lock();
        const int iy = graphConvertFloatToInt32(y);
        const int ix = graphConvertFloatToInt32(x);
        const std::uint32_t indexBits = static_cast<std::uint32_t>(ix) +
            static_cast<std::uint32_t>(iy) * static_cast<std::uint32_t>(m_backBufferPitchPixels);
        const std::ptrdiff_t index = static_cast<std::int32_t>(indexBits);

        if ((m_graphFlags & 2u) != 0u)
        {
            static_cast<DWORD*>(m_lockedBackBufferPixels)[index] = color;
            return;
        }

        const DWORD greenShift = (8u - g_color16GreenShift) & 31u;
        const DWORD redShift = (16u - g_color16RedShift) & 31u;
        const WORD packed = static_cast<WORD>(
            ((color >> greenShift) & g_color16GreenMask) |
            ((color >> redShift) & g_color16RedMask) |
            ((color >> 3u) & 0x1Fu));
        static_cast<WORD*>(m_lockedBackBufferPixels)[index] = packed;
    }


    void GRAPH::Line(float x0, float y0, float x1, float y1, DWORD color)
    {

        constexpr double kCoordinateLimit = 10000.0;
        if (std::fabs(static_cast<double>(x0)) > kCoordinateLimit)
        {
            x0 = 0.0f;
            (void)logFileLoggerResourceError(g_fileLogger, "GRAPH", 4, "x in Line", 0);
        }
        if (std::fabs(static_cast<double>(x1)) > kCoordinateLimit)
        {
            x1 = 0.0f;
            (void)logFileLoggerResourceError(g_fileLogger, "GRAPH", 4, "x1 in Line", 0);
        }
        if (std::fabs(static_cast<double>(y0)) > kCoordinateLimit)
        {
            y0 = 0.0f;
            (void)logFileLoggerResourceError(g_fileLogger, "GRAPH", 4, "y in Line", 0);
        }
        if (std::fabs(static_cast<double>(y1)) > kCoordinateLimit)
        {
            y1 = 0.0f;
            (void)logFileLoggerResourceError(g_fileLogger, "GRAPH", 4, "y1 in Line", 0);
        }

        int major = graphConvertFloatToInt32(x0);
        int minor = graphConvertFloatToInt32(y0);
        int majorStep = (x1 > x0) ? 1 : -1;
        int minorStep = (y1 > y0) ? 1 : -1;

        int dx = std::abs(graphConvertFloatToInt32(x1 - x0));
        int dy = std::abs(graphConvertFloatToInt32(y1 - y0));
        bool axesSwapped = false;
        if (dy > dx)
        {
            std::swap(major, minor);
            std::swap(dx, dy);
            std::swap(majorStep, minorStep);
            axesSwapped = true;
        }

        int error = dy * 2 - dx;
        const int errorAdvance = dy * 2;
        int remaining = dx;
        while (remaining != 0)
        {
            if (axesSwapped)
                drawBackBufferPixel(static_cast<float>(minor), static_cast<float>(major), color);
            else
                drawBackBufferPixel(static_cast<float>(major), static_cast<float>(minor), color);

            if (error >= 0)
            {
                const int subtract = dx * 2;
                do
                {
                    minor += minorStep;
                    error -= subtract;
                }
                while (error >= 0);
            }

            major += majorStep;
            error += errorAdvance;
            --remaining;
        }

        drawBackBufferPixel(x1, y1, color);
    }


    void GRAPH::Box(float left, float top, float right, float bottom, DWORD color)
    {

        Line(left, top, right, top, color);
        Line(left, bottom, right, bottom, color);
        Line(left, top, left, bottom, color);
        Line(right, top, right, bottom, color);
    }


    float GRAPH::ViewXMin() noexcept
    {
        return m_viewportLeft;
    }


    float GRAPH::ViewXMax() noexcept
    {
        return m_viewportRight;
    }


    float GRAPH::ViewYMin() noexcept
    {
        return m_viewportTop;
    }


    float GRAPH::ViewYMax() noexcept
    {
        return m_viewportBottom;
    }


    int GRAPH::setViewport(float left, float top, float right, float bottom)
    {

        m_viewportLeft = left;
        m_viewportRight = right;
        m_viewportTop = top;
        m_viewportBottom = bottom;
        g_softwareClipLeft = graphConvertFloatToInt32(left);
        g_softwareClipRight = graphConvertFloatToInt32(right);
        g_softwareClipTop = graphConvertFloatToInt32(top);
        g_softwareClipBottom = graphConvertFloatToInt32(bottom);

        int result = g_softwareClipBottom;
        IDirect3DDevice8* const device = graphDevice(m_device);
        if (!device)
            return result;

        D3DVIEWPORT8 viewport{};
        viewport.X = static_cast<DWORD>(graphConvertFloatToInt32(left));
        viewport.Y = static_cast<DWORD>(graphConvertFloatToInt32(top));
        viewport.Width = static_cast<DWORD>(graphConvertFloatToInt32(right - left));
        viewport.Height = static_cast<DWORD>(graphConvertFloatToInt32(bottom - top));
        viewport.MinZ = 0.0f;
        viewport.MaxZ = 1.0f;

        result = static_cast<int>(device->SetViewport(&viewport));
        if (result != D3D_OK)
            (void)logFileLoggerResourceError(g_fileLogger, "GRAPH", 8, "viewport", result);

        D3DMATRIX matrix{};
        matrix._11 = 2.0f / static_cast<float>(static_cast<std::int32_t>(viewport.Width));
        matrix._22 = -2.0f / static_cast<float>(static_cast<std::int32_t>(viewport.Height));
        matrix._33 = (1.0f / (viewport.MaxZ - viewport.MinZ)) * 0.0010000000474974513f;
        matrix._44 = 1.0f;

        result = static_cast<int>(device->SetTransform(D3DTS_PROJECTION, &matrix));
        if (result < 0)
            result = static_cast<int>(logFileLoggerResourceError(g_fileLogger, "GRAPH", 8, "Transform projection", result));
        return result;
    }


    int GRAPH::drawTextureRectClipped(const RECTI& destination, const RECTI& source, BASE_TEXTURE& texture)
    {


        RECTI dst = destination;
        RECTI src = source;
        if (floatLessOrUnordered(static_cast<float>(dst.right), m_viewportLeft) ||
            static_cast<float>(dst.left) >= m_viewportRight ||
            floatLessOrUnordered(static_cast<float>(dst.bottom), m_viewportTop) ||
            static_cast<float>(dst.top) >= m_viewportBottom)
        {
            return 0;
        }

        if (floatLessOrUnordered(static_cast<float>(dst.left), m_viewportLeft))
        {
            const int clippedLeft = graphConvertFloatToInt32(m_viewportLeft);
            src.left += clippedLeft - dst.left;
            dst.left = clippedLeft;
        }
        if (floatLessOrUnordered(static_cast<float>(dst.top), m_viewportTop))
        {
            const int clippedTop = graphConvertFloatToInt32(m_viewportTop);
            src.top += clippedTop - dst.top;
            dst.top = clippedTop;
        }
        if (static_cast<float>(dst.right) > m_viewportRight)
        {
            const int clippedRight = graphConvertFloatToInt32(m_viewportRight);
            src.right += clippedRight - dst.right;
            dst.right = clippedRight;
        }
        if (static_cast<float>(dst.bottom) > m_viewportBottom)
        {
            const int clippedBottom = graphConvertFloatToInt32(m_viewportBottom);
            src.bottom += clippedBottom - dst.bottom;
            dst.bottom = clippedBottom;
        }

        int sourcePitchBytes = 0;
        const std::uint16_t* sourcePixels = texture.lock16(&sourcePitchBytes, &src);

        const int width = dst.right - dst.left;
        const int height = dst.bottom - dst.top;
        const int sourcePitchWords = sourcePitchBytes / 2;
        const std::ptrdiff_t destinationOffset =
            static_cast<std::ptrdiff_t>(dst.left) +
            static_cast<std::ptrdiff_t>(dst.top) * static_cast<std::ptrdiff_t>(m_softwareDepthPitch);
        std::uint16_t* destinationRow = m_softwareDepthBuffer + destinationOffset;
        const std::uint16_t* sourceRow = sourcePixels;

        for (int row = 0; row < height; ++row)
        {
            for (int column = 0; column < width; ++column)
                destinationRow[column] = sourceRow[column];
            sourceRow += sourcePitchWords;
            destinationRow += m_softwareDepthPitch;
        }
        return 0;
    }


    int GRAPH::drawPrimitiveUp(DWORD primitiveType, DWORD vertexShader, const void* vertexData, DWORD vertexStride, int vertexCount)
    {


        int primitiveCount = vertexCount;
        switch (primitiveType)
        {
        case 5:
        case 6:
            primitiveCount = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(vertexCount) - 2u);
            break;
        case 2:
            primitiveCount = vertexCount / 2;
            break;
        case 4:
            primitiveCount = vertexCount / 3;
            break;
        default:
            break;
        }

        IDirect3DDevice8* const device = graphDevice(m_device);
        device->SetVertexShader(vertexShader);
        const HRESULT result = graphDevice(m_device)->DrawPrimitiveUP(
            static_cast<D3DPRIMITIVETYPE>(primitiveType),
            static_cast<UINT>(primitiveCount),
            vertexData,
            static_cast<UINT>(vertexStride));
        if (result != D3D_OK && g_fileLogger)
            return static_cast<int>(logFileLoggerResourceError(
                g_fileLogger, "GRAPH", 10, "DrawPrimitiveUP", static_cast<int>(result)));
        return static_cast<int>(result);
    }

    int GRAPH::initializeWindowDevice(void* hWnd)
    {


        core::RealCurrentTime = ::timeGetTime();


        if (core::g_startupRegistryPathOwner)
        {
            const STRING* const registryPath = core::g_startupRegistryPathOwner->Path();
            registryPath->WriteRegistryInt(STRING("ScreenX"), graphConvertFloatToInt32(m_sizeX));
            registryPath->WriteRegistryInt(STRING("ScreenY"), graphConvertFloatToInt32(m_sizeY));
            registryPath->WriteRegistryInt(STRING("BPP"), (m_graphFlags & 0x2u) != 0u ? 32 : 16);
            registryPath->WriteRegistryInt(STRING("Device"), m_selectedAdapterIndex);
            registryPath->WriteRegistryInt(STRING("FullScreen"), fullscreenRequested() ? 1 : 0);
        }


        const DD_DRIVER& startupCatalog = selectedAdapterRecord();


        int lowDetail = 0;
        int tripleBufferOption = 1;
        if (core::g_startupRegistryPathOwner)
        {
            const STRING* const registryPath = core::g_startupRegistryPathOwner->Path();
            lowDetail = registryPath->ReadRegistryInt(STRING("LowDetail"), 0);
            tripleBufferOption = registryPath->ReadRegistryInt(STRING("TripleBuffer"), 1);
        }

        DWORD flags = m_graphFlags;
        flags = (flags & ~0x00000200u) |
                ((lowDetail & 1) != 0 ? 0x00000200u : 0u);

        flags &= ~0x00000020u;
        flags = (flags & ~0x00000010u) |
                ((startupCatalog.capabilityFlags & 0x2u) != 0u ? 0x00000010u : 0u);
        if ((startupCatalog.capabilityFlags & 0x2u) != 0u)
            writeLogLine(g_fileLogger, "zm_nongdi");
        flags &= ~0x00000010u;

        const bool transientFullscreenBuffer =
            tripleBufferOption != 0 &&
            (flags & 0x00000080u) != 0u &&
            (flags & 0x00000400u) == 0u;
        flags = (flags & ~0x00000040u) |
                (transientFullscreenBuffer ? 0x00000040u : 0u);

        if ((startupCatalog.capabilityFlags & 0x1u) == 0u)
            flags |= 0x00000080u;
        m_graphFlags = flags;

        if (!fullscreenRequested())
        {
            const int desktopWidth = ::GetSystemMetrics(SM_CXSCREEN);
            const int desktopHeight = ::GetSystemMetrics(SM_CYSCREEN);
            if (static_cast<float>(desktopWidth) < m_sizeX)
                m_sizeX = static_cast<float>(desktopWidth);
            if (static_cast<float>(desktopHeight) < m_sizeY)
                m_sizeY = static_cast<float>(desktopHeight);

            const bool desktop32 = displayFormatBits(startupCatalog.desktopDisplayFormat) == 32;
            if (((m_graphFlags >> 1u) & 1u) != static_cast<DWORD>(desktop32))
                m_graphFlags = (m_graphFlags & ~0x2u) | (desktop32 ? 0x2u : 0u);
        }


        const int halfVideoBudget = static_cast<int>(startupCatalog.videoMemoryBudgetBytes) / 2;
        const float tripleBufferFootprint = m_sizeY * m_sizeX * 2.0f * 3.0f;
        if (!(tripleBufferFootprint < static_cast<float>(halfVideoBudget)))
            m_graphFlags &= ~0x00000040u;


        m_graphFlags &= ~0x00000040u;

        RECT windowRect{};
        RECT clientRect{};
        m_windowHandle = hWnd;
        HWND const window = static_cast<HWND>(m_windowHandle);
        GetWindowRect(window, &windowRect);
        GetClientRect(window, &clientRect);

        POINT clientTopLeft{clientRect.left, clientRect.top};
        POINT clientBottomRight{clientRect.right, clientRect.bottom};
        ClientToScreen(window, &clientTopLeft);
        ClientToScreen(window, &clientBottomRight);
        m_deviceLifecycleState = 0x17u;

        if (init(hWnd) != 0)
            return 1;

        if (fullscreenRequested() && (m_graphFlags & 0x00000400u) == 0u)
        {
            (void)setViewport(
                0.0f,
                0.0f,
                static_cast<float>(m_sizeX),
                static_cast<float>(m_sizeY));
        }
        else
        {
            (void)setViewport(
                static_cast<float>(clientTopLeft.x - windowRect.left),
                static_cast<float>(clientTopLeft.y - windowRect.top),
                static_cast<float>(clientBottomRight.x - windowRect.left),
                static_cast<float>(clientBottomRight.y - windowRect.top));
        }

        writeLogLine(g_fileLogger, 
            "SetViewPort (%.0f,%.0f) - (%.0f,%.0f)",
            m_viewportLeft,
            m_viewportTop,
            m_viewportRight,
            m_viewportBottom);

        writeLogLine(g_fileLogger, "%s", buildTextureStageDebugText());
        (void)graphDevice(m_device)->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(2u), 2u);
        (void)graphDevice(m_device)->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(3u), 0u);
        (void)graphDevice(m_device)->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(1u), 4u);
        (void)graphDevice(m_device)->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(5u), 2u);
        (void)graphDevice(m_device)->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(6u), 0u);
        (void)graphDevice(m_device)->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(4u), 4u);
        (void)graphDevice(m_device)->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(17u), 1u);
        (void)graphDevice(m_device)->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(16u), 1u);


        (void)graphDevice(m_device)->SetRenderState(static_cast<D3DRENDERSTATETYPE>(29u), 0u);
        m_graphFlags &= ~0x00002000u;
        (void)graphDevice(m_device)->SetRenderState(static_cast<D3DRENDERSTATETYPE>(27u), 0u);
        m_graphFlags &= ~0x00004000u;
        (void)SetRenderState(26u, 1u);
        (void)SetRenderState(142u, 0u);
        (void)SetRenderState(137u, 0u);
        (void)SetRenderState(15u, 0u);
        (void)graphDevice(m_device)->SetRenderState(static_cast<D3DRENDERSTATETYPE>(23u), 8u);
        (void)graphDevice(m_device)->SetRenderState(static_cast<D3DRENDERSTATETYPE>(14u), 0u);
        (void)SetRenderState(7u, 1u);
        (void)SetRenderState(23u, 7u);

        writeLogLine(g_fileLogger, "%s", buildTextureStageDebugText());
        BASE_TEXTURE::ConfigureCaps(graphDevice(m_device));

        m_lightBuffer = new (std::nothrow) BASE_TEXTURE(0x100, 0x100, 0x29u, 0u);
        if (!m_lightBuffer->isLoaded())
        {
            logFileLoggerResourceError(g_fileLogger, "%s", 3, "light buffer", 0, "GRAPH");
            return 1;
        }
        if (m_lightBuffer->format() == 0x29u)
        {
            std::array<DWORD, 256> palette{};
            for (DWORD value = 0; value < 256u; ++value)
                palette[value] = 0xFF000000u | value | (value << 8u) | (value << 16u);
            m_lightBuffer->createPaletteSlot(palette.data());
        }

        m_hiBuffer = new (std::nothrow) BASE_TEXTURE(0x100, 0x100, 0x17u, 0u);
        if (!m_hiBuffer->isLoaded())
        {
            logFileLoggerResourceError(g_fileLogger, "%s", 3, "hiBuffer", 0, "GRAPH");
            return 1;
        }

        m_alphaBuffer = new (std::nothrow) BASE_TEXTURE(0x100, 0x100, 0x1Au, 0u);
        if (!m_alphaBuffer->isLoaded())
        {
            logFileLoggerResourceError(g_fileLogger, "%s", 3, "alphaBuffer", 0, "GRAPH");
            return 1;
        }

        clearFrameBuffers(0xFF000000u);
        rebuildTextFont(STRING("Courier"), 7, 8);

        if (m_hiBuffer->format() == 0x17u)
        {
            g_color16RedMask = 0xF800u;
            g_color16GreenMask = 0x07E0u;
            g_color16RedShift = 8u;
            g_color16GreenShift = 3u;
        }
        else
        {
            g_color16RedMask = 0x7C00u;
            g_color16GreenMask = 0x03E0u;
            g_color16RedShift = 7u;
            g_color16GreenShift = 2u;
        }
        buildGraphIntensityPalette(m_intensityPalette16, m_hiBuffer->format() == 0x17u);

        STRING caps;


        if ((m_graphFlags & 0x00000004u) != 0u)
            (caps).append("ALPHAPALETTE ");
        if ((m_graphFlags & 0x00000010u) != 0u)
            (caps).append("NONGDI ");
        if ((m_graphFlags & 0x00000020u) != 0u)
            (caps).append("SOFTWARE ");
        else
            (caps).append("HARDWARE ");
        if ((m_graphFlags & 0x00000200u) != 0u)
            (caps).append("LOWDETAIL ");
        if ((m_graphFlags & 0x00000008u) != 0u)
            (caps).append("AGP ");
        if (!fullscreenRequested())
            (caps).append("WINDOWED ");
        if ((m_graphFlags & 0x00000040u) != 0u)
            (caps).append("TRIPLEBUFFER ");
        else
            (caps).append("DOUBLEBUFFER ");
        if ((m_graphFlags & 0x00000800u) != 0u)
            (caps).append("DOTPRODUCT3 ");
        if ((m_graphFlags & 0x00001000u) == 0u)
            (caps).append("NOTMODULATE2X ");
        if ((m_graphFlags & 0x00002000u) == 0u)
            (caps).append("CAN'T_Z_BLT ");
        if ((m_graphFlags & 0x00000002u) != 0u)
            (caps).append("COLOR32 ");
        if ((m_graphFlags & 0x00000100u) != 0u)
            (caps).append("VSYNC ");
        STRING pixelShader;
        constructFormattedString(pixelShader, "PIXELSHADER=%i", static_cast<int>(m_pixelShaderVersion));
        (caps).append(pixelShader);
        writeLogLine(g_fileLogger, "caps=%s", caps.c_str());

        if (!m_lockedBackBufferPixels)
        {
            D3DLOCKED_RECT lockedRect;
            const HRESULT lockResult =
                graphSurface(m_backBuffer)->LockRect(&lockedRect, nullptr, 0u);
            if (lockResult < 0)
                logFileLoggerResourceError(g_fileLogger, "%s", 0, "backBuffer", 0, "GRAPH");

            m_lockedBackBufferPixels = lockedRect.pBits;
            const int bytesPerPixel = (m_graphFlags & 0x2u) != 0u ? 4 : 2;
            m_backBufferPitchPixels = lockedRect.Pitch / bytesPerPixel;
        }
        if (m_lockedBackBufferPixels)
        {
            graphSurface(m_backBuffer)->UnlockRect();
            m_lockedBackBufferPixels = nullptr;
        }

        const int bytesPerPixel = (m_graphFlags & 0x2u) != 0u ? 4 : 2;
        writeLogLine(g_fileLogger, 
            "Pitch=%i zPitch=%i",
            m_backBufferPitchPixels * bytesPerPixel,
            2 * m_softwareDepthPitch);
        reloadPaletteLightBuffer();
        return 0;
    }


    int GRAPH::init(void* hWnd)
    {
        (void)hWnd;
        std::memset(&m_d3d8PresentParameters, 0, sizeof(m_d3d8PresentParameters));
        D3DPRESENT_PARAMETERS8& pp = m_d3d8PresentParameters;
        const DD_DRIVER& adapterRecord = selectedAdapterRecord();

        pp.Windowed = fullscreenRequested() ? FALSE : TRUE;
        pp.BackBufferCount = 1u;
        pp.MultiSampleType = D3DMULTISAMPLE_NONE;
        pp.SwapEffect = (m_graphFlags & 0x00000100u) != 0u
            ? D3DSWAPEFFECT_COPY_VSYNC_D3D8
            : D3DSWAPEFFECT_COPY;
        pp.hDeviceWindow = static_cast<HWND>(m_windowHandle);
        pp.EnableAutoDepthStencil = FALSE;
        pp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;

        const int width = graphConvertFloatToInt32(m_sizeX);
        const int height = graphConvertFloatToInt32(m_sizeY);
        const int bitsPerPixel = (m_graphFlags & 0x00000002u) != 0u ? 32 : 16;

        if (fullscreenRequested())
        {
            const int modeIndex = adapterRecord.GetMode(width, height, bitsPerPixel);
            if (modeIndex < 0)
            {
                writeLogLine(g_fileLogger, "Selected display mode %.0fx%.0f", static_cast<double>(m_sizeX), static_cast<double>(m_sizeY));
                logFileLoggerResourceError(g_fileLogger, "%s", 10, "Unsupported resolution", 0, "GRAPH");
                return 1;
            }
            m_selectedDisplayFormat = adapterRecord.displayModeFormats[modeIndex];
        }
        else
        {
            m_selectedDisplayFormat = adapterRecord.desktopDisplayFormat;
        }


        pp.BackBufferWidth = static_cast<UINT>(width);
        pp.BackBufferHeight = static_cast<UINT>(height);
        pp.BackBufferFormat = static_cast<D3DFORMAT>(m_selectedDisplayFormat);

        const STRING selectedFormatName = D3DFormatToString(m_selectedDisplayFormat);
        const STRING desktopFormatName = D3DFormatToString(adapterRecord.desktopDisplayFormat);
        const STRING zBufferFormatName = D3DFormatToString(static_cast<DWORD>(pp.AutoDepthStencilFormat));
        writeLogLine(g_fileLogger, 
            "Selected display mode %.0fx%.0f %s desktop %s zbuffer %s",
            static_cast<double>(m_sizeX),
            static_cast<double>(m_sizeY),
            selectedFormatName.c_str(),
            desktopFormatName.c_str(),
            zBufferFormatName.c_str());

        IDirect3D8* const d3d = graphD3D(m_direct3D);
        if (!d3d)
            return 1;

        const D3DDEVTYPE deviceType = (m_graphFlags & 0x00000020u) != 0u
            ? D3DDEVTYPE_REF
            : D3DDEVTYPE_HAL;

        IDirect3DDevice8* device = nullptr;
        HRESULT hr = d3d->CreateDevice(
            static_cast<UINT>(m_selectedAdapterIndex),
            deviceType,
            static_cast<HWND>(m_windowHandle),
            D3DCREATE_HARDWARE_VERTEXPROCESSING,
            &pp,
            &device);
        if (hr != D3D_OK)
        {
            hr = d3d->CreateDevice(
                static_cast<UINT>(m_selectedAdapterIndex),
                deviceType,
                static_cast<HWND>(m_windowHandle),
                D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                &pp,
                &device);
        }
        if (hr != D3D_OK || !device)
        {
            logFileLoggerResourceError(g_fileLogger, "%s", 3, "3dDevice", static_cast<int>(hr), "GRAPH");
            return 1;
        }
        m_device = device;

        IDirect3DSurface8* backBuffer = nullptr;
        hr = device->GetBackBuffer(0u, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
        if (hr != D3D_OK || !backBuffer)
        {
            logFileLoggerResourceError(g_fileLogger, "%s", 9, "BackBuffer", static_cast<int>(hr), "GRAPH");
            return 1;
        }
        m_backBuffer = backBuffer;


        (void)device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        (void)device->SetRenderState(D3DRS_ZENABLE, FALSE);

        D3DCAPS8 caps{};
        hr = device->GetDeviceCaps(&caps);
        if (hr != D3D_OK)
        {
            logFileLoggerResourceError(g_fileLogger, "%s", 9, "Caps", static_cast<int>(hr), "GRAPH");
            return 1;
        }
        DWORD derivedCapabilityFlags = 0u;
        if ((caps.TextureOpCaps & 0x00800000u) != 0u)
            derivedCapabilityFlags |= 0x00000800u;
        if ((caps.DevCaps & 0x00001000u) != 0u)
            derivedCapabilityFlags |= 0x00000008u;
        if ((caps.TextureCaps & 0x00000080u) != 0u)
            derivedCapabilityFlags |= 0x00000004u;
        if ((caps.TextureOpCaps & 0x00000010u) != 0u)
            derivedCapabilityFlags |= 0x00001000u;
        m_graphFlags = (m_graphFlags & 0xFFFFE7F3u) | derivedCapabilityFlags;
        m_pixelShaderVersion = caps.PixelShaderVersion;


        IDirect3DSurface8* stagingSurface = nullptr;
        hr = device->CreateImageSurface(
            pp.BackBufferWidth, pp.BackBufferHeight, pp.BackBufferFormat, &stagingSurface);
        if (hr != D3D_OK || !stagingSurface)
        {
            logFileLoggerResourceError(g_fileLogger, "%s", 3, "tempBuffer", static_cast<int>(hr), "GRAPH");
            return 1;
        }
        m_tempBuffer = stagingSurface;

        const std::size_t depthWords =
            static_cast<std::size_t>(pp.BackBufferWidth) * static_cast<std::size_t>(pp.BackBufferHeight);
        m_softwareDepthBuffer = static_cast<std::uint16_t*>(
            ::operator new(depthWords * sizeof(std::uint16_t), std::nothrow));
        if (depthWords != 0u && !m_softwareDepthBuffer)
            return 1;
        m_softwareDepthPitch = width;

        m_viewportLeft = 0.0f;
        m_viewportTop = 0.0f;
        m_viewportRight = m_sizeX;
        m_viewportBottom = m_sizeY;

        return 0;
    }


    


    ANGLE GRAPH::WindDirection() const noexcept
    {
        return ANGLE(static_cast<unsigned char>(m_windDirection));
    }


    float GRAPH::SizeX() const
    {
        return m_sizeX;
    }


    float GRAPH::SizeY() const
    {
        return m_sizeY;
    }


    void GRAPH::SetWind(int speed, ANGLE direction)
    {
        m_windSpeed = static_cast<float>(speed) * 0.001f;
        m_windDirection = direction.value;
    }


    int GRAPH::Effect(int effect, int argument1, int argument2, int duration)
    {

        int result = effect;
        if (effect < 0 || effect >= 16)
            return result;

        if (effect == 3 || effect == 9 || effect == 10)
        {
            m_effectStartTimes[10] = 0;
            m_effectStartTimes[9] = 0;
            m_effectStartTimes[3] = 0;
        }

        std::uint32_t effectDuration = static_cast<std::uint32_t>(duration);
        if (effectDuration == 0u)
        {
            effectDuration = 0x400u;
            switch (effect)
            {
            case 2: effectDuration = 0x200u; break;
            case 1:
            case 3: effectDuration = 0x900u; break;
            case 9: effectDuration = 0x500u; break;
            default: break;
            }
        }

        const std::size_t index = static_cast<std::size_t>(effect);


        m_effectStartTimes[index] = core::RealCurrentTime;
        m_effectDurations[index] = effectDuration;
        m_effectArgument1[index] = static_cast<std::uint32_t>(argument1);
        m_effectArgument2[index] = static_cast<std::uint32_t>(argument2);

        if (effect == 0)
        {
            std::memset(m_effectStartTimes, 0, sizeof(m_effectStartTimes));
            return 0;
        }

        if (effect == 5)
        {
            if (m_tempBuffer)
            {
                m_effectStartTimes[10] = 0;
                m_effectStartTimes[9] = 0;
                m_effectStartTimes[3] = 0;
                const HRESULT copyResult = graphDevice(m_device)->CopyRects(
                    graphSurface(m_backBuffer), nullptr, 0u,
                    static_cast<IDirect3DSurface8*>(m_tempBuffer), nullptr);
                result = static_cast<int>(copyResult);
                if (copyResult != D3D_OK && g_fileLogger)
                {
                    result = static_cast<int>(logFileLoggerResourceError(
                        g_fileLogger, "GRAPH", 1, "for EFF_ALPHAAPPEAR",
                        static_cast<int>(copyResult)));
                }
            }
            return result;
        }

        if (effect == 2)
        {
            const float cameraY = core::GlobalApplicationDrawDispatcherState().cameraShiftY();
            std::uint32_t cameraYBits = 0u;
            std::memcpy(&cameraYBits, &cameraY, sizeof(cameraYBits));
            m_effectSnapshotXBits = cameraYBits;
            const float currentCameraY = core::GlobalApplicationDrawDispatcherState().cameraShiftY();
            std::memcpy(&cameraYBits, &currentCameraY, sizeof(cameraYBits));
            m_effectSnapshotYBits = cameraYBits;
            return static_cast<int>(cameraYBits);
        }

        if (effect == 11)
        {
            m_effectGammaPair.first = m_gammaPair.first;
            m_effectGammaPair.second = m_gammaPair.second;
            return static_cast<std::int32_t>(m_gammaPair.second);
        }

        return result;
    }


    void GRAPH::DrawEffect(int drawEffects)
    {

        std::uint32_t now = core::RealCurrentTime;

        const std::uint32_t effect5Start = m_effectStartTimes[5];
        if (effect5Start != 0u)
        {
            const std::uint32_t elapsed = now - effect5Start;
            const std::uint32_t duration = m_effectDurations[5];
            if (elapsed >= duration)
            {
                m_effectStartTimes[5] = 0u;
            }
            else
            {
                SetAlphaBlend(6u, 5u);
                std::uint32_t tileNow = now;
                const float left = m_viewportLeft;
                const float top = m_viewportTop;
                const float right = m_viewportRight;
                const float bottom = m_viewportBottom;
                for (float tileY = top; tileY < bottom; tileY += 256.0f)
                {
                    for (float tileX = left; tileX < right; tileX += 256.0f)
                    {
                        const float tileWidth = (tileX + 256.0f < right) ? 256.0f : (right - tileX);
                        const float tileHeight = (tileY + 256.0f < bottom) ? 256.0f : (bottom - tileY);
                        const RECTI destination{
                            graphConvertFloatToInt32(tileX - left),
                            graphConvertFloatToInt32(tileY - top),
                            graphConvertFloatToInt32(tileX + tileWidth - left),
                            graphConvertFloatToInt32(tileY + tileHeight - top)};
                        const RECTI copyOrigin{0, 0, 0, 0};
                        const RECTI source{0, 0,
                            graphConvertFloatToInt32(tileWidth),
                            graphConvertFloatToInt32(tileHeight)};
                        if (drawEffects != 0)
                        {
                            int copyResult = -1;
                            copyResult = m_hiBuffer->PrepareSurfaceCopy(
                                static_cast<IDirect3DSurface8*>(m_tempBuffer), destination, &copyOrigin);
                            if (copyResult == 0)
                            {
                                const DWORD tileElapsed = tileNow - effect5Start;
                                const DWORD alpha = (tileElapsed << 8u) / duration;


                                const Color diffuse(static_cast<int>(alpha), 255, 255, 255);
                                const Color specular(0, 0, 0);
                                const Gamma colors(diffuse, specular);
                                m_hiBuffer->DrawFixedDepthRectangle(destination, source, &colors.first);
                            }

                            tileNow = core::RealCurrentTime;
                        }
                    }
                }
            }
            now = core::RealCurrentTime;
        }


        const std::uint32_t effect2Start = m_effectStartTimes[2];
        if (effect2Start != 0u)
        {
            const std::uint32_t elapsed = now - effect2Start;
            const std::uint32_t duration = m_effectDurations[2];
            MAP* const map = Map;
            const float startX = floatFromBits(m_effectSnapshotXBits);
            const float startY = floatFromBits(m_effectSnapshotYBits);
            const float targetX = static_cast<float>(static_cast<std::int32_t>(m_effectArgument1[2]));
            const float targetY = static_cast<float>(static_cast<std::int32_t>(m_effectArgument2[2]));
            if (elapsed > duration)
            {
                map->SetShiftCoor(targetX, targetY, 0);
                m_effectStartTimes[2] = 0u;
            }
            else
            {

                const float t =
                    static_cast<float>(static_cast<std::int32_t>(elapsed)) /
                    static_cast<float>(static_cast<std::int32_t>(duration));
                map->SetShiftCoor((targetX - startX) * t + startX,
                                  (targetY - startY) * t + startY,
                                  0);
            }
            now = core::RealCurrentTime;
        }

        const std::uint32_t effect1Start = m_effectStartTimes[1];
        if (effect1Start != 0u)
        {
            const std::uint32_t duration = m_effectDurations[1];
            const std::uint32_t elapsed = now - effect1Start;
            if (elapsed >= duration)
            {
                m_effectStartTimes[1] = 0u;
            }
            else if (drawEffects != 0)
            {
                const std::uint32_t ninth = duration / 9u;
                DWORD numerator = 0u;
                DWORD denominator = duration;
                if (elapsed >= ninth)
                {
                    numerator = (duration - elapsed) << 8u;
                    denominator = duration - ninth;
                }
                else
                {
                    numerator = (elapsed * 9u) << 8u;
                }
                const DWORD scale = numerator / denominator;
                const DWORD color = graphScaleRgb(m_effectArgument1[1], scale);
                LightBar(0.0f, 0.0f, m_sizeX, m_sizeY, color);
                LightBar(0.0f, 0.0f, m_sizeX, m_sizeY, color);
                LightBar(0.0f, 0.0f, m_sizeX, m_sizeY, color);
            }
            now = core::RealCurrentTime;
        }


        const std::uint32_t effect3Start = m_effectStartTimes[3];
        if (effect3Start != 0u)
        {
            const std::uint32_t duration = m_effectDurations[3];
            const std::uint32_t elapsed = now - effect3Start;
            if (elapsed >= duration)
            {
                m_effectStartTimes[3] = 0u;
            }
            else if (drawEffects != 0)
            {


                const std::uint32_t denominator = 4u * duration;
                const std::uint32_t riseEnd = denominator / 9u;
                const std::uint32_t plateauEnd = (5u * duration) / 9u;
                DWORD level = 255u;
                if (elapsed < riseEnd)
                {
                    const std::uint32_t startScaled = (effect3Start * 9u) << 8u;
                    const std::uint32_t nowScaled = (now * 9u) << 8u;

                    level = (nowScaled - startScaled) / denominator;
                }
                else if (elapsed > plateauEnd)
                {
                    const std::uint32_t endScaled = ((effect3Start + duration) * 9u) << 8u;
                    const std::uint32_t nowScaled = (now * 9u) << 8u;
                    level = (endScaled - nowScaled) / denominator;
                }
                const DWORD color = graphGrayRgb(level);
                ShadowBar(0.0f, 0.0f, m_sizeX, m_sizeY, static_cast<int>(color));
            }
            now = core::RealCurrentTime;
        }


        const std::uint32_t effect9Start = m_effectStartTimes[9];
        if (effect9Start != 0u)
        {
            const std::uint32_t duration = m_effectDurations[9];
            const std::uint32_t elapsed = now - effect9Start;
            if (elapsed >= duration)
            {
                m_effectStartTimes[9] = 0u;
            }
            else if (drawEffects != 0)
            {
                const DWORD level = elapsed >= (4u * duration) / 5u
                    ? 255u
                    : (1280u * elapsed) / (4u * duration);
                const DWORD color = graphGrayRgb(level);
                ShadowBar(0.0f, 0.0f, m_sizeX, m_sizeY, static_cast<int>(color));
            }

            now = core::RealCurrentTime;
        }


        const std::uint32_t effect10Start = m_effectStartTimes[10];
        if (effect10Start != 0u)
        {
            const std::uint32_t duration = m_effectDurations[10];
            const std::uint32_t elapsed = now - effect10Start;
            if (elapsed >= duration)
            {
                m_effectStartTimes[10] = 0u;
            }
            else if (drawEffects != 0)
            {
                const DWORD level = ((duration - elapsed) << 8u) / duration;
                const DWORD color = graphGrayRgb(level);
                ShadowBar(0.0f, 0.0f, m_sizeX, m_sizeY, static_cast<int>(color));
            }
        }

        now = core::RealCurrentTime;
        const std::uint32_t effect11Start = m_effectStartTimes[11];
        if (effect11Start != 0u)
        {
            const std::uint32_t duration = m_effectDurations[11];
            const std::uint32_t elapsed = now - effect11Start;
            const Gamma targetGamma(Gamma::DECODE, m_effectArgument1[11]);

            if (elapsed >= duration)
            {
                setGamma(targetGamma);
                m_effectStartTimes[11] = 0u;
                return;
            }

            if (drawEffects != 0)
            {
                const float t =
                    static_cast<float>(elapsed) / static_cast<float>(duration);

                const auto signedGammaLane = [](const Gamma& value, unsigned int shift) noexcept -> int
                {
                    const int negative = static_cast<int>((value.first >> shift) & 0xFFu);
                    if (negative != 0)
                        return -negative;
                    return static_cast<int>((value.second >> shift) & 0xFFu);
                };
                const auto interpolatedLane = [&](unsigned int shift) noexcept -> int
                {
                    const int fromValue = signedGammaLane(m_effectGammaPair, shift);
                    const int toValue = signedGammaLane(targetGamma, shift);
                    const float value =
                        static_cast<float>(toValue - fromValue) * t + static_cast<float>(fromValue);
                    return static_cast<int>(value);
                };
                const Gamma interpolated(
                    interpolatedLane(24u),
                    interpolatedLane(16u),
                    interpolatedLane(8u),
                    interpolatedLane(0u));
                setGamma(interpolated);
            }
        }
    }


    int GRAPH::GetEffectState(int effect) const
    {

        if (effect < 1 || effect > 15)
            return -1;

        const std::size_t index = static_cast<std::size_t>(effect);
        const std::uint32_t start = m_effectStartTimes[index];
        if (start == 0u)
            return -1;

        const std::uint32_t now = core::RealCurrentTime;
        const std::uint32_t elapsedTimes100 = (now - start) * 100u;
        return static_cast<int>(elapsedTimes100 / m_effectDurations[index]);
    }


    void GRAPH::setGamma(const Gamma& gamma)
    {

        if (m_gammaPair.first == gamma.first &&
            m_gammaPair.second == gamma.second)
            return;

        m_gammaPair = gamma;

        auto& appVidTable = core::GlobalApplicationVidTable();
        const int vidCount = appVidTable.count();
        for (int slot = 0; slot < static_cast<int>(core::ApplicationVidTable::kCapacity); ++slot)
        {

            VID* const vid = (slot < vidCount) ? appVidTable.slot(slot) : nullptr;


            if (!vid || vid == EmptyVid)
                continue;

            vid->SetGamma(gamma, 4);
        }
    }

    void GRAPH::DrawLightSource(float x, float y, float z, float sizeXValue, float sizeYValue, DWORD color)
    {


        g_lightBufferToggle ^= 1u;

        if ((color & 0x00FFFFFFu) == 0u)
            return;

        const int sizeX = graphConvertFloatToInt32(sizeXValue);
        const int sizeY = graphConvertFloatToInt32(sizeYValue);
        int halfExtentX = static_cast<std::int32_t>(static_cast<std::uint32_t>(sizeX / 2) * 3u) & ~3;
        int halfExtentY = static_cast<std::int32_t>(static_cast<std::uint32_t>(sizeY / 2) * 3u) & ~3;
        if (halfExtentX > 512)
            halfExtentX = 512;
        if (halfExtentY > 512)
            halfExtentY = 512;

        const int depthScaleDivisor = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(sizeX) * static_cast<std::uint32_t>(sizeY)) / 500;
        const int sourceZ = graphConvertFloatToInt32(z);
        const int doubledSourceZ = static_cast<std::int32_t>(static_cast<std::uint32_t>(sourceZ) * 2u);
        const int lightCenterZ = static_cast<std::int32_t>(static_cast<std::uint32_t>(doubledSourceZ / 3) + 8u);
        const float centerX = x;
        const float centerY = y - (static_cast<float>(lightCenterZ) - z);

        const float right = centerX + static_cast<float>(halfExtentX);
        if (floatLessOrUnordered(right, m_viewportLeft))
            return;
        const float left = centerX - static_cast<float>(halfExtentX);
        if (!floatLessOrUnordered(left, m_viewportRight))
            return;
        const float bottom = centerY + static_cast<float>(halfExtentY);
        if (floatLessOrUnordered(bottom, m_viewportTop))
            return;
        const float top = centerY - static_cast<float>(halfExtentY);
        if (!floatLessOrUnordered(top, m_viewportBottom))
            return;

        RECTI destination{
            graphConvertFloatToInt32(left),
            graphConvertFloatToInt32(top),
            graphConvertFloatToInt32(right),
            graphConvertFloatToInt32(bottom)
        };
        RECTI source{0, 0, halfExtentX / 2, halfExtentY / 2};

        BASE_TEXTURE* const texture = g_lightBufferToggle != 0u ? m_hiBuffer : m_lightBuffer;

        int texturePitchBytes = 0;
        std::uint16_t* const locked = texture->lock16(&texturePitchBytes, &source);
        if (!locked)
        {
            logFileLoggerResourceError(g_fileLogger, "%s", 10, "light buffer", 0, "GRAPH");
            return;
        }

        const bool paletteTexture = texture->format() == 0x29u;
        const int texturePitch = paletteTexture ? texturePitchBytes : texturePitchBytes / 2;
        std::uint8_t* const output8 = reinterpret_cast<std::uint8_t*>(locked);
        std::uint16_t* const output16 = locked;
        const std::uint16_t* const worldDepth = softwareDepthBuffer();
        const int worldDepthPitch = softwareDepthPitch();

        int outputY = 0;
        for (int yOffset = -halfExtentY; yOffset < halfExtentY; yOffset += 4, outputY += 4)
        {
            const float sampleY = centerY + static_cast<float>(yOffset);
            const float sampleY3 = sampleY + 3.0f;
            const int outputRow = outputY / 4;

            for (int xOffset = -halfExtentX; xOffset < halfExtentX; xOffset += 4)
            {
                const float sampleX = centerX + static_cast<float>(xOffset);
                const float sampleX3 = sampleX + 3.0f;
                int sampledDepth = 0x7FFF;

                if (!floatLessOrUnordered(sampleX, m_viewportLeft) &&
                    floatLessOrUnordered(sampleX, m_viewportRight) &&
                    !floatLessOrUnordered(sampleY, m_viewportTop) &&
                    floatLessOrUnordered(sampleY, m_viewportBottom))
                {
                    const int x = static_cast<std::int32_t>(static_cast<std::uint32_t>(xOffset) + static_cast<std::uint32_t>(graphConvertFloatToInt32(centerX)));
                    const int y = static_cast<std::int32_t>(static_cast<std::uint32_t>(yOffset) + static_cast<std::uint32_t>(graphConvertFloatToInt32(centerY)));
                    sampledDepth = static_cast<int>(worldDepth[y * worldDepthPitch + x] >> 3u) - 128;
                }

                int sampledDepth3 = 0x7FFF;
                if (!floatLessOrUnordered(sampleX3, m_viewportLeft) &&
                    floatLessOrUnordered(sampleX3, m_viewportRight) &&
                    !floatLessOrUnordered(sampleY3, m_viewportTop) &&
                    floatLessOrUnordered(sampleY3, m_viewportBottom))
                {
                    const int x = static_cast<std::int32_t>(static_cast<std::uint32_t>(xOffset) + static_cast<std::uint32_t>(graphConvertFloatToInt32(centerX)) + 3u);
                    const int y = static_cast<std::int32_t>(static_cast<std::uint32_t>(yOffset) + static_cast<std::uint32_t>(graphConvertFloatToInt32(centerY)) + 3u);
                    sampledDepth3 = static_cast<int>(worldDepth[y * worldDepthPitch + x] >> 3u) - 128;
                }

                if (sampledDepth3 < sampledDepth)
                    sampledDepth = sampledDepth3;
                if (sampledDepth == 0x7FFF)
                    continue;

                const int vertical = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(yOffset) + static_cast<std::uint32_t>(sampledDepth) - static_cast<std::uint32_t>(lightCenterZ));
                const int depthDelta = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(sampledDepth) - static_cast<std::uint32_t>(sourceZ));
                const int xSquare = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(xOffset) * static_cast<std::uint32_t>(xOffset));
                const int verticalSquare = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(vertical) * static_cast<std::uint32_t>(vertical));
                const int verticalNine = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(verticalSquare) * 9u);
                const int depthSquare = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(depthDelta) * static_cast<std::uint32_t>(depthDelta));
                const int metric = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(xSquare) +
                    static_cast<std::uint32_t>(verticalNine / 4) +
                    static_cast<std::uint32_t>(depthSquare / 4));
                int intensity = depthScaleDivisor != 0
                    ? 256 - metric / depthScaleDivisor
                    : 256 - metric;
                if (intensity < 0)
                    intensity = 0;
                else if (intensity > 255)
                    intensity = 255;

                const int outputX = (xOffset + halfExtentX) / 4;
                if (paletteTexture)
                    output8[outputRow * texturePitch + outputX] = static_cast<std::uint8_t>(intensity);
                else
                    output16[outputRow * texturePitch + outputX] = m_intensityPalette16[static_cast<std::size_t>(intensity)];
            }
        }

        texture->unlock();
        SetRenderState(29u, 0u);
        SetAlphaBlend(9u, 2u);

        ++source.left;
        --source.right;
        ++source.top;
        --source.bottom;

        const float deviceDepth =
            (z + sizeXValue + 50.0f) * 0.0001220703125f + 0.015625f;
        const DWORD deviceDepthBits = graphFloatBits(deviceDepth);
        const DWORD colors[2] = {~color, 0xFF000000u};
        texture->DrawDepthRectangle(deviceDepthBits, deviceDepthBits, destination, source, colors);
    }


    void GRAPH::LoadParameters(RESOURCE* map)
    {
        Gamma gamma{};
        std::uint32_t direction = 0u;
        map->read(&m_renderFlags, 4);
        map->read(&gamma.first, 4);
        map->read(&gamma.second, 4);
        setGamma(gamma);
        map->read(&direction, 4);
        m_windDirection = (m_windDirection & 0xFFFFFF00u) | (direction & 0xFFu);
        map->read(&m_windSpeed, 4);
    }


    void GRAPH::OldLoadParameters(RESOURCE* map)
    {
        std::uint32_t packedGamma = 0u;
        std::uint8_t direction = 0u;
        std::int16_t magnitude = 0;
        map->read(&m_renderFlags, 4);
        map->read(&packedGamma, 4);
        const Gamma gamma(Gamma::DECODE, packedGamma);
        setGamma(gamma);
        map->read(&direction, 1);
        map->read(&magnitude, 2);
        SetWind(static_cast<int>(magnitude), ANGLE(direction));
    }
}
