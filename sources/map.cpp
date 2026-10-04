#include "map.h"
#include "vid/vid_software.h"
#include "vid/vid_software16.h"
#include "vid/vid_hardware.h"
#include "vid/vid_hardware_z.h"
#include "vid/vid_surface.h"
#include "vid/vid_light.h"
#include "vid/vid_font.h"
#include "core/application.h"
#include "core/resource.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "core/configuration.h"
#include "constant.h"
#include "images/picture.h"
#include "graph.h"
#include "unit.h"
#include "avia.h"
#include "creature.h"
#include "civ_robot.h"
#include "engine.h"
#include "balloon.h"
#include "depo.h"
#include "cannon.h"
#include "building.h"
#include "rail.h"
#include "sprite_act_const.h"
#include "script/action_constants.h"
#include "sprite_collector.h"
#include "mouse.h"
#include "menu.h"
#include "sound/sound_engine.h"
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <limits>
#include <new>
#include <cstdlib>
#include "win/application_win.h"
#include <mmsystem.h>
#include "d3d8.h"
#include <xmmintrin.h>

namespace as1
{


    MAP* Map = nullptr;
    std::uint32_t prev_second_time = 0;


    PRIMITIVE::PRIMITIVE(VID* vid, float x, float y, float z, ANGLE dir, SPRITE* parent)
        : SPRITE(vid,
                 x + core::GlobalApplicationDrawDispatcherState().cameraShiftX(),
                 y + core::GlobalApplicationDrawDispatcherState().cameraShiftY(),
                 z,
                 dir,
                 parent)
    {


        applicationFrameSpriteList().append(this);
    }

    PRIMITIVE::~PRIMITIVE()
    {


        applicationMenu().clearSelectedSpriteIfMatches(this);
        applicationFrameSpriteList().removeSorted(this);
        win::applicationWinInstance()->transferFrom(this);
    }


    long double approximatePlanarDistance(float dx, float dy) noexcept
    {


        const float ax = std::fabs(dx);
        const float ay = std::fabs(dy);
        const float metric =
            (ax <= ay || std::isnan(ax) || std::isnan(ay))
                ? ax * 0.5f + ay
                : ax + ay * 0.5f;
        return static_cast<long double>(metric);
    }

    namespace
    {
        constexpr float UNLIMITED = 999999.0f;

        int mapStackValueToInteger(const script::StackObject& value)
        {
            return (value.flags & script::STACK_OBJECT_STRING)
                ? value.text.Int()
                : value.intValue;
        }


        STRING gamePathString(const GamePath& path)
        {
            return path;
        }

        bool isAbsoluteWindowsPath(const std::string& path) noexcept
        {
            return (path.size() >= 2u && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':') ||
                   (!path.empty() && (path[0] == '\\' || path[0] == '/'));
        }

        std::string joinGamePath(const std::string& root, const std::string& child)
        {
            if (root.empty() || root == "." || isAbsoluteWindowsPath(child))
                return child;
            std::string out = root;
            if (!out.empty() && out.back() != '\\' && out.back() != '/')
                out.push_back('\\');
            out += child;
            return out;
        }


        __forceinline int mapConvertFloatToInt32(float value) noexcept
        {
            const long double d = static_cast<long double>(value);
            if (!std::isfinite(d) ||
                d < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                d > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        __forceinline std::int32_t mapConvertExtendedToInt32(long double value) noexcept
        {
            if (!std::isfinite(value) ||
                value < -9223372036854775808.0L ||
                value >= 9223372036854775808.0L)
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(value));
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(converted));
        }

        __forceinline int truncateFloatToInt32ForMap(float value) noexcept
        {
            return _mm_cvtt_ss2si(_mm_set_ss(value));
        }

        __forceinline float minssForMap(float destination, float source) noexcept
        {
            return _mm_cvtss_f32(_mm_min_ss(_mm_set_ss(destination), _mm_set_ss(source)));
        }

        __forceinline float maxssForMap(float destination, float source) noexcept
        {
            return _mm_cvtss_f32(_mm_max_ss(_mm_set_ss(destination), _mm_set_ss(source)));
        }

        __forceinline int mapMultiplyAndConvertToInt32(float value, float multiplier) noexcept
        {
            const long double d = static_cast<long double>(value) * static_cast<long double>(multiplier);
            if (!std::isfinite(d) ||
                d < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                d > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        __forceinline bool floatLessOrUnordered(float lhs, float rhs) noexcept
        {
            return lhs < rhs || std::isnan(lhs) || std::isnan(rhs);
        }

        __forceinline int terrainGridDimension(float extent) noexcept
        {
            const int roundedExtent = static_cast<int>(extent + 7.0f);
            return (roundedExtent + (roundedExtent < 0 ? 7 : 0)) >> 3;
        }

        __forceinline int mapGridCoordinate(float value, float limit) noexcept
        {
            if (floatLessOrUnordered(value, 0.0f))
                return 0;
            const long double chosen = floatLessOrUnordered(value, limit)
                ? static_cast<long double>(value)
                : static_cast<long double>(limit) - 1.0L;
            const long double scaled = chosen * 0.125L;
            if (!(scaled >= -9223372036854775808.0L &&
                  scaled < 9223372036854775808.0L))
                return 0;
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(
                static_cast<std::int64_t>(scaled)));
        }

        std::uint32_t currentMilliseconds()
        {
            return static_cast<std::uint32_t>(::timeGetTime());
        }

        std::string normalizeGamePath(std::string s)
        {
            std::replace(s.begin(), s.end(), '\\', '/');
            while (!s.empty() && (s.front() == '/' || s.front() == '.'))
            {
                if (s.front() == '.')
                {
                    s.erase(s.begin());
                    if (!s.empty() && s.front() == '/')
                        s.erase(s.begin());
                }
                else
                    s.erase(s.begin());
            }
            return s;
        }

        std::string lowerCopy(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        bool isApplicationRenderPass(int pass) noexcept
        {
            return pass >= 0 && pass <= 10;
        }

        int applicationRenderPassSequenceIndex(int pass) noexcept
        {
            return isApplicationRenderPass(pass) ? pass : -1;
        }

        bool hasExtension(const std::string& path, const char* ext)
        {
            const std::string l = lowerCopy(path);
            const std::string e = ext;
            return l.size() >= e.size() &&
                   l.compare(l.size() - e.size(), e.size(), e) == 0;
        }

    }


    void RelationTable::append(int oldSpriteAddr, SPRITE* newSpritePtr)
    {
        if (!oldSpriteAddr)
            return;

        if (m_old.m_count >= m_old.m_capacity)
        {
            const int oldCapacity = m_old.m_capacity;
            const int newCapacity = 2 * oldCapacity + 4;
            if (newCapacity > oldCapacity)
            {
                int* const oldItems = reinterpret_cast<int*>(m_old.m_items);
                int* const newItems = static_cast<int*>(::operator new(static_cast<size_t>(newCapacity) * sizeof(int), std::nothrow));
                if (!newItems)
                    fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", newCapacity);
                if (oldItems)
                {
                    for (int i = 0; i < oldCapacity; ++i)
                        newItems[i] = oldItems[i];
                    ::operator delete(oldItems);
                }
                m_old.m_items = reinterpret_cast<SPRITE**>(newItems);
                m_old.m_capacity = newCapacity;
            }
        }
        reinterpret_cast<int*>(m_old.m_items)[m_old.m_count++] = oldSpriteAddr;

        if (m_new.m_count >= m_new.m_capacity)
        {
            const int oldCapacity = m_new.m_capacity;
            const int newCapacity = 2 * oldCapacity + 4;
            if (newCapacity > oldCapacity)
            {
                DWORD* const oldItems = reinterpret_cast<DWORD*>(m_new.m_items);
                DWORD* const newItems = static_cast<DWORD*>(::operator new(static_cast<size_t>(newCapacity) * sizeof(DWORD), std::nothrow));
                if (!newItems)
                    fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", newCapacity);
                if (oldItems)
                {
                    for (int i = 0; i < oldCapacity; ++i)
                        newItems[i] = oldItems[i];
                    ::operator delete(oldItems);
                }
                m_new.m_items = reinterpret_cast<SPRITE**>(newItems);
                m_new.m_capacity = newCapacity;
            }
        }
        reinterpret_cast<DWORD*>(m_new.m_items)[m_new.m_count++] =
            static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(newSpritePtr));
    }


