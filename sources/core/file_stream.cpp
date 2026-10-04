#include "file_stream.h"

#include <io.h>

namespace as1
{

    FSTREAM::FSTREAM(const std::string& path, const char* modeText)
    {
        m_file = path.empty() ? nullptr : std::fopen(path.c_str(), modeText ? modeText : "rb");
    }

    int FSTREAM::read(void* buf, unsigned size)
    {
        return static_cast<int>(size - std::fread(buf, 1, size, m_file));
    }

    int FSTREAM::write(const void* buf, unsigned size)
    {
        return static_cast<int>(size - std::fwrite(buf, 1, size, m_file));
    }
}
