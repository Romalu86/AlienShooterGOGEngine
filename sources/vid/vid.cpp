#include "vid.h"
#include "vid_software.h"
#include "vid_software16.h"
#include "../core/resource.h"
#include "../core/log.h"
#include "../core/file_logger.h"
#include "../map.h"
#include "../constant.h"
#include "../sprite.h"
#include "../mouse.h"
#include "../graph.h"
#include "../core/application.h"
#include "../sound/sound_engine.h"
#include "../script/vid_data_codes.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <array>
#include <new>
#include <cmath>
#include <limits>
#include <cstdint>

namespace as1
{


    void VID::AddVidToVid(SPRITE*) {}
    void VID::Draw(const SPRITE*) {}
#pragma warning(push)
#pragma warning(disable: 4716)
    int VID::DrawShadow(const SPRITE*) const {}
#pragma warning(pop)
    void VID::DrawToVid(SPRITE*, void*, BASE_TEXTURE*, BASE_TEXTURE*) {}
    void VID::Load(RESOURCE*) {}
    void VID::SetReColorForArmy(int) {}
    int VID::HaveShadow() const { return 0; }


    int g_vidMemoryInUse = 0;


    VID* EmptyVid = new VID;

    namespace
    {

        constexpr float UNLIMITED = 999999.0f;


        std::int32_t multiplyWrap32(std::int32_t lhs, std::int32_t rhs) noexcept
        {
            return static_cast<std::int32_t>(
                static_cast<std::uint32_t>(lhs) * static_cast<std::uint32_t>(rhs));
        }

        std::int32_t addWrap32(std::int32_t lhs, std::int32_t rhs) noexcept
        {
            return static_cast<std::int32_t>(
                static_cast<std::uint32_t>(lhs) + static_cast<std::uint32_t>(rhs));
        }

        int signedScaleShift(int numerator, int shift) noexcept
        {
            const int mask = (1 << shift) - 1;
            if (numerator < 0)
                numerator += mask;
            return numerator >> shift;
        }

        int signedDivide32(std::int32_t numerator, std::int32_t denominator) noexcept
        {
            if (denominator == 0)
                std::abort();
            return numerator / denominator;
        }

        int scaleHpBySixteenth(int hp, int nextMaxHp, int oldMaxHp) noexcept
        {
            const std::uint32_t product = static_cast<std::uint32_t>(multiplyWrap32(hp, nextMaxHp));
            const std::int32_t shifted = static_cast<std::int32_t>(product << 4u);
            return signedScaleShift(signedDivide32(shifted, oldMaxHp), 4);
        }

        int scaleHpByByteFraction(int hp, int nextMaxHp, int oldMaxHp) noexcept
        {
            const std::uint32_t product = static_cast<std::uint32_t>(multiplyWrap32(hp, nextMaxHp));
            const std::int32_t shifted = static_cast<std::int32_t>(product << 8u);
            return signedScaleShift(signedDivide32(shifted, oldMaxHp), 8);
        }

        int percentOfBaseHp(int maxHp, int percentValue) noexcept
        {
            return multiplyWrap32(maxHp, percentValue) / 100;
        }

        int divideBy1000SignedMagic(std::int32_t value) noexcept
        {

            const std::int64_t product = static_cast<std::int64_t>(0x10624DD3) * static_cast<std::int64_t>(value);
            std::int32_t high = static_cast<std::int32_t>(product >> 32);
            std::int32_t result = high >> 6;
            result += static_cast<std::uint32_t>(result) >> 31;
            return result;
        }


    }


