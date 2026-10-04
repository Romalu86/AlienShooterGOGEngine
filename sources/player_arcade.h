#pragma once

#include "player.h"
#include "message.h"
#include <cstddef>
#include <cstdint>

namespace as1
{
    class MAP;


    class PLAYER_ARCADE final : public PLAYER
    {
    public:
        static constexpr std::size_t ObjectSize = 0x32Cu;

        PLAYER_ARCADE(int controlMode, int playerSlot) noexcept;
        PLAYER_ARCADE* scalarDeletingDestructor(unsigned char deleteSelfFlag) noexcept override;
        std::intptr_t DeletePointerToSprite(SPRITE* sprite) noexcept override;
        void processInput(input::InputMessageState* inputState) noexcept override;

        void PutMessage(STRING* text, float x, float y) noexcept override;


        DWORD SetCleverEnemyAttack(int value) noexcept;


    private:
        MESSAGE m_message;
    };

#if defined(_M_IX86)
    static_assert(sizeof(PLAYER_ARCADE) == PLAYER_ARCADE::ObjectSize, "PLAYER_ARCADE layout mismatch");
#endif

}
