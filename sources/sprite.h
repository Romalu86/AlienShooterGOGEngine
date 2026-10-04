#pragma once
#include "sprite_ptr.h"
#include "core/log.h"
#include "core/types.h"
#include "sprite_act_const.h"
#include "vid/vid.h"
#include <vector>
#include <string>
#include <array>
#include <map>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <cstring>


namespace as1
{

    extern std::array<std::uint32_t, 1024> g_directionTrigWindow;

    class INPUT;


    extern int g_pathSearchSecondaryBestCost;
    extern int g_pathSearchResultScore;
    class SPRITE;
    template <int Indexed> class BaseSpriteList;
    namespace core { template <class T> class List; class R_DOT; struct R_POS; }
    class VID;
    class MAP;
    class MENU;
    extern MAP* Map;
    class GRAPH;
    class STRING;
    class BaseStream;

    namespace SpriteLayout
    {
        constexpr std::size_t CommandSerializationBias = 0x3Cu;
        constexpr std::size_t SharedPrimaryState = 0x78u;
        constexpr std::size_t SharedSecondaryState = 0x7Cu;
        constexpr std::size_t ExtendedStateBase = 0x8Cu;
        constexpr std::size_t AmmoFixedPoint = 0x80u;
        constexpr std::size_t TurnTimer = 0x88u;
        constexpr std::size_t BehaviorFlags = 0x8Cu;

        constexpr std::size_t LinkerX = 0x70u;
        constexpr std::size_t LinkerY = 0x74u;
        constexpr std::size_t LinkerZ = 0x78u;
        constexpr std::size_t LinkerDirection = 0x7Cu;
        constexpr std::size_t LinkerOwner = 0x80u;
        constexpr std::size_t DerivedStateBase = 0x90u;


        constexpr std::size_t EngineChainPrevious = 0x94u;
        constexpr std::size_t EngineChainNext = 0x98u;
        constexpr std::size_t EngineCommandReferenceOwner = 0x9Cu;
        constexpr std::size_t EngineCommandArgument0 = 0xA0u;
        constexpr std::size_t EngineCommandArgument1 = 0xA4u;
        constexpr std::size_t EngineCommandArgument2 = 0xA8u;
        constexpr std::size_t EngineAccelerationDelay = 0xACu;
        constexpr std::size_t EngineTargetSpeed = 0xB0u;
        constexpr std::size_t PushLineActive = 0xB4u;
        constexpr std::size_t EngineRepairLinkHp = 0x70u;
        constexpr std::size_t PrimaryPathNode = 0xB8u;
        constexpr std::size_t PrimaryPathProgress = 0xBCu;
        constexpr std::size_t PrimaryPathAuxiliary = 0xC0u;
        constexpr std::size_t PrimaryPathEdgeIndex = 0xC4u;
        constexpr std::size_t SecondaryPathNode = 0xC8u;
        constexpr std::size_t SecondaryPathProgress = 0xCCu;
        constexpr std::size_t SecondaryPathAuxiliary = 0xD0u;
        constexpr std::size_t SecondaryPathEdgeIndex = 0xD4u;
        constexpr std::size_t EngineBusy = 0xD8u;
        constexpr std::size_t ProductionBatchCompletionPending = 0xDCu;
        constexpr std::size_t PreviousPathX = 0xE0u;
        constexpr std::size_t PreviousPathY = 0xE4u;
        constexpr std::size_t PreviousPathZ = 0xE8u;
        constexpr std::size_t RouteActionReady = 0xECu;
        constexpr std::size_t RouteActionStartTime = 0xF0u;
        constexpr std::size_t ProductionSequenceId = 0xF4u;
        constexpr std::size_t PathBuffer = 0xF8u;
        constexpr std::size_t PathBufferSize = 0xABCu;
        constexpr std::size_t WordStride = sizeof(std::uint32_t);
    }

    enum SpriteTypeMask : DWORD
    {
        U_TERRAIN = 1,
        U_OBJECT = 2,
        U_UNIT = 4,
        U_MONSTER = 4,
        U_AVIA = 8,
        U_MENU = 16,
        U_RAILWAY = 32,
        U_REGION = 64,
        U_CANNON = 512,
        U_SPRITE = 1024
    };

    enum SpriteClassId : DWORD
    {

        B_TERRAIN = 0,
        B_OBJECT = 1,
        B_UNIT = 2,
        B_BUILDING = 3,
        B_AVIA = 4,
        B_CANNON = 5,
        B_PRIMITIVE = 6,
        B_MAN = 7,
        B_BUILDEDTERRAIN = 8,
        B_SPRITE = 9,
        B_FRAME = 10,
        B_BALL = 11,
        B_LINKER = 12,
        B_TEXT = 19,
        B_CIV_ROBOT = 20,
        B_ENGINE = 21,
        B_RAIL = 22,
        B_REGION = 23,
        B_DEPO = 24,
        B_CREATURE = 25,
        B_BALLOON = 26,
        B_MISSILE = 27
    };

