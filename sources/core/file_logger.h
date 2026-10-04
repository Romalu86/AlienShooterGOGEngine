#pragma once
#include "types.h"
#include "as_string.h"
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace as1
{

    class FileLogger
    {
    public:
        explicit FileLogger(bool rewriteLog);
        virtual ~FileLogger();

        bool IsOpen() const { return fileHandle() != nullptr; }
        const char* Path() const { return pathStorage(); }



        __forceinline std::uint32_t messageWindowToken() const { return m_messageWindowToken; }
        __forceinline std::uint32_t& mutableMessageWindowToken() { return m_messageWindowToken; }
        __forceinline FILE* fileHandle() const { return m_file; }
        __forceinline FILE*& mutableFileHandle() { return m_file; }
        __forceinline void clearFileHandle() { m_file = nullptr; }
        __forceinline const char* pathStorage() const { return m_path; }
        __forceinline char* mutablePathStorage() { return m_path; }
    private:
        static constexpr size_t PathCapacity = 0x400;
        static constexpr size_t MessageCapacity = 1024;


        std::uint32_t m_messageWindowToken;
        FILE* m_file;
        char m_path[PathCapacity];
    };

#if defined(_M_IX86) || defined(__i386__)
#endif

    extern FileLogger* g_fileLogger;
    extern char* g_executablePath;
    std::intptr_t logFileLoggerResourceError(FileLogger* logger, const char* contextFormat, int errorCode, const char* detailText, int detailValue, ...);
}
