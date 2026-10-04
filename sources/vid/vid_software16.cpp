#include "vid/vid_software16.h"
#include "sprite.h"
#include "graph.h"
#include "map.h"
#include "graphics/base_texture.h"
#include "graphics/color.h"
#include "core/application.h"
#include "core/log.h"
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cmath>
#include <limits>
#include <xmmintrin.h>
#include <array>
#include <cstring>
#include <new>

namespace as1
{
    namespace
    {
        __forceinline int software16X87Int64Low32(float value) noexcept
        {
            const double convertedValue = static_cast<double>(value);
            if (!std::isfinite(convertedValue) ||
                convertedValue < -9223372036854775808.0 ||
                convertedValue >= 9223372036854775808.0)
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(convertedValue));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        int wrapSubSoftware16(int lhs, int rhs) noexcept
        {
            return static_cast<int>(static_cast<std::uint32_t>(lhs) - static_cast<std::uint32_t>(rhs));
        }

        int softwareDrawXToInt3216(float x, float cameraX, int halfWidth) noexcept
        {
            int value = wrapSubSoftware16(software16X87Int64Low32(x), software16X87Int64Low32(cameraX));
            return wrapSubSoftware16(value, halfWidth);
        }

        int softwareDrawYToInt3216(float y, float z, float cameraY, int halfHeight) noexcept
        {
            float projectedY = y - z;
            int value = wrapSubSoftware16(software16X87Int64Low32(projectedY), software16X87Int64Low32(cameraY));
            return wrapSubSoftware16(value, halfHeight);
        }
    }



VID_SOFTWARE16* VID_SOFTWARE16::CreateMirror()
    {

        return new (std::nothrow) VID_SOFTWARE16(*this);
    }


    __forceinline
    DWORD VID_SOFTWARE16::UnpackRgb565ToBgra(WORD value) const
    {

        const DWORD packedColor = static_cast<DWORD>(value);
        const DWORD red = (packedColor << ((16u - g_color16RedShift) & 31u)) & 0x00FF0000u;
        const DWORD green = (packedColor << ((8u - g_color16GreenShift) & 31u)) & 0x0000FF00u;
        const DWORD blue = (packedColor & 0x001Fu) << 3u;
        return 0xFF000000u | red | green | blue;
    }

    __forceinline
    WORD VID_SOFTWARE16::PackRgb565FromBgra(DWORD value) const
    {

        return static_cast<WORD>(
            ((value >> 3u) & 0x001Fu) |
            (g_color16RedMask & (value >> ((16u - g_color16RedShift) & 31u))) |
            (g_color16GreenMask & (value >> ((8u - g_color16GreenShift) & 31u))));
    }


    void VID_SOFTWARE16::SetGammaToPalette(void* palette, const Gamma& gamma)
    {
        if (!palette || (gamma.first == 0u && gamma.second == 0u))
            return;
        if ((formatFlags() & VID_TYPE_ALPHA) != 0u)
        {
            DWORD* entries = static_cast<DWORD*>(palette);
            for (size_t i = 0; i < 256u; ++i)
                entries[i] = BlendGammaColor(gamma, entries[i]);
            return;
        }
        WORD* entries = static_cast<WORD*>(palette);
        for (size_t i = 0; i < 256u; ++i)
            entries[i] = PackRgb565FromBgra(
                BlendGammaColor(gamma, UnpackRgb565ToBgra(entries[i])));
    }