    enum ObjectPropertyFlag : DWORD
    {
        P_RANDBIRTH = 1u << 0,
        P_GRAVITY = 1u << 1,
        P_GRAVITY2 = 1u << 2,
        P_BUILDSIZETOGRIDZ = 1u << 3,
        P_TRACK = 1u << 4,
        P_BUILDVIDZTOGRIDZ = 1u << 5,
        P_HASH = 1u << 6,
        P_MAP = 1u << 6,
        P_BIRTHASSMOKE = 1u << 7,
        P_NOISE = 1u << 8,
        P_ZEROZ = 1u << 9,
        P_RANDSPEED = 1u << 10,
        P_GAMMA = 1u << 11,
        P_WIND = 1u << 12,
        P_SKIPMAPED = 1u << 13,
        P_CRUSH = 1u << 14,
        P_ALWAYSTOP = 1u << 15,
        P_WAVE = 1u << 16,
        P_INVISIBLEFORENEMY = 1u << 17,
        P_CREATECHILDEND = 1u << 18,
        P_VERTDIR = 1u << 19,
        P_MOVEWITHANYDIRECTION = 1u << 20,
        P_BLUR = 1u << 21,
        P_RANDZSPEED = 1u << 22,
        P_DBLLIGHT = 1u << 23,
        P_ONEPHASE = 1u << 24,
        P_NOTCHANGELINKERCOOR = 1u << 25,
        P_RADIALDAMAGE = 1u << 26,
        P_SELFMOVING = 1u << 27,
        P_BOUNCE = 1u << 28,
        P_HARDWAREDIRECT = 1u << 29,
        P_GROUND = 1u << 30,
        P_MAPPEDBUILD = 1u << 30,
        P_NOTDAMAGEFORFRIEND = 1u << 31
    };

    struct ACT
    {
        std::uint32_t opcode = 0;
        std::uint32_t argument1 = 0;
        std::uint32_t argument2 = 0;
        std::uint32_t argument3 = 0;
    };


    namespace core
    {


        template <>
        class List<ACT>
        {
        public:
            List();
            List(const List& other) = delete;
            List& operator=(const List& other) = delete;
            ~List();

            void clear();
            __forceinline
            void releaseCommandRecordsTail();
            void clearTargetReferences(SPRITE* target);


            void append(ACT action);

            void insertAt(std::uint32_t index, ACT action);

            void resize(std::uint32_t requiredCapacity);


            __forceinline
            void setCommandRecordCount(std::uint32_t count);
            void saveCommandRecordsToStream(BaseStream* stream);
            void restoreCommandRecordsFromStream(BaseStream* stream, const SPRITE* ownerSprite);
            bool restoreOldMapCommandRecordsFromStream(BaseStream* stream, int mapVersion, const SPRITE* ownerSprite, int* armyBucket);

            struct CommandRecordStorage
            {
                std::uint32_t words[4];
            };

            struct CommandRecordList
            {
                CommandRecordList() noexcept;
                virtual ~CommandRecordList();

                std::uint32_t count = 0;
                std::uint32_t capacity = 0;
                CommandRecordStorage* records = nullptr;
            };


            int No() const noexcept;

            ACT* operator[](int index) noexcept
            {
                return reinterpret_cast<ACT*>(m_commandRecords.records + index);
            }
            const ACT* operator[](int index) const noexcept
            {
                return reinterpret_cast<const ACT*>(m_commandRecords.records + index);
            }
            size_t size() const { return m_commandRecords.count; }
            bool empty() const { return m_commandRecords.count == 0; }

        private:
            friend class ::as1::SPRITE;

            __forceinline
            void ensureCommandRecordCapacity(std::uint32_t requiredCapacity);
            __forceinline
            void writeCommandRecord(std::size_t index, const ACT& command);

            CommandRecordList m_commandRecords;
        };
    }


    struct EX_SPRITE_DATA
    {
        explicit EX_SPRITE_DATA(SPRITE* source) noexcept;

        struct ItemList
        {
            ItemList() noexcept;
            virtual ~ItemList();

            std::uint32_t count = 0;
            std::uint32_t capacity = 0;
            std::int32_t* values = nullptr;
        };

        float sourceX;
        float sourceY;
        float sourceZ;
        std::uint32_t changeCoorTime;
        std::uint32_t lifetimeRemaining;
        std::uint32_t childCadence;
        std::uint32_t effectTimestamp;
        float effectCurvePosition;
        union
        {
            std::uint32_t maxSpeedBits;
            float maxSpeed;
        };
        std::uint32_t gammaFirst;
        std::uint32_t gammaSecond;
        ItemList items;
    };

#if UINTPTR_MAX == 0xFFFFFFFFu
    static_assert(sizeof(core::List<ACT>::CommandRecordList) == 0x10, "ACT list subobject size");
    static_assert(sizeof(EX_SPRITE_DATA::ItemList) == 0x10, "command-word list subobject size");
    static_assert(offsetof(EX_SPRITE_DATA, changeCoorTime) == 0x0C, "EX_SPRITE_DATA change-coordinate time offset");
    static_assert(offsetof(EX_SPRITE_DATA, lifetimeRemaining) == 0x10, "EX_SPRITE_DATA lifetime offset");
    static_assert(offsetof(EX_SPRITE_DATA, effectTimestamp) == 0x18, "EX_SPRITE_DATA effect timestamp offset");
    static_assert(offsetof(EX_SPRITE_DATA, effectCurvePosition) == 0x1C, "EX_SPRITE_DATA effect curve offset");
    static_assert(offsetof(EX_SPRITE_DATA, maxSpeedBits) == 0x20, "EX_SPRITE_DATA max speed offset");
    static_assert(offsetof(EX_SPRITE_DATA, gammaFirst) == 0x24, "EX_SPRITE_DATA gamma0 offset");
    static_assert(offsetof(EX_SPRITE_DATA, gammaSecond) == 0x28, "EX_SPRITE_DATA gamma1 offset");
    static_assert(offsetof(EX_SPRITE_DATA, items) == 0x2C, "EX_SPRITE_DATA items offset");
    static_assert(sizeof(EX_SPRITE_DATA) == 0x3C, "EX_SPRITE_DATA size");
#endif


