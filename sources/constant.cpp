#include "constant.h"
#include "core/resource.h"
#include "core/log.h"

namespace as1
{
    CONSTANT* g_baseConstants = nullptr;

    CONSTANT* CONSTANT::Load(RESOURCE* res)
    {
        if (res->GoBegin(RESOURCE::ResTypes::CONSTANT))
        {
            writeLogLine(g_fileLogger, "!!!ERROR!!! CNST Load Constant section not found");
            return this;
        }

        (void)res->read(&entries[0], 4u);
        (void)res->read(&entries[1], 4u);
        (void)res->read(&entries[2], 4u);
        (void)res->read(&entries[3], 4u);
        (void)res->read(&entries[4], 4u);
        (void)res->read(&entries[5], 4u);
        (void)res->read(&entries[6], 4u);
        (void)res->read(&entries[7], 4u);
        (void)res->read(&entries[8], 4u);
        (void)res->read(&entries[9], 4u);

        DWORD discardedCnstValue;
        (void)res->read(&discardedCnstValue, 4u);

        (void)res->read(&entries[11], 4u);
        (void)res->read(&entries[12], 4u);
        (void)res->read(&entries[13], 4u);
        (void)res->read(&entries[14], 4u);
        (void)res->read(&entries[15], 4u);
        (void)res->read(&entries[16], 4u);
        (void)res->read(&entries[17], 4u);
        (void)res->read(&entries[18], 4u);
        (void)res->read(&entries[19], 4u);
        (void)res->read(&entries[20], 4u);
        (void)res->read(&entries[21], 4u);
        (void)res->read(&entries[22], 4u);
        (void)res->read(&entries[23], 4u);
        (void)res->read(&entries[24], 4u);
        (void)res->read(&entries[25], 4u);

        float* const values = reinterpret_cast<float*>(entries.data());
        values[0] /= 1000.0f;
        values[1] /= 1000.0f;
        values[2] /= 1000000.0f;
        values[3] /= 1000000.0f;
        values[7] /= 1000.0f;
        values[6] /= 1000.0f;
        values[24] /= 1000.0f;
        return this;
    }
}