    float VID::CalculateZSpeed(float verticalDelta, float projectedXYLength) const noexcept
    {

        const DWORD propertyFlags = properties();
        float result = 0.0f;

        if ((propertyFlags & 0x00000002u) != 0)
        {
            const CONSTANT* const constants = g_baseConstants;
            float property2Gravity = 0.0f;
            std::memcpy(&property2Gravity, &constants->entries[2], sizeof(property2Gravity));
            result = projectedXYLength * property2Gravity / maxSpeedValue() * 0.5f + verticalDelta * maxSpeedValue() / projectedXYLength;
            result *= (result > 0.0f) ? 1.1f : 0.89999998f;
        }
        else if ((propertyFlags & 0x00000004u) != 0)
        {
            const CONSTANT* const constants = g_baseConstants;
            float property4Gravity = 0.0f;
            std::memcpy(&property4Gravity, &constants->entries[3], sizeof(property4Gravity));
            result = projectedXYLength * property4Gravity / maxSpeedValue() * 0.5f + verticalDelta * maxSpeedValue() / projectedXYLength;
            result *= (result > 0.0f) ? 1.1f : 0.89999998f;
        }
        else if ((propertyFlags & 0x08000000u) != 0)
        {
            result = maximumZSpeed();
        }
        else
        {
            const float topZGate = topZValue();

            if (topZGate == 0.0f || std::isnan(topZGate))
                result = verticalDelta * maxSpeedValue() / projectedXYLength;
            else
                result = 0.0f;
        }


        if (result > maximumZSpeed())
            result = maximumZSpeed();
        else
        {
            const float negativeMaxZSpeed = -maximumZSpeed();

            if (result < negativeMaxZSpeed || std::isnan(result))
                result = negativeMaxZSpeed;
        }
        return result;
    }


    VID::VID()
    {
        baseGamma = Gamma{};
        for (Gamma& gamma : armyGammaOverrides)
            gamma = Gamma{};
        scaleXYZ = VECTOR{1.0f, 1.0f, 1.0f};


        vidRuntimeFlags &= 0xFFFFFF80u;
        spriteClass = 6;
        spriteType = 0;
        property = 0;
        sizeXYZ = VECTOR{24.0f, 16.0f, 20.0f};
        halfSizeXY = VECTOR2{12.0f, 8.0f};
        nextMirror = this;
        type = 0;
        exchangedVid = this;
        layer = 15;
        nVid = -1;
        maxHp = 0;
        noDir = 1;
        directionQuantizationOffsetValue = 0;
        frameSpeedDefault = 71;
        nLinkVid = 0;
        linkVid = nullptr;
        nWeapon = 0;
        weapon = nullptr;
        noGridZ = 0;
        gridZ = nullptr;
        gridCadrShift = nullptr;
        actionAuxStateRequiredValue = 0;
        movementTactEnabledValue = 0;
        notCreateAsChildFlag = 0;

        for (int i = 0; i < NO_ANIMATION; ++i)
        {
            sfx[i] = 0;
            nChildVid[i] = 0;
            childVid[i] = nullptr;
            noAnimCadr[i] = 0;
            animationBaseFrame[i] = 0;
            animationFrameCount[i] = 0;
            frameSpeed[i] = 71;
        }

        ResetSprites();
    }


    VID* VID::CreateMirror()
    {

        return new (std::nothrow) VID();
    }


    VID::~VID()
    {

        const DWORD liveSpriteSum = NoSprites();
        if (liveSpriteSum != 0)
            logVidResourceError(10, "Not all sprites with this VID deleted", static_cast<int>(liveSpriteSum));

        VID* const mirrorNext = nextMirrorVid();
        if (mirrorNext != this)
        {
            VID* mirrorPrevious = mirrorNext;
            while (mirrorPrevious->nextMirror != this)
                mirrorPrevious = mirrorPrevious->nextMirror;
            mirrorPrevious->nextMirror = mirrorNext;
        }

        if (gridZ)
        {

            ::operator delete(gridZ);
            gridZ = nullptr;
        }
        if (gridCadrShift)
        {
            ::operator delete(gridCadrShift);
            gridCadrShift = nullptr;
        }
        noGridZ = 0;

    }


    int VID::CanFight() const noexcept
    {

        return (hasWeaponChildDescriptor() != 0u && weaponCount() != 0u) ? 1 : 0;
    }


    int VID::PropNotCreateAsChild() const noexcept
    {

        return notCreateAsChildFlag;
    }


