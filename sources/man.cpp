#include "man.h"

#include "core/application.h"
#include "map.h"
#include "graph.h"
#include "vid/vid.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <new>

namespace
{

    __forceinline std::int32_t manImul32Low(std::int32_t a, std::int32_t b) noexcept
    {
        return static_cast<std::int32_t>(
            static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(static_cast<std::uint32_t>(a)) *
                static_cast<std::uint32_t>(b)));
    }

    __forceinline std::int32_t manAdd32Wrap(std::int32_t a, std::int32_t b) noexcept
    {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
    }

    __forceinline std::int32_t manSub32Wrap(std::int32_t a, std::int32_t b) noexcept
    {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
    }

    __forceinline float intToFloat(std::int32_t value) noexcept
    {
        return static_cast<float>(value);
    }

    __forceinline float addIntAndFloat(std::int32_t value, float addend) noexcept
    {
        return static_cast<float>(
            static_cast<long double>(value) + static_cast<long double>(addend));
    }

    __forceinline int manX87Int64Low32(float value) noexcept
    {
        const double convertedValue = static_cast<double>(value);
        if (!std::isfinite(convertedValue) ||
            convertedValue < -9223372036854775808.0 ||
            convertedValue >= 9223372036854775808.0)
            return 0;

        const std::int64_t converted = static_cast<std::int64_t>(std::trunc(convertedValue));
        return static_cast<int>(static_cast<std::uint32_t>(converted));
    }

    

    

}

namespace as1
{

    MAN::MAN(VID* vid, float x, float y, float z, ANGLE direction, SPRITE* parent)
        : UNIT(vid, x, y, z, ANGLE(direction.Int() & 0xFF), parent)
    {


        insertUniqueItem(0x0105);
        for (int i = 2; i < 10; ++i)
            weaponAmmo[i] = 0;
    }


    MAN::~MAN()
    {
    }


