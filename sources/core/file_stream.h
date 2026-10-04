#pragma once
#include "base_stream.h"
#include <cstdio>
#include <string>
#include <io.h>

namespace as1
{
    class FSTREAM final : public BaseStream
    {
    public:
        FSTREAM() = default;
        FSTREAM(const std::string& path, const char* mode);
        __forceinline ~FSTREAM() override
        {
            if (m_file)
                std::fclose(m_file);
        }

        __forceinline void close()
        {
            if (m_file)
            {
                std::fclose(m_file);
                m_file = nullptr;
            }
        }
        bool isOpen() const { return m_file != nullptr; }
        __forceinline bool isWritable() const { return m_file != nullptr; }
        __forceinline size_t seek(size_t pos)
        {
            if (!m_file)
                return 0;
            std::fseek(m_file, static_cast<long>(pos), SEEK_SET);
            const long current = std::ftell(m_file);
            return current >= 0 ? static_cast<size_t>(current) : 0;
        }
        __forceinline size_t shift(int delta)
        {
            if (!m_file)
                return 0;
            std::fseek(m_file, delta, SEEK_CUR);
            return position();
        }
        __forceinline size_t position() const
        {
            if (!m_file)
                return 0;
            const long current = std::ftell(m_file);
            return current >= 0 ? static_cast<size_t>(current) : 0;
        }
        __forceinline size_t length() const
        {
            if (!m_file)
                return 0;
            const int fd = _fileno(m_file);
            if (fd < 0)
                return 0;
            const long value = _filelength(fd);
            return value > 0 ? static_cast<size_t>(value) : 0;
        }

        int read(void* buf, unsigned size) override;
        int write(const void* buf, unsigned size) override;

        std::FILE* nativeFile() const { return m_file; }

    private:

        std::FILE* m_file = nullptr;
    };


}