    SPRITE* RelationTable::getPointer(int oldSpriteAddr) const noexcept
    {
        int index = m_old.m_count;
        if (!index)
            return nullptr;
        const int* const oldItems = reinterpret_cast<int*>(m_old.m_items);
        while (index)
        {
            --index;
            if (oldItems[index] == oldSpriteAddr)
                return decodePointer<SPRITE>(reinterpret_cast<DWORD*>(m_new.m_items)[index]);
        }
        return nullptr;
    }


    void RelationTable::clear() noexcept
    {
        void* const oldItems = static_cast<void*>(m_old.m_items);
        m_old.m_capacity = 0;
        m_old.m_count = 0;
        if (oldItems)
            ::operator delete(oldItems);
        m_old.m_items = nullptr;

        void* const newItems = static_cast<void*>(m_new.m_items);
        m_new.m_capacity = 0;
        m_new.m_count = 0;
        if (newItems)
            ::operator delete(newItems);
        m_new.m_items = nullptr;
    }

    __forceinline void MAP::releaseTerrainGridStorage() noexcept
    {
        BYTE* const owner = reinterpret_cast<BYTE*>(this);
        short*& grid = *reinterpret_cast<short**>(owner + core::application_layout::TerrainGrid);
        short*& temporaryTerrainGrid = *reinterpret_cast<short**>(owner + core::application_layout::TempTerrainGrid);
        if (grid)
            ::operator delete(static_cast<void*>(grid));
        if (temporaryTerrainGrid)
            ::operator delete(static_cast<void*>(temporaryTerrainGrid));
        grid = nullptr;
        temporaryTerrainGrid = nullptr;
    }


    STRING MAP::ScriptVariable(STRING name)
    {
        return getScript().GetVariableStr(name);
    }

void MAP::invalidateFontVidDeviceObjects() noexcept
    {
        core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
        const int count = table.count();
        for (int index = 0; index < count; ++index)
        {
            VID* const vid = table.slot(index);
            if (vid && (vid->formatFlags() & VID_TYPE_FONT) != 0u)
                (void)static_cast<VID_FONT*>(vid)->InvalidateDeviceObjects();
        }
    }


    void MAP::restoreFontVidDeviceObjects() noexcept
    {
        core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
        const int count = table.count();
        for (int index = 0; index < count; ++index)
        {
            VID* const vid = table.slot(index);
            if (vid && (vid->formatFlags() & VID_TYPE_FONT) != 0u)
                (void)static_cast<VID_FONT*>(vid)->RestoreDeviceObjects();
        }
    }


