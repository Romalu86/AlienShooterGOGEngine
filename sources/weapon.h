#pragma once

#include "core/types.h"

#include <array>
#include <cstddef>

namespace as1
{


    struct WEAPON
    {


        static constexpr std::size_t RECORD_SIZE = 0x264u;
        static constexpr std::size_t SERIALIZED_RECORD_SIZE = RECORD_SIZE;
        static constexpr std::size_t RUNTIME_RECORD_SIZE = RECORD_SIZE;

        std::array<BYTE, RECORD_SIZE> recordData{};
    };

#if defined(_M_IX86) || defined(__i386__)
#endif

}
