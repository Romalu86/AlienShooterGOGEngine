#pragma once
#include "vid.h"
#include <array>

namespace as1
{

class VID_SOFTWARE : public VID
{
public:
    VID_SOFTWARE();
    VID_SOFTWARE(const VID_SOFTWARE& other);
    ~VID_SOFTWARE() override;
    VID_SOFTWARE* CreateMirror() override;
    void Load(RESOURCE* resource) override;
    void SetLayer() override;
    void Draw(const SPRITE* sprite) override;
    int DrawShadow(const SPRITE* sprite) const override;
    int HaveShadow() const override;
    virtual int PaletteSize() const;

    DWORD* frameOffsets() const noexcept { return m_frameOffsets; }
    DWORD frameStorageBytes() const noexcept { return m_frameStorageBytes; }
    BYTE* frameStorage() const noexcept { return m_frameStorage; }
    void setFrameOffsets(DWORD* value) noexcept { m_frameOffsets = value; }
    void setFrameStorageBytes(DWORD value) noexcept { m_frameStorageBytes = value; }
    void setFrameStorage(BYTE* value) noexcept { m_frameStorage = value; }

    void SetGamma(const Gamma& gamma, unsigned n_gamma) override;

    virtual void SetGammaToPalette(void* palette, const Gamma& gamma);

private:


    DWORD* m_frameOffsets = nullptr;
    DWORD m_frameStorageBytes = 0;
    BYTE* m_frameStorage = nullptr;

};

#if defined(_M_IX86) || defined(__i386__)
#endif

}