    VID* MAP::CreateVid(RESOURCE* res, int nvid)
    {
        STRING objectName;
        objectName.Read(res);

        VID loaded_vid;
        loaded_vid.nVid = nvid;
        loaded_vid.name = objectName;
        loaded_vid.noCadr = 32000;
        const size_t parameterBegin = res->position();
        loaded_vid.LoadParameters(res);

        STRING vidFileName;
        vidFileName.Read(res);
        const std::string normalizedVidFileName = lowerCopy(std::string(vidFileName.c_str()));
        vidFileName = STRING(normalizedVidFileName.c_str());


        const std::string sourceVidName = normalizedVidFileName;
        std::string loadVidName = sourceVidName;

        res->seek(parameterBegin);

        {
            core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
            const int vidCount = table.count();
            const std::string normalizedVidName = normalizedVidFileName;

            for (int index = 0; index < vidCount; ++index)
            {
                VID* const existing = table.slot(index);
                if (!existing)
                    continue;

                if (std::string(existing->sourceVidPath().c_str()) != normalizedVidName)
                    continue;

                const DWORD existingClass = existing->spriteClassId();
                if (loaded_vid.spriteClassId() == B_BUILDEDTERRAIN)
                {
                    if (existingClass != B_BUILDEDTERRAIN)
                        continue;
                }
                else if (existingClass == B_BUILDEDTERRAIN)
                {
                    continue;
                }

                if ((existing->formatFlags() & VID_TYPE_HARDWARE) == 0u)
                {
                    if (existing->baseGamma.first != loaded_vid.baseGamma.first ||
                        existing->baseGamma.second != loaded_vid.baseGamma.second)
                    {
                        continue;
                    }
                    if (((existing->properties() ^ loaded_vid.properties()) & P_GAMMA) != 0u)
                        continue;
                }

                std::unique_ptr<VID> mirror(existing->CreateMirror());

                mirror->nVid = nvid;
                mirror->name = objectName;
                mirror->vidName = vidFileName;
                mirror->LoadParameters(res);
                mirror->SetLayer();

                VID* const mirrorVid = mirror.get();
                (void)mirror.release();
                return mirrorVid;
            }

            res->seek(parameterBegin);
        }

        std::unique_ptr<VID> vid;
        WORD realType = 0;
        bool realHeadLoaded = false;
        RESOURCE resourceForLoad;
        bool generatedTempVid = false;
        const bool fontSource = hasExtension(loadVidName, ".fon") || hasExtension(loadVidName, ".ttf");
        const bool pictureSource = hasExtension(loadVidName, ".tga") ||
                                   hasExtension(loadVidName, ".bmp") ||
                                   hasExtension(loadVidName, ".flc");

        if (!fontSource && pictureSource)
        {
            const GamePath sourcePath = STRING(normalizeGamePath(loadVidName).c_str());
            const STRING colorPath(gamePathString(sourcePath));


            int pictureOpenResult = 1;

            if (loaded_vid.spriteClass != 19)
            {
                images::PICTURE_COMPOSITE_RESOURCE composite;
                pictureOpenResult = composite.openFilenames(colorPath, STRING(""), STRING(""));
                if (pictureOpenResult == 0)
                {
                    char* const tempName = _tempnam("c:\\tmp", "vid");
                    if (tempName)
                    {
                        loadVidName = tempName;
                        std::free(tempName);
                        composite.writeVidResource(STRING(loadVidName.c_str()), 0u);
                        generatedTempVid = true;
                    }
                }
            }
            else
            {
                images::PICTURE_SCROLL_COMPOSITE_RESOURCE composite;
                pictureOpenResult = composite.openFilenames(colorPath, STRING(""), STRING(""));
                if (pictureOpenResult == 0)
                {
                    char* const tempName = _tempnam("c:\\tmp", "vid");
                    if (tempName)
                    {
                        loadVidName = tempName;
                        std::free(tempName);
                        composite.writeVidResource(STRING(loadVidName.c_str()), 0u);
                        generatedTempVid = true;
                    }
                }
            }

            if (pictureOpenResult != 0 || !generatedTempVid)
                writeLogLine(g_fileLogger, "LOAD::Can't open file %s", sourceVidName.c_str());
        }

        if (fontSource)
        {
            realType = static_cast<WORD>(VID_TYPE_FONT);
            loaded_vid.type = realType;
            vid.reset(new (std::nothrow) VID_FONT());
        }
        else
        {
            const GamePath resolvedVidPath = generatedTempVid
                ? STRING(loadVidName.c_str())
                : STRING(normalizeGamePath(loadVidName).c_str());
            const STRING resolvedVidFilePath = gamePathString(resolvedVidPath);
            if (resourceForLoad.openFile(&resolvedVidFilePath, RESOURCE::ResTypes::VID) != 0)
            {
                logFileLoggerResourceError(g_fileLogger, "VID [%i-%s]", 7, loadVidName.c_str(), 0, nvid, objectName.c_str());
                if (generatedTempVid)
                    std::remove(loadVidName.c_str());
                return nullptr;
            }
            if (resourceForLoad.GoBegin(RESOURCE::ResTypes::HEAD))
                writeLogLine(g_fileLogger, "!!!ERROR!!!VID '%s': Load() not HEAD ", loadVidName.c_str());
            (void)resourceForLoad.read(&realType, sizeof(realType));
            loaded_vid.type = realType;


            if (realType & VID_TYPE_NEW_ZBUFFER)
                vid.reset(new (std::nothrow) VID_SURFACE());
            else if (realType & 0x0080u)
                vid.reset(new (std::nothrow) VID_LIGHT());
            else if ((realType & 0x0020u) && (realType & 0x0002u) && (realType & 0x0004u))
                vid.reset(new (std::nothrow) VID_HARDWARE_Z());
            else if (realType & 0x0020u)
                vid.reset(new (std::nothrow) VID_HARDWARE());
            else if (loaded_vid.spriteClass == B_BUILDEDTERRAIN)
                vid.reset(new (std::nothrow) VID_SOFTWARE16());
            else if (Graph->uses32BitColorDepth())
                vid.reset(new (std::nothrow) VID_SOFTWARE());
            else
                vid.reset(new (std::nothrow) VID_SOFTWARE16());

            realHeadLoaded = true;
        }

        vid->type = realType;
        vid->nVid = nvid;
        vid->name = objectName;


        vid->vidName = STRING(loadVidName.c_str());
        if (realHeadLoaded)
        {


            (void)resourceForLoad.read(&vid->frameSpeedDefault, 2);
            (void)resourceForLoad.read(&vid->noCadr, 2);
            (void)resourceForLoad.read(&vid->vidSizeX, 2);
            (void)resourceForLoad.read(&vid->vidSizeY, 2);
        }

        vid->LoadParameters(res);

        if (fontSource)
            vid->Load(nullptr);
        else if (realHeadLoaded)
            vid->Load(&resourceForLoad);

        if (generatedTempVid)
        {
            resourceForLoad.close();
            std::remove(loadVidName.c_str());
        }

        vid->SetLayer();

        VID* loadedVid = vid.get();

        (void)vid.release();
        return loadedVid;
    }


    VID* MAP::Vid(int nvid) const
    {


        if (nvid < 0)
            return EmptyVid;


        const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
        const int count = *reinterpret_cast<const int*>(owner + core::application_layout::VidCount);
        if (nvid >= count)
            return EmptyVid;

        VID* const vid = *reinterpret_cast<VID* const*>(
            owner + core::application_layout::VidTable + static_cast<std::size_t>(nvid) * 4u);
        return vid ? vid : EmptyVid;
    }


    int MAP::ValidateVid(int nvid) const
    {
        const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
        const int count = *reinterpret_cast<const int*>(owner + core::application_layout::VidCount);
        if (nvid < 0 || nvid >= count)
            return 0;
        return *reinterpret_cast<VID* const*>(
            owner + core::application_layout::VidTable + static_cast<std::size_t>(nvid) * 4u) != nullptr ? 1 : 0;
    }


