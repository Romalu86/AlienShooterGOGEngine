#pragma once

#include "unit.h"

namespace as1
{
    class AVIA : public UNIT
    {
    public:
        AVIA(VID* vid, float x, float y, float z, ANGLE direction, SPRITE* parent = nullptr);
        int Action(int opcode, std::intptr_t argument1Payload, int argument2Value, int argument3Value) override;


        virtual void updateFlightBehavior(int behaviorArgument1 = 0, int behaviorArgument2 = 0) noexcept;
        virtual void updateAltitudeState() noexcept;
        virtual void updateFlightAuxiliaryBehavior() noexcept;
        virtual int probeForwardFlightObstacle() noexcept;
        virtual void chooseFlightAvoidanceTurn() noexcept;
        virtual void faceFlightTargetAndUpdateCombat() noexcept;
        virtual void updateFlightCombatBehavior() noexcept;
        virtual void updateFlightIdleBehavior() noexcept;
    };

#if defined(_M_IX86)
    static_assert(sizeof(AVIA) == 0x90, "AVIA size");
#endif

}
