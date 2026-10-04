#include "file_logger.h"
#include "log.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdlib.h>

namespace as1
{
    namespace
    {
        __forceinline std::intptr_t formatResourceErrorLog(FileLogger* logger,
                                      const char* contextFormat,
                                      int errorCode,
                                      const char* detailText,
                                      int detailValue,
                                      va_list contextArgs);
    }

    FileLogger::FileLogger(bool rewriteLog)
    {
        m_messageWindowToken = 0;
        m_file = nullptr;
        m_path[0] = '\0';

        STRING logName;
        if (rewriteLog)
        {
            STRING date;
            constructCurrentDateString(date);
            STRING datedName("logs\\error", date.c_str());
            datedName += " ";

            STRING time;
            constructCurrentTimeString(time);
            datedName += time.c_str();
            datedName += ".log";
            datedName.Replace(":", "h");
            datedName.Replace(":", "m");
            logName = datedName;
        }
        else
        {
            logName = "logs\\error.log";
        }

        const char* const mode = rewriteLog ? "wt" : "at";
        mutableFileHandle() = FOpen(&logName, mode);
        if (!fileHandle())
        {
            STRING fallback = logName.After("logs\\");
            mutableFileHandle() = FOpen(&fallback, mode);
            logName = fallback;
        }

        std::strcpy(mutablePathStorage(), logName.c_str());

        STRING executablePath((g_executablePath ? g_executablePath : ""));
        STRING executableTimes;
        constructFileTimestampString(executableTimes, executablePath);
        STRING date;
        constructCurrentDateString(date);
        STRING time;
        constructCurrentTimeString(time);
        writeLogLine(this, "----< %s %s >----< %s (%s) >----",
                     date.c_str(), time.c_str(), executablePath.c_str(), executableTimes.c_str());
    }

    FileLogger* g_fileLogger = nullptr;
    char* g_executablePath = nullptr;

    namespace
    {

        __forceinline std::intptr_t formatResourceErrorLog(FileLogger* logger,
                                      const char* contextFormat,
                                      int errorCode,
                                      const char* detailText,
                                      int detailValue,
                                      va_list contextArgs)
        {

            STRING timestampText;
            constructCurrentTimeString(timestampText);

            char messageBuffer[0x400] = {};
            std::sprintf(messageBuffer, "!!!ERROR %s!!!", timestampText.c_str());

            timestampText.ReleaseOwnedStorage();

            char* const contextWrite = messageBuffer + std::strlen(messageBuffer);
            std::vsprintf(contextWrite, contextFormat, contextArgs);

            std::strcat(messageBuffer, ": ");

            const char* suffix = nullptr;
            switch (errorCode)
            {
            case 0: suffix = "0x%X Couldn't lock %s"; break;
            case 1: suffix = "0x%X Couldn't copy %s"; break;
            case 2: suffix = "%i There was not enough memory for %s"; break;
            case 3: suffix = "0x%X Couldn't create the %s"; break;
            case 4: suffix = "0x%X Invalid %s"; break;
            case 5: suffix = "0x%X Load %s"; break;
            case 6: suffix = "0x%X Save %s"; break;
            case 7: suffix = "0x%X Couldn't open '%s'"; break;
            case 8: suffix = "0x%X Couldn't set the %s"; break;
            case 9: suffix = "0x%X Couldn't get the %s"; break;
            case 10: suffix = "%i %s"; break;
            case 11: suffix = "0x%X Section can't found (%s)"; break;
            case 12: suffix = "0x%X Unable initialize %s"; break;
            case 13: suffix = "%i Missing %s"; break;
            case 14: suffix = "%i Unknownn %s"; break;
            default: break;
            }
            if (suffix)
                std::strcat(messageBuffer, suffix);


            return writeLogLine(logger, messageBuffer, detailValue, detailText);
        }
    }

    std::intptr_t logFileLoggerResourceError(FileLogger* logger, const char* contextFormat, int errorCode, const char* detailText, int detailValue, ...)
    {
        va_list args;
        va_start(args, detailValue);
        const std::intptr_t result = formatResourceErrorLog(logger, contextFormat, errorCode, detailText, detailValue, args);
        va_end(args);
        return result;
    }

    FileLogger::~FileLogger()
    {
        FILE* file = fileHandle();
        if (file)
            std::fclose(file);
        clearFileHandle();

        const char* const active = pathStorage();
        if (std::strcmp(active, "logs\\error.log") == 0 ||
            std::strcmp(active, "error.log") == 0)
            return;

        ::DeleteFileA("logs\\error.log");
        if (!::MoveFileA(active, "logs\\error.log"))
        {
            ::DeleteFileA("error.log");
            (void)::MoveFileA(active, "error.log");
        }
    }

}