    void core::Application::transferFrom(SPRITE* sprite)
    {
        MAP* const map = reinterpret_cast<MAP*>(this);
        BYTE* const owner = reinterpret_cast<BYTE*>(this);
        PLAYER* const players[4] = {
            *reinterpret_cast<PLAYER**>(owner + core::application_layout::PlayerSlots),
            *reinterpret_cast<PLAYER**>(owner + core::application_layout::PlayerSlots + core::application_layout::PlayerSlotStride),
            *reinterpret_cast<PLAYER**>(owner + core::application_layout::PlayerSlots + core::application_layout::PlayerSlotStride * 2u),
            *reinterpret_cast<PLAYER**>(owner + core::application_layout::PlayerSlots + core::application_layout::PlayerSlotStride * 3u)
        };
        for (PLAYER* const player : players)
        {
            (void)player->DeletePointerToSprite(sprite);
        }

        map->getScript().DeletePointerToObject(sprite);
        map->groupOwner().DeletePointerToSprite(sprite);

        if (sprite->listReferenceCount() > 1)
        {
            SPRITE_COLLECTOR* hash = GlobalSpriteCollector();
            int cursor = static_cast<int>(hash->overflowList().count()) - 1;
            while (cursor >= 0)
            {
                const core::List<SPRITE*>& overflow = hash->overflowList();
                SPRITE* current = overflow.at(static_cast<std::size_t>(cursor));
                while (!current && --cursor >= 0)
                    current = overflow.at(static_cast<std::size_t>(cursor));
                if (!current)
                    break;

                current->DeletePointerToSprite(sprite);
                hash = GlobalSpriteCollector();
                if (cursor > static_cast<int>(hash->overflowList().count()))
                    cursor = static_cast<int>(hash->overflowList().count());
                --cursor;
            }
        }

        for (int pass = 0; pass < core::ApplicationDrawDispatcherState::PassCount; ++pass)
        {
            if (sprite->listReferenceCount() <= 1)
                continue;

            const core::ApplicationDrawPassBucket& bucket = *reinterpret_cast<const core::ApplicationDrawPassBucket*>(
                owner + core::application_layout::DrawLayerOwners +
                static_cast<std::size_t>(pass) * core::application_layout::DrawLayerStride);
            int cursor = bucket.count() - 1;
            while (cursor >= 0)
            {
                SPRITE* current = bucket.spriteAt(cursor);
                while (!current && --cursor >= 0)
                    current = bucket.spriteAt(cursor);
                if (!current)
                    break;
                current->DeletePointerToSprite(sprite);
                --cursor;
            }
        }
    }


    void core::Application::deinitialize()
    {
        MAP* const map = reinterpret_cast<MAP*>(this);
        BYTE* const applicationOwner = reinterpret_cast<BYTE*>(this);
        int& vidCount = *reinterpret_cast<int*>(applicationOwner + core::application_layout::VidCount);
        VID** const vidSlots = reinterpret_cast<VID**>(applicationOwner + core::application_layout::VidTable);

        for (int index = 0; index < vidCount; ++index)
        {
            VID* const vid = vidSlots[index];
            if (!vid)
                continue;
            const int spriteBucket0Count = static_cast<int>(vid->NoSprites(0));
            const int spriteBucket1Count = static_cast<int>(vid->NoSprites(1));
            const int spriteBucket2Count = static_cast<int>(vid->NoSprites(2));
            const int spriteBucket3Count = static_cast<int>(vid->NoSprites(3));
            if ((spriteBucket0Count + spriteBucket1Count + spriteBucket2Count + spriteBucket3Count) == 0)
                continue;
            writeLogLine(g_fileLogger, "NoVid[%3i]=%i %i %i %i %s Layer=%i %s",
                       index, spriteBucket0Count, spriteBucket1Count, spriteBucket2Count, spriteBucket3Count,
                       vid->name.c_str(), vid->renderLayer(), vid->sourceVidPath().c_str());
        }

        map->relationTable().clear();

        *reinterpret_cast<float*>(applicationOwner + core::application_layout::TickScale) = 1.0f;
        *reinterpret_cast<std::uint32_t*>(applicationOwner + core::application_layout::Fps) = 0u;
        *reinterpret_cast<std::uint32_t*>(applicationOwner + core::application_layout::FpsCounter) = 0u;

        Graph->m_movie.Release();
        Graph->SetWind(25, ANGLE(static_cast<unsigned char>(200)));
        Graph->SetEnvironment(0xFFFFFFFFu);
        (void)Graph->Effect(11, 0, 0, 1);

        std::uint32_t& applicationFlags = *reinterpret_cast<std::uint32_t*>(
            applicationOwner + core::application_layout::Flags);
        if ((applicationFlags & application_flags::DemoUseResource) != 0u)
        {
            Mouse->Enable();
            map->demoResource().close();
        }
        if ((applicationFlags & application_flags::DemoWriteToResource) != 0u)
        {
            const std::int32_t endMarker = -1;
            map->demoResource().write(&endMarker, sizeof(endMarker));
            map->demoResource().EndSection();
            map->demoResource().close();
        }

        applicationFlags &= ~(application_flags::DemoUseResource | application_flags::DemoWriteToResource);
        *reinterpret_cast<std::uint32_t*>(applicationOwner + core::application_layout::ScrollType) = 1u;

        writeLogLine(g_fileLogger, "Player release");

        PLAYER** const playerSlots = reinterpret_cast<PLAYER**>(
            applicationOwner + core::application_layout::PlayerSlots);
        for (int index = 0; index < 4; ++index)
        {
            PLAYER* const player = playerSlots[index];
            if (!player)
                continue;

            player->reset();
        }

        writeLogLine(g_fileLogger, "Sprite release");

        ENGINE::globaldeleting = 1;
        for (int pass = 0; pass < core::ApplicationDrawDispatcherState::PassCount; ++pass)
        {
            core::ApplicationDrawPassBucket& bucket = *reinterpret_cast<core::ApplicationDrawPassBucket*>(
                applicationOwner + core::application_layout::DrawLayerOwners +
                static_cast<std::size_t>(pass) * core::application_layout::DrawLayerStride);
            int cursor = bucket.count() - 1;
            while (cursor >= 0)
            {
                SPRITE* sprite = bucket.spriteAt(cursor);
                while (!sprite && --cursor >= 0)
                    sprite = bucket.spriteAt(cursor);
                if (!sprite)
                    break;

                delete sprite;
                --cursor;
            }
        }

        ENGINE::globaldeleting = 0;
        for (int pass = 0; pass < core::ApplicationDrawDispatcherState::PassCount; ++pass)
        {
            const core::ApplicationDrawPassBucket& bucket = *reinterpret_cast<const core::ApplicationDrawPassBucket*>(
                applicationOwner + core::application_layout::DrawLayerOwners +
                static_cast<std::size_t>(pass) * core::application_layout::DrawLayerStride);
            for (int cursor = bucket.count() - 1; cursor >= 0; --cursor)
            {
                SPRITE* const survivor = bucket.spriteAt(cursor);
                if (!survivor)
                    continue;
                const int nvid = survivor->Vid() ? survivor->Vid()->nVid : -1;
                logFileLoggerResourceError(g_fileLogger, "SPRITE %i", 10, "Sprite exist after delete", cursor, nvid);
                break;
            }
        }

        if (!map->groupOwner().empty())
            logFileLoggerResourceError(g_fileLogger, "%s", 10, "Incorrect delete groups in DeleteAll()", 0, "");

        writeLogLine(g_fileLogger, "Menu   release");

        BaseSpriteList<0>& frameList = *reinterpret_cast<BaseSpriteList<0>*>(
            applicationOwner + core::application_layout::Menu);
        if (frameList.activeCount() != 0)
        {
            SPRITE* const first = frameList.at(0);
            const int nvid = first->Vid() ? first->Vid()->nVid : -1;
            logFileLoggerResourceError(g_fileLogger, "SPRITE %i", 10, "Menu sprite exist after delete", 0, nvid);
            frameList.deleteAllSprites();
        }

        const std::uint32_t now = core::CurrentTimeMilliseconds();
        const std::uint32_t start = *reinterpret_cast<std::uint32_t*>(
            applicationOwner + core::application_layout::WorldStartTime);
        const std::uint32_t elapsed = now - start;
        const std::uint32_t worldFrameCounter = *reinterpret_cast<std::uint32_t*>(
            applicationOwner + core::application_layout::WorldFrameCounter);
        const std::uint32_t averageFps = elapsed != 0u
            ? (1000u * worldFrameCounter) / elapsed
            : 0u;
        writeLogLine(g_fileLogger, "Average fps=%i", static_cast<int>(averageFps));

        writeLogLine(g_fileLogger, "Script release");

        map->getScript().resetScriptVmState();
        *reinterpret_cast<std::uint32_t*>(applicationOwner + core::application_layout::WorldFrameCounter) = 0u;

        reinterpret_cast<core::Application*>(this)->DeleteExtraVid();

        for (int index = 0; index < vidCount; ++index)
        {
            if (VID* const vid = vidSlots[index])
                vid->ResetSprites();
        }

        for (std::size_t i = 0; i < core::kScriptCallbackSlotCount; ++i)
            core::EvFunctionNumber[i] = 1000000 + static_cast<int>(i);
    }


