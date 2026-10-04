#include "core/configuration.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "core/application.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <new>
#include <cstdint>
#include <cstring>
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <stdlib.h>
#include <objbase.h>
#include "win/resources/resource.h"
#include "win/main_sw.h"
#include "win/application_win.h"
#include "input.h"
#include "mouse.h"
#include "sound/sound_engine.h"

namespace
{


    int topLevelExceptionFilter(const EXCEPTION_POINTERS* exceptionPointers)
    {
        const EXCEPTION_RECORD* const record =
            exceptionPointers ? exceptionPointers->ExceptionRecord : nullptr;
        if (!record)
            return EXCEPTION_EXECUTE_HANDLER;

        const auto logException = [&](const char* format, std::uintptr_t extra = 0u, bool hasExtra = false)
        {
            if (!as1::g_fileLogger || !as1::g_fileLogger->fileHandle())
                return;
            const unsigned int address = static_cast<unsigned int>(
                reinterpret_cast<std::uintptr_t>(record->ExceptionAddress));
            if (hasExtra)
                as1::writeLogLine(as1::g_fileLogger, format, address, static_cast<unsigned int>(extra));
            else
                as1::writeLogLine(as1::g_fileLogger, format, address);
        };

        switch (record->ExceptionCode)
        {
        case EXCEPTION_ACCESS_VIOLATION:
            if (record->NumberParameters >= 2)
            {
                const bool writeAccess = record->ExceptionInformation[0] != 0;
                logException(writeAccess
                    ? "!!!ERROR EXCEPTION 0x%X!!!: Access violation write to 0x%X"
                    : "!!!ERROR EXCEPTION 0x%X!!!: Access violation read from 0x%X",
                    static_cast<std::uintptr_t>(record->ExceptionInformation[1]), true);
            }
            break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_ARRAY_BOUNDS_EXCEEDED");
            break;
        case EXCEPTION_BREAKPOINT:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_BREAKPOINT");
            break;
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_DATATYPE_MISALIGNMENT");
            break;
        case EXCEPTION_FLT_DENORMAL_OPERAND:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_FLT_DENORMAL_OPERAND");
            break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            logException("!!!ERROR EXCEPTION 0x%X!!!:FLT divide by zero");
            break;
        case EXCEPTION_FLT_INEXACT_RESULT:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_FLT_INEXACT_RESULT");
            break;
        case EXCEPTION_FLT_INVALID_OPERATION:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_FLT_INVALID_OPERATION");
            break;
        case EXCEPTION_FLT_OVERFLOW:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_FLT_OVERFLOW");
            break;
        case EXCEPTION_FLT_STACK_CHECK:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_FLT_STACK_CHECK");
            break;
        case EXCEPTION_FLT_UNDERFLOW:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_FLT_UNDERFLOW");
            break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_ILLEGAL_INSTRUCTION");
            break;
        case EXCEPTION_IN_PAGE_ERROR:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_IN_PAGE_ERROR");
            break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            logException("!!!ERROR EXCEPTION 0x%X!!!:INT divide by zero");
            break;
        case EXCEPTION_INT_OVERFLOW:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_INT_OVERFLOW");
            break;
        case EXCEPTION_INVALID_DISPOSITION:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_INVALID_DISPOSITION");
            break;
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_NONCONTINUABLE_EXCEPTION");
            break;
        case EXCEPTION_PRIV_INSTRUCTION:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_PRIV_INSTRUCTION");
            break;
        case EXCEPTION_SINGLE_STEP:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_SINGLE_STEP");
            break;
        case EXCEPTION_STACK_OVERFLOW:
            logException("!!!ERROR EXCEPTION 0x%X!!!: EXCEPTION_STACK_OVERFLOW");
            break;
        default:
            break;
        }
        return EXCEPTION_EXECUTE_HANDLER;
    }


    int runWin32Application(HINSTANCE instance, HINSTANCE previousInstance, LPSTR commandLine, int showCmd)
    {
        char* programPath = nullptr;
        as1::g_executablePath = (::_get_pgmptr(&programPath) == 0) ? programPath : nullptr;

        as1::win::ApplicationWinInit shellInit{};
        shellInit.hInstance = instance;
        shellInit.previousInstance = previousInstance;
        shellInit.commandLine = commandLine ? commandLine : "";
        shellInit.showCmd = showCmd;
        void* const storage = ::operator new(as1::core::application_layout::ObjectSize, std::nothrow);
        as1::win::ApplicationWin* applicationShell = storage
            ? new (storage) as1::win::ApplicationWin(shellInit)
            : nullptr;
        if (applicationShell)
            as1::core::g_applicationOwner = applicationShell;
        if (!applicationShell)
            return 0;


        as1::STRING commandLineString(commandLine);
        char* commandLineStorage = commandLineString.DetachOwnedStorage();
        const char* commandLineOwner = commandLineStorage;

        as1::win::ApplicationWin* const returnedOwner =
            applicationShell->initializeDerivedApplicationStartup(instance,
                                         previousInstance,
                                         &commandLineOwner,
                                         showCmd,
                                         &as1::core::StartupSettings());


        as1::core::g_applicationOwner = returnedOwner;
        as1::Map = reinterpret_cast<as1::MAP*>(returnedOwner);

        if (commandLineStorage != as1::STRING::SharedEmptyText())
            ::operator delete(commandLineStorage);


        if (!returnedOwner)
            return 0;


        if (returnedOwner->initialized())
        {
            do
            {
            } while (returnedOwner->pumpFrame() == 0);
        }

        returnedOwner->~ApplicationWin();
        if (as1::Map == reinterpret_cast<as1::MAP*>(returnedOwner))
            as1::Map = nullptr;
        if (as1::core::ApplicationOwner() == returnedOwner)
            as1::core::g_applicationOwner = nullptr;
        ::operator delete(returnedOwner);
        return 0;
    }
}


int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
    __try
    {
        return runWin32Application(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
    }
    __except (topLevelExceptionFilter(
        (const EXCEPTION_POINTERS*)GetExceptionInformation()))
    {
        return 0;
    }
}
