#include "vid/vid_font.h"

#include "graph.h"
#include "sprite.h"
#include "core/log.h"
#include "core/application.h"

#include <cmath>
#include <new>

namespace as1
{
    VID_FONT::VID_FONT(const VID_FONT& other)
        : VID()
    {
        nextMirror = other.nextMirrorVid();
        const_cast<VID_FONT&>(other).nextMirror = this;
        layer = other.layer;
        type = other.type;
        frameSpeedDefault = other.frameSpeedDefault;
        noCadr = other.noCadr;
        setVidWidth(static_cast<short>(other.vidWidth()));
        setVidHeight(static_cast<short>(other.vidHeight()));
        m_fontOwner = other.m_fontOwner;
    }



    VID_FONT* VID_FONT::CreateMirror()
    {
        return new (std::nothrow) VID_FONT(*this);
    }



    int VID_FONT::HaveShadow() const
    {
        return 0;
    }

    void VID_FONT::SetLayer()
    {


        layer = 14;
    }




}