    int VID::SetPropNotCreateAsChild(int value) noexcept
    {

        notCreateAsChildFlag = value;
        return notCreateAsChildFlag;
    }


    int VID::PropBirthAsSmoke() const noexcept
    {

        return static_cast<int>(properties() & P_BIRTHASSMOKE);
    }


    int VID::PropHide() const noexcept
    {
        return static_cast<int>((vidRuntimeFlags >> 6u) & 1u);
    }


    DWORD VID::NoSprites(int army) const
    {
        return spriteCountsByArmy[army];
    }


    int VID::recolorUnitCounterValue(int bucket) const noexcept
    {
        return recolorUnitCounters[bucket & 3];
    }


    void VID::ResetSprites() noexcept
    {

        std::fill(std::begin(scriptFunction), std::end(scriptFunction), -1);
        scriptAux0 = -1;
        scriptAux1 = -1;
        unitLimits.fill(-1);
        std::fill(std::begin(spriteCountsByArmy), std::end(spriteCountsByArmy), 0u);
        std::fill(std::begin(killedUnitCounters), std::end(killedUnitCounters), 0);
        std::fill(std::begin(recolorUnitCounters), std::end(recolorUnitCounters), 0);
        std::fill(std::begin(maxHpByArmy), std::end(maxHpByArmy), maxHp);
        lastSpriteCountChangeTimestampMs = 0;
        vidRuntimeFlags &= ~0x10u;
    }


    int VID::GetFireDamage() const noexcept
    {

        std::int32_t linkContribution = 0;
        if (const VID* link = linkedVid())
            linkContribution = static_cast<std::int32_t>(link->GetFireDamage());

        std::int32_t deathChildContribution = 0;
        if (const VID* deathChild = deathChildVid())
            deathChildContribution = multiplyWrap32(
                static_cast<std::int32_t>(deathChild->GetFireDamage()),
                static_cast<std::int32_t>(deathNoChildValue()));

        std::int32_t birthChildContribution = 0;
        if (const VID* birthChild = birthChildVid())
            birthChildContribution = multiplyWrap32(
                static_cast<std::int32_t>(birthChild->GetFireDamage()),
                static_cast<std::int32_t>(birthNoChildValue()));

        std::int32_t fightChildContribution = 0;
        if (const VID* fightChild = fightChildVid())
            fightChildContribution = multiplyWrap32(
                static_cast<std::int32_t>(fightChild->GetFireDamage()),
                static_cast<std::int32_t>(fightNoChildValue()));

        std::int32_t contribution = static_cast<std::int32_t>(deathDamageMinimumBits());
        contribution = addWrap32(contribution, fightChildContribution);
        contribution = addWrap32(contribution, birthChildContribution);
        contribution = addWrap32(contribution, deathChildContribution);
        contribution = addWrap32(contribution, linkContribution);
        return contribution;
    }


    int VID::GetBuildTime() const noexcept
    {
        const VID* owner = this;
        if (const VID* link = linkedVid())
        {
            if (link->weaponCount() != 0u)
                owner = link;
        }


        return divideBy1000SignedMagic(owner->weaponBuildTime());
    }


    void VID::SetPropHide(int enabled) noexcept
    {
        constexpr unsigned int mask = 0x00000040u;
        const unsigned int bit = enabled != 0 ? mask : 0u;
        for (VID* vid = this; vid; vid = vid->linkedVid())
            vid->vidRuntimeFlags = (vid->vidRuntimeFlags & ~mask) | bit;
    }


