#pragma once

namespace as1
{
    extern char* g_registrationInformation;
}

__declspec(dllexport) void __stdcall GetRegistrationInformation(char* information);
