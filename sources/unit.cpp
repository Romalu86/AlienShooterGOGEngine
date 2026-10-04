#include "unit.h"
#include "map.h"
#include "win/application_win.h"
#include "core/application.h"
#include "core/log.h"
#include "graphics/angle.h"

#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

namespace as1
{

    UNIT::UNIT(VID* vid, float x, float y, float z, ANGLE direction, SPRITE* parent)
        : TERRAIN(vid, x, y, z, direction, parent)
    {

        m_sharedPrimaryState = 0;
        m_sharedSecondaryState = 0;
        m_commandStateSentinel = -1;
        m_turnTimer = 0;

        const VID* valueOwner = vid;
        if (childChain())
        {
            VID* const childVid = childChain()->Vid();
            VID* const linkVid = vid->linkedVid();
            if (childVid == linkVid &&
                childVid->hasWeaponChildDescriptor() != 0u &&
                childVid->weaponCount() != 0u)
                valueOwner = linkVid;
        }


        m_behaviorFlags = valueOwner->weaponDefaultBehavior();

        const VID* counterOwner = vid;
        if (VID* const linkVid = vid->linkedVid())
        {
            if (linkVid->hasWeaponChildDescriptor() != 0u &&
                linkVid->weaponCount() != 0u)
                counterOwner = linkVid;
        }
        m_ammoFixedPoint = counterOwner->weaponRecordAmmoCapacity() << 6;
    }


    UNIT::~UNIT()
    {
        win::applicationWinInstance()->transferFrom(this);
    }


