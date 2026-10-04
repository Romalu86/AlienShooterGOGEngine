#include "core/configuration.h"


namespace as1
{
    REGISTRY::REGISTRY(const STRING& registryPath)
        : m_path(registryPath)
    {
    }



}

namespace as1 { namespace core
{
    STRING* g_startupStringsIniPathOwner = nullptr;
    REGISTRY* g_startupRegistryPathOwner = nullptr;

    StartupSettingsBlock g_startupSettings = {
        {},
        {640u, 800u, 1024u},
        {480u, 600u, 768u},
        {16u, 32u},
        0u, 0, 640, 480, 32, 1
    };


} }