    void VID::SetHpCoeff(int army, int hpPercent) noexcept
    {

        const int bucket = army & 3;
        for (VID* vid = this; vid; vid = vid->linkedVid())
        {
            const int oldMaxHp = vid->GetMaxHp(bucket);
            if (hpPercent >= 0)
                vid->maxHpByArmy[bucket] = percentOfBaseHp(vid->maxHp, hpPercent);

            if (vid->maxHp == 0)
                continue;

            const int layer = vid->renderLayer();
            const core::ApplicationDrawPassBucket& passBucket =
                core::GlobalApplicationDrawDispatcherState().drawPassBucket(layer);
            int cursor = passBucket.count() - 1;
            if (cursor < 0)
                continue;

            SPRITE* sprite = nullptr;
            for (;;)
            {
                SPRITE* const* slots = passBucket.data();
                while (cursor >= 0 && slots[cursor] == nullptr)
                    --cursor;
                if (cursor < 0)
                    break;

                sprite = slots[cursor];
                if (sprite->Vid() == vid)
                {
                    if (static_cast<int>(sprite->armyIndex()) == bucket)
                    {
                        const int newMaxHp = vid->GetMaxHp(bucket);
                        const int hp = scaleHpBySixteenth(
                            sprite->Hp(), newMaxHp, oldMaxHp);
                        sprite->ChangeHp(hp);
                    }
                }

                --cursor;
                if (cursor < 0)
                    break;
            }
        }
    }


    void VID::SetMaxHp(int army, int newHp) noexcept
    {

        const int bucket = army & 3;
        for (VID* vid = this; vid; vid = vid->linkedVid())
        {
            const int oldMaxHp = vid->GetMaxHp(bucket);
            if (newHp >= 0)
                vid->maxHpByArmy[bucket] = newHp;

            if (vid->maxHp == 0)
                continue;

            const int layer = vid->renderLayer();
            const core::ApplicationDrawPassBucket& passBucket =
                core::GlobalApplicationDrawDispatcherState().drawPassBucket(layer);
            int cursor = passBucket.count() - 1;
            if (cursor < 0)
                continue;

            for (;;)
            {
                SPRITE* const* slots = passBucket.data();
                while (cursor >= 0 && slots[cursor] == nullptr)
                    --cursor;
                if (cursor < 0)
                    break;
                SPRITE* const sprite = slots[cursor];
                if (sprite->Vid() == vid &&
                    sprite->armyIndex() == bucket)
                {
                    const int newMaxHp = vid->GetMaxHp(bucket);
                    const int hp = scaleHpByByteFraction(
                        sprite->Hp(), newMaxHp, oldMaxHp);
                    sprite->ChangeHp(hp);
                }
                --cursor;
                if (cursor < 0)
                    break;
            }
        }
    }