    namespace
    {
        constexpr int END_SPRITE_INT = -1;
        SPRITE* const END_SPRITE_PTR = reinterpret_cast<SPRITE*>(~uintptr_t(0));

    }


    float MAP::FromScreenX(float x) const
    {
        const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
        return x + *reinterpret_cast<const float*>(owner + core::application_layout::CameraShiftX);
    }


    float MAP::FromScreenY(float y) const
    {
        const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
        return y + *reinterpret_cast<const float*>(owner + core::application_layout::CameraShiftY);
    }


    float MAP::ToScreenX(float x) const
    {
        const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
        const float shiftX = *reinterpret_cast<const float*>(owner + core::application_layout::CameraShiftX);
        return x - shiftX;
    }


    float MAP::ToScreenY(float y) const
    {
        const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
        return y - *reinterpret_cast<const float*>(owner + core::application_layout::CameraShiftY);
    }


    SPRITE* MAP::FirstSprite(int layer, int* index)
    {
        BYTE* const owner = reinterpret_cast<BYTE*>(this);
        auto* const sprites = reinterpret_cast<core::List<SPRITE*>*>(
            owner + core::application_layout::DrawLayerOwners +
            static_cast<std::size_t>(layer) * core::application_layout::DrawLayerStride);
        *index = sprites->activeCount() - 1;
        SPRITE* const* const data = sprites->data();
        if (*index < 0)
            return nullptr;
        while (!data[*index])
        {
            --*index;
            if (*index < 0)
                return nullptr;
        }
        return data[*index];
    }


    SPRITE* MAP::NextSprite(int layer, int* index)
    {
        --*index;
        if (*index < 0)
            return nullptr;

        BYTE* const owner = reinterpret_cast<BYTE*>(this);
        auto* const sprites = reinterpret_cast<core::List<SPRITE*>*>(
            owner + core::application_layout::DrawLayerOwners +
            static_cast<std::size_t>(layer) * core::application_layout::DrawLayerStride);
        SPRITE* const* const data = sprites->data();
        while (!data[*index])
        {
            --*index;
            if (*index < 0)
                return nullptr;
        }
        return data[*index];
    }

    

    void MAP::SetShiftCoor(float centerX, float centerY, int effect)
    {
        BYTE* const owner = reinterpret_cast<BYTE*>(this);
        const VECTOR2 scrollMin{
            *reinterpret_cast<const float*>(owner + core::application_layout::ScrollMinX),
            *reinterpret_cast<const float*>(owner + core::application_layout::ScrollMinY)};
        const VECTOR2 scrollMax{
            *reinterpret_cast<const float*>(owner + core::application_layout::ScrollMaxX),
            *reinterpret_cast<const float*>(owner + core::application_layout::ScrollMaxY)};
        GRAPH* const graph = Graph;
        if (!graph)
            return;
        const float screenW = graph->screenWidth();
        const float screenH = graph->screenHeight();
                const float clampMinX = scrollMin.x - graph->viewportLeft();
        const float clampMinY = scrollMin.y - graph->viewportTop();
        const float clampMaxX = scrollMax.x - graph->viewportRight();
        const float clampMaxY = scrollMax.y - graph->viewportBottom();


        float shiftX = centerX - screenW * 0.5f;
        float shiftY = centerY - screenH * 0.5f;


        if ((static_cast<std::uint32_t>(effect) & 0x10000000u) == 0u)
        {

            shiftX = maxssForMap(clampMinX, shiftX);
            shiftX = minssForMap(clampMaxX, shiftX);
            shiftY = maxssForMap(clampMinY, shiftY);
            shiftY = minssForMap(clampMaxY, shiftY);
        }

        const float currentCameraX = *reinterpret_cast<const float*>(owner + core::application_layout::CameraShiftX);
        const float currentCameraY = *reinterpret_cast<const float*>(owner + core::application_layout::CameraShiftY);

        if (currentCameraX == shiftX && currentCameraY == shiftY)
            return;

        if (effect == 2)
        {
            graph->Effect(2,
                               truncateFloatToInt32ForMap(centerX),
                               truncateFloatToInt32ForMap(centerY),
                               0);
            return;
        }

        const float deltaX = shiftX - currentCameraX;
        const float deltaY = shiftY - currentCameraY;
        *reinterpret_cast<float*>(owner + core::application_layout::CameraShiftX) = shiftX;
        *reinterpret_cast<float*>(owner + core::application_layout::CameraShiftY) = shiftY;

        BaseSpriteList<0>& frameList = applicationFrameSpriteList();
        const int persistentCount = frameList.activeCount();
        for (int i = 0; i < persistentCount; ++i)
        {
            SPRITE* sprite = frameList.at(static_cast<std::size_t>(i));
            if (sprite)
                sprite->ChangeCoor(sprite->X() + deltaX, sprite->Y() + deltaY, sprite->Z());
        }

        SPRITE* currentMouseSprite = mouseSprite();
        currentMouseSprite->ChangeCoor(currentMouseSprite->X() + deltaX,
                                       currentMouseSprite->Y() + deltaY,
                                       currentMouseSprite->Z());


        D3DMATRIX view{};
        view._11 = 1.0f;
        view._22 = 1.0f;
        view._32 = -1.0f;
        view._33 = 1.0f;


        const float viewportCenterX = (graph->viewportRight() + graph->viewportLeft()) * 0.5f;
        const float viewportCenterY = (graph->viewportBottom() + graph->viewportTop()) * 0.5f;
        view._41 = -shiftX - viewportCenterX;
        view._42 = -shiftY - viewportCenterY;
        view._44 = 1.0f;

        IDirect3DDevice8* device = static_cast<IDirect3DDevice8*>(graph->deviceHandle());
        const HRESULT result = device->SetTransform(D3DTS_VIEW, &view);
        if (FAILED(result))
            logFileLoggerResourceError(g_fileLogger, "%s", 8, "Transform view", static_cast<int>(result), "GRAPH");
    }