    class SPRITE
    {
    public:
        SPRITE(VID* vid, float x, float y, float z, ANGLE dir, SPRITE* parent = nullptr);
        virtual ~SPRITE();


        VID* Vid() const { return m_vid; }

        float X() const { return m_xyz.x; }

        float Y() const { return m_xyz.y; }

        float Z() const { return m_xyz.z; }

        float xCoordinateValue() const noexcept { return m_xyz.x; }
        float yCoordinateValue() const noexcept { return m_xyz.y; }
        const VECTOR& xyz() const { return m_xyz; }


        __declspec(noinline) ANGLE Direction() const;
        int directionIndex() const noexcept { return m_direction.Int(); }
        int directionIndexValue() const noexcept { return m_direction.Int(); }

        ANGLE DirectionTo(const SPRITE* sprite) const;
        __declspec(noinline)
        ANGLE DirectionTo(const SPRITE* sprite, int* projectedLength) const;
        int Animation() const { return m_currentAnimation; }
        void setCurrentAnimationDirect(int value) noexcept { m_currentAnimation = value; }
        int currentFrame() const { return m_currentFrame; }
        void setCurrentFrameDirect(int value) noexcept { m_currentFrame = value; }
        void setXPosition(float value) noexcept { m_xyz.x = value; }
        void setYPosition(float value) noexcept { m_xyz.y = value; }
        int currentFrameBegin() const { return m_currentFrameBegin; }

        int currentFrameEnd() const { return m_currentFrameEnd; }
        void ChangeAnimation(int animationId);
        virtual int Action(int opcode, std::intptr_t argument1Payload, int argument2Value, int argument3Value);
        virtual void Tact();
        virtual void MoveTact();
        virtual void DeletePointerToSprite(SPRITE* sprite);
        virtual void Draw();
        virtual void DrawDebugOverlay();
        virtual void DrawRelationDebugOverlay();

        void DrawRectangle();

        float Speed() const { return m_speed; }
        void setSpeedDirect(float value) noexcept { m_speed = value; }
        __forceinline void ChangeSpeed(float value) noexcept { m_speed = value; }
        float ZSpeed() const { return m_zSpeed; }
        void setZSpeedDirect(float value) noexcept { m_zSpeed = value; }
        __forceinline void syncExDataMaxSpeedFromVid() noexcept
        {
            if (!m_exData || !m_vid)
                return;
            m_exData->maxSpeed = m_vid->maxSpeedValue();
        }
        __forceinline void setRuntimeMaxSpeedDirect(float value) noexcept
        {
            m_exData->maxSpeed = value;
        }

        __forceinline float MaxSpeed() const noexcept
        {
            return m_exData ? m_exData->maxSpeed : m_vid->maxSpeedValue();
        }

