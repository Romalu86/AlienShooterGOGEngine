#pragma once

#include <cstdint>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace as1
{
    class Color;
    struct Gamma
    {
        DWORD first = 0;
        DWORD second = 0;

        enum GAMMA_CREATE { DECODE };

        Gamma() noexcept = default;
        Gamma(DWORD firstValue, DWORD secondValue) noexcept
            : first(firstValue), second(secondValue) {}
        Gamma(Color diffuseColor, Color specularColor) noexcept;


        Gamma(int alphaDelta, int redDelta, int greenDelta, int blueDelta) noexcept;


        Gamma(GAMMA_CREATE type, DWORD packed) noexcept;

        Gamma(const Gamma& other) noexcept;
        Gamma& operator=(const Gamma&) noexcept = default;



        Gamma* setRedDelta(int value) noexcept;
        Gamma* setGreenDelta(int value) noexcept;
        Gamma* setBlueDelta(int value) noexcept;
        Gamma* setSaturatingAdd(Gamma lhs, Gamma rhs) noexcept;
    };



    DWORD BlendGammaColor(const Gamma& maskAndColor, DWORD baseColor);

}