    SPRITE* MAP::OldLoadSprite(BaseStream* res)
    {
        int oldAddr = 0;
        std::int16_t nvid16 = 0;
        std::int16_t x16 = 0;
        std::int16_t y16 = 0;
        std::int16_t z16 = 0;
        BYTE directionByte = 0;
        BYTE discardedSpriteRecordByte = 0;

        res->read(&oldAddr, 4);
        if (oldAddr == END_SPRITE_INT)
            return END_SPRITE_PTR;

        res->read(&nvid16, 2);
        res->read(&x16, 2);
        res->read(&y16, 2);
        res->read(&z16, 2);
        res->read(&directionByte, 1);
        res->read(&discardedSpriteRecordByte, 1);
        (void)discardedSpriteRecordByte;

        const int nvid = static_cast<int>(nvid16);
        SPRITE* newSprite = nullptr;
        const bool isNullRecord = (nvid == -1);
        const bool hasVid = !isNullRecord && ValidateVid(nvid);
        if (hasVid)
        {
            newSprite = reinterpret_cast<core::Application*>(this)->CreateSprite(
                Vid(nvid),
                VECTOR(static_cast<float>(x16), static_cast<float>(y16), static_cast<float>(z16)),
                ANGLE(static_cast<int>(directionByte)),
                nullptr);
            BindLoadedSpriteHandle(oldAddr, newSprite);
        }
        else
        {
            BindLoadedSpriteHandle(oldAddr, nullptr);
            logFileLoggerResourceError(g_fileLogger, "%s", 3, "sprite, this vid not exist", nvid, "");
        }
        return newSprite;
    }


    SPRITE* MAP::LoadSprite(BaseStream* res, int version)
    {
        int oldAddr = 0;
        int nvid = 0;
        int army = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        ANGLE direct;

        res->read(&oldAddr, 4);
        if (oldAddr == END_SPRITE_INT)
            return END_SPRITE_PTR;

        res->read(&nvid, 4);
        if (version > 9)
        {
            res->read(&x, 4);
            res->read(&y, 4);
            res->read(&z, 4);
        }
        else
        {
            int ix = 0, iy = 0, iz = 0;
            res->read(&ix, 4);
            res->read(&iy, 4);
            res->read(&iz, 4);
            x = static_cast<float>(ix);
            y = static_cast<float>(iy);
            z = static_cast<float>(iz);
        }

        int directionValue = 0;
        res->read(&directionValue, 4);
        direct = ANGLE(static_cast<unsigned char>(directionValue));
        res->read(&army, 4);

        SPRITE* newSprite = nullptr;
        const bool isNullRecord = (nvid == -1);
        const bool hasVid = !isNullRecord && ValidateVid(nvid);
        if (hasVid)
        {
            newSprite = reinterpret_cast<core::Application*>(this)->CreateSprite(
                Vid(nvid), VECTOR(x, y, z), direct, nullptr);
            if (newSprite)
            {
                BindLoadedSpriteHandle(oldAddr, newSprite);
                newSprite->ChangeArmy(army);
            }
        }
        else
        {
            logFileLoggerResourceError(g_fileLogger, "%s", 3, "sprite, this vid not exist", nvid, "");
        }
        if (!newSprite)
            BindLoadedSpriteHandle(oldAddr, nullptr);
        return newSprite;
    }


    void MAP::CreateEmptyHardwareGround()
    {
        core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
        if (appVidTable.count() < 1025)
            appVidTable.setStoredCount(1025);

        if (VID* const oldGround = appVidTable.slot(1024))
        {
            delete oldGround;
        }

        const int groundSizeY = mapConvertFloatToInt32(SizeY());
        const int groundSizeX = mapConvertFloatToInt32(SizeX());
        auto ground = std::unique_ptr<VID_HARDWARE>(
            new (std::nothrow) VID_HARDWARE(1024, groundSizeX, groundSizeY));
        VID_HARDWARE* const groundVid = ground.get();
        groundVid->weapon = weaponTable();

        (void)ground.release();

        appVidTable.setSlotCell(1024, groundVid);

        reinterpret_cast<core::Application*>(this)->CreateSprite(
            groundVid,
            VECTOR(SizeX() * 0.5f, SizeY() * 0.5f, 0.0f),
            ANGLE(static_cast<unsigned char>(0)),
            nullptr);
        (void)writeLogLine(g_fileLogger, "Create Empty Hardware Ground");
    }

    PLAYER* MAP::Player(int playerIndex) const noexcept
    {

        return *reinterpret_cast<PLAYER* const*>(
            reinterpret_cast<const BYTE*>(this) +
            core::application_layout::PlayerSlots +
            static_cast<std::size_t>(playerIndex & 3) * sizeof(std::uint32_t));
    }


    void MAP::SetFlagman(int playerIndex, SPRITE* sprite) noexcept
    {


        PLAYER* const player = *reinterpret_cast<PLAYER**>(
            reinterpret_cast<BYTE*>(this) +
            core::application_layout::PlayerSlots +
            static_cast<std::size_t>(playerIndex & 3) * sizeof(std::uint32_t));
        player->SetFlagman(sprite);
    }


    SPRITE* MAP::Flagman(int playerIndex) const noexcept
    {
        PLAYER* const player = *reinterpret_cast<PLAYER* const*>(
            reinterpret_cast<const BYTE*>(this) +
            core::application_layout::PlayerSlots +
            static_cast<std::size_t>(playerIndex & 3) * sizeof(std::uint32_t));

        return *reinterpret_cast<SPRITE* const*>(
            reinterpret_cast<const BYTE*>(player) + 0x10u);
    }


    SPRITE* MAP::ReadPointer(BaseStream* stream)
    {
        std::int32_t handle = 0;
        stream->read(&handle, 4u);
        if (handle == -1)
            return END_SPRITE_PTR;
        return relationTable().getPointer(handle);
    }


