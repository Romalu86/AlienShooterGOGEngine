#include "builded_terrain.h"

#include <cstdint>

namespace as1
{
    namespace
    {
        __forceinline VID* builtTerrainGroundVid() noexcept
        {
            auto* const application = static_cast<std::uint8_t*>(core::ApplicationOwner());
            const int count = *reinterpret_cast<const int*>(
                application + core::application_layout::VidCount);
            if (count <= 1024)
                return EmptyVid;

            VID* const ground = *reinterpret_cast<VID* const*>(
                application + core::application_layout::VidTable + 1024u * sizeof(VID*));
            return ground ? ground : EmptyVid;
        }
    }

    BUILDED_TERRAIN::BUILDED_TERRAIN(VID* vid, float x, float y, float z, ANGLE dir, SPRITE* parent)
        : SPRITE(vid, x, y, z, dir, parent)
    {
        VID* ground = builtTerrainGroundVid();
        if (ground == EmptyVid)
        {
            mapOwner()->CreateEmptyHardwareGround();
            ground = builtTerrainGroundVid();
        }

        if (ground != EmptyVid && ground->directionCount() == 1)
        {
            ground->AddVidToVid(this);

            if ((Vid()->properties() & 0x00000040u) == 0u)
                ChangeAnimation(15);
        }
    }

    void BUILDED_TERRAIN::Draw()
    {
        VID* const ground = builtTerrainGroundVid();
        if (ground == EmptyVid || ground->directionCount() != 1)
            Vid()->Draw(this);
    }
}
