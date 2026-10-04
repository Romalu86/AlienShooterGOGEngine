#pragma once

#include "unit.h"

#include <array>
#include <cstdint>

namespace as1
{
    class DEPO : public UNIT
    {
    public:
        DEPO(VID* vid, float x, float y, float z, ANGLE direction, SPRITE* parent = nullptr);
        ~DEPO() override;
        int Action(int opcode, std::intptr_t argument1Payload, int argument2Value, int argument3Value) override;
        void MoveTact() override;
        void AddUnitToQueue(int nvid) noexcept;
        void BuildNextUnit() noexcept;
        int ActionBuildUnit(int actionArgument1, int actionArgument2) noexcept;


    private:
        std::uint32_t m_createdEngineSequence;
        std::array<std::uint16_t, 20> m_queuedNvids;
        std::array<std::uint8_t, 0xA0> m_paddingAfterQueuedNvids;
        std::array<std::uint32_t, 20> m_queuedBuildTimes;
        std::array<std::uint8_t, 0x140> m_paddingAfterBuildTimes;
        std::array<std::uint32_t, 20> m_queuedCompletionFlags;
        std::array<std::uint8_t, 0x140> m_paddingAfterCompletionFlags;
        std::uint32_t m_activeQueueCursor;
        std::uint32_t m_queueCapacity;
        std::uint32_t m_queueCount;
    };


#if defined(_M_IX86)
    static_assert(sizeof(DEPO) == 0x488, "DEPO size");
#endif

}
