#include "message.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>

#include "core/application.h"
#include "core/as_string.h"
#include "graph.h"
#include "graphics/angle.h"
#include "map.h"
#include "menu.h"
#include "sound/sound_engine.h"
#include "sprite.h"
#include "vid/vid.h"

namespace as1
{
    namespace
    {
        SPRITE* decodeSpritePointer(DWORD value) noexcept
        {
            return reinterpret_cast<SPRITE*>(static_cast<std::uintptr_t>(value));
        }

        DWORD encodeSpritePointer(SPRITE* sprite) noexcept
        {
            return static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(sprite));
        }

        std::uint32_t floatBits(float value) noexcept
        {
            std::uint32_t bits = 0u;
            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
        }

        bool floatEqualOrUnordered(float lhs, float rhs) noexcept
        {
            return lhs == rhs || std::isnan(lhs) || std::isnan(rhs);
        }

        int truncateFloatToInt32(float value) noexcept
        {
            if (!std::isfinite(value) ||
                value < static_cast<float>(std::numeric_limits<std::int32_t>::min()) ||
                value >= 2147483648.0f)
                return std::numeric_limits<std::int32_t>::min();
            return static_cast<int>(std::trunc(value));
        }

        int projectedRowToInt32(float spriteY, float spriteZ,
                                float cameraY, float viewportTop) noexcept
        {
            float value = spriteY - spriteZ;
            value -= cameraY;
            value -= viewportTop;
            return truncateFloatToInt32(value);
        }

        int multiplyLow32(int lhs, int rhs) noexcept
        {
            const std::uint64_t product =
                static_cast<std::uint64_t>(static_cast<std::uint32_t>(lhs)) *
                static_cast<std::uint64_t>(static_cast<std::uint32_t>(rhs));
            return static_cast<int>(static_cast<std::uint32_t>(product));
        }

