#pragma once
#include "core/types.h"
#include "core/as_string.h"
#include "core/base_stream.h"
#include "core/resource.h"
#include "core/application.h"
#include "weapon.h"
#include "vid/vid.h"
#include "sprite.h"
#include "script.h"
#include "groups.h"
#include "player.h"
#include "base_sprite_list.h"

namespace as1
{
    using GamePath = STRING;

    class GRAPH;
    class MAP;
    extern MAP* Map;

    extern std::uint32_t prev_second_time;
    namespace win { class ApplicationWin; }

    long double approximatePlanarDistance(float dx, float dy) noexcept;

    class RelationTable
    {
    public:
        RelationTable() noexcept = default;
        ~RelationTable() noexcept = default;
        RelationTable(const RelationTable&) = delete;
        RelationTable& operator=(const RelationTable&) = delete;

        void append(int oldSpriteAddr, SPRITE* newSpritePtr);
        SPRITE* getPointer(int oldSpriteAddr) const noexcept;
        void clear() noexcept;
        size_t size() const noexcept { return m_old.count(); }

    private:
        core::List<SPRITE*> m_old;
        core::List<SPRITE*> m_new;

        template <class T>
        static T* decodePointer(DWORD pointer) noexcept
        {
            return reinterpret_cast<T*>(static_cast<std::uintptr_t>(pointer));
        }
    };

#if defined(_M_IX86)
    static_assert(sizeof(RelationTable) == 0x20u, "RelationTable layout 0x20 bytes");
#endif

    class MAP
    {
    public:
        __forceinline const STRING& objectsResource() const noexcept
        {
            const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
            return *reinterpret_cast<const STRING*>(owner + core::application_layout::ResourceName);
        }

