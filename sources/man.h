#pragma once

#include "unit.h"

namespace as1
{

    class MAN final : public UNIT
    {
    public:
        MAN(VID* vid, float x, float y, float z, ANGLE direction, SPRITE* parent = nullptr);
        ~MAN() override;
        int Action(int opcode, std::intptr_t argument1Payload, int argument2Value, int argument3Value) override;
        void MoveTact() override;

        int ChangeWeapon(int weapon) noexcept;

    private:
        friend class SPRITE;
        int weaponAmmo[10];
    };

#if defined(_M_IX86)
    static_assert(sizeof(MAN) == 0xB8, "MAN size");
#endif

}
