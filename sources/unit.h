#pragma once

#include "sprite.h"

namespace as1
{
    class UNIT : public TERRAIN
    {
    public:
        UNIT(VID* vid, float x, float y, float z, ANGLE direction, SPRITE* parent = nullptr);
        ~UNIT() override;
        int Action(int opcode, std::intptr_t argument1Payload, int argument2Value, int argument3Value) override;
        void MoveTact() override;
        void DrawDebugOverlay() override;

    private:
        friend class MAN;
        int m_sharedPrimaryState;
        int m_sharedSecondaryState;
        int m_ammoFixedPoint;
        int m_commandStateSentinel;
        int m_turnTimer;
        int m_behaviorFlags;
    };

#if defined(_M_IX86)
    static_assert(sizeof(UNIT) == 0x90, "UNIT size");
#endif

}
