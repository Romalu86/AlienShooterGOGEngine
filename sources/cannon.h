#pragma once

#include "sprite.h"

namespace as1
{
    class CANNON : public SPRITE
    {
    public:
        CANNON(VID* vid, float x, float y, float z, ANGLE direction, SPRITE* parent = nullptr);
        ~CANNON() override = default;
        int Action(int opcode, std::intptr_t argument1Payload, int argument2Value, int argument3Value) override;
        void MoveTact() override;
        void DeletePointerToSprite(SPRITE* sprite) override;

    private:
        int m_cannonMotionFlags;
    };
#if defined(_M_IX86)
    static_assert(sizeof(CANNON) == 0x74, "CANNON size");
#endif

}