        void invalidateFontVidDeviceObjects() noexcept;
        void restoreFontVidDeviceObjects() noexcept;
        size_t noWeapon() const { const int count = weaponCount(); return count > 0 ? static_cast<size_t>(count) : 0u; }
        const WEAPON* weapons() const { return weaponTable(); }
        int weaponCount() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const BYTE*>(this) + core::application_layout::WeaponCount);
        }
        WEAPON* weaponTable() noexcept
        {
            return *reinterpret_cast<WEAPON**>(reinterpret_cast<BYTE*>(this) + core::application_layout::WeaponTable);
        }
        const WEAPON* weaponTable() const noexcept
        {
            return *reinterpret_cast<WEAPON* const*>(reinterpret_cast<const BYTE*>(this) + core::application_layout::WeaponTable);
        }
        size_t noGroup() const { return groupOwner().size(); }
        float SizeX() const
        {
            return *reinterpret_cast<const float*>(reinterpret_cast<const BYTE*>(this) + core::application_layout::MapExtentX);
        }
        float SizeY() const
        {
            return *reinterpret_cast<const float*>(reinterpret_cast<const BYTE*>(this) + core::application_layout::MapExtentY);
        }
        float FromScreenX(float x) const;
        float FromScreenY(float y) const;
        float ToScreenX(float x) const;
        float ToScreenY(float y) const;


        void SetShiftCoor(float centerX, float centerY, int effect = 0);
        void SetShiftCoor(const VECTOR2& center, int effect = 0) { SetShiftCoor(center.x, center.y, effect); }
        SPRITE* FirstSprite(int layer, int* index);

        SPRITE* NextSprite(int layer, int* index);
        int noGridX() const { return terrainGridWidth(); }
        int noGridY() const { return terrainGridHeight(); }
        const short* gridZ() const { return terrainGrid(); }
        VID* CreateVid(RESOURCE* res, int nvid);
        VID* Vid(int nvid) const;

        int ValidateVid(int nvid) const;

        __forceinline const SCRIPT& script() const noexcept
        {
            const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
            return *reinterpret_cast<const SCRIPT*>(owner + core::application_layout::ScriptRuntime);
        }
        __forceinline SCRIPT& getScript() noexcept
        {
            BYTE* const owner = reinterpret_cast<BYTE*>(this);
            return *reinterpret_cast<SCRIPT*>(owner + core::application_layout::ScriptRuntime);
        }
        STRING ScriptVariable(STRING name);
        int ExecFunc(int opcode);
        STRING* PopStr();
        int PopInt();
        int PopObject();
        VID* PopVid(const char* errorContext);
        void PushInt(int value);
        void PushStr(const STRING& value);
        void PushObject(int value);
        __forceinline RESOURCE& demoResource() noexcept
        {
            BYTE* const owner = reinterpret_cast<BYTE*>(this);
            return *reinterpret_cast<RESOURCE*>(owner + core::application_layout::DemoResource);
        }
        __forceinline const RESOURCE& demoResource() const noexcept
        {
            const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
            return *reinterpret_cast<const RESOURCE*>(owner + core::application_layout::DemoResource);
        }
        SPRITE* OldLoadSprite(BaseStream* res);
        SPRITE* LoadSprite(BaseStream* res, int version);
        void CreateEmptyHardwareGround();
        PLAYER* Player(int playerIndex) const noexcept;
        void SetFlagman(int playerIndex, SPRITE* sprite) noexcept;
        SPRITE* Flagman(int playerIndex) const noexcept;

        SPRITE* ReadPointer(BaseStream* stream);


        __forceinline
        SPRITE* ReadSpriteHandle(BaseStream* stream, int* oldAddress = nullptr) const
        {
            int handle = -1;
            if (stream)
                stream->read(&handle, 4u);
            if (oldAddress)
                *oldAddress = handle;
            return (handle == -1) ? nullptr : ResolveOldSpriteHandle(handle);
        }
        __forceinline
        SPRITE* ResolveOldSpriteHandle(int oldAddress) const
        {
            if (oldAddress == 0 || oldAddress == -1)
                return nullptr;
            return relationTable().getPointer(oldAddress);
        }
        __forceinline GROUPS& groupOwner() noexcept
        {
            BYTE* const owner = reinterpret_cast<BYTE*>(this);
            return *reinterpret_cast<GROUPS*>(owner + core::application_layout::Groups);
        }
        __forceinline const GROUPS& groupOwner() const noexcept
        {
            const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
            return *reinterpret_cast<const GROUPS*>(owner + core::application_layout::Groups);
        }
        __forceinline
        void BindLoadedSpriteHandle(int oldAddress, SPRITE* sprite)
        {
            if (oldAddress == 0 || oldAddress == -1)
                return;
            relationTable().append(oldAddress, sprite);
        }


        float GetGroundZScr(float screenX, float screenY) const noexcept;
        float GetGroundZ(float x, float y) const;
        float GetGroundZ(const VID* vid, VECTOR2 v) const;
        void ResetGroundZ();
        void SetGroundZ(float x, float y, float z);
        void SetTempGroundZ(float x, float y, float z);
        void ClearTempGroundZ(float x, float y, float z);

    private:
        friend class core::Application;
        friend class win::ApplicationWin;

        __forceinline
        short* terrainGrid() noexcept
        {
            const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
            return *reinterpret_cast<short* const*>(owner + core::application_layout::TerrainGrid);
        }
        __forceinline
        const short* terrainGrid() const noexcept
        {
            const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
            return *reinterpret_cast<short* const*>(owner + core::application_layout::TerrainGrid);
        }
        __forceinline
        int terrainGridWidth() const noexcept
        {
            const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
            return *reinterpret_cast<const int*>(owner + core::application_layout::TerrainGridWidth);
        }
        __forceinline
        int terrainGridHeight() const noexcept
        {
            const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
            return *reinterpret_cast<const int*>(owner + core::application_layout::TerrainGridHeight);
        }
        __forceinline
        void setTerrainGridDimensions(int x, int y) noexcept
        {
            BYTE* const owner = reinterpret_cast<BYTE*>(this);
            *reinterpret_cast<int*>(owner + core::application_layout::TerrainGridWidth) = x;
            *reinterpret_cast<int*>(owner + core::application_layout::TerrainGridHeight) = y;
        }
        void releaseTerrainGridStorage() noexcept;
        __forceinline
        RelationTable& relationTable() noexcept
        {
            BYTE* const owner = reinterpret_cast<BYTE*>(this);
            return *reinterpret_cast<RelationTable*>(owner + core::application_layout::RelationTable);
        }
        __forceinline
        const RelationTable& relationTable() const noexcept
        {
            const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
            return *reinterpret_cast<const RelationTable*>(owner + core::application_layout::RelationTable);
        }

    };
}