    int UNIT::Action(int opcode, std::intptr_t argument1Payload, int argument2Value, int argument3Value)
    {
        const int argument1 = static_cast<int>(argument1Payload);
        const int argument2 = argument2Value;
        const int argument3 = argument3Value;


        const int op = opcode;
        switch (op)
        {
        case static_cast<int>(ActionCode::ACT_ADD_AMMO):
        {
            const int weaponValue = m_vid->GetMaxAmmo();
            if (weaponValue == 999999)
            {
                setAmmoFixedPoint(63999936);
                return weaponValue;
            }

            const std::uint32_t sum = static_cast<std::uint32_t>(ammoFixedPoint()) +
                (static_cast<std::uint32_t>(argument1) << 6);
            setAmmoFixedPoint(static_cast<std::int32_t>(sum));
            const int result = ammoFixedPoint();
            if (ammoFixedPoint() < 0)
                setAmmoFixedPoint(0);
            return result;
        }

        case static_cast<int>(ActionCode::ACT_GET_AMMO):
            return ammoCount();

        case static_cast<int>(ActionCode::ACT_SET_BEHAVE):
            setBehaviorFlags(argument1);
            if (m_childChain)
                m_childChain->dispatchVirtualAction(static_cast<std::uint32_t>(op), argument1, argument2, argument3);
            return 0;

        case static_cast<int>(ActionCode::ACT_GET_BEHAVE):
            return behaviorFlags();

        case static_cast<int>(ActionCode::ACT_NEXT_COMMAND):
        {
            if (m_currentAnimation >= 15)
                return 0;

            const bool specialMoveAnimation =
                (m_runtimeFlags & 0x80u) == 0u &&
                (m_currentAnimation == 8 || m_currentAnimation == 10) &&
                m_vid->declaredAnimationFrameCount(10) != 0;
            if (specialMoveAnimation)
            {


                if (m_actionTimer != 0u)
                {
                    if (m_currentAnimation != 10)
                        ChangeAnimation(10);
                }
                else if (m_currentAnimation == 10)
                {
                    ChangeAnimation(0);
                }
            }
            else
            {


                if (m_speed == 0.0f || std::isnan(m_speed))
                {
                    ChangeAnimation(0);
                }
                else if (m_currentAnimation != 2)
                {
                    ChangeAnimation(2);
                }
            }

            const DWORD commandBits = m_runtimeFlags & SPRITE::CommandBitsMask;
            if (commandBits == 0x0Cu)
            {
                SPRITE* const child = m_childChain;
                if (child)
                {
                    VID* const childVid = child->m_vid;
                    if (childVid == m_vid->linkedVid() &&
                        childVid->hasWeaponChildDescriptor() != 0u &&
                        childVid->weaponCount() != 0u &&
                        m_goalSprite != nullptr &&
                        child->m_goalSprite == nullptr)
                    {
                        child->SetCommand(3, m_goalSprite);
                    }
                }
            }

            if (commandBits == 0x10u)
            {
                SPRITE* const child = m_childChain;
                if (child)
                {
                    VID* const childVid = child->m_vid;
                    if (childVid == m_vid->linkedVid() &&
                        childVid->hasWeaponChildDescriptor() != 0u &&
                        childVid->weaponCount() != 0u &&
                        m_goalSprite != nullptr &&
                        child->m_goalSprite == nullptr)
                    {
                        child->SetCommand(4, m_goalSprite);
                    }
                }
            }

            int decision = 0;
            SPRITE* const linkedWeaponChild = m_childChain;
            if (!(linkedWeaponChild != nullptr &&
                  linkedWeaponChild->m_vid == m_vid->linkedVid() &&
                  linkedWeaponChild->m_vid->hasWeaponChildDescriptor() != 0u &&
                  linkedWeaponChild->m_vid->weaponCount() != 0u) &&
                turnTimer() != 0)
            {
                decision = 2;
            }
            else
            {
                std::uint32_t delta = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                std::uint32_t decisionDelta = static_cast<std::uint32_t>(
                    m_vid->frameSpeed[m_currentAnimation]);
                if (delta > decisionDelta)
                    decisionDelta = delta;

                decision = AttackTact(decisionDelta);
                setAttackDecisionCode(decision);
            }

            if (decision == 7)
            {
                SetCommand(0, nullptr);
            }
            else if (decision == 1)
            {
                const float runtimeMaxSpeed = MaxSpeed();
                if (runtimeMaxSpeed == 0.0f || std::isnan(runtimeMaxSpeed))
                {
                    SPRITE* const child = m_childChain;
                    if (child)
                    {
                        VID* const childVid = child->m_vid;
                        if (childVid == m_vid->linkedVid() &&
                            childVid->hasWeaponChildDescriptor() != 0u &&
                            childVid->weaponCount() != 0u &&
                            child->m_goalSprite != nullptr)
                        {
                            SetCommand(0, nullptr);
                        }
                        else if (m_goalSprite != nullptr)
                        {
                            SetCommand(0, nullptr);
                        }
                    }
                    else if (m_goalSprite != nullptr)
                    {
                        SetCommand(0, nullptr);
                    }
                }
                else if (m_speed == 0.0f || std::isnan(m_speed))
                {
                    StartMove();
                }

                const DWORD postBits = m_runtimeFlags & SPRITE::CommandBitsMask;
                if (postBits == 0x0Cu || postBits == 0x10u)
                {
                    SPRITE* const child = m_childChain;
                    if (child)
                    {
                        VID* const childVid = child->m_vid;
                        if (childVid == m_vid->linkedVid() &&
                            childVid->hasWeaponChildDescriptor() != 0u &&
                            childVid->weaponCount() != 0u &&
                            (behaviorFlags() & 1) != 0)
                        {
                            if (child->m_actionTimer != 0u || (std::rand() % 4) == 0)
                            {
                                if (SPRITE* const target = SeekEnemy())
                                    child->SetCommand(4, target);
                            }
                        }
                    }
                }
            }
            else if (decision == 0)
            {


                if (m_speed != 0.0f && !std::isnan(m_speed))
                {
                    bool stopMoving = true;
                    SPRITE* const child = m_childChain;
                    if (child)
                    {
                        VID* const childVid = child->m_vid;
                        if (childVid == m_vid->linkedVid() &&
                            childVid->hasWeaponChildDescriptor() != 0u &&
                            childVid->weaponCount() != 0u &&
                            m_goalSprite != child->m_goalSprite)
                        {
                            stopMoving = false;
                        }
                    }

                    if (stopMoving)
                        Stop();
                }
            }
            else if (decision == 3)
            {
                SetCommand(0, nullptr);
            }
            else if (decision == 2 &&
                     (behaviorFlags() & 2) != 0 &&
                     (m_speed == 0.0f || std::isnan(m_speed)))
            {
                StartMove();
            }

            if (decision == 6)
            {
                const int behavior = behaviorFlags();
                if ((behavior & 1) != 0)
                {
                    if ((behavior & 2) != 0)
                    {
                        if (SPRITE* const target = SeekEnemy())
                            SetCommand(4, target);
                    }
                    else
                    {
                        SPRITE* const child = m_childChain;
                        if (child)
                        {
                            VID* const childVid = child->m_vid;
                            if (childVid == m_vid->linkedVid() &&
                                childVid->hasWeaponChildDescriptor() != 0u &&
                                childVid->weaponCount() != 0u)
                            {
                                if (SPRITE* const target = SeekEnemy())
                                    child->SetCommand(4, target);
                            }
                        }
                    }
                }
            }

            if (decision == 2 || decision == 5)
            {
                if ((behaviorFlags() & 1) != 0)
                {
                    const DWORD postBits = m_runtimeFlags & SPRITE::CommandBitsMask;
                    if (postBits == 0u || postBits == 4u || postBits == 0x10u)
                    {


                        bool acquire = m_currentFrameEnd > m_currentFrameBegin;
                        if (!acquire)
                        {
                            SPRITE* const child = m_childChain;
                            std::uint32_t timer = m_actionTimer;
                            if (child)
                            {
                                VID* const childVid = child->m_vid;
                                if (childVid == m_vid->linkedVid() &&
                                    childVid->hasWeaponChildDescriptor() != 0u &&
                                    childVid->weaponCount() != 0u)
                                {
                                    timer = child->m_actionTimer;
                                }
                            }
                            acquire = timer != 0u || (std::rand() % 11) == 0;
                        }

                        if (acquire)
                        {
                            if (SPRITE* const target = SeekEnemy())
                                SetCommand(4, target);
                        }
                    }
                }
            }

            if ((m_runtimeFlags & SPRITE::CommandBitsMask) != 0u ||
                m_goalSprite != nullptr ||
                m_actionTimer != 0u)
            {
                return 0;
            }

            Stop();
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_DAMAGE):
        {


            const int result = TERRAIN::Action(
                op, static_cast<std::intptr_t>(argument1), argument2, argument3);

            if (argument1 >= 0 &&
                (m_runtimeFlags & SPRITE::CommandBitsMask) == 0u)
            {
                const float runtimeMaxSpeed = MaxSpeed();
                if (runtimeMaxSpeed != 0.0f && !std::isnan(runtimeMaxSpeed))
                {
                    const int randomDirection = std::rand() & 0xFF;
                    constexpr float kDamageMoveDistance = 64.0f;
                    const float x = X() + directionSinValue(randomDirection) * kDamageMoveDistance;
                    const float y = Y() - directionCosValue(randomDirection) * kDamageMoveDistance;
                    SPRITE* const helper = new (std::nothrow) SPRITE(
                        EmptyVid, x, y, Z(),
                        ANGLE(static_cast<unsigned char>(0)), nullptr);
                    Move(helper);
                }
            }
            return result;
        }

        case static_cast<int>(ActionCode::ACT_REPAIR):
            setAmmoFixedPoint(static_cast<std::int32_t>(static_cast<std::uint32_t>(m_vid->GetMaxAmmo()) << 6));
            return TERRAIN::Action(op, static_cast<std::intptr_t>(argument1), argument2, argument3);

        case static_cast<int>(ActionCode::ACT_SAVE):
        {
            TERRAIN::Action(op, static_cast<std::intptr_t>(argument1), argument2, argument3);
            BaseStream* const stream = reinterpret_cast<BaseStream*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            const int behaviorState = behaviorFlags();
            stream->write(&behaviorState, 4u);

            return 0;
        }

        case static_cast<int>(ActionCode::ACT_RESTORE):
        {
            TERRAIN::Action(op, static_cast<std::intptr_t>(argument1), argument2, argument3);
            BaseStream* const stream = reinterpret_cast<BaseStream*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            int behaviorState = 0;
            stream->read(&behaviorState, 4u);
            setBehaviorFlags(behaviorState);

            const int mapVersion = argument2;
            if (mapVersion == 11)
            {
                std::int32_t count = 0;
                stream->read(&count, 4u);
                const std::uint32_t byteCount = static_cast<std::uint32_t>(count) << 2u;
                std::int32_t* values = nullptr;
                if (count > 0)
                {
                    values = static_cast<std::int32_t*>(
                        ::operator new(static_cast<std::size_t>(byteCount), std::nothrow));
                    if (!values)
                        fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", count);
                }

                stream->read(values, byteCount);
                for (std::int32_t i = 0; i < count; ++i)
                    InsertItem(values[i]);

                if (values)
                    ::operator delete(values);
            }
            else if (mapVersion < 11)
            {
                std::int32_t count = 0;
                stream->read(&count, 4u);
                const std::uint32_t byteCount = static_cast<std::uint32_t>(count) << 1u;
                std::int16_t* values = nullptr;
                if (count > 0)
                {
                    values = static_cast<std::int16_t*>(
                        ::operator new(static_cast<std::size_t>(byteCount), std::nothrow));
                    if (!values)
                        fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", count);
                }

                stream->read(values, byteCount);
                for (std::int32_t i = 0; i < count; ++i)
                    InsertItem(static_cast<std::int32_t>(values[i]));

                if (values)
                    ::operator delete(values);
            }
            return 0;
        }

        case SpriteActConst::ACT_RESTORE_OLD_MAP:
        {
            TERRAIN::Action(op, static_cast<std::intptr_t>(argument1), argument2, argument3);
            BaseStream* const stream = reinterpret_cast<BaseStream*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            const int mapVersion = argument2;
            if (mapVersion < 7)
            {
                std::uint8_t bucket = 0;
                stream->read(&bucket, 1u);
                ChangeArmy(static_cast<int>(bucket));
            }

            std::uint8_t behaviorState = 0;
            stream->read(&behaviorState, 1u);
            setBehaviorFlags(static_cast<int>(behaviorState));
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_SET_ARMY):
        {


            const int previousArmy = armyIndex();
            ChangeArmy(argument1);
            if (previousArmy != armyIndex() && m_vid->nvid() == 104)
            {
                const int selfArgument = static_cast<int>(static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(this)));
                (void)core::Application::callScriptFunction(
                    core::EvFunctionNumber[15u], selfArgument, 0, 0);
            }
            return 0;
        }

        default:
            return TERRAIN::Action(op, static_cast<std::intptr_t>(argument1), argument2, argument3);
        }
    }

    __declspec(safebuffers)

    void UNIT::MoveTact()
    {
        VID* const vid = Vid();
        if (vid->spriteClassId() != B_UNIT && vid->spriteClassId() != B_AVIA)
        {
            SPRITE::MoveTact();
            return;
        }

        VECTOR candidate{X(), Y(), Z()};
        computeNextMovementPosition(&candidate.x, &candidate.y, &candidate.z);

        const std::uint32_t deltaMs =
            core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();

        int remainingTurn = turnTimer();
        if (remainingTurn != 0)
        {
            const int fps = static_cast<int>(core::DisplayedFramesPerSecond());
            if (std::abs(remainingTurn) > fps)
            {
                const int halfFps = fps / 2;
                remainingTurn = remainingTurn > 0 ? halfFps : -halfFps;
                setTurnTimer(remainingTurn);
            }

            const unsigned char offset = remainingTurn > 0 ? 0x40u : 0xC0u;
            const ANGLE desired(static_cast<unsigned char>(Direction().Int() + offset));
            RotateTact(GlideDirection(desired), deltaMs);

            remainingTurn = turnTimer();
            setTurnTimer(remainingTurn < 0 ? remainingTurn + 1 : remainingTurn - 1);
        }
        else if (SPRITE* const target = Goal())
        {
            const float speed = Speed();
            if (speed != 0.0f && !std::isnan(speed))
            {
                const unsigned char reverse = speed < 0.0f ? 0x80u : 0u;
                ANGLE desired = DirectionTo(target);
                desired = ANGLE(static_cast<unsigned char>(desired.Int() + reverse));

                RotateTact(GlideDirection(desired), deltaMs);

                if ((runtimeFlags() & SPRITE::CrossedGoalAxesMask) == SPRITE::CrossedGoalAxesMask)
                    Stop();
            }
        }

        const float currentX = X();
        const float currentY = Y();
        const float currentZ = Z();
        const bool positionChanged =
            !(currentX == candidate.x || std::isnan(currentX) || std::isnan(candidate.x)) ||
            !(currentY == candidate.y || std::isnan(currentY) || std::isnan(candidate.y)) ||
            !(currentZ == candidate.z || std::isnan(currentZ) || std::isnan(candidate.z));

        if (!positionChanged)
            return;

        if (CanPlaceWithCrushAndGlide(&candidate.x, &candidate.y, &candidate.z) == nullptr)
        {
            steerAwayFromMapBoundary(candidate.x, candidate.y);
            ChangeCoor(candidate.x, candidate.y, candidate.z);
            return;
        }

        if (turnTimer() == 0)
        {
            const int halfFps = static_cast<int>(core::DisplayedFramesPerSecond()) / 2;
            setTurnTimer((std::rand() % 2) != 0 ? halfFps : -halfFps);
        }
    }

    void UNIT::DrawDebugOverlay()
    {

        SPRITE::DrawDebugOverlay();
    }

}
