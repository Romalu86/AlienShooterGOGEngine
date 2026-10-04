#pragma once

#include "unit.h"

namespace as1
{
    class BUILDING : public UNIT
    {
    public:
        BUILDING(VID* vid, float x, float y, float z, ANGLE direction, SPRITE* parent = nullptr);
        ~BUILDING() override = default;
        int Action(int opcode, std::intptr_t argument1Payload, int argument2Value, int argument3Value) override;
        void MoveTact() override;

        std::uintptr_t pendingBuildVidHandle() const noexcept { return m_pendingBuildVidHandle; }
        std::uint32_t serviceActivityFlag() const noexcept { return m_serviceActivityFlag; }
        std::uint32_t lastServiceTargetHandle() const noexcept { return m_lastServiceTargetHandle; }

    private:
        std::uintptr_t m_pendingBuildVidHandle;
        int m_buildPositionX;
        int m_buildPositionY;
        std::uint32_t m_serviceActivityFlag;
        std::uint32_t m_lastServiceTargetHandle;
    };


#if defined(_M_IX86)
    static_assert(sizeof(BUILDING) == 0xA4, "BUILDING size");
#endif

}