    int MAN::Action(int opcode, std::intptr_t argument1Payload, int argument2Value, int argument3Value)
    {
        const int argument1 = static_cast<int>(argument1Payload);
        const int argument2 = argument2Value;
        const int argument3 = argument3Value;

        switch (opcode)
        {
        case static_cast<int>(ActionCode::ACT_COOR_ATTACK):
        {


            SPRITE* const child = childChain();
            if (!child)
                return 0;

            VID* const ownVid = Vid();
            VID* const childVid = child->Vid();
            VID* const linkVid = ownVid->linkedVid();
            if (childVid != linkVid ||
                childVid->hasWeaponChildDescriptor() == 0u ||
                childVid->weaponCount() == 0u ||
                child->actionTimer() > 5000u ||
                (child->Animation() == 8 && child->currentFrame() <= child->currentFrameEnd()))
            {
                return 0;
            }

            const float worldX = intToFloat(argument1);
            const float worldY = intToFloat(argument2);
            const core::ApplicationDrawDispatcherState& drawState =
                core::GlobalApplicationDrawDispatcherState();
            GRAPH* const graph = Graph;
            const float screenX = worldX - drawState.cameraShiftX();
            const float screenY = worldY - drawState.cameraShiftY();

            float height = 0.0f;
            int constructorY = argument2;
            if (graph &&
                screenX >= graph->viewportLeft() &&
                screenX < graph->viewportRight() &&
                screenY >= graph->viewportTop() &&
                screenY < graph->viewportBottom())
            {
                const int pixelX = manX87Int64Low32(screenX);
                const int pixelY = manX87Int64Low32(screenY);
                const std::uint16_t* const depth = graph->softwareDepthBuffer();
                const int pitch = graph->softwareDepthPitch();
                if (depth)
                {
                    const int pixelIndex = manAdd32Wrap(pixelX, manImul32Low(pitch, pixelY));
                    height = static_cast<float>(depth[pixelIndex] >> 3) - 128.0f;
                    if (height > Z() + 70.0f)
                        height = Z() + 50.0f;
                }
            }
            else
            {
                const int weaponMode =
                    childVid->hasWeaponChildDescriptor() != 0u && childVid->weaponCount() != 0u
                        ? childVid->weaponTypeMask()
                        : ownVid->weaponTypeMask();
                if (weaponMode == 8)
                {
                    const float probeY = addIntAndFloat(argument2, 80.0f);
                    height = mapOwner()->GetGroundZ(worldX, probeY) + 80.0f;
                }
                else
                {
                    constructorY = manSub32Wrap(argument2, 19);
                    height = mapOwner()->GetGroundZ(worldX, worldY) + 19.0f;
                }
            }

            SPRITE* const marker = new (std::nothrow) SPRITE(
                EmptyVid,
                worldX,
                addIntAndFloat(constructorY, height),
                height,
                ANGLE(static_cast<unsigned char>(0)),
                nullptr);
            child->SetCommand(4, marker);
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_NEXT_COMMAND):
        {
            if (Animation() >= 15)
                return 0;

            SPRITE* const child = childChain();
            if (Goal() || (child && child->Goal()))
            {
                std::uint32_t delta = static_cast<std::uint32_t>(
                    Vid()->frameSpeed[Animation()]);
                const std::uint32_t frameDelta = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                if (frameDelta > delta)
                    delta = frameDelta;
                setAttackDecisionCode(AttackTact(delta));
            }


            const int animation = Animation();
            if (Speed() != 0.0f)
            {
                if ((runtimeFlags() & 0x80u) == 0u)
                {
                    const float quarterMaximum = MaxSpeed() * 0.25f;
                    if (quarterMaximum >= Speed())
                    {
                        if (animation != 1)
                            ChangeAnimation(1);
                        return 0;
                    }
                }

                bool keepTurnAnimation = false;
                if (animation == 6 && childChain() != nullptr && Vid() != nullptr)
                    keepTurnAnimation = (Vid()->nVid == 9);

                if (!keepTurnAnimation && animation != 2)
                    ChangeAnimation(2);

                if (SPRITE* const moveChild = childChain())
                {
                    if (moveChild->Animation() == 0)
                        moveChild->ChangeAnimation(2);
                }
                return 0;
            }

            if (animation != 0 && animation != 10)
                ChangeAnimation(0);

            if (SPRITE* const moveChild = childChain())
            {
                if (moveChild->Animation() == 2)
                    moveChild->ChangeAnimation(0);
            }
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_ADD_ITEM):
        {
            const std::int32_t word = argument1;
            if (argument1 == 301 || argument1 == 235)
            {
                InsertItem(word);
                return 0;
            }

            if (insertUniqueItem(word) != 0)
                return 0;
            if (argument1 < 260 || argument1 > 269)
                return 0;

            if (UNIT::Action(static_cast<int>(ActionCode::ACT_GET_AMMO), 0, 0, 0) != 0 &&
                argument1 - 260 <= Vid()->linkedVid()->nvid() - 10 &&
                argument1 != 260)
            {
                return 0;
            }
            ChangeWeapon(argument1 - 260);
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_GET_AMMO):
        {


            const int index = argument1;
            if (index != 0 && index != Vid()->linkedVid()->nvid() - 10)
                return weaponAmmo[index];
            return UNIT::Action(static_cast<int>(ActionCode::ACT_GET_AMMO), 0, 0, 0);
        }

        case static_cast<int>(ActionCode::ACT_ADD_AMMO):
        {

            const int index = argument2;
            if (index > 9)
                return 0;
            if (index != 0 && index != Vid()->linkedVid()->nvid() - 10)
            {
                MAN* const man = this;
                const int value = manAdd32Wrap(man->weaponAmmo[index], argument1);
                man->weaponAmmo[index] = value;
                return value;
            }
            return UNIT::Action(static_cast<int>(ActionCode::ACT_ADD_AMMO), static_cast<std::intptr_t>(argument1), 0, 0);
        }

        case static_cast<int>(ActionCode::ACT_DAMAGE):
        {
            int damage = argument1;
            if (damage > 0)
            {
                VID* const ownVid = Vid();
                const int ownNvid = ownVid->nvid();
                if (ownNvid != 350)
                {
                    for (SPRITE* child = childChain(); child; child = child->childChain())
                    {
                        const int childNvid = child->Vid()->nvid();
                        if (childNvid == 203 || childNvid == 181)
                            return 0;
                    }

                    static constexpr int kArmorDamagePercent[3] = {50, 70, 90};
                    for (SPRITE* armor = childChain(); armor; armor = armor->childChain())
                    {
                        const int armorNvid = armor->Vid()->nvid();
                        if (armorNvid < 200 || armorNvid > 202)
                            continue;

                        const int percent = kArmorDamagePercent[armorNvid - 200];
                        const int scaledDamage = manImul32Low(damage, percent);
                        const int armorDamage = manAdd32Wrap(scaledDamage, 50) / 100;
                        (void)armor->dispatchVirtualAction(
                            ActionCode::ACT_DAMAGE, armorDamage, argument2, argument3);

                        damage = manAdd32Wrap(damage, scaledDamage / -100);
                    }
                }

                if (damage >= Hp() &&
                    dispatchVirtualAction(ActionCode::ACT_HAVE_ITEM, 230, 0, 0) != 0)
                {
                    static_cast<void>(dispatchVirtualAction(ActionCode::ACT_DELETE_ITEM, 230, 0, 0));
                    const int bucket = armyIndex();
                    ChangeHp(Vid()->GetMaxHp(bucket));

                    core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
                    VID* createVid = EmptyVid;
                    if (table.count() > 181)
                    {
                        if (VID* const replacementVid = table.slot(181))
                            createVid = replacementVid;
                    }
                    static_cast<void>(reinterpret_cast<core::Application*>(core::ApplicationOwner())->CreateSprite(
                        createVid,
                        VECTOR{X(), Y(), Z() + 22.0f},
                        ANGLE(static_cast<unsigned char>(0)),
                        this));
                    return 0;
                }
            }
            return SPRITE::Action(static_cast<int>(ActionCode::ACT_DAMAGE), static_cast<std::intptr_t>(damage), argument2, argument3);
        }

        case static_cast<int>(ActionCode::ACT_CHANGE_VID):
        {


            VID* const previousVid = Vid();
            VID* const previousLink = previousVid->linkedVid();
            MAN* const man = this;
            if (previousVid->nvid() < 20)
            {
                const int currentAmmo = UNIT::Action(
                    static_cast<int>(ActionCode::ACT_GET_AMMO), 0, 0, 0);
                man->weaponAmmo[previousLink->nvid() - 10] = currentAmmo;
            }

            static_cast<void>(UNIT::Action(
                static_cast<int>(ActionCode::ACT_CHANGE_VID),
                static_cast<std::intptr_t>(argument1), argument2, argument3));

            VID* const vid = Vid();
            if (vid->nvid() > 20)
            {
                const int weaponValue = vid->GetMaxAmmo();
                const int currentAmmo = UNIT::Action(
                    static_cast<int>(ActionCode::ACT_GET_AMMO), 0, 0, 0);
                static_cast<void>(UNIT::Action(
                    static_cast<int>(ActionCode::ACT_ADD_AMMO),
                    static_cast<std::intptr_t>(manSub32Wrap(weaponValue, currentAmmo)),
                    0, 0));
                return 0;
            }

            VID* const link = vid->linkedVid();
            const int currentAmmo = UNIT::Action(
                static_cast<int>(ActionCode::ACT_GET_AMMO), 0, 0, 0);
            const int storedAmmo = man->weaponAmmo[link->nvid() - 10];
            static_cast<void>(UNIT::Action(
                static_cast<int>(ActionCode::ACT_ADD_AMMO),
                static_cast<std::intptr_t>(manSub32Wrap(storedAmmo, currentAmmo)),
                0, 0));
            return 0;
        }

        default:


            return UNIT::Action(
                opcode, static_cast<std::intptr_t>(argument1), argument2, argument3);
        }

    }


