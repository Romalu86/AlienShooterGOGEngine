#include "avia.h"

#include "core/application.h"
#include "graphics/angle.h"
#include "map.h"
#include "vid/vid.h"

#include <algorithm>
#include <cstdlib>
#include <new>

namespace as1
{
    namespace
    {
        std::uint32_t flightFrameDeltaMilliseconds(const VID* vid, int animation) noexcept
        {
            const std::uint32_t delta =
                core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
            const std::uint32_t frameSpeed =
                static_cast<std::uint32_t>(vid->frameSpeed[animation]);
            return std::max(delta, frameSpeed);
        }
    }

    AVIA::AVIA(VID* vid, float x, float y, float z, ANGLE direction, SPRITE* parent)
        : UNIT(vid, x, y, z, direction, parent)
    {
        StartMove();
        setZSpeedDirect(Vid()->maximumZSpeed());
    }

    int AVIA::Action(int opcode, std::intptr_t argument1Payload, int argument2Value, int argument3Value)
    {
        switch (opcode)
        {
        case 0x82:
            updateFlightBehavior(static_cast<int>(argument1Payload), argument2Value);
            return 0;

        case 0x21:
        {
            const float targetX = static_cast<float>(static_cast<int>(argument1Payload));
            const float targetY = static_cast<float>(argument2Value);
            MAP* const map = mapOwner();
            const float targetZ = map->GetGroundZ(Vid(), VECTOR2{targetX, targetY}) + 80.0f;
            SPRITE* const helper = new (std::nothrow) SPRITE(
                EmptyVid, targetX, targetY, targetZ, ANGLE(static_cast<unsigned char>(0)), nullptr);
            Move(helper);
            return 0;
        }

        case 0x55:
            return TERRAIN::Action(opcode, argument1Payload, argument2Value, argument3Value);

        default:
            return UNIT::Action(opcode, argument1Payload, argument2Value, argument3Value);
        }
    }

    void AVIA::updateFlightIdleBehavior() noexcept
    {
        const int animation = Animation();
        if (animation == 4)
        {
            if ((std::rand() % 3) == 0)
                ChangeAnimation(2);
            else
                ChangeDirection(directionIndex() - 0x20);
        }
        else if (animation == 5)
        {
            if ((std::rand() % 3) == 0)
                ChangeAnimation(2);
            else
                ChangeDirection(directionIndex() + 0x20);
        }
        else if (animation == 2)
        {
            if ((std::rand() % 11) == 0)
                ChangeAnimation((std::rand() % 2) == 0 ? 5 : 4);
        }
        else
        {
            ChangeAnimation(2);
        }

        const int randomValue = std::rand();
        if ((randomValue % 21) == 0 && (behaviorFlags() & 1) != 0)
        {
            if (SPRITE* const target = SeekEnemy())
                (void)SetCommand(4, target);
        }
    }

    void AVIA::updateFlightCombatBehavior() noexcept
    {
        const std::uint32_t deltaMs = flightFrameDeltaMilliseconds(Vid(), Animation());
        const int result = AttackTact(static_cast<int>(deltaMs));
        setAttackDecisionCode(result);

        if ((behaviorFlags() & 1) == 0)
            return;

        if (result == 6 && (std::rand() % 21) != 0)
            return;

        if (SPRITE* const target = SeekEnemy())
            (void)SetCommand(4, target);
    }

    void AVIA::faceFlightTargetAndUpdateCombat() noexcept
    {
        SPRITE* const target = Goal();
        const std::uint32_t deltaMs = flightFrameDeltaMilliseconds(Vid(), Animation());
        const int direction = DirectionFromFloatXY(target->X() - X(), target->Y() - Y()).Int();
        const int turnResult = RotateTact(
            ANGLE(static_cast<unsigned char>(direction)), deltaMs).Int();

        if (turnResult == 0)
            ChangeAnimation(2);

        updateFlightCombatBehavior();
    }

    void AVIA::updateAltitudeState() noexcept
    {
        VID* const vid = Vid();
        const float maxZSpeed = vid->maximumZSpeed();

        const float lowerGround = mapOwner()->GetGroundZ(vid, VECTOR2{X(), Y()});
        const float lower = (vid->moveUpZ() + lowerGround) - 10.0f;
        if (lower > Z())
        {
            setZSpeedDirect(maxZSpeed);
            return;
        }

        const float upperGround = mapOwner()->GetGroundZ(vid, VECTOR2{X(), Y()});
        const float upper = (vid->moveUpZ() + upperGround) + 10.0f;
        if (Z() > upper)
        {
            setZSpeedDirect(-maxZSpeed);
            return;
        }

        setZSpeedDirect(0.0f);
    }

    void AVIA::updateFlightAuxiliaryBehavior() noexcept
    {
    }

    int AVIA::probeForwardFlightObstacle() noexcept
    {
        const DWORD direction = static_cast<DWORD>(directionIndex());
        const float x = X() + directionSinUncheckedValue(direction) * 128.0f;
        const float y = Y() - directionCosValue(static_cast<int>(direction)) * 128.0f;
        return probeMovementFootprint(x, y) ? 1 : 0;
    }

    void AVIA::chooseFlightAvoidanceTurn() noexcept
    {
        const int probeDirection = (directionIndex() + 0x10) & 0xFF;
        const float x = X() + directionSinValue(probeDirection) * 128.0f;
        const float y = Y() - directionCosValue(probeDirection) * 128.0f;
        if (probeMovementFootprint(x, y))
        {
            ChangeAnimation(5);
            ChangeDirection(directionIndex() + 0x10);
            return;
        }

        ChangeAnimation(4);
        ChangeDirection(directionIndex() - 0x10);
    }

    void AVIA::updateFlightBehavior(int behaviorArgument1, int behaviorArgument2) noexcept
    {
        (void)behaviorArgument1;
        (void)behaviorArgument2;

        const int animation = Animation();
        if (animation >= 0x0F || animation == 0x0C)
            return;

        if (animation >= 7 && animation != 0x0A)
            ChangeAnimation(0);

        updateAltitudeState();
        updateFlightAuxiliaryBehavior();

        if (childBacklink())
            return;

        if ((runtimeFlags() & 0x00000080u) == 0u)
            setRuntimeFlags(runtimeFlags() | 0x00000080u);

        if (Goal() != nullptr)
            updateFlightCombatBehavior();
        else
            updateFlightIdleBehavior();
    }
}