    void VID::LoadParameters(RESOURCE* res)
    {


        (void)res->read(&spriteType, 4);
        (void)res->read(&spriteClass, 4);
        (void)res->read(&property, 4);
        (void)res->read(&moveMask, 4);
        (void)res->read(&sizeXYZ.x, 4);
        (void)res->read(&sizeXYZ.y, 4);
        (void)res->read(&sizeXYZ.z, 4);
        (void)res->read(&maxHp, 4);
        (void)res->read(&maxSpeed, 4);
        (void)res->read(&maxZSpeed, 4);
        (void)res->read(&acceleration, 4);
        (void)res->read(&slow, 4);
        (void)res->read(&rotationSpeed, 4);
        (void)res->read(&nWeapon, 4);
        (void)res->read(&deathRange, 4);
        (void)res->read(&deathDamageMin, 4);
        (void)res->read(&linkXYZ.x, 4);
        (void)res->read(&linkXYZ.y, 4);
        (void)res->read(&linkXYZ.z, 4);
        (void)res->read(&nLinkVid, 4);
        (void)res->read(&topZ, 4);
        (void)res->read(&forMoveUpZ, 4);
        (void)res->read(&forMoveDownZ, 4);
        (void)res->read(&lifeTime, 4);

        res->shiftCurrentUnchecked(16);
        (void)res->read(&noDir, 4);

        (void)res->read(noAnimCadr, sizeof(noAnimCadr));
        (void)res->read(sfx, sizeof(sfx));
        (void)res->read(frameSpeed, sizeof(frameSpeed));
        (void)res->read(childX, sizeof(childX));
        (void)res->read(childY, sizeof(childY));
        (void)res->read(childZ, sizeof(childZ));
        (void)res->read(nChildVid, sizeof(nChildVid));
        (void)res->read(noChild, sizeof(noChild));

        int red = 0;
        int green = 0;
        int blue = 0;
        int alpha = 0;
        (void)res->read(&red, 4);
        (void)res->read(&green, 4);
        (void)res->read(&blue, 4);
        (void)res->read(&alpha, 4);
        baseGamma = Gamma(alpha, red, green, blue);

        (void)res->read(&scaleXYZ.x, 4);
        (void)res->read(&scaleXYZ.y, 4);
        (void)res->read(&scaleXYZ.z, 4);

        if ((formatFlags() & VID_TYPE_ZBUFFER) != 0u &&
            (formatFlags() & VID_TYPE_HARDWARE) != 0u)
            scaleXYZ = VECTOR{1.0f, 1.0f, 1.0f};

        if (rotationSpeed == UNLIMITED)
            rotationSpeed = 0.0f;
        else if (rotationSpeed == 0.0f)
            rotationSpeed = UNLIMITED;
        else
            rotationSpeed = 256.0f / rotationSpeed;

        if (maxSpeed != UNLIMITED)
            maxSpeed /= 1000.0f;
        if (maxZSpeed != UNLIMITED)
            maxZSpeed /= 1000.0f;
        if (acceleration != UNLIMITED)
            acceleration /= 1000000.0f;
        if (slow != UNLIMITED)
            slow /= 1000000.0f;

        setMovementTactEnabled(
            (maxSpeed != 0.0f || maxZSpeed != 0.0f || (property & 0x00001006u) != 0u) ? 1 : 0);

        if (noDir == 0)
        {
            logVidResourceError(4, "NoDir==0", 0);
            std::exit(1);
        }
        for (int i = 0; i < NO_ANIMATION; ++i)
            if (frameSpeed[i] == 0)
                frameSpeed[i] = static_cast<int>(frameSpeedDefault);

        halfSizeXY = { sizeXYZ.x * 0.5f, sizeXYZ.y * 0.5f };
        setDirectionQuantizationOffset(128 / noDir);


        if (noCadr != 0)
        {
            if (noCadr < noDir)
            {
                logVidResourceError(4, "noCadr < noDir", 0);
                noDir = noCadr;
            }
        }
        else
        {
            logVidResourceError(4, "noCadr==0", 0);
        }

        const int noCadrCount = static_cast<int>(noCadr);

        int requestedFrames = 0;
        for (int i = 0; i < NO_ANIMATION; ++i)
            requestedFrames += static_cast<int>(noAnimCadr[i]) * static_cast<int>(noDir);

        if (requestedFrames > noCadrCount)
        {
            logVidResourceError(13, "noCadr for noAnimCadr and noDir", 0);
            for (int i = NO_ANIMATION - 1; i >= 0 && requestedFrames > noCadrCount; --i)
            {
                const int currentAnimFrames = static_cast<int>(noAnimCadr[i]) * static_cast<int>(noDir);
                const int withoutCurrent = requestedFrames - currentAnimFrames;
                if (withoutCurrent > noCadrCount)
                {
                    noAnimCadr[i] = 0;
                    requestedFrames = withoutCurrent;
                    continue;
                }

                const int overrun = requestedFrames - noCadrCount;
                noAnimCadr[i] -= overrun / noDir;
                break;
            }
        }

        const std::uint8_t* const soundOwner =
            reinterpret_cast<const std::uint8_t*>(sound::g_globalSoundEngine);
        const std::uint8_t* const soundTable =
            *reinterpret_cast<const std::uint8_t* const*>(soundOwner + 0x08u);
        if (soundTable != nullptr)
        {
            const int loadedSfxCount =
                *reinterpret_cast<const int*>(soundOwner + 0x04u);
            for (int i = 0; i < NO_ANIMATION; ++i)
            {
                const int nsfx = sfx[i];
                bool sfxLoaded = false;
                if (nsfx >= 0 && nsfx <= loadedSfxCount)
                {


                    constexpr std::size_t kSfxEntrySize = 0x48u;
                    constexpr std::size_t kFirstBufferOffset = 0x24u;
                    const std::uint8_t* const entry =
                        soundTable + static_cast<std::size_t>(nsfx) * kSfxEntrySize;
                    sfxLoaded = *reinterpret_cast<void* const*>(
                        entry + kFirstBufferOffset) != nullptr;
                }

                if (!sfxLoaded && nVid != -1)
                {
                    logVidResourceError(4, "sfx", nsfx);
                    sfx[i] = 0;
                }
            }
        }

        int totalCadr = 0;
        int firstRealAnim = -1;
        for (int i = 0; i < NO_ANIMATION; ++i)
        {
            const int animCadr = static_cast<int>(noAnimCadr[i]);
            if (animCadr != 0)
            {
                animationBaseFrame[i] = totalCadr;
                animationFrameCount[i] = animCadr;

                if (firstRealAnim < 0)
                {
                    firstRealAnim = i;
                    for (int j = 0; j < i; ++j)
                    {
                        if (animationFrameCount[j] == 0)
                            animationFrameCount[j] = animCadr;
                    }
                }
            }
            else
            {
                bool copiedClass10OddFallback = false;
                if (spriteClass == 0x0Au && (i & 1) != 0 && i <= 7 &&
                    noAnimCadr[firstRealAnim + 1] != 0)
                {

                    animationBaseFrame[i] = animationBaseFrame[firstRealAnim + 1];
                    animationFrameCount[i] = animationFrameCount[firstRealAnim + 1];
                    copiedClass10OddFallback = true;
                }

                if (!copiedClass10OddFallback)
                {
                    animationBaseFrame[i] = 0;


                    animationFrameCount[i] = firstRealAnim >= 0
                        ? animationFrameCount[firstRealAnim]
                        : animationBaseFrame[NO_ANIMATION - 1];
                }
            }

            totalCadr += animCadr * static_cast<int>(noDir);
            if (totalCadr > noCadrCount)
            {
                logVidResourceError(10, "noCadr and noAnimCadr and noDir", i);
                animationBaseFrame[i] = 0;


                animationFrameCount[i] = firstRealAnim >= 0
                    ? animationFrameCount[firstRealAnim]
                    : animationBaseFrame[NO_ANIMATION - 1];
            }
        }

        if (spriteClass == 8)
        {
            vidRuntimeFlags |= 0x20u;
            const std::uint8_t* const appOwner =
                static_cast<const std::uint8_t*>(core::ApplicationOwner());
            const DWORD appFlags = *reinterpret_cast<const DWORD*>(
                appOwner + core::application_layout::Flags);
            if ((appFlags & 0x00000001u) != 0u)
                spriteClass = 0;
        }


        if ((property & 0x00000008u) != 0u && noGridZ == 0)
        {
            const int allocationExtent = static_cast<int>(sizeXYZ.x) + 17;
            const int allocationRows = allocationExtent / 8;
            const int scratchCount = (allocationRows * allocationExtent) / 8 + 1;
            const std::size_t scratchBytes =
                static_cast<std::size_t>(scratchCount) * sizeof(VECTOR);
            VECTOR* const scratch =
                static_cast<VECTOR*>(::operator new[](scratchBytes));

            noGridZ = 0;
            for (int y = 0; static_cast<float>(y) < sizeXYZ.y; y += 8)
            {
                for (int x = 0; static_cast<float>(x) < sizeXYZ.x; x += 8)
                {
                    VECTOR& dot = scratch[noGridZ++];
                    dot.x = static_cast<float>(x) - halfSizeXY.x;
                    dot.y = static_cast<float>(y) - halfSizeXY.y;
                    dot.z = sizeXYZ.z;
                }

                VECTOR& rightDot = scratch[noGridZ++];
                rightDot.x = halfSizeXY.x;
                rightDot.y = static_cast<float>(y) - halfSizeXY.y;
                rightDot.z = sizeXYZ.z;
            }

            for (int x = 0; static_cast<float>(x) < sizeXYZ.x; x += 8)
            {
                VECTOR& bottomDot = scratch[noGridZ++];
                bottomDot.x = static_cast<float>(x) - halfSizeXY.x;
                bottomDot.y = halfSizeXY.y;
                bottomDot.z = sizeXYZ.z;
            }

            VECTOR& corner = scratch[noGridZ++];
            corner.x = halfSizeXY.x;
            corner.y = halfSizeXY.y;
            corner.z = sizeXYZ.z;

            if (gridZ)
                ::operator delete[](gridZ);

            const std::size_t finalBytes =
                static_cast<std::size_t>(noGridZ) * sizeof(VECTOR);
            gridZ = static_cast<VECTOR*>(::operator new(finalBytes));
            for (int i = 0; i < noGridZ; ++i)
                gridZ[i] = scratch[i];

            ::operator delete[](scratch);
        }
    }


