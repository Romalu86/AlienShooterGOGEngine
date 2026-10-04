#include "man.h"
#include <cstring>

#include "player_arcade.h"

#include "core/resource.h"
#include "map.h"
#include "menu.h"
#include "graph.h"
#include "input.h"
#include "sprite.h"
#include "vid/vid.h"
#include "graphics/angle.h"
#include "script/action_constants.h"
#include "core/application.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "core/as_string.h"
#include "core/configuration.h"
#include "core/profile_p.h"
#include <new>
#include <cstdio>
#include "sound/sound_engine.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace as1
{
    namespace
    {
        int playerTruncateFloatToInt32(float value) noexcept
        {
            if (!std::isfinite(value) ||
                value < static_cast<float>(std::numeric_limits<std::int32_t>::min()) ||
                value >= 2147483648.0f)
                return std::numeric_limits<std::int32_t>::min();
            return static_cast<int>(std::trunc(value));
        }

        bool playerOrderedFloatEqual(float lhs, float rhs) noexcept
        {
            return !std::isnan(lhs) && !std::isnan(rhs) && lhs == rhs;
        }
    }


    PLAYER_ARCADE::PLAYER_ARCADE(int controlMode, int playerSlot) noexcept
        : PLAYER(controlMode, playerSlot),
          m_message(4, -1, 0x43C70000u, 0x43C20000u, 5, 0x1388u)
    {

        GRAPH* const graph = Graph;
                m_message.baseX = graph->viewportRight() - 242.0f;
        m_message.baseY = graph->viewportBottom() - 92.0f;
        m_message.rowDirection = 1;
    }


    DWORD PLAYER_ARCADE::SetCleverEnemyAttack(int value) noexcept
    {
        DWORD& messageVptrFlags = *reinterpret_cast<DWORD*>(&m_message);
        const DWORD result = (messageVptrFlags & ~2u) | (value != 0 ? 2u : 0u);
        messageVptrFlags = result;
        return result;
    }


    void PLAYER_ARCADE::PutMessage(STRING* text, float x, float y) noexcept
    {
        (void)m_message.Put(text, x, y);
    }


    PLAYER_ARCADE* PLAYER_ARCADE::scalarDeletingDestructor(unsigned char deleteSelfFlag) noexcept
    {
        m_message.~MESSAGE();
        this->PLAYER::~PLAYER();
        PLAYER_ARCADE* const result = this;
        if ((deleteSelfFlag & 1u) != 0u)
            ::operator delete(result);
        return result;
    }


    std::intptr_t PLAYER_ARCADE::DeletePointerToSprite(SPRITE* sprite) noexcept
    {
        m_message.DeletePointerToSprite(sprite);
        return PLAYER::DeletePointerToSprite(sprite);
    }


    void PLAYER_ARCADE::processInput(as1::input::InputMessageState* inputState) noexcept
    {

        {
            MAP* const map = Map;
            GROUPS& groups = map->groupOwner();
            for (Group* group = groups.First(); group;)
            {
                SPRITE* goal = nullptr;
                int command = 0;
                int hasPlainSprite = 0;

                if (group->activeCount() > 0)
                {
                    for (int i = 0; i < group->activeCount(); ++i)
                    {
                        SPRITE* const sprite = group->at(static_cast<std::size_t>(i));
                        const std::uint32_t runtimeFlags = sprite->runtimeFlags();
                        const std::uint32_t masked = runtimeFlags & SPRITE::CommandBitsMask;
                        if (masked == 0u)
                            hasPlainSprite = 1;
                        if (masked == 0x0Cu || masked == 0x10u)
                        {
                            goal = sprite->Goal();
                            command = static_cast<int>((runtimeFlags >> 2) & 0x1Fu);
                        }
                    }

                    if (goal && command && hasPlainSprite)
                    {
                        for (int i = 0; i < group->activeCount(); ++i)
                        {
                            SPRITE* const sprite = group->at(static_cast<std::size_t>(i));
                            if ((sprite->runtimeFlags() & SPRITE::CommandBitsMask) == 0u)
                                sprite->SetCommand(command, goal);
                        }
                    }
                }

                group = groups.Next(group);
            }
        }


        m_message.Tact();

        SPRITE* controlled = Flagman();
        if (!controlled)
            return;
        VID* controlledVid = controlled->Vid();
        if (controlledVid->spriteClassId() != 7u)
            return;

        const std::uint32_t flags = inputState->flags;
        const float groundAttackZ =
            Map->GetGroundZScr(inputState->worldX, inputState->worldY) + 10.0f;
        const float attackZ =
            (controlled->Z() + 80.0f < groundAttackZ)
                ? (controlled->Z() + 60.0f)
                : groundAttackZ;

        if ((flags & 0x00008000u) != 0u)
        {
            controlled->dispatchVirtualAction(
                static_cast<std::uint32_t>(ActionCode::ACT_MOVE),
                playerTruncateFloatToInt32(inputState->worldX),
                playerTruncateFloatToInt32(inputState->worldY + controlled->Z()),
                0);
        }

        if (((flags & 0x00000700u) != 0u || (flags & 0x00000080u) != 0u) &&
            ((controlled->runtimeFlags() & SPRITE::CommandBitsMask) == 4u))
        {
            controlled->SetCommandWithoutLink(0, nullptr);
        }

        if ((flags & 0x00004000u) != 0u)
        {
            if (controlledVid->spriteClassId() == 7u &&
                controlled->ammoCount() < controlledVid->fightNoChildValue())
            {
                SPRITE* const child = controlled->childChain();
                int selected = child->Vid()->nvid() - 0x0B;
                if (static_cast<MAN*>(controlled)->ChangeWeapon(selected) == 0)
                {
                    do
                    {
                        --selected;
                    }
                    while (static_cast<MAN*>(controlled)->ChangeWeapon(selected) == 0);
                }
            }

            controlled->dispatchVirtualAction(
                static_cast<std::uint32_t>(ActionCode::ACT_COOR_ATTACK),
                playerTruncateFloatToInt32(inputState->worldX),
                playerTruncateFloatToInt32(inputState->worldY),
                playerTruncateFloatToInt32(attackZ));
        }

        const std::uint32_t lastCode = inputState->lastCode;
        if (lastCode >= 0x30u && lastCode <= 0x39u && controlledVid->spriteClassId() == 7u)
            static_cast<MAN*>(controlled)->ChangeWeapon(static_cast<int>(lastCode - 0x30u));

        {
            int selected = controlledVid->linkedVid()->nvid() - 0x0A;
            int delta = inputState->wheelDelta;
            if (delta == 0)
            {
                const as1::input::InputControlKeys& keys = as1::input::g_inputControlKeys;
                if (inputState->keyCode == keys.nextWeapon)
                    delta = 1;
                else if (inputState->keyCode == keys.previousWeapon)
                {
                    if (selected > 0)
                    {
                        do
                        {
                            --selected;
                        }
                        while (static_cast<MAN*>(controlled)->ChangeWeapon(selected) == 0 && selected > 0);
                    }
                    delta = 0;
                }
            }

            if (delta > 0)
            {
                do
                {
                    if (selected < 10)
                    {
                        do
                        {
                            ++selected;
                        }
                        while (static_cast<MAN*>(controlled)->ChangeWeapon(selected) == 0 && selected < 10);
                    }
                    --delta;
                }
                while (delta > 0);
            }
            else if (delta < 0 && selected > 0)
            {
                do
                {
                    --selected;
                }
                while (static_cast<MAN*>(controlled)->ChangeWeapon(selected) == 0 && selected > 0);
            }
        }

        const std::uint32_t deltaMs =
            as1::core::CurrentTimeMilliseconds() - as1::core::PreviousWorldTimeMilliseconds();
        int commandBase = as1::input::g_relativeControlEnabled ? controlled->directionIndex() : 0;
        const bool moveLeft = (flags & 0x00000080u) != 0u;
        const bool moveRight = (flags & 0x00000100u) != 0u;
        const bool moveDown = (flags & 0x00000200u) != 0u;
        const bool moveUp = (flags & 0x00000400u) != 0u;

        if (moveUp && moveRight)
            controlled->RotateTact(commandBase + 0x28, deltaMs);
        else if (moveUp && moveLeft)
            controlled->RotateTact(commandBase + 0xD8, deltaMs);
        else if (moveDown && moveRight)
            controlled->RotateTact(commandBase + 0x58, deltaMs);
        else if (moveDown && moveLeft)
            controlled->RotateTact(commandBase + 0xA8, deltaMs);
        else if (moveLeft)
            controlled->RotateTact(controlled->GlideDirection(commandBase + 0xC0), deltaMs);
        else if (moveRight)
            controlled->RotateTact(controlled->GlideDirection(commandBase + 0x40), deltaMs);
        else if (moveDown)
            controlled->RotateTact(controlled->GlideDirection(commandBase + 0x80), deltaMs);
        else if (moveUp)
            controlled->RotateTact(controlled->GlideDirection(commandBase), deltaMs);
        else if ((controlled->runtimeFlags() & SPRITE::CommandBitsMask) != 4u)
            controlled->Stop();

        if ((flags & 0x00000700u) != 0u || (flags & 0x00000080u) != 0u)
            controlled->StartMove();

        SPRITE* const child = controlled->childChain();
        if (child)
        {
            VID* const childVid = child->Vid();
            if (childVid == controlledVid->linkedVid() && controlled->Animation() < 15)
            {


                const ANGLE desiredChildDirection = DirectionFromFloatXY(
                    inputState->worldX - controlled->X(),
                    inputState->worldY + controlled->Z() - controlled->Y());

                const bool speedIsOrderedZero = playerOrderedFloatEqual(controlled->Speed(), 0.0f);


                const bool backwardTurnState =
                    !speedIsOrderedZero || (controlled->runtimeFlags() & 0x80u) != 0u;
                if (backwardTurnState && controlledVid->declaredAnimationFrameCount(6) != 0)
                {
                    const unsigned char ownerDirection =
                        static_cast<unsigned char>(controlled->directionIndex() & 0xFF);
                    const unsigned char childDirection =
                        static_cast<unsigned char>(child->directionIndex() & 0xFF);
                    const unsigned char deltaA = static_cast<unsigned char>(ownerDirection - childDirection);
                    const unsigned char deltaB = static_cast<unsigned char>(childDirection - ownerDirection);
                    const unsigned char minDelta = std::min(deltaA, deltaB);

                    if (minDelta > 0x40u)
                    {
                        if (controlled->Animation() != 6)
                            controlled->ChangeAnimation(6);
                    }
                    else if (controlled->Animation() == 6)
                    {
                        controlled->ChangeAnimation(0);
                        controlled->ChangeDirection(ANGLE(
                            static_cast<unsigned char>(controlled->directionIndex() + 0x80u)));
                    }
                }
                else if (controlled->Animation() == 6)
                {
                    controlled->ChangeAnimation(0);
                    controlled->ChangeDirection(ANGLE(
                        static_cast<unsigned char>(controlled->directionIndex() + 0x80u)));
                }

                if (speedIsOrderedZero && controlled->Animation() == 2)
                    controlled->ChangeAnimation(0);

                if (speedIsOrderedZero && controlledVid->nVid == 9)
                {
                    const unsigned char childDirection =
                        static_cast<unsigned char>(child->directionIndex() & 0xFF);
                    const unsigned char ownerDirection =
                        static_cast<unsigned char>(controlled->directionIndex() & 0xFF);
                    const unsigned char deltaA = static_cast<unsigned char>(ownerDirection - childDirection);
                    const unsigned char deltaB = static_cast<unsigned char>(childDirection - ownerDirection);
                    const unsigned char minDelta = std::min(deltaA, deltaB);

                    if (as1::core::g_childRotationCorrectionPending != 0u)
                    {
                        if (controlled->RotateTact(ANGLE(childDirection), deltaMs).Int() == 0)
                            as1::core::g_childRotationCorrectionPending = 0u;
                    }
                    else
                    {
                        as1::core::g_childRotationCorrectionPending = minDelta > 0x37u ? 1u : 0u;
                    }
                }


                const DWORD childCommandBits = child->runtimeFlags() & SPRITE::CommandBitsMask;
                if (childCommandBits != 0x10u)
                    child->RotateTact(desiredChildDirection, deltaMs);
            }
        }

        SPRITE* auxiliary = auxiliarySprite();
        if (auxiliary)
        {
            VID* auxiliaryVid = auxiliary->Vid();
            if ((auxiliaryVid->spriteTypeId() & 0x2u) != 0u)
            {
                const int nextRef = auxiliary->listReferenceCount() - 1;
                auxiliary->setListReferenceCount(nextRef);
                if (nextRef < 0)
                {
                    const int nvid = auxiliaryVid ? auxiliaryVid->nVid : -1;
                    logFileLoggerResourceError(g_fileLogger, "SPRITE %i", 4, "noRef at Release", nextRef, nvid);
                }
                else if (nextRef == 0)
                {
                    delete auxiliary;
                }
                setAuxiliarySprite(nullptr);
            }
        }

    }


}
