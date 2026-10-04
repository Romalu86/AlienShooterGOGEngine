#include "man.h"

#include "player.h"

#include "core/resource.h"
#include "map.h"
#include "sprite.h"
#include "vid/vid.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "core/as_string.h"
#include "core/configuration.h"
#include "core/profile_p.h"

#include <cstdio>
#include <new>

namespace as1
{
    PLAYER::PLAYER(int controlModeValue, int playerSlotValue) noexcept
    {
        m_state.playerSlot = playerSlotValue;
        m_state.controlledSprite = 0u;

        m_state.auxiliarySprite = 0u;
        m_state.controlMode = controlModeValue;
        m_state.money = 1000u;
    }


    PLAYER::~PLAYER() noexcept
    {
        reset();


    }


    PLAYER* PLAYER::scalarDeletingDestructor(unsigned char deleteSelfFlag) noexcept
    {
        this->~PLAYER();
        PLAYER* const result = this;
        if ((deleteSelfFlag & 1u) != 0u)
            ::operator delete(result);
        return result;
    }


    void PLAYER::PutMessage(STRING*, float, float) noexcept
    {

    }


    void PLAYER::reset() noexcept
    {
        SPRITE* const controlled = Flagman();
        if (controlled)
        {
            const int nextRef = controlled->listReferenceCount() - 1;
            controlled->setListReferenceCount(nextRef);
            if (nextRef < 0)
            {
                VID* const vid = controlled->Vid();
                logFileLoggerResourceError(g_fileLogger, "SPRITE %i", 4, "noRef at Release", nextRef, vid ? vid->nVid : -1);
            }
            else if (nextRef == 0)
            {
                delete controlled;
            }
        }
        setControlledSprite(nullptr);

        SPRITE* const auxiliary = auxiliarySprite();
        if (auxiliary)
        {
            const int nextRef = auxiliary->listReferenceCount() - 1;
            auxiliary->setListReferenceCount(nextRef);
            if (nextRef < 0)
            {
                VID* const vid = auxiliary->Vid();
                logFileLoggerResourceError(g_fileLogger, "SPRITE %i", 4, "noRef at Release", nextRef, vid ? vid->nVid : -1);
            }
            else if (nextRef == 0)
            {
                delete auxiliary;
            }
        }
        setAuxiliarySprite(nullptr);

        m_state.money = 1000u;
    }


    void PLAYER::SetFlagman(SPRITE* sprite) noexcept
    {
        if (sprite)
            sprite->setListReferenceCount(sprite->listReferenceCount() + 1);

        SPRITE* const previous = Flagman();
        if (previous)
        {
            const int nextRef = previous->listReferenceCount() - 1;
            previous->setListReferenceCount(nextRef);
            if (nextRef < 0)
            {
                VID* const vid = previous->Vid();
                logFileLoggerResourceError(g_fileLogger, "SPRITE %i", 4, "noRef at Release", nextRef, vid ? vid->nVid : -1);
            }
            else if (nextRef == 0)
            {
                delete previous;
            }
        }
        setControlledSprite(sprite);
    }


    int PLAYER::saveControlledSpriteReference(RESOURCE* mapResource) noexcept
    {
        return mapResource->write(reinterpret_cast<const BYTE*>(&m_state.controlledSprite), sizeof(m_state.controlledSprite));
    }


    int PLAYER::loadControlledSpriteReference(RESOURCE* mapResource) noexcept
    {
        SPRITE* const resolved = Map->ReadPointer(mapResource);
        if (resolved)
            resolved->setListReferenceCount(resolved->listReferenceCount() + 1);

        SPRITE* const previous = Flagman();
        if (previous)
        {
            const int nextRef = previous->listReferenceCount() - 1;
            previous->setListReferenceCount(nextRef);
            if (nextRef < 0)
            {
                VID* const vid = previous->Vid();
                logFileLoggerResourceError(g_fileLogger, "SPRITE %i", 4, "noRef at Release", nextRef, vid ? vid->nVid : -1);
            }
            else if (nextRef == 0)
            {
                delete previous;
            }
        }
        setControlledSprite(resolved);
        return static_cast<int>(reinterpret_cast<std::uintptr_t>(resolved) & 0xFFFFFFFFu);
    }


    STRING* PLAYER::getAuxiliaryUnitName(STRING* out) noexcept
    {
        if (SPRITE* const auxiliary = auxiliarySprite())
        {
            VID* const vid = auxiliary->Vid();
            char keyBuffer[0x80]{};
            _itoa(vid->nvid(), keyBuffer, 10);
            STRING section("Units");
            STRING key(keyBuffer);
            STRING defaultValue;
            as1::core::profile_p::readProfileStringInto(
                *out, *as1::core::g_startupStringsIniPathOwner, section, key, defaultValue);
        }
        else
        {
            *out = STRING();
        }
        return out;
    }


    std::intptr_t PLAYER::DeletePointerToSprite(SPRITE* sprite) noexcept
    {
        (void)m_state.spriteList.removeSorted(sprite);

        SPRITE* const controlled = Flagman();
        if (controlled == sprite)
        {
            const int nextRef = controlled->listReferenceCount() - 1;
            controlled->setListReferenceCount(nextRef);
            if (nextRef < 0)
            {
                VID* const vid = controlled->Vid();
                logFileLoggerResourceError(g_fileLogger, "SPRITE %i", 4, "noRef at Release", nextRef, vid ? vid->nVid : -1);
            }
            else if (nextRef == 0)
            {
                delete controlled;
            }
            setControlledSprite(nullptr);
        }

        SPRITE* const auxiliary = auxiliarySprite();
        std::intptr_t result = reinterpret_cast<std::intptr_t>(auxiliary);
        if (auxiliary == sprite)
        {
            if (auxiliary)
            {
                const int nextRef = auxiliary->listReferenceCount() - 1;
                auxiliary->setListReferenceCount(nextRef);
                result = static_cast<std::intptr_t>(nextRef);
                if (nextRef < 0)
                {
                    VID* const vid = auxiliary->Vid();
                    result = logFileLoggerResourceError(g_fileLogger, "SPRITE %i", 4,
                                                        "noRef at Release", nextRef, vid ? vid->nVid : -1);
                }
                else if (nextRef == 0)
                {
                    delete auxiliary;
                    result = reinterpret_cast<std::intptr_t>(auxiliary);
                }
            }
            setAuxiliarySprite(nullptr);
        }
        return result;
    }


    void PLAYER::SetMoney(int value) noexcept
    {
        m_state.money = static_cast<DWORD>(value);
    }


    SPRITE* PLAYER::Flagman() const noexcept
    {
        return reinterpret_cast<SPRITE*>(static_cast<std::uintptr_t>(m_state.controlledSprite));
    }
}