    std::intptr_t VID::logVidResourceError(int errorCode, const char* detailText, int detailValue) const
    {

        return logFileLoggerResourceError(
            g_fileLogger, "VID [%i-%s]", errorCode, detailText, detailValue, nVid, name.c_str());
    }


    void VID::SetChildAndLink()
    {
        std::uint8_t* const appOwner =
            static_cast<std::uint8_t*>(core::ApplicationOwner());
        const int vidCount =
            *reinterpret_cast<const int*>(
                appOwner + core::application_layout::VidCount);
        VID* const* const vidTable =
            reinterpret_cast<VID* const*>(
                appOwner + core::application_layout::VidTable);

        const int linkedVidId = nLinkVid;
        if (linkedVidId)
        {
            if (linkedVidId >= 0 && linkedVidId < vidCount && vidTable[linkedVidId])
                linkVid = vidTable[linkedVidId]->exchangedVid;
            else
                logVidResourceError(4, "LinkVid", linkedVidId);
        }


        const std::uint8_t* const ex = weapon->recordData.data();
        for (int i = 0; i < 8; ++i)
        {
            const std::size_t step = static_cast<std::size_t>(i) * 4u;


            if (*reinterpret_cast<const std::uint32_t*>(ex + 0x064u + step) != 0u ||
                *reinterpret_cast<const std::uint32_t*>(ex + 0x084u + step) != 0u ||
                *reinterpret_cast<const std::uint32_t*>(ex + 0x0A4u + step) != 0u ||
                *reinterpret_cast<const std::uint32_t*>(ex + 0x0C4u + step) != 0u)
            {
                vidRuntimeFlags |= 0x01u;
            }

            if (*reinterpret_cast<const float*>(ex + 0x0E4u + step) != 1.0f ||
                *reinterpret_cast<const float*>(ex + 0x104u + step) != 1.0f ||
                *reinterpret_cast<const float*>(ex + 0x124u + step) != 1.0f)
            {
                vidRuntimeFlags |= 0x02u;
            }

            if (*reinterpret_cast<const float*>(ex + 0x144u + step) != 0.0f ||
                *reinterpret_cast<const float*>(ex + 0x164u + step) != 0.0f ||
                *reinterpret_cast<const float*>(ex + 0x184u + step) != 0.0f)
            {
                vidRuntimeFlags |= 0x04u;
            }

            if (*reinterpret_cast<const std::uint32_t*>(ex + 0x1A4u + step) != 0u ||
                *reinterpret_cast<const std::uint32_t*>(ex + 0x1C4u + step) != 0u ||
                *reinterpret_cast<const std::uint32_t*>(ex + 0x1E4u + step) != 0u)
            {
                vidRuntimeFlags |= 0x08u;
            }
        }


        if (lifeTime != 999999 ||
            (property & 0x00200400u) != 0u ||
            (vidRuntimeFlags & 0x0Fu) != 0u)
        {
            actionAuxStateRequiredValue = 1;
        }

        for (int i = 0; i < NO_ANIMATION; ++i)
        {
            const int sourceChild = nChildVid[i];
            if (!sourceChild)
                continue;

            const std::uint32_t sourceIndexBits = static_cast<std::uint32_t>(sourceChild);
            const std::uint32_t signMask = 0u - (sourceIndexBits >> 31u);
            const int childIndex = static_cast<std::int32_t>((sourceIndexBits ^ signMask) - signMask);
            if (childIndex >= 0 && childIndex < vidCount && vidTable[childIndex])
            {
                VID* const mirror = vidTable[childIndex]->exchangedVid;
                childVid[i] = mirror;
                if (mirror && (mirror->property & 0x00000080u) != 0u)
                    actionAuxStateRequiredValue = 1;
            }
            else
            {
                logVidResourceError(4, "child", sourceChild);
            }
        }
    }


