#pragma once
#include "vid.h"
#include "vid/vid_texcoor.h"
#include "graphics/base_texture.h"
#include <array>
#include <cstddef>

namespace as1
{
class VID_SURFACE : public VID
{
public:
    __forceinline VID_SURFACE() : m_surfaceTexcoordOwners(nullptr), m_surfaceTextureOwners(nullptr) {}
    VID_SURFACE(const VID_SURFACE& other);
    ~VID_SURFACE() override;
    VID_SURFACE* CreateMirror() override;
    void SetLayer() override;

    void Draw(const SPRITE* sprite) override;

    void Load(RESOURCE* globalRes) override;
    BASE_TEXTURE* const* surfaceTextureOwners() const { return m_surfaceTextureOwners; }
    VID_TEXCOOR* const* surfaceTexcoordOwners() const { return m_surfaceTexcoordOwners; }

private:


    std::array<BYTE, 0x28> m_surfaceStateStorage;


    mutable VID_TEXCOOR** m_surfaceTexcoordOwners;
    mutable BASE_TEXTURE** m_surfaceTextureOwners;


    DWORD m_surfaceSourceFormat;
};
#if defined(_M_IX86) || defined(__i386__)
#endif

}
