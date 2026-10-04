#include "registration_export.h"

namespace as1
{
    char* g_registrationInformation = nullptr;
}

__declspec(dllexport) void __stdcall GetRegistrationInformation(char* information)
{
    as1::g_registrationInformation = information;
}