    void VID_SOFTWARE16::Draw(const SPRITE* sprite)
    {

        const DWORD property = properties();
        if ((runtimeAuxFlags() & 0x40u) != 0u)
            return;

        GRAPH* const graph = Graph;
                const int clipLeft = g_softwareClipLeft;
        const int clipTop = g_softwareClipTop;
        const int clipRight = g_softwareClipRight;
        const int clipBottom = g_softwareClipBottom;

        const int sizeX = static_cast<std::int16_t>(vidWidth());
        const int sizeY = static_cast<std::int16_t>(vidHeight());
        const std::uint8_t* const application =
            static_cast<const std::uint8_t*>(core::ApplicationOwner());
        const float cameraX = *reinterpret_cast<const float*>(
            application + core::application_layout::CameraShiftX);
        const float cameraY = *reinterpret_cast<const float*>(
            application + core::application_layout::CameraShiftY);
        const int drawLeft = softwareDrawXToInt3216(sprite->X(), cameraX, sizeX / 2);
        int drawTop = softwareDrawYToInt3216(sprite->Y(), sprite->Z(), cameraY, sizeY / 2);

        if (drawLeft + sizeX < clipLeft || drawLeft >= clipRight ||
            drawTop + sizeY < clipTop || drawTop >= clipBottom)
            return;

        int baseDepth = software16X87Int64Low32(sprite->Z() * 8.0f);
        if ((property & P_ALWAYSTOP) != 0u && baseDepth < 0x3FFF)
            baseDepth += 0x3FFF;
        else if ((property & P_WAVE) != 0u)
        {
            const int waveDepth = software16X87Int64Low32(
                SPRITE::directionSinValue(static_cast<int>((core::CurrentTimeMilliseconds() >> 3u) & 0xFFu)) *
                topZValue() * 8.0f);
            baseDepth += waveDepth;
            drawTop += waveDepth / -8;
        }

        const int frame = sprite->currentFrame();
        BYTE* const frameBase = frameStorage() + frameOffsets()[frame];
        const int contourCount = static_cast<std::int16_t>(*reinterpret_cast<const WORD*>(frameBase));
        BYTE* row = frameBase + 2 + 6 * contourCount;
        const int frameTop = static_cast<std::int16_t>(*reinterpret_cast<const WORD*>(row));
        const int rowCount = static_cast<std::int16_t>(*reinterpret_cast<const WORD*>(row + 2));
        row += 4;
        drawTop += frameTop;
        const int drawBottom = drawTop + rowCount;
        if (drawTop >= clipBottom || drawBottom < clipTop)
            return;

        WORD* const depthBase = graph->softwareDepthBuffer();
        const int depthPitch = graph->softwareDepthPitch();
        if (!graph->backBufferPixels())
            graph->Lock();
        WORD* const colorBase = static_cast<WORD*>(graph->backBufferPixels());
        const int colorPitch = graph->backBufferPitchPixels();
        const WORD typeFlags = formatFlags();
        if ((typeFlags & VID_TYPE_TEXTURE) == 0u)
            return;
        const bool palettePayload = (typeFlags & VID_TYPE_PALETTE) != 0u;
        const bool alphaPalettePayload =
            (typeFlags & (VID_TYPE_ALPHA | VID_TYPE_TEXTURE | VID_TYPE_PALETTE)) ==
            (VID_TYPE_ALPHA | VID_TYPE_TEXTURE | VID_TYPE_PALETTE);
        const bool zPalettePayload = !alphaPalettePayload && palettePayload &&
                                     (typeFlags & VID_TYPE_ZBUFFER) != 0u;
        const bool directPayload = !palettePayload;

        const BYTE* drawPaletteBlock = nullptr;
        std::array<DWORD, 256> drawPaletteScratch{};
        {

            const size_t blockBytes = static_cast<size_t>(PaletteSize());
            Gamma spriteOverride{};
            const bool hasSpriteOverride = sprite->spriteGammaOverride(spriteOverride);
            size_t blockIndex = 0u;
            if (!hasSpriteOverride)
            {
                blockIndex = (typeFlags & VID_TYPE_3D) != 0u
                    ? static_cast<size_t>(sprite->armyIndex())
                    : 0u;
                drawPaletteBlock = frameStorage() + blockIndex * blockBytes;
            }
            else
            {

                blockIndex = (typeFlags & VID_TYPE_3D) != 0u ? 4u : 0u;
                std::memcpy(drawPaletteScratch.data(), frameStorage() + blockIndex * blockBytes, blockBytes);
                Gamma drawPaletteApply{};
                drawPaletteApply = sprite->GetGamma();
                if ((property & P_GAMMA) == 0u)
                    drawPaletteApply.setSaturatingAdd(drawPaletteApply, Graph->gammaPair());
                SetGammaToPalette(drawPaletteScratch.data(), drawPaletteApply);
                drawPaletteBlock = reinterpret_cast<const BYTE*>(drawPaletteScratch.data());
            }
        }

        const int constantDepthInt = std::min(baseDepth + 0x400, 0x7FFF);
        const WORD baseDepthWord = static_cast<WORD>(baseDepth);


        const bool slopedConstantDepthPayload =
            !zPalettePayload && this->sizeZ() > this->sizeY();
        const int constantDepthStep = slopedConstantDepthPayload ? -8 : 0;
        int rowDepthInt = constantDepthInt;
        if (slopedConstantDepthPayload)
            rowDepthInt += rowCount * 8;
        WORD rowDepth = static_cast<WORD>(rowDepthInt);

        if (alphaPalettePayload)
        {
            g_packedSoftwareDepth = (g_packedSoftwareDepth & 0xFFFF0000u) | static_cast<DWORD>(rowDepth);
        }
        else if (zPalettePayload)
        {
            g_packedSoftwareDepth = static_cast<DWORD>(baseDepthWord) | (static_cast<DWORD>(baseDepthWord) << 16u);
        }

        for (int sourceRow = 0; sourceRow < rowCount; ++sourceRow)
        {
            const int dy = drawTop + sourceRow;
            if (dy >= clipBottom)
                break;
            const bool visibleRow = dy >= clipTop;
            int sourceX = 0;
            for (;;)
            {
                const int skip = row[0];
                const int run = row[1];
                row += 2;
                if (skip == 0 && run == 0)
                    break;
                sourceX += skip;

                const WORD* zWords = nullptr;
                if (zPalettePayload)
                {
                    zWords = reinterpret_cast<const WORD*>(row);
                    row += static_cast<size_t>(run) * 2u;
                }

                const BYTE* paletteIndexes = nullptr;
                const WORD* colorWords = nullptr;
                if (palettePayload)
                {
                    paletteIndexes = row;
                    row += static_cast<size_t>(run);
                }
                else
                {
                    colorWords = reinterpret_cast<const WORD*>(row);
                    row += static_cast<size_t>(run) * 2u;
                }

                if (visibleRow)
                {
                    WORD* const colorRow = colorBase + static_cast<size_t>(dy) * static_cast<size_t>(colorPitch);
                    WORD* const depthRow = depthBase + static_cast<size_t>(dy) * static_cast<size_t>(depthPitch);
                    for (int i = 0; i < run; ++i)
                    {
                        const int dx = drawLeft + sourceX + i;
                        if (dx < clipLeft || dx >= clipRight)
                            continue;

                        const WORD oldDepth = depthRow[dx];
                        WORD z = rowDepth;
                        if (zPalettePayload)
                            z = static_cast<WORD>(baseDepthWord + zWords[i]);

                        if (alphaPalettePayload)
                        {
                            if (z < oldDepth)
                                continue;
                        }
                        else if (zPalettePayload)
                        {
                            if (static_cast<std::int16_t>(z) <= static_cast<std::int16_t>(oldDepth))
                                continue;
                        }
                        else if (palettePayload || directPayload)
                        {


                            if (rowDepthInt < static_cast<int>(oldDepth))
                                continue;
                        }

                        if (alphaPalettePayload)
                        {
                            const DWORD source = reinterpret_cast<const DWORD*>(drawPaletteBlock)[paletteIndexes[i]];
                            const DWORD dest = UnpackRgb565ToBgra(colorRow[dx]);
                            const DWORD alpha = source >> 24u;
                            const DWORD sourceWeight = alpha + 1u;
                            const DWORD destWeight = 256u - sourceWeight;
                            const DWORD b = (sourceWeight * (source & 0xFFu) + destWeight * (dest & 0xFFu)) >> 8u;
                            const DWORD g = (sourceWeight * ((source >> 8u) & 0xFFu) + destWeight * ((dest >> 8u) & 0xFFu)) >> 8u;
                            const DWORD r = (sourceWeight * ((source >> 16u) & 0xFFu) + destWeight * ((dest >> 16u) & 0xFFu)) >> 8u;
                            colorRow[dx] = PackRgb565FromBgra(0xFF000000u | (r << 16u) | (g << 8u) | b);
                            continue;
                        }

                        WORD sourceWord = 0u;
                        if (palettePayload)
                        {
                            sourceWord = reinterpret_cast<const WORD*>(drawPaletteBlock)[paletteIndexes[i]];
                        }
                        else
                        {


                            sourceWord = colorWords[i];
                        }

                        depthRow[dx] = z;
                        colorRow[dx] = sourceWord;
                    }
                }
                sourceX += run;
            }


            if (constantDepthStep != 0)
            {
                rowDepthInt += constantDepthStep;
                rowDepth = static_cast<WORD>(rowDepthInt);
                if (alphaPalettePayload)
                    g_packedSoftwareDepth = (g_packedSoftwareDepth & 0xFFFF0000u) | static_cast<DWORD>(rowDepth);
            }
        }
    }