        __forceinline void destroyMessageStackArray(DWORD tableAddress) noexcept
        {
            if (tableAddress == 0u)
                return;

            unsigned char* const table = reinterpret_cast<unsigned char*>(static_cast<std::uintptr_t>(tableAddress));
            int* const allocation = reinterpret_cast<int*>(table) - 1;
            const int count = *allocation;
            for (int i = count - 1; i >= 0; --i)
            {
                char* const text = *reinterpret_cast<char**>(table + static_cast<std::size_t>(i) * sizeof(MESSAGE_STACK));
                if (text != STRING::SharedEmptyText())
                    ::operator delete(text);
            }
            ::operator delete(allocation);
        }
    }

    __forceinline MESSAGE_STACK_LIST::MESSAGE_STACK_LIST() noexcept
        : count(0), capacity(0), entries(0u)
    {
    }


    __forceinline MESSAGE_STACK_LIST::~MESSAGE_STACK_LIST() noexcept
    {
        destroyMessageStackArray(entries);
        entries = 0u;
        count = 0;
    }


    void MESSAGE_STACK_LIST::clear() noexcept
    {
        capacity = 0;
        count = 0;
        destroyMessageStackArray(entries);
        entries = 0u;
    }


    MESSAGE_STACK_LIST* MESSAGE_STACK_LIST::scalarDeletingDestructor(unsigned char deleteSelfFlag) noexcept
    {
        this->~MESSAGE_STACK_LIST();
        MESSAGE_STACK_LIST* const result = this;
        if ((deleteSelfFlag & 1u) != 0u)
            ::operator delete(result);
        return result;
    }


    MESSAGE::MESSAGE(int textVidValue, int markerVidValue,
                     DWORD baseXBits, DWORD baseYBits,
                     int lineCountValue, DWORD updateIntervalValue) noexcept
    {
        lineCount = lineCountValue;
        updateInterval = updateIntervalValue;
        std::memcpy(&baseX, &baseXBits, sizeof(baseX));
        std::memcpy(&baseY, &baseYBits, sizeof(baseY));
        textVid = textVidValue;
        markerVid = markerVidValue;
        stack.entries = 0u;
        stack.count = 0;
        stack.capacity = 0;
        rowDirection = -1;
        for (int i = 0; i < lineCount; ++i)
        {
            markerSprites[i] = 0u;
            textSprites[i] = 0u;
        }
    }


    MESSAGE::~MESSAGE() noexcept
    {
        const int count = lineCount;
        if (count > 0)
        {
            for (int i = 0; i < count; ++i)
            {
                if (SPRITE* const marker = decodeSpritePointer(markerSprites[i]))
                    delete marker;
                markerSprites[i] = 0u;
                if (SPRITE* const textSprite = decodeSpritePointer(textSprites[i]))
                    delete textSprite;
                textSprites[i] = 0u;
            }
        }


    }


    MESSAGE* MESSAGE::scalarDeletingDestructor(unsigned char deleteSelfFlag) noexcept
    {
        this->~MESSAGE();
        if ((deleteSelfFlag & 1u) != 0u)
            ::operator delete(this);
        return this;
    }


    void MESSAGE::DeletePointerToSprite(SPRITE* sprite) noexcept
    {
        const int count = lineCount;
        if (count <= 0)
            return;
        const DWORD target = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(sprite) & 0xFFFFFFFFu);
        for (int i = 0; i < count; ++i)
        {
            if (markerSprites[i] == target)
                markerSprites[i] = 0u;
            if (textSprites[i] == target)
                textSprites[i] = 0u;
        }
    }


    __forceinline int MESSAGE::font_height() const noexcept
    {
        VID* vid = nullptr;
        if (textVid >= 0 && textVid < as1::core::GlobalApplicationVidTable().count())
            vid = as1::core::GlobalApplicationVidTable().slot(textVid);
        if (!vid)
            vid = EmptyVid;
        return static_cast<int>(static_cast<short>(vid->vidHeight())) + 1;
    }

    __forceinline bool MESSAGE::validateText(const STRING* text, int* fontHeightOut) noexcept
    {
        const int height = font_height();
        if (fontHeightOut)
            *fontHeightOut = height;
        return std::strcmp(text->c_str(), "") != 0;
    }

    void MESSAGE::Put(STRING* text, float x, float y) noexcept
    {
        int fontHeight = 1;
        if (!validateText(text, &fontHeight))
            return;

        const int count = lineCount;
        lastUpdate = as1::core::CurrentTimeMilliseconds();
        const int terminalIndex = count - 1;
        if (markerSprites[terminalIndex] != 0u)
            Shift();

        MAP* const map = Map;
        GRAPH* const graph = Graph;
        const float viewportTop = graph->viewportTop();

        if (markerSprites[terminalIndex] == 0u)
        {
            VID* vid = nullptr;
            if (textVid >= 0 && textVid < as1::core::GlobalApplicationVidTable().count())
                vid = as1::core::GlobalApplicationVidTable().slot(textVid);
            if (!vid)
                vid = EmptyVid;
            const float spriteY = static_cast<float>(fontHeight * terminalIndex)
                                + baseY + viewportTop + 2000.0f;
            SPRITE* const created = reinterpret_cast<core::Application*>(core::ApplicationOwner())->CreateSprite(
                vid, VECTOR{baseX, spriteY, 2000.0f}, ANGLE(static_cast<unsigned char>(0)), nullptr);
            markerSprites[terminalIndex] = encodeSpritePointer(created);
        }

        if (SPRITE* const routeSprite = decodeSpritePointer(markerSprites[terminalIndex]))
        {
            routeSprite->dispatchVirtualAction(ActionCode::ACT_SET_TEXT,
                static_cast<int>(reinterpret_cast<std::uintptr_t>(text)), 0, 0);
        }

        targetX[terminalIndex] = x;
        targetY[terminalIndex] = y;

        if (!floatEqualOrUnordered(x, -1.0f) || !floatEqualOrUnordered(y, -1.0f))
        {
            VID* vid = nullptr;
            if (markerVid >= 0 && markerVid < as1::core::GlobalApplicationVidTable().count())
                vid = as1::core::GlobalApplicationVidTable().slot(markerVid);
            if (!vid)
                vid = EmptyVid;

            GRAPH* const graph = Graph;
            const float viewportTop = graph->viewportTop();
            const float spriteY = static_cast<float>(fontHeight * terminalIndex)
                                + static_cast<float>(fontHeight / 2)
                                + baseY + viewportTop + 2000.0f;
            const float spriteX = baseX - 10.0f;
            SPRITE* const created = reinterpret_cast<core::Application*>(core::ApplicationOwner())->CreateSprite(
                vid, VECTOR{spriteX, spriteY, 2000.0f}, ANGLE(static_cast<unsigned char>(0)), nullptr);
            textSprites[terminalIndex] = encodeSpritePointer(created);
        }

    }


    void MESSAGE::Tact() noexcept
    {
        const std::uint32_t now = as1::core::CurrentTimeMilliseconds();

        int index = 0;
        while (index < stack.count)
        {
            auto* const table = reinterpret_cast<MESSAGE_STACK*>(static_cast<std::uintptr_t>(stack.entries));
            MESSAGE_STACK& entry = table[index];

            if (now - as1::core::PreviousWorldTimeMilliseconds() < entry.time)
            {
                entry.time = as1::core::PreviousWorldTimeMilliseconds() + entry.time - now;
                ++index;
                continue;
            }

            Put(reinterpret_cast<STRING*>(&entry), entry.x, entry.y);

            int count = stack.count;
            if (index >= 0 && index < count)
            {
                --count;
                stack.count = count;
                for (int move = index; move < count; ++move)
                {
                    MESSAGE_STACK& dst = table[move];
                    MESSAGE_STACK& src = table[move + 1];
                    (*reinterpret_cast<STRING*>(&dst)).Assign(*reinterpret_cast<const STRING*>(&src));
                    dst.x = src.x;
                    dst.y = src.y;
                    dst.time = src.time;
                }
            }

            if (stack.count == 0)
                stack.clear();
        }

        const std::uint32_t previous = lastUpdate;
        if (now - previous > updateInterval)
        {
            Shift();
            lastUpdate = now;
        }

        MENU& frameList = applicationMenu();
        if ((frameList.controlFlags() & 1u) != 0u && frameList.NVidUnderCursor() == markerVid)
        {
            SPRITE* const selected = frameList.selectedSprite();
            GRAPH* const graph = Graph;
            const int height = font_height();
            const int projectedRowNumerator = projectedRowToInt32(
                selected->Y(), selected->Z(),
                as1::core::GlobalApplicationDrawDispatcherState().cameraShiftY(),
                graph->viewportTop());
            const int row = projectedRowNumerator / height;
            const float cameraX = targetX[row];
            const float cameraY = targetY[row];
            if (!floatEqualOrUnordered(cameraX, -999999.0f) || floatBits(cameraY) != 0xC97423F0u)
                Map->SetShiftCoor(cameraX, cameraY, 2);
        }
    }


    void MESSAGE::Shift() noexcept
    {
        const int count = lineCount;
        if (count <= 0)
            return;

        auto releaseLeadingSprite = [](DWORD& slot) noexcept {
            if (SPRITE* const sprite = decodeSpritePointer(slot))
                delete sprite;
            slot = 0u;
        };

        releaseLeadingSprite(textSprites[0]);
        releaseLeadingSprite(markerSprites[0]);

        if (count <= 1)
            return;

        const int height = font_height();
        const int rowDelta = multiplyLow32(rowDirection, height);

        for (int i = 1; i < count; ++i)
        {
            SPRITE* const sprite = decodeSpritePointer(markerSprites[i]);
            if (sprite)
            {
                sprite->ChangeCoor(sprite->X(), sprite->Y() + static_cast<float>(rowDelta), sprite->Z());
                markerSprites[i - 1] = markerSprites[i];
                targetX[i - 1] = targetX[i];
                targetY[i - 1] = targetY[i];
                markerSprites[i] = 0u;
            }
        }

        for (int i = 1; i < count; ++i)
        {
            SPRITE* const sprite = decodeSpritePointer(textSprites[i]);
            if (sprite)
            {
                sprite->ChangeCoor(sprite->X(), sprite->Y() + static_cast<float>(rowDelta), sprite->Z());
                textSprites[i - 1] = textSprites[i];
                textSprites[i] = 0u;
            }
        }
    }
}
