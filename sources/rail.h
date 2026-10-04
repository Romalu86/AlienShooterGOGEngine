#pragma once

#include "sprite.h"
#include "core/weak_controller.h"

namespace as1
{
    class RAIL : public TERRAIN
    {
    public:
        RAIL(VID* vid, float x, float y, float z, ANGLE direction, SPRITE* parent = nullptr);
        ~RAIL() override;
        int Action(int opcode, std::intptr_t argument1Payload, int argument2Value, int argument3Value) override;
        core::R_DOT* firstRailNode() const noexcept { return m_firstRailNode; }
        core::R_DOT* secondRailNode() const noexcept { return m_secondRailNode; }

    private:

        core::R_DOT* m_firstRailNode = nullptr;
        core::R_DOT* m_secondRailNode = nullptr;
    };
#if defined(_M_IX86)
    static_assert(sizeof(RAIL) == 0x80, "RAIL size");
#endif

}
