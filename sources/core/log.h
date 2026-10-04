#pragma once
#include "types.h"
#include <cstdarg>
#include <cstdint>

namespace as1
{
    class FileLogger;


    std::intptr_t logAndShowError(FileLogger* logger, const char* format, ...);


    std::intptr_t rewriteLogLine(FileLogger* logger, const char* format, ...);


    std::intptr_t writeLogLine(FileLogger* logger, const char* format, ...);


    [[noreturn]] void fatalLogError(FileLogger* logger, const char* format, ...);


    std::intptr_t logFileLoggerResourceError(FileLogger* logger, const char* contextFormat, int errorCode, const char* detailText, int detailValue, ...);

    extern FileLogger* g_fileLogger;
}