    void VID_SOFTWARE16::DrawToVid(
        SPRITE* sprite,
        void* textureSizeData,
        BASE_TEXTURE* texture,
        BASE_TEXTURE* zTexture)
    {
        VID_HARDWARE::TEX_SIZE* const texSize = static_cast<VID_HARDWARE::TEX_SIZE*>(textureSizeData);

        const DWORD property = properties();
        if ((runtimeAuxFlags() & 0x40u) != 0u)
            return;

        const VID_HARDWARE::TEX_SIZE& tile = *texSize;
        const int clipLeft = tile.sourceX;
        const int clipTop = tile.sourceY;
        const int clipRight = tile.sourceX + tile.width;
        const int clipBottom = tile.sourceY + tile.height;

        const int sizeX = static_cast<std::int16_t>(vidWidth());
        const int sizeY = static_cast<std::int16_t>(vidHeight());

        float drawLeftFloat = sprite->X();
        drawLeftFloat -= static_cast<float>(sizeX / 2);
        drawLeftFloat -= static_cast<float>(tile.destinationX);
        drawLeftFloat -= static_cast<float>(tile.sourceX);
        const int drawLeft = software16X87Int64Low32(drawLeftFloat);

        float drawTopFloat = sprite->Y() - sprite->Z();
        drawTopFloat -= static_cast<float>(sizeY / 2);
        drawTopFloat -= static_cast<float>(tile.destinationY);
        drawTopFloat -= static_cast<float>(tile.sourceY);
        int drawTop = software16X87Int64Low32(drawTopFloat);

        if (drawLeft + sizeX < clipLeft || drawLeft >= clipRight ||
            drawTop + sizeY < clipTop || drawTop >= clipBottom)
            return;

        int baseDepth = software16X87Int64Low32(sprite->Z() * 8.0f);
        if ((property & P_ALWAYSTOP) != 0u && baseDepth < 0x3FFF)
            baseDepth += 0x3FFF;
        else if ((property & P_WAVE) != 0u)
        {
            const int waveDepth = software16X87Int64Low32(
                SPRITE::directionSinValue(static_cast<int>((core::CurrentTimeMilliseconds() >> 3u) & 0xFFu)) *
                topZValue() * 8.0f);
            baseDepth += waveDepth;
            drawTop += waveDepth / -8;
        }

        const int frame = sprite->currentFrame();
        BYTE* const frameBase = frameStorage() + frameOffsets()[frame];
        const int contourCount = static_cast<std::int16_t>(*reinterpret_cast<const WORD*>(frameBase));
        BYTE* row = frameBase + 2 + 6 * contourCount;
        const int frameTop = static_cast<std::int16_t>(*reinterpret_cast<const WORD*>(row));
        const int rowCount = static_cast<std::int16_t>(*reinterpret_cast<const WORD*>(row + 2));
        row += 4;
        drawTop += frameTop;
        const int drawBottom = drawTop + rowCount;
        if (drawTop >= clipBottom || drawBottom < clipTop)
            return;

        int zPitchBytes = 0;
        WORD* const zBits = zTexture->lock16(&zPitchBytes, nullptr);
        int colorPitchBytes = 0;
        WORD* const colorBits = texture->lock16(&colorPitchBytes, nullptr);
        const WORD typeFlags = formatFlags();


        const bool texturePayload = (typeFlags & VID_TYPE_TEXTURE) != 0u;
        const bool palettePayload = (typeFlags & VID_TYPE_PALETTE) != 0u;
        const bool alphaPalettePayload =
            (typeFlags & (VID_TYPE_ALPHA | VID_TYPE_TEXTURE | VID_TYPE_PALETTE)) ==
            (VID_TYPE_ALPHA | VID_TYPE_TEXTURE | VID_TYPE_PALETTE);
        const bool zPalettePayload = !alphaPalettePayload && texturePayload && palettePayload &&
                                     (typeFlags & VID_TYPE_ZBUFFER) != 0u;
        const bool constantPalettePayload = texturePayload && palettePayload && !alphaPalettePayload && !zPalettePayload;
        const bool directPayload = texturePayload && !palettePayload;

        if (!texturePayload)
        {
            texture->unlock();
            zTexture->unlock();
            return;
        }

        const BYTE* paletteBlock = nullptr;
        if (palettePayload)
        {
            const std::size_t blockBytes = static_cast<std::size_t>(PaletteSize());
            const std::size_t blockIndex = (typeFlags & VID_TYPE_3D) != 0u
                ? static_cast<std::size_t>(sprite->armyIndex())
                : 0u;
            paletteBlock = frameStorage() + blockIndex * blockBytes;
        }

        const int constantDepthInt = std::min(baseDepth + 0x400, 0x7FFF);
        const WORD baseDepthWord = static_cast<WORD>(baseDepth);


        const bool slopedConstantDepthPayload =
            !zPalettePayload && this->sizeZ() > this->sizeY();
        const int constantDepthStep = slopedConstantDepthPayload ? -8 : 0;
        int rowDepthInt = constantDepthInt;
        if (slopedConstantDepthPayload)
            rowDepthInt += rowCount * 8;
        WORD rowDepth = static_cast<WORD>(rowDepthInt);

        if (alphaPalettePayload)
        {
            g_packedSoftwareDepth = (g_packedSoftwareDepth & 0xFFFF0000u) | static_cast<DWORD>(rowDepth);
        }
        else if (zPalettePayload)
        {
            g_packedSoftwareDepth = static_cast<DWORD>(baseDepthWord) | (static_cast<DWORD>(baseDepthWord) << 16u);
        }

        for (int sourceRow = 0; sourceRow < rowCount; ++sourceRow)
        {
            const int dy = drawTop + sourceRow;
            if (dy >= clipBottom)
                break;
            const bool visibleRow = dy >= clipTop;
            int sourceX = 0;

            for (;;)
            {
                const int skip = row[0];
                const int run = row[1];
                row += 2;
                if (skip == 0 && run == 0)
                    break;
                sourceX += skip;

                const WORD* zWords = nullptr;
                if (zPalettePayload)
                {
                    zWords = reinterpret_cast<const WORD*>(row);
                    row += static_cast<std::size_t>(run) * 2u;
                }

                const BYTE* paletteIndexes = nullptr;
                const WORD* colorWords = nullptr;
                if (palettePayload)
                {
                    paletteIndexes = row;
                    row += static_cast<std::size_t>(run);
                }
                else
                {
                    colorWords = reinterpret_cast<const WORD*>(row);
                    row += static_cast<std::size_t>(run) * 2u;
                }

                if (visibleRow)
                {

                    WORD* const colorRow = reinterpret_cast<WORD*>(reinterpret_cast<BYTE*>(colorBits) +
                        static_cast<std::ptrdiff_t>(dy) * static_cast<std::ptrdiff_t>(colorPitchBytes));
                    WORD* const depthRow = reinterpret_cast<WORD*>(reinterpret_cast<BYTE*>(zBits) +
                        static_cast<std::ptrdiff_t>(dy) * static_cast<std::ptrdiff_t>(zPitchBytes));

                    for (int i = 0; i < run; ++i)
                    {
                        const int dx = drawLeft + sourceX + i;
                        if (dx < clipLeft || dx >= clipRight)
                            continue;
                        const WORD oldDepth = depthRow[dx];

                        if (alphaPalettePayload)
                        {


                            if (rowDepth < oldDepth)
                                continue;

                            const DWORD sourceColor = reinterpret_cast<const DWORD*>(paletteBlock)[paletteIndexes[i]];
                            const DWORD destination = UnpackRgb565ToBgra(colorRow[dx]);
                            const DWORD alpha = sourceColor >> 24u;
                            const DWORD sourceWeight = alpha + 1u;
                            const DWORD destinationWeight = 256u - sourceWeight;
                            const DWORD b = (sourceWeight * (sourceColor & 0xFFu) + destinationWeight * (destination & 0xFFu)) >> 8u;
                            const DWORD g = (sourceWeight * ((sourceColor >> 8u) & 0xFFu) + destinationWeight * ((destination >> 8u) & 0xFFu)) >> 8u;
                            const DWORD r = (sourceWeight * ((sourceColor >> 16u) & 0xFFu) + destinationWeight * ((destination >> 16u) & 0xFFu)) >> 8u;
                            colorRow[dx] = PackRgb565FromBgra(0xFF000000u | (r << 16u) | (g << 8u) | b);

                            continue;
                        }

                        if (zPalettePayload)
                        {
                            const WORD z = static_cast<WORD>(baseDepthWord + zWords[i]);

                            if (static_cast<std::int16_t>(z) <= static_cast<std::int16_t>(oldDepth))
                                continue;
                            depthRow[dx] = z;
                            colorRow[dx] = reinterpret_cast<const WORD*>(paletteBlock)[paletteIndexes[i]];
                            continue;
                        }


                        if (rowDepthInt < static_cast<int>(oldDepth))
                            continue;

                        depthRow[dx] = rowDepth;
                        if (constantPalettePayload)
                            colorRow[dx] = reinterpret_cast<const WORD*>(paletteBlock)[paletteIndexes[i]];
                        else if (directPayload)
                            colorRow[dx] = colorWords[i];
                    }
                }

                sourceX += run;
            }


            if (constantDepthStep != 0)
            {
                rowDepthInt += constantDepthStep;
                rowDepth = static_cast<WORD>(rowDepthInt);
                if (alphaPalettePayload)
                    g_packedSoftwareDepth = (g_packedSoftwareDepth & 0xFFFF0000u) | static_cast<DWORD>(rowDepth);
            }
        }


        texture->unlock();
        zTexture->unlock();
    }

    int VID_SOFTWARE16::PaletteSize() const
    {

        return ((formatFlags() & VID_TYPE_ALPHA) != 0 ? 4 : 2) << 8;
    }
}