    void MAN::MoveTact()
    {
        VECTOR candidate{X(), Y(), Z()};
        computeNextMovementPosition(&candidate.x, &candidate.y, &candidate.z);

        const float applicationSizeX = core::ApplicationMapWidth();
        const float applicationSizeY = core::ApplicationMapHeight();

        const bool changedXY =
            (candidate.x != X() && !std::isunordered(candidate.x, X())) ||
            (candidate.y != Y() && !std::isunordered(candidate.y, Y()));
        if (changedXY &&
            candidate.x >= 0.0f && candidate.x < applicationSizeX &&
            candidate.y >= 0.0f && candidate.y < applicationSizeY &&
            CanPlaceWithCrushAndGlide(&candidate.x, &candidate.y, &candidate.z) == nullptr)
        {
            ChangeCoor(candidate.x, candidate.y, candidate.z);
        }

        if (Goal() && (runtimeFlags() & SPRITE::CommandBitsMask) == 4u)
        {
            const int reverse = Speed() >= 0.0f ? 0 : 128;
            const std::uint32_t delta = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
            const int desired = DirectionTo(Goal()).Int() + reverse;
            const ANGLE turn = GlideDirection(ANGLE(static_cast<unsigned char>(desired)));
            RotateTact(turn, delta);

            if ((runtimeFlags() & SPRITE::CrossedGoalAxesMask) == SPRITE::CrossedGoalAxesMask)
            {
                Stop();
            }
            else
            {
                SPRITE* const target = Goal();
                VID* const vid = Vid();
                VID* const targetVid = target->Vid();
                if (static_cast<double>(vid->halfSizeX()) + targetVid->halfSizeX() >
                        std::fabs(static_cast<double>(X()) - target->X()) &&
                    static_cast<double>(vid->halfSizeY()) + targetVid->halfSizeY() >
                        std::fabs(static_cast<double>(Y()) - target->Y()))
                {
                    Stop();
                }
            }
        }
    }


    int MAN::ChangeWeapon(int weapon) noexcept
    {
        VID* const currentLinkVid = Vid()->linkedVid();
        if (currentLinkVid->nvid() > 20)
            return 0;

        if (weapon == 10)
            weapon = 0;

        if (findLastCommandWord(weapon + 0x104) < 0)
            return 0;

        SPRITE* const link = childChain();
        if (!link || link->Vid() != currentLinkVid)
            return 0;

        const int currentWeapon = currentLinkVid->nvid() - 10;
        weaponAmmo[currentWeapon] = UNIT::Action(static_cast<int>(ActionCode::ACT_GET_AMMO), 0, 0, 0);

        link->dispatchVirtualAction(ActionCode::ACT_CHANGE_VID, weapon + 10, 0, 0);
        Vid()->setLinkedVid(mapOwner()->Vid(weapon + 10));

        const int currentAmmo = UNIT::Action(static_cast<int>(ActionCode::ACT_GET_AMMO), 0, 0, 0);
        UNIT::Action(static_cast<int>(ActionCode::ACT_ADD_AMMO),
                     static_cast<std::intptr_t>(weaponAmmo[weapon] - currentAmmo), 0, 0);
        return 1;
    }
}