    void MAP::SetGroundZ(float x, float y, float z)
    {
        const float mapSizeX = SizeX();
        const float mapSizeY = SizeY();
        if (floatLessOrUnordered(x, 0.0f))
            return;
        if (!floatLessOrUnordered(x, mapSizeX))
            return;
        if (floatLessOrUnordered(y, 0.0f))
            return;
        if (!floatLessOrUnordered(y, mapSizeY))
            return;

        const int zInt = mapConvertFloatToInt32(z);
        const int negativeGridY = mapMultiplyAndConvertToInt32(y, -0.125f);
        const int yProduct = static_cast<int>(
            static_cast<std::uint32_t>(negativeGridY) *
            static_cast<std::uint32_t>(terrainGridWidth()));
        const int xInt = mapConvertFloatToInt32(x);
        const int xDiv8 = (xInt + (xInt < 0 ? 7 : 0)) >> 3;
        const int index = static_cast<int>(
            static_cast<std::uint32_t>(xDiv8) - static_cast<std::uint32_t>(yProduct));
        short* const grid = terrainGrid();
        if (static_cast<int>(grid[index]) < zInt)
            grid[index] = static_cast<short>(static_cast<unsigned int>(zInt) & 0xFFFFu);
    }


    void MAP::SetTempGroundZ(float x, float y, float z)
    {
        const float mapSizeX = SizeX();
        const float mapSizeY = SizeY();
        if (floatLessOrUnordered(x, 0.0f))
            return;
        if (!floatLessOrUnordered(x, mapSizeX))
            return;
        if (floatLessOrUnordered(y, 0.0f))
            return;
        if (!floatLessOrUnordered(y, mapSizeY))
            return;

        const int zInt = mapConvertFloatToInt32(z);
        const int negativeGridY = mapMultiplyAndConvertToInt32(y, -0.125f);
        const int yProduct = static_cast<int>(
            static_cast<std::uint32_t>(negativeGridY) *
            static_cast<std::uint32_t>(terrainGridWidth()));
        const int xInt = mapConvertFloatToInt32(x);
        const int xDiv8 = (xInt + (xInt < 0 ? 7 : 0)) >> 3;
        const int index = static_cast<int>(
            static_cast<std::uint32_t>(xDiv8) - static_cast<std::uint32_t>(yProduct));
        const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
        short* const temporaryTerrainGrid = *reinterpret_cast<short* const*>(owner + core::application_layout::TempTerrainGrid);
        if (static_cast<int>(temporaryTerrainGrid[index]) < zInt)
            temporaryTerrainGrid[index] = static_cast<short>(static_cast<unsigned int>(zInt) & 0xFFFFu);
    }


    void MAP::ClearTempGroundZ(float x, float y, float z)
    {
        const float mapSizeX = SizeX();
        const float mapSizeY = SizeY();
        if (floatLessOrUnordered(x, 0.0f))
            return;
        if (!floatLessOrUnordered(x, mapSizeX))
            return;
        if (floatLessOrUnordered(y, 0.0f))
            return;
        if (!floatLessOrUnordered(y, mapSizeY))
            return;

        const int zInt = mapConvertFloatToInt32(z);
        const int negativeGridY = mapMultiplyAndConvertToInt32(y, -0.125f);
        const int yProduct = static_cast<int>(
            static_cast<std::uint32_t>(negativeGridY) *
            static_cast<std::uint32_t>(terrainGridWidth()));
        const int xInt = mapConvertFloatToInt32(x);
        const int xDiv8 = (xInt + (xInt < 0 ? 7 : 0)) >> 3;
        const int index = static_cast<int>(
            static_cast<std::uint32_t>(xDiv8) - static_cast<std::uint32_t>(yProduct));
        const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
        short* const temporaryTerrainGrid = *reinterpret_cast<short* const*>(owner + core::application_layout::TempTerrainGrid);
        if (static_cast<int>(temporaryTerrainGrid[index]) == zInt)
            temporaryTerrainGrid[index] = 0;
    }


    float MAP::GetGroundZ(float x, float y) const
    {
        const float mapSizeX = SizeX();
        const float mapSizeY = SizeY();
        const int gridY = mapGridCoordinate(y, mapSizeY);
        const int gridX = mapGridCoordinate(x, mapSizeX);
        const std::int32_t index = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(gridX) +
            static_cast<std::uint32_t>(terrainGridWidth()) *
            static_cast<std::uint32_t>(gridY));
        const short* const permanent = terrainGrid();
        const int permanentZ = static_cast<int>(permanent[index]);
        const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
        const short* const temporaryTerrainGrid = *reinterpret_cast<short* const*>(owner + core::application_layout::TempTerrainGrid);
        const int temporaryZ = static_cast<int>(temporaryTerrainGrid[index]);
        return static_cast<float>(permanentZ > temporaryZ ? permanentZ : temporaryZ);
    }


    float MAP::GetGroundZScr(float screenX, float screenY) const noexcept
    {
        const int baseY = mapGridCoordinate(screenY, SizeY());
        const int gridX = mapGridCoordinate(screenX, SizeX());
        const int width = terrainGridWidth();
        const int height = terrainGridHeight();

        int row = static_cast<std::int32_t>(static_cast<std::uint32_t>(baseY) + 32u);
        if (row >= height)
            row = static_cast<std::int32_t>(static_cast<std::uint32_t>(height) - 1u);

        std::uint32_t index = static_cast<std::uint32_t>(gridX) +
            static_cast<std::uint32_t>(row) * static_cast<std::uint32_t>(width);
        const short* const permanent = terrainGrid();
        const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
        const short* const temporary = *reinterpret_cast<short* const*>(
            owner + core::application_layout::TempTerrainGrid);

        for (; row >= baseY;
             row = static_cast<std::int32_t>(static_cast<std::uint32_t>(row) - 1u),
             index -= static_cast<std::uint32_t>(width))
        {
            const int z = static_cast<std::int32_t>(
                (static_cast<std::uint32_t>(row) - static_cast<std::uint32_t>(baseY)) << 3u);
            const std::int32_t cell = static_cast<std::int32_t>(index);
            if (static_cast<int>(permanent[cell]) >= z ||
                static_cast<int>(temporary[cell]) >= z)
                return static_cast<float>(z);
        }
        return 0.0f;
    }


