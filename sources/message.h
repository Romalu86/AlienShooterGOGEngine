#pragma once

#include "core/types.h"
#include <cstddef>
#include <cstdint>

namespace as1
{
    class PLAYER_ARCADE;
    class SPRITE;
    class STRING;


    struct MESSAGE_STACK
    {
        DWORD text;
        float x;
        float y;
        DWORD time;
    };


    struct MESSAGE_STACK_LIST
    {
        __forceinline MESSAGE_STACK_LIST() noexcept;
        __forceinline ~MESSAGE_STACK_LIST() noexcept;
        virtual MESSAGE_STACK_LIST* scalarDeletingDestructor(unsigned char deleteSelfFlag) noexcept;
        void clear() noexcept;

        int count;
        int capacity;
        DWORD entries;
    };


    class MESSAGE
    {
    public:
        static constexpr std::size_t ObjectSize = 0x304u;

        MESSAGE(int textVid, int markerVid,
                DWORD baseXBits, DWORD baseYBits,
                int lineCount, DWORD updateInterval) noexcept;
        ~MESSAGE() noexcept;


        virtual MESSAGE* scalarDeletingDestructor(unsigned char deleteSelfFlag) noexcept;
        virtual void DeletePointerToSprite(SPRITE* sprite) noexcept;
        void Tact() noexcept;
        void Put(STRING* text, float targetX, float targetY) noexcept;
        void Shift() noexcept;
        __forceinline int font_height() const noexcept;

    private:
        friend class PLAYER_ARCADE;

        __forceinline bool validateText(const STRING* text, int* fontHeightOut = nullptr) noexcept;

        int lineCount;
        DWORD updateInterval;
        int textVid;
        int markerVid;
        float baseX;
        float baseY;
        int rowDirection;
        DWORD lastUpdate;
        DWORD textSprites[45];
        DWORD markerSprites[45];
        float targetX[45];
        float targetY[45];
        MESSAGE_STACK_LIST stack;
    };

#if defined(_M_IX86)
    static_assert(sizeof(MESSAGE_STACK_LIST) == 0x10u, "MESSAGE_STACK_LIST layout mismatch");
    static_assert(sizeof(MESSAGE) == MESSAGE::ObjectSize, "MESSAGE layout mismatch");
#endif

}
