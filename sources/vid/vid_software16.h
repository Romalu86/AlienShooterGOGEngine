#pragma once
#include "vid_software.h"
#include "vid_hardware.h"

namespace as1
{

class BASE_TEXTURE;

class VID_SOFTWARE16 : public VID_SOFTWARE
{
public:
    VID_SOFTWARE16() = default;
    __forceinline VID_SOFTWARE16(const VID_SOFTWARE16& other) : VID_SOFTWARE(other) {}
    VID_SOFTWARE16* CreateMirror() override;
    void Draw(const SPRITE* sprite) override;

    void DrawToVid(SPRITE* sprite, void* texSize, BASE_TEXTURE* texture, BASE_TEXTURE* zTexture) override;
    int PaletteSize() const override;
    void SetGammaToPalette(void* palette, const Gamma& gamma) override;

private:
    DWORD UnpackRgb565ToBgra(WORD value) const;
    WORD PackRgb565FromBgra(DWORD value) const;

};

#if defined(_M_IX86) || defined(__i386__)
#endif

}
