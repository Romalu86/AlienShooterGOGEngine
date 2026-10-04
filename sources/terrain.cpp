#include "sprite.h"
#include "engine.h"
#include "sprite_act_const.h"
#include "vid/vid.h"
#include "map.h"
#include "win/application_win.h"
#include "graph.h"
#include "core/application.h"
#include "core/as_string.h"
#include "core/base_stream.h"
#include "core/resource.h"
#include "core/log.h"
#include "menu.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <cstdlib>

namespace as1
{

    TERRAIN::TERRAIN(VID* vid, float x, float y, float z, ANGLE dir, SPRITE* parent)
        : SPRITE(vid, x, y, z, dir, parent),
          m_repairLinkHp(-1),
          m_terrainState(0)
    {
    }

    int TERRAIN::Action(int opcode, std::intptr_t argument1Payload, int argument2Value, int argument3Value)
    {
        const int argument1 = static_cast<int>(argument1Payload);
        const int argument2 = argument2Value;
        const int argument3 = argument3Value;


        if (opcode == static_cast<int>(ActionCode::ACT_REPAIR))
        {
            ChangeHp(Vid()->GetMaxHp(armyIndex()));
            return 0;
        }

        if (opcode == SpriteActConst::ACT_RESTORE_OLD_MAP)
        {
            SPRITE::Action(opcode, argument1Payload, argument2, argument3);
            int localArgC = argument3;
            BaseStream* const stream = reinterpret_cast<BaseStream*>(
                static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            stream->read(&localArgC, static_cast<unsigned>((argument2 > 7) + 1));
            return 0;
        }

        return SPRITE::Action(opcode, argument1Payload, argument2, argument3);
    }

}
