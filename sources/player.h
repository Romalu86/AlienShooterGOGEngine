#pragma once

#include "core/types.h"
#include "base_sprite_list.h"
#include <cstddef>
#include <cstdint>

namespace as1
{
    class RESOURCE;
    class SPRITE;
    class STRING;
    namespace input { struct InputMessageState; }


    class PLAYER
    {
    public:
        static constexpr std::size_t ObjectSize = 0x28u;

        PLAYER(int controlMode, int playerSlot) noexcept;
        ~PLAYER() noexcept;


        virtual PLAYER* scalarDeletingDestructor(unsigned char deleteSelfFlag) noexcept;
        virtual std::intptr_t DeletePointerToSprite(SPRITE* sprite) noexcept;
        virtual int saveControlledSpriteReference(RESOURCE* mapResource) noexcept;
        virtual int loadControlledSpriteReference(RESOURCE* mapResource) noexcept;
        virtual void reset() noexcept;
        virtual void SetFlagman(SPRITE* sprite) noexcept;
        virtual void processInput(input::InputMessageState*) noexcept {}
        virtual void onScriptModeEnabled() noexcept {}
        virtual void onScriptModeDisabled() noexcept {}
        virtual void PutMessage(STRING* text, float x, float y) noexcept;
        virtual void onSpriteAssigned(SPRITE*) noexcept {}
        virtual STRING* getAuxiliaryUnitName(STRING* out) noexcept;

        __forceinline int controlMode() const noexcept { return m_state.controlMode; }
        __forceinline int playerSlot() const noexcept { return m_state.playerSlot; }

        SPRITE* Flagman() const noexcept;
        __forceinline void setControlledSprite(SPRITE* value) noexcept
        {
            m_state.controlledSprite = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(value));
        }

        __forceinline SPRITE* auxiliarySprite() const noexcept
        {
            return reinterpret_cast<SPRITE*>(static_cast<std::uintptr_t>(m_state.auxiliarySprite));
        }

        __forceinline void setAuxiliarySprite(SPRITE* value) noexcept
        {
            m_state.auxiliarySprite = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(value));
        }

        DWORD money() const noexcept { return m_state.money; }
        DWORD getMoney() const noexcept { return m_state.money; }

        void SetMoney(int value) noexcept;
        void setMoney(DWORD value) noexcept { SetMoney(static_cast<int>(value)); }

    protected:
        struct BaseOwnerLayout
        {


            DWORD money;
            int controlMode;
            int playerSlot;
            DWORD controlledSprite;
            BaseSpriteList<0> spriteList;
            DWORD auxiliarySprite;
        };


        BaseOwnerLayout m_state;
    };

#if defined(_M_IX86)
    static_assert(sizeof(PLAYER) == PLAYER::ObjectSize, "PLAYER layout mismatch");
#endif

}