    float MAP::GetGroundZ(const VID* vid, VECTOR2 v) const
    {
        if (vid->spriteClass != B_MAN)
            return GetGroundZ(v.x, v.y);

        const float mapSizeX = SizeX();
        const float mapSizeY = SizeY();


        const long double halfXExtended =
            static_cast<long double>(vid->sizeXYZ.x) * 0.5L;
        const long double halfYExtended =
            static_cast<long double>(vid->sizeXYZ.y) * 0.5L;
        const float halfYStored = static_cast<float>(halfYExtended);
        const float maxXInput = static_cast<float>(
            static_cast<long double>(v.x) + halfXExtended - 3.0L);
        const float maxYInput = static_cast<float>(
            static_cast<long double>(v.y) + halfYExtended - 3.0L);
        const float minXInput = static_cast<float>(
            static_cast<long double>(v.x) - (halfXExtended - 3.0L));
        const float minYInput = static_cast<float>(
            static_cast<long double>(v.y) -
            (static_cast<long double>(halfYStored) - 3.0L));

        const auto scaledGridCoordinate = [](float value, float limit) noexcept -> long double
        {
            if (floatLessOrUnordered(value, 0.0f))
                return 0.0L;
            if (floatLessOrUnordered(value, limit))
                return static_cast<long double>(value) * 0.125L;
            return (static_cast<long double>(limit) - 1.0L) * 0.125L;
        };

        const long double minX = scaledGridCoordinate(minXInput, mapSizeX);
        const long double minY = scaledGridCoordinate(minYInput, mapSizeY);
        const long double maxX = scaledGridCoordinate(maxXInput, mapSizeX);
        const long double maxY = scaledGridCoordinate(maxYInput, mapSizeY);

        std::int32_t result = -16383;
        if (minY > maxY)
            return static_cast<float>(result);

        const short* const permanent = terrainGrid();
        const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
        const short* const temporary = *reinterpret_cast<short* const*>(
            owner + core::application_layout::TempTerrainGrid);
        const std::int32_t gridX = terrainGridWidth();
        long double scanY = minY;
        for (;;)
        {
            if (minX <= maxX)
            {
                const std::int32_t gy = mapConvertExtendedToInt32(scanY);
                const std::int32_t row = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(gy) *
                    static_cast<std::uint32_t>(gridX));

                long double scanX = minX;
                for (;;)
                {
                    const std::int32_t gx = mapConvertExtendedToInt32(scanX);
                    const std::int32_t index = static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(row) +
                        static_cast<std::uint32_t>(gx));
                    const std::int32_t permanentZ = static_cast<std::int16_t>(permanent[index]);
                    if (permanentZ > result)
                        result = permanentZ;
                    const std::int32_t temporaryZ = static_cast<std::int16_t>(temporary[index]);
                    if (temporaryZ > result)
                        result = temporaryZ;

                    scanX += 1.0L;
                    if (scanX > maxX)
                        break;
                }
            }

            scanY += 1.0L;
            if (scanY > maxY)
                break;
        }

        return static_cast<float>(result);
    }


    void MAP::ResetGroundZ()
    {
        BYTE* const owner = reinterpret_cast<BYTE*>(this);
        short*& ground = *reinterpret_cast<short**>(owner + core::application_layout::TerrainGrid);
        short*& tempGround = *reinterpret_cast<short**>(owner + core::application_layout::TempTerrainGrid);
        if (ground)
            ::operator delete(static_cast<void*>(ground));
        if (tempGround)
            ::operator delete(static_cast<void*>(tempGround));

        const int gridX = terrainGridDimension(SizeX());
        const int gridY = terrainGridDimension(SizeY());
        setTerrainGridDimensions(gridX, gridY);
        const std::size_t cells = static_cast<std::size_t>(gridX) * static_cast<std::size_t>(gridY);
        const std::size_t bytes = cells * sizeof(short);
        ground = static_cast<short*>(::operator new(bytes));
        tempGround = static_cast<short*>(::operator new(bytes));
        std::memset(ground, 0, bytes);
        std::memset(tempGround, 0, bytes);
    }


    STRING* MAP::PopStr()
    {
        SCRIPT* const script = reinterpret_cast<SCRIPT*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::ScriptRuntime);
        const int newIndex = script->m_data.stack.count - 1;
        script->m_data.stack.count = newIndex;
        script::StackObject* const top = script->mutableExecutionStackStorageAt(newIndex);

        if ((top->flags & script::STACK_OBJECT_INT) != 0)
        {
            char numericTextBuffer[0x80];
            std::memset(numericTextBuffer, 0, sizeof(numericTextBuffer));
            _itoa(top->intValue, numericTextBuffer, 10);

            STRING convertedText;
            ::new (static_cast<void*>(&convertedText)) as1::STRING(numericTextBuffer);
            (top->text).Assign(convertedText);
            convertedText.ReleaseOwnedStorage();
        }

        return &top->text;
    }


    int MAP::PopInt()
    {
        SCRIPT* const script = reinterpret_cast<SCRIPT*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::ScriptRuntime);
        const int oldIndex = script->m_data.stack.count - 1;
        script::StackObject* const top = script->mutableExecutionStackStorageAt(oldIndex);
        script->m_data.stack.count = oldIndex;
        return mapStackValueToInteger(*top);
    }


    VID* MAP::PopVid(const char* errorContext)
    {
        const int nvid = PopInt();
        auto* const owner = reinterpret_cast<std::uint8_t*>(this);
        const int count = *reinterpret_cast<const int*>(
            owner + core::application_layout::VidCount);
        VID* vid = EmptyVid;
        if (nvid >= 0 && nvid < count)
        {
            VID* const slot = *reinterpret_cast<VID**>(
                owner + core::application_layout::VidTable +
                static_cast<std::size_t>(nvid) * sizeof(VID*));
            if (slot)
                vid = slot;
        }
        if (vid == EmptyVid && errorContext && *errorContext)
            writeLogLine(g_fileLogger, "!!!ERROR!!!SCRIPT: Invalid nvid %s %i", errorContext, nvid);
        return vid;
    }


    void MAP::PushInt(int value)
    {
        SCRIPT* const script = reinterpret_cast<SCRIPT*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::ScriptRuntime);
        script::StackObject obj;
        obj.assignFields(static_cast<std::uint8_t>(script::STACK_OBJECT_INT), value, STRING());
        script->m_data.stack.appendFields(
            obj.flags, obj.intValue, obj.text);
    }


    void MAP::PushStr(const STRING& value)
    {
        SCRIPT* const script = reinterpret_cast<SCRIPT*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::ScriptRuntime);

        STRING copiedText;
        ::new (static_cast<void*>(&copiedText)) as1::STRING(value.c_str());

        script->m_data.stack.appendFields(
            static_cast<std::uint8_t>(script::STACK_OBJECT_STRING), 0, copiedText);
        copiedText.ReleaseOwnedStorage();
    }


}

