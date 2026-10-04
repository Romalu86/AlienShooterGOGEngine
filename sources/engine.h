#pragma once

#include "unit.h"
#include <cmath>
#include <cstdint>

namespace as1
{
    using R_POS = core::R_POS;

    struct TRAIN_INFO;

    class ENGINE : public UNIT
    {
    public:

        static int globaldeleting;
        ENGINE(VID* vid, float x, float y, float z, ANGLE direction, SPRITE* parent = nullptr);
        ~ENGINE() override;
        void MoveTact() override;
        void MoveEngineTact() noexcept;
        void Stop() noexcept;
        void SetCommandToTrain(int command, int argument1, int argument2, int argument3) noexcept;
        void SetCommandToTrain(int command, int x, int y) noexcept;

        void ReverseTrain() noexcept;

        void ReCalcMoveParameters() noexcept;

        ENGINE* GetIntersecting() noexcept;

        int IsTouch(ENGINE* engine, int fromMoveTact) noexcept;

        void ClearDotBusy() noexcept;

        void SetDotBusy() noexcept;

        void PullTail(const R_POS* oldHead) noexcept;
        int Action(int opcode, std::intptr_t argument1Payload, int argument2Value, int argument3Value) override;
        void DeletePointerToSprite(SPRITE* sprite) override;
        void DrawDebugOverlay() override;
        void DrawRelationDebugOverlay() override;

        void RepairReciprocalChainLinks() noexcept;

        int RepairByRepair(ENGINE* target) noexcept;
        void RepairTact() noexcept;
        void AddAmmoTact() noexcept;
        SPRITE* findEngineChainSpecialWeaponNode() noexcept;
        int engineChainContainsArmy(int bucket) noexcept;
        int updateCombatDecision() noexcept;
        void stopEngineChain() noexcept;
        void setEnginePathGoalFromCoordinates(float x, float y, float z, int unusedParameter, int project2D) noexcept;

        SPRITE* chainPrevious() const noexcept { return engineChainPrevious(); }
        SPRITE* chainNext() const noexcept { return engineChainNext(); }
        int productionBatchCompletionPending() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::ProductionBatchCompletionPending);
        }

        int isBusy() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::EngineBusy);
        }
        void setBusy(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineBusy) = value;
        }

        int repairLinkHp() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::EngineRepairLinkHp);
        }
        void setRepairLinkHp(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineRepairLinkHp) = value;
        }

        int productionSequenceId() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::ProductionSequenceId);
        }

        void setChainPrevious(SPRITE* value) noexcept { setEngineChainPrevious(value); }
        void setChainNext(SPRITE* value) noexcept { setEngineChainNext(value); }
        void setProductionBatchCompletionPending(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::ProductionBatchCompletionPending) = value;
        }
        void setProductionSequenceId(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::ProductionSequenceId) = value;
        }

    private:

        std::array<std::uint8_t, 0xA30> m_engineStateStorage;
    };


    struct TRAIN_INFO
    {
        std::uint32_t flags = 0;
        float maxBattleRange = 0.0f;
        float minBattleRange = 999999.0f;
        float power = 0.0f;
        float weight = 0.0f;
        float trainweight = 0.0f;
        int speed = 10000;
        int no = 0;
        int hp = 0;
        int max_hp = 0;
        int weapon = 0;
        int build_time = 0;
        int noAmmo = 0;
        int ammo = 0;
        int maxAmmo = 0;
        int percentAmmo = 0;


        explicit TRAIN_INFO(const ENGINE* engine) noexcept;

        void AddEngine(const ENGINE* engine) noexcept;
        __forceinline int CanMove() const noexcept { return Acceleration() > 7; }
        __forceinline int HaveAmmo() const noexcept { return weapon > 0; }
        __forceinline int IsDamaged() const noexcept { return hp < max_hp; }
        __forceinline int NeedAmmo() const noexcept { return percentAmmo < 100; }
        int HaveAmmoWagon() const noexcept { return (flags & 1u) != 0u; }
        int HaveRepair() const noexcept { return (flags & 2u) != 0u; }
        __forceinline int Acceleration() const noexcept
        {
            if (weight == 0.0f || std::isnan(weight))
                return 0;
            const long double scaled =
                (static_cast<long double>(power) / static_cast<long double>(weight)) * 8.0L;
            if (!std::isfinite(scaled) ||
                scaled >= 9223372036854775808.0L || scaled < -9223372036854775808.0L)
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(scaled));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }
    };


#if defined(_M_IX86)
    static_assert(sizeof(ENGINE) == 0xAC0, "ENGINE size");
    static_assert(SpriteLayout::DerivedStateBase == sizeof(UNIT), "ENGINE derived-state base");
    static_assert(SpriteLayout::PathBufferSize + sizeof(int) == sizeof(ENGINE), "ENGINE tail field");
#endif

}
