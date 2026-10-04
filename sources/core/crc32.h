#pragma once

#include <cstddef>
#include <cstdint>

namespace as1
{
    class Crc32
    {
    public:
        Crc32() = default;
        Crc32(const void* data, unsigned int size);


        std::uint32_t add(const void* data, int size);
        operator unsigned int() const;

    private:
        std::uint32_t m_crc = 0;
    };

}