        DWORD Timer() const { return m_actionTimer; }
        int AddListReference()
        {
            m_listReferenceCount = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(m_listReferenceCount) + 1u);
            return m_listReferenceCount;
        }
        int Release()
        {
            m_listReferenceCount = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(m_listReferenceCount) - 1u);
            const int refs = m_listReferenceCount;
            if (refs > 0)
                return refs;
            if (refs >= 0)
            {
                delete this;
                return 0;
            }
    
    
            const int nvid = m_vid ? m_vid->nVid : -1;
            logFileLoggerResourceError(g_fileLogger, "SPRITE %i", 4,
                                       "noRef at Release", refs, nvid);
            return 0;
        }
        int listReferenceCount() const { return m_listReferenceCount; }
        __forceinline MAP* mapOwner() const noexcept
        {
            return Map;
        }
        VID* vidPointer() const noexcept { return m_vid; }
        void setVidPointerDirect(VID* value) noexcept { m_vid = value; }
        void setListReferenceCount(int value) noexcept { m_listReferenceCount = value; }
        SPRITE* bestTargetSprite() const noexcept { return m_bestTargetSprite; }
        SPRITE* parentSprite() const noexcept { return m_childBacklink; }
        void setBestTargetSprite(SPRITE* value) noexcept { m_bestTargetSprite = value; }
        void SetGamma(const Gamma& gamma) noexcept;
        __forceinline int dispatchVirtualAction(std::uint32_t opcode, int argument1, int argument2, int argument3) noexcept
        {
            return Action(static_cast<int>(opcode), static_cast<std::intptr_t>(argument1), argument2, argument3);
        }
        int dispatchVirtualAction(ActionCode opcode, int argument1, int argument2, int argument3) noexcept
        {
            return dispatchVirtualAction(static_cast<std::uint32_t>(opcode), argument1, argument2, argument3);
        }
        void SetGoal(SPRITE* goal) noexcept;
        int SetCommand(int argument1, SPRITE* goal) noexcept;
        int SetCommandWithoutLink(int argument1, SPRITE* goal) noexcept;

        void Move(SPRITE* goal) noexcept;
        void ChangeDirection(ANGLE direction) noexcept;
        void updateLinkerCoordinateForDirection(ANGLE direction) noexcept;
        SPRITE* CanPlace(float x, float y, float z);
        SPRITE* CanPlaceWithCrush(float x, float y, float z);
        SPRITE* probeMovementFootprint(float x, float y) noexcept;
        static float directionSinValue(int index) noexcept
        {
            float value;
            std::memcpy(&value, &g_directionTrigWindow[static_cast<std::size_t>(index & 0xFF)], sizeof(value));
            return value;
        }
        static float directionSinUncheckedValue(DWORD index) noexcept
        {
            float value;
            std::memcpy(&value, &g_directionTrigWindow[index], sizeof(value));
            return value;
        }
        static float directionCosValue(int index) noexcept
        {
            float value;
            std::memcpy(&value, &g_directionTrigWindow[256u + static_cast<std::size_t>(index & 0xFF)], sizeof(value));
            return value;
        }
        static float directionSinAuxValue(int index) noexcept
        {
            float value;
            std::memcpy(&value, &g_directionTrigWindow[512u + static_cast<std::size_t>(index & 0xFF)], sizeof(value));
            return value;
        }
        static float directionCosAuxValue(int index) noexcept
        {
            float value;
            std::memcpy(&value, &g_directionTrigWindow[768u + static_cast<std::size_t>(index & 0xFF)], sizeof(value));
            return value;
        }

        ANGLE GlideDirection(ANGLE value) noexcept;


        ANGLE RotateTact(ANGLE value, std::uint32_t deltaMs) noexcept;

        int AttackTact(int deltaTime) noexcept;

        SPRITE* SeekEnemy();
        int enemyPriority(float candidateMetric, float selectedMetric, SPRITE* candidate, SPRITE* selected) noexcept;
        void ResetActionStack() noexcept;
        int traceMovementCollisionTo(float* xOut, float* yOut, float* zOut) noexcept;
        void Stop();
        int StartMove() noexcept;
        int PercentHp() noexcept;
        void ChangeHp(int newHp) noexcept;

        void PlaySFX(int nsfx) noexcept;
        void triggerAnimationSlotEvent(int animationSlot) noexcept;
        int CanShotEnemy(const SPRITE* owner) const noexcept;
        int HaveLink() const noexcept;
        int Attack(SPRITE* owner) noexcept;
        int GetFireDamage() noexcept;
        SPRITE* engineChainHead() noexcept;
        SPRITE* engineChainTail() noexcept;
        bool isInEngineChain(SPRITE* target) noexcept;
        SPRITE* findCrossingConstraintOwner() noexcept;
        SPRITE* resolvePathOwnerRelation(int* relationOut) noexcept;
        int scaledEngineChainLength() noexcept;
        void approachEngineTargetSpeed(float* speedOut) noexcept;
        void updatePositionFromPathEndpoints() noexcept;
        void splitEngineChainAtPosition(float x, float y) noexcept;
        void initializeEnginePathEndpoints() noexcept;
        int attachEngineChain(SPRITE* value) noexcept;
        int canLinkEngineChain(SPRITE* target) noexcept;
        float resolveEngineChainCollision(SPRITE* target, int mode) noexcept;
        int resolveEngineChainPathInteraction(core::R_POS* pathPair, float* distanceOut) noexcept;
        void applyEngineChainPathMovement(core::R_POS* pathPair, float speed, int delay) noexcept;
        void clearCommandsTargetingThisSprite() noexcept;
        int createRouteMarkerSprites(core::R_DOT* pathNode) noexcept;
        int createPathSpritesFromBuffer(core::R_DOT* pathNode, BaseSpriteList<0>* list, int nvid) noexcept;
        int evaluateEngineTargetRangeState() noexcept;
        int pathBufferReachesSecondaryTarget(core::R_DOT* pathNode) noexcept;
        void suppressDrawRecursive() noexcept;
        void restoreDrawRecursive() noexcept;
        int Hp() const noexcept { return m_hp; }
        void setHpDirect(int value) noexcept { m_hp = value; }

        EX_SPRITE_DATA* ExData() noexcept { return m_exData; }
        const EX_SPRITE_DATA* ExData() const noexcept { return m_exData; }


        __forceinline core::List<ACT>* ActionStack() noexcept { return &m_commandStack; }
        bool hasExData() const noexcept { return m_exData != nullptr; }
        std::uint32_t exDataLifeTime() const noexcept { return m_exData ? m_exData->lifetimeRemaining : 0; }
        std::uint32_t exDataChildCadence() const noexcept { return m_exData ? m_exData->childCadence : 0; }

        Gamma GetGamma() const noexcept;
        bool spriteGammaOverride(Gamma& out) const noexcept
        {
            if (!m_exData ||
                (m_exData->gammaFirst == 0u && m_exData->gammaSecond == 0u))
                return false;
            out.first = m_exData->gammaFirst;
            out.second = m_exData->gammaSecond;
            return true;
        }
        float exDataX() const noexcept { return m_exData ? m_exData->sourceX : 0.0f; }
        float exDataY() const noexcept { return m_exData ? m_exData->sourceY : 0.0f; }
        float exDataZ() const noexcept { return m_exData ? m_exData->sourceZ : 0.0f; }
        float exDataEffectCurvePosition() const noexcept { return m_exData->effectCurvePosition; }
        float blurHistoryX() const noexcept { return m_exData->sourceX; }
        float blurHistoryY() const noexcept { return m_exData->sourceY; }
        float blurHistoryZ() const noexcept { return m_exData->sourceZ; }
        std::uint32_t applicationBucketTime() const noexcept { return m_applicationBucketTime; }
        void setApplicationBucketTime(std::uint32_t value) noexcept { m_applicationBucketTime = value; }
        std::uint32_t createTime() const noexcept { return m_createTime; }
        void setCreateTime(std::uint32_t value) noexcept { m_createTime = value; }
        static constexpr unsigned CommandBitsShift = 2u;
        static constexpr DWORD CommandValueMask = 31u;
        static constexpr DWORD CommandBitsMask = CommandValueMask << CommandBitsShift;
        static constexpr unsigned ArmyBitsShift = 11u;
        static constexpr DWORD ArmyValueMask = 3u;
        static constexpr DWORD ArmyBitsMask = ArmyValueMask << ArmyBitsShift;
        static constexpr DWORD MovementStartedFlag = 0x00000080u;
        static constexpr DWORD SpatialHashRemovedFlag = 0x00000100u;
        static constexpr DWORD ChildSpawnToggleFlag = 0x00002000u;
        static constexpr DWORD CrossedGoalXFlag = 0x00004000u;
        static constexpr DWORD CrossedGoalYFlag = 0x00008000u;
        static constexpr DWORD CrossedGoalAxesMask = 0x0000C000u;
        static constexpr DWORD DrawSuppressedFlag = 0x00010000u;

        DWORD runtimeFlags() const noexcept { return m_runtimeFlags; }
        DWORD commandBits() const noexcept { return m_runtimeFlags & CommandBitsMask; }
        int commandIndex() const noexcept { return static_cast<int>((m_runtimeFlags >> CommandBitsShift) & CommandValueMask); }
        DWORD armyBits() const noexcept { return m_runtimeFlags & ArmyBitsMask; }
        int armyIndex() const noexcept { return static_cast<int>((m_runtimeFlags >> ArmyBitsShift) & ArmyValueMask); }
        bool sameArmy(const SPRITE& other) const noexcept { return armyIndex() == other.armyIndex(); }

        int IsCommand(int value) const noexcept { return value == commandIndex() ? 1 : 0; }
        void setRuntimeFlags(DWORD value) noexcept { m_runtimeFlags = value; }
        int attackDecisionCode() const noexcept { return m_attackDecisionCode; }
        void setAttackDecisionCode(int value) noexcept { m_attackDecisionCode = value; }

        __declspec(noinline) SPRITE* Goal() const noexcept;
        void setGoalSpriteDirect(SPRITE* value) noexcept { m_goalSprite = value; }
        std::uint32_t actionTimer() const noexcept { return m_actionTimer; }
        void setActionTimer(std::uint32_t value) noexcept { m_actionTimer = value; }
        __forceinline float linkerX() const noexcept
        {
            return *reinterpret_cast<const float*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::LinkerX);
        }
        __forceinline float linkerY() const noexcept
        {
            return *reinterpret_cast<const float*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::LinkerY);
        }
        __forceinline float linkerZ() const noexcept
        {
            return *reinterpret_cast<const float*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::LinkerZ);
        }
        __forceinline int linkerDirection() const noexcept
        {
            return static_cast<int>(*reinterpret_cast<const unsigned char*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::LinkerDirection));
        }
        __forceinline SPRITE* linkerOwner() const noexcept
        {
            return *reinterpret_cast<SPRITE* const*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::LinkerOwner);
        }
        int sharedPrimaryState() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::SharedPrimaryState);
        }
        void setSharedPrimaryState(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::SharedPrimaryState) = value;
        }
        int sharedSecondaryState() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::SharedSecondaryState);
        }
        void setSharedSecondaryState(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::SharedSecondaryState) = value;
        }
        __forceinline int ammoFixedPoint() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::AmmoFixedPoint);
        }
        __forceinline int turnTimer() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::TurnTimer);
        }
        __forceinline void setTurnTimer(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::TurnTimer) = value;
        }
        __forceinline int behaviorFlags() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::BehaviorFlags);
        }
        __forceinline void setBehaviorFlags(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::BehaviorFlags) = value;
        }
        bool isDrawSuppressed() const noexcept { return (m_runtimeFlags & DrawSuppressedFlag) != 0; }

        SPRITE* childChain() const noexcept { return m_childChain; }
        SPRITE* engineChainPrevious() const noexcept
        {
            return *reinterpret_cast<SPRITE* const*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::EngineChainPrevious);
        }

        SPRITE* engineChainNext() const noexcept
        {
            return *reinterpret_cast<SPRITE* const*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::EngineChainNext);
        }
        SPRITE* engineCommandReferenceOwner() const noexcept
        {
            return *reinterpret_cast<SPRITE* const*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::EngineCommandReferenceOwner);
        }
        SPRITE* childBacklink() const noexcept { return m_childBacklink; }
        void setChildChain(SPRITE* value) noexcept { m_childChain = value; }
        void setEngineChainPrevious(SPRITE* value) noexcept
        {
            *reinterpret_cast<SPRITE**>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineChainPrevious) = value;
        }
        void setEngineChainNext(SPRITE* value) noexcept
        {
            *reinterpret_cast<SPRITE**>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineChainNext) = value;
        }
        void setEngineCommandReferenceOwner(SPRITE* value) noexcept
        {
            *reinterpret_cast<SPRITE**>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineCommandReferenceOwner) = value;
        }

        SPRITE*& engineChainPreviousRef() noexcept
        {
            return *reinterpret_cast<SPRITE**>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineChainPrevious);
        }
        SPRITE*& engineChainNextRef() noexcept
        {
            return *reinterpret_cast<SPRITE**>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineChainNext);
        }
        SPRITE*& engineCommandReferenceOwnerRef() noexcept
        {
            return *reinterpret_cast<SPRITE**>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineCommandReferenceOwner);
        }
        int& engineCommandArgument0Ref() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineCommandArgument0);
        }
        int& engineCommandArgument1Ref() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineCommandArgument1);
        }
        int& engineCommandArgument2Ref() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineCommandArgument2);
        }
        int& engineAccelerationDelayRef() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineAccelerationDelay);
        }
        float& engineTargetSpeedRef() noexcept
        {
            return *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineTargetSpeed);
        }
        int& pushLineActiveRef() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::PushLineActive);
        }
        core::R_DOT*& primaryPathNodeRef() noexcept
        {
            return *reinterpret_cast<core::R_DOT**>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::PrimaryPathNode);
        }
        int& primaryPathProgressRef() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::PrimaryPathProgress);
        }
        int& primaryPathAuxiliaryRef() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::PrimaryPathAuxiliary);
        }
        int& primaryPathEdgeIndexRef() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::PrimaryPathEdgeIndex);
        }
        core::R_DOT*& secondaryPathNodeRef() noexcept
        {
            return *reinterpret_cast<core::R_DOT**>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::SecondaryPathNode);
        }
        int& secondaryPathProgressRef() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::SecondaryPathProgress);
        }
        int& secondaryPathAuxiliaryRef() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::SecondaryPathAuxiliary);
        }
        int& secondaryPathEdgeIndexRef() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::SecondaryPathEdgeIndex);
        }
        float& previousPathXRef() noexcept
        {
            return *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::PreviousPathX);
        }
        float& previousPathYRef() noexcept
        {
            return *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::PreviousPathY);
        }
        float& previousPathZRef() noexcept
        {
            return *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::PreviousPathZ);
        }
        int& routeActionReadyRef() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::RouteActionReady);
        }
        int& routeActionStartTimeRef() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::RouteActionStartTime);
        }
        int& pathBufferSizeRef() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::PathBufferSize);
        }
        core::R_DOT* primaryPathNode() const noexcept
        {
            return *reinterpret_cast<core::R_DOT* const*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::PrimaryPathNode);
        }
        int primaryPathEdgeIndex() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::PrimaryPathEdgeIndex);
        }
        core::R_DOT* secondaryPathNode() const noexcept
        {
            return *reinterpret_cast<core::R_DOT* const*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::SecondaryPathNode);
        }
        int secondaryPathEdgeIndex() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::SecondaryPathEdgeIndex);
        }
        int engineCommandArgument0Value() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::EngineCommandArgument0);
        }
        void setEngineCommandArgument0(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineCommandArgument0) = value;
        }
        int engineCommandArgument1Value() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::EngineCommandArgument1);
        }
        void setEngineCommandArgument1(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineCommandArgument1) = value;
        }
        core::R_DOT* engineCommandArgument0Node() const noexcept { return reinterpret_cast<core::R_DOT*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(engineCommandArgument0Value()))); }
        core::R_DOT* engineCommandArgument1Node() const noexcept { return reinterpret_cast<core::R_DOT*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(engineCommandArgument1Value()))); }
        core::R_DOT* engineCommandArgument2Node() const noexcept
        {
            const int value = *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::EngineCommandArgument2);
            return reinterpret_cast<core::R_DOT*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(value)));
        }
        std::uint32_t commandRecordCount() const noexcept { return m_commandStack.m_commandRecords.count; }
        std::uint32_t commandRecordWord(std::size_t index, std::size_t word) const noexcept { return m_commandStack.m_commandRecords.records[index].words[word]; }
        unsigned char* pathBufferData() noexcept
        {
            return reinterpret_cast<unsigned char*>(this) + SpriteLayout::PathBuffer;
        }
        const unsigned char* pathBufferData() const noexcept
        {
            return reinterpret_cast<const unsigned char*>(this) + SpriteLayout::PathBuffer;
        }
        int pathBufferSize() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::PathBufferSize);
        }
        void setPathBufferSize(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::PathBufferSize) = value;
        }
        void setChildBacklink(SPRITE* value) noexcept { m_childBacklink = value; }
        int steerAwayFromMapBoundary(float x, float y) noexcept;
        void computeNextMovementPosition(float* xOut, float* yOut, float* zOut) noexcept;
        SPRITE* CanPlaceWithCrushAndGlide(float* xOut, float* yOut, float* zOut);
        void ChangeCoor(float x, float y, float z) noexcept;

        void CreateChild();


        void Remove();

        void Insert();
        unsigned int serializeSpriteRecord(RESOURCE* resource) noexcept;
        int ChangeArmy(int bucketIndex) noexcept;
        void ensureLinkedVidChild() noexcept;

        int insertChildChainHead(SPRITE* child);
        int appendChildChain(SPRITE* child);
        void detachFromChildChain();
        int deleteChildByVid(VID* childVid);

        static __forceinline ACT buildCommandRecord(std::uint32_t opcode, int argument1, int argument2, int argument3) noexcept
        {
            ACT out{};
            out.opcode = opcode;
            out.argument1 = static_cast<std::uint32_t>(argument1);
            out.argument2 = static_cast<std::uint32_t>(argument2);
            out.argument3 = static_cast<std::uint32_t>(argument3);
            return out;
        }
        STRING GetTextActions() const;
        STRING GetTextItems() const;
        void SetTextActions(const STRING* text);
        void SetTextItems(const STRING* text);
        void AddActionAfterStop(int actionCode, int argument1, int argument2, int argument3);
        int ActionStackHaveCommand(int command) const noexcept;


        void serializeCommandWordsText(STRING& out) const;
        std::string serializeCommandWordsText() const;
        void parseCommandWordsText(STRING text);
        __forceinline std::uint32_t NoItems() const noexcept { return m_exData ? m_exData->items.count : 0u; }
        __forceinline const std::int32_t* commandWordData() const noexcept { return m_exData ? m_exData->items.values : nullptr; }
        __forceinline int findLastCommandWord(std::int32_t word) const noexcept
        {
            if (!m_exData || m_exData->items.count == 0u || !m_exData->items.values)
                return -1;
            std::uint32_t index = m_exData->items.count;
            const std::int32_t* cursor = m_exData->items.values + index;
            while (index != 0u)
            {
                --cursor;
                --index;
                if (*cursor == word)
                    return static_cast<int>(index);
            }
            return -1;
        }
        int removeCommandWordValue(std::int32_t word) noexcept;
        __forceinline int GetItemNumber(int index) const noexcept
        {
            if (index < 0 || !m_exData)
                return 0;
            const std::uint32_t itemIndex = static_cast<std::uint32_t>(index);
            if (!m_exData->items.values || itemIndex >= m_exData->items.count)
                return 0;
            return m_exData->items.values[itemIndex];
        }
        int InsertItem(std::int32_t word) noexcept;
        int insertUniqueItem(std::int32_t word) noexcept;
        int clearCommandWordList() noexcept;
        __forceinline std::uint32_t lastCommandOpcode() const noexcept
        {
            const auto& owner = m_commandStack.m_commandRecords;
            return owner.count == 0u ? 0u : owner.records[owner.count - 1u].words[0];
        }

        __forceinline int ammoCount() const noexcept
        {
            int value = ammoFixedPoint();
            const int signBits = value < 0 ? -1 : 0;
            value += (signBits & 0x3F);
            return value >> 6;
        }

        __forceinline void setAmmoFixedPoint(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::AmmoFixedPoint) = value;
        }
        int refillAmmoByCapacityFraction(int divisor) noexcept;
        int NeedRepairByRepair() const noexcept;
        int ammoMissingPercent() const noexcept;
        __forceinline int derivedStateValue(int index) const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::DerivedStateBase + static_cast<std::size_t>(index) * SpriteLayout::WordStride);
        }
        __forceinline int setDerivedStateValue(int index, int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::DerivedStateBase + static_cast<std::size_t>(index) * SpriteLayout::WordStride) = value;
            return value;
        }

        const core::List<ACT>& commandStack() const { return m_commandStack; }
        size_t noCommandStackEntry() const { return m_commandStack.size(); }

    private:
        friend class FRAME;
        friend class PRIMITIVE;
        friend class UNIT;
        friend class ENGINE;
        friend class MENU;
        friend struct EX_SPRITE_DATA;
        int m_attackDecisionCode = 0;
        int m_currentFrameBegin = 0;
        int m_currentFrame = 0;
        int m_currentFrameEnd = 0;
        std::uint32_t m_applicationBucketTime = 0;
        std::uint32_t m_createTime = 0;
        VID* m_vid = nullptr;
        float m_speed = 0.0f;
        float m_zSpeed = 0.0f;
        DWORD m_runtimeFlags;
        int m_listReferenceCount = 0;
        VECTOR m_xyz;
        SPRITE* m_goalSprite = nullptr;
        SPRITE* m_childChain = nullptr;
        SPRITE* m_childBacklink = nullptr;
        int m_currentAnimation = 0;
        ANGLE m_direction;
        std::uint32_t m_actionTimer = 0;
        int m_hp = 0;
        core::List<ACT> m_commandStack;
        EX_SPRITE_DATA* m_exData = nullptr;
        PTR_SPRITE m_bestTargetSprite;

    };


    class TERRAIN : public SPRITE
    {
    public:
        TERRAIN(VID* vid, float x, float y, float z, ANGLE dir, SPRITE* parent = nullptr);
        int Action(int opcode, std::intptr_t argument1Payload, int argument2Value, int argument3Value) override;

        void AddHpPerSecond(int hpToAdd) noexcept;

    private:
        int m_repairLinkHp;
        int m_terrainState;
    };

    class LINKER : public SPRITE
    {
    public:
        LINKER(VID* vid, float x, float y, float z, ANGLE dir, SPRITE* parent = nullptr);
        ~LINKER() override;

    private:


        float m_linkOffsetX;
        float m_linkOffsetY;
        float m_linkOffsetZ;
        int m_linkDirection;
        SPRITE* m_linkOwner;
    };
    class PRIMITIVE : public SPRITE
    {
    public:
        PRIMITIVE(VID* vid, float x, float y, float z, ANGLE dir, SPRITE* parent = nullptr);
        ~PRIMITIVE() override;
        int Action(int opcode, std::intptr_t argument1Payload, int argument2Value, int argument3Value) override;
    };
    class REGION : public SPRITE
    {
    public:
        REGION(VID* vid, float x, float y, float z, ANGLE dir, SPRITE* parent = nullptr);
        ~REGION() override;
        int Action(int opcode, std::intptr_t argument1Payload, int argument2Value, int argument3Value) override;
        void Draw() override;
        void DrawDebugOverlay() override;

        float ScreenLeft() const noexcept;
        float ScreenTop() const noexcept;
        float ScreenRight() const noexcept;
        float ScreenBottom() const noexcept;
        int SetFogParameters(int start, int end, int color);

        static constexpr std::uint32_t FogAnimatedFlag = 1u << 0;
        static constexpr std::uint32_t FogBlendFlag = 1u << 1;
        static constexpr std::uint32_t FullViewportFlag = 1u << 3;

        std::uint32_t regionFlags() const noexcept { return m_regionFlags; }
        float regionWidth() const noexcept { return m_regionWidth; }
        float regionHeight() const noexcept { return m_regionHeight; }
        VID* sourceMappedVid(int index) const noexcept { return m_sourceVidMap[index]; }
        VID* targetMappedVid(int index) const noexcept { return m_targetVidMap[index]; }

    private:
        int m_fogRampPhase;
        int m_lastFogRampPhase;
        void* m_fogRamp;
        std::uint32_t m_regionFlags;
        int m_fogEnd;
        int m_fogStart;
        std::uint32_t m_fogColor;
        int m_unusedRegionState;
        float m_regionWidth;
        float m_regionHeight;
        VID* m_savedRegionVid;
        int m_persistedRegionState;
        VID* m_sourceVidMap[6];
        VID* m_targetVidMap[6];
    };

    VID* resolveRegionMappedVid(VID* sourceVid, float x, float y, float z) noexcept;


    class FRAME : public SPRITE
    {
    public:
        FRAME(VID* vid, float x, float y, float z, ANGLE dir, SPRITE* parent = nullptr);
        void Tact() override;
        void MoveTact() override {}
        void DeletePointerToSprite(SPRITE*) override {}
        void DrawDebugOverlay() override {}
    };


    class STEXT : public PRIMITIVE
    {
    public:
        STEXT(VID* vid, float x, float y, float z, ANGLE dir, SPRITE* parent = nullptr);
        ~STEXT() override;

        const char* text() const noexcept { return m_text; }
        const char* textClass() const noexcept { return m_textClass; }
        int textLength() const noexcept { return m_textLength; }
        int textFlags() const noexcept { return m_textFlags; }
        void assignText(const char* text);
        void assignTextClass(const char* text);
        int Action(int opcode, std::intptr_t argument1Payload, int argument2Value, int argument3Value) override;
        void Draw() override;
        int CalcTextProperty() noexcept;

    private:


        char* m_text;
        char* m_textClass;
        int m_textLength;
        int m_textFlags;
        int m_lineCount;
        int m_maxLineLength;
    };

#if defined(_M_IX86)
    static_assert(sizeof(SPRITE) == 0x70, "SPRITE size");
    static_assert(sizeof(TERRAIN) == 0x78, "TERRAIN size");
    static_assert(sizeof(LINKER) == 0x84, "LINKER size");
    static_assert(sizeof(PRIMITIVE) == 0x70, "PRIMITIVE size");
    static_assert(sizeof(REGION) == 0xD0, "REGION size");
    static_assert(sizeof(FRAME) == 0x70, "FRAME size");
    static_assert(sizeof(STEXT) == 0x88, "STEXT size");
#endif

}



namespace as1
{
    inline PTR_SPRITE::~PTR_SPRITE()
    {
        if (sprite)
        {
            const int nvid = sprite->Vid() ? sprite->Vid()->nVid : -1;
            logFileLoggerResourceError(g_fileLogger, "SPRITE %i", 10, "PTR_SPRITE with this sprite not clear", 0, nvid);
        }
    }

    inline PTR_SPRITE& PTR_SPRITE::operator=(SPRITE* value)
    {
        if (value)
            value->AddListReference();
        if (sprite)
            sprite->Release();
        sprite = value;
        return *this;
    }
}