    void VID::SetLayer()
    {

        layer = 0;
    }

    int VID::RealDirection(const ANGLE& dir) const
    {
        const std::uint32_t directionByte =
            static_cast<std::uint32_t>((dir.Int() + directionQuantizationOffset()) & 0xFF);
        const std::uint32_t product = directionByte * static_cast<std::uint32_t>(noDir);
        return static_cast<int>(product >> 8u);
    }

    ANGLE VID::SteppedDirection(const ANGLE& dir) const
    {

        if (noDir == 0)
            return ANGLE(static_cast<unsigned char>(dir.Int()));
        const int real = RealDirection(dir);
        return ANGLE(static_cast<unsigned char>((real << 8) / noDir));
    }

    void VID::SetGamma(const Gamma& gamma, unsigned n_gamma)
    {

        if (n_gamma >= 4)
        {
            if (n_gamma != 4)
                logVidResourceError(4, "n_gamma in VID::SetGamma", static_cast<int>(n_gamma));
            return;
        }

        armyGammaOverrides[n_gamma] = gamma;
    }

    void VID::SetGridZ(const SPRITE* sprite)
    {


        if (sprite == Mouse)
            return;

        const int frame = sprite->currentFrame();
        int begin = 0;
        int end = noGridZ;
        if (gridCadrShift)
        {
            const int frameCount = static_cast<int>(noCadr);
            begin = frame < frameCount ? gridCadrShift[frame] : 0;
            end = frame < frameCount - 1 ? gridCadrShift[frame + 1] : noGridZ;
        }

        if (begin >= end)
            return;

        MAP* const map = sprite->mapOwner();

        const std::uint8_t* const application =
            static_cast<const std::uint8_t*>(core::ApplicationOwner());
        const std::uint32_t applicationFlags =
            *reinterpret_cast<const std::uint32_t*>(application + core::application_layout::Flags);
        const bool permanent =
            (applicationFlags & 1u) == 0u &&
            sprite->Vid()->spriteClassId() == 8u;

        for (int i = begin; i < end; ++i)
        {
            const VECTOR& dot = gridZ[i];
            const float x = sprite->X() + dot.x;
            const float y = sprite->Y() + dot.y;
            const float z = sprite->Z() + dot.z;
            if (permanent)
                map->SetGroundZ(x, y, z);
            else
                map->SetTempGroundZ(x, y, z);
        }
    }

    void VID::ResetGridZ(const SPRITE* sprite)
    {

        if (sprite == Mouse)
            return;


        const std::uint8_t* const application =
            static_cast<const std::uint8_t*>(core::ApplicationOwner());
        const std::uint32_t applicationFlags =
            *reinterpret_cast<const std::uint32_t*>(application + core::application_layout::Flags);
        if ((applicationFlags & 1u) == 0u &&
            sprite->Vid()->spriteClassId() == 8u)
        {
            return;
        }

        const int frame = sprite->currentFrame();
        int begin = 0;
        int end = noGridZ;
        if (gridCadrShift)
        {
            const int frameCount = static_cast<int>(noCadr);
            begin = frame < frameCount ? gridCadrShift[frame] : 0;
            end = frame < frameCount - 1 ? gridCadrShift[frame + 1] : noGridZ;
        }

        if (begin >= end)
            return;

        MAP* const map = sprite->mapOwner();
        for (int i = begin; i < end; ++i)
        {
            const VECTOR& dot = gridZ[i];
            map->ClearTempGroundZ(sprite->X() + dot.x,
                                  sprite->Y() + dot.y,
                                  sprite->Z() + dot.z);
        }
    }

}
