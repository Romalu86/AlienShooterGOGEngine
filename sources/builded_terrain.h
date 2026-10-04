#pragma once

#include "sprite.h"
#include "map.h"
#include "core/application.h"
#include "core/log.h"

namespace as1
{
    class BUILDED_TERRAIN : public SPRITE
    {
    public:


        BUILDED_TERRAIN(VID* vid, float x, float y, float z, ANGLE dir, SPRITE* parent = nullptr);

        void Draw() override;
    };
#if defined(_M_IX86)
    static_assert(sizeof(BUILDED_TERRAIN) == 0x70, "BUILDED_TERRAIN size");
#endif

}
