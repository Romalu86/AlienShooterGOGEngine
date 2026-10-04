#include "core/application.h"
#include "core/as_string.h"
#include "core/file_logger.h"
#include "core/log.h"

#include "graph.h"
#include "base_sprite_list.h"
#include "menu.h"
#include "map.h"
#include "mouse.h"
#include "sprite.h"
#include "sprite_collector.h"
#include "builded_terrain.h"
#include "vid/vid_hardware.h"
#include "unit.h"
#include "man.h"
#include "cannon.h"
#include "building.h"
#include "rail.h"
#include "depo.h"
#include "avia.h"
#include "vid/vid.h"
#include "script.h"
#include "constant.h"

#include <mmsystem.h>

#include <cmath>
#include <cstring>
#include <new>
#include <initializer_list>
#include <utility>

namespace as1 { namespace core
{


    std::uint32_t g_currentTimeMilliseconds = 0;
    std::uint32_t g_previousWorldTimeMilliseconds = 0;
    void* g_applicationOwner = nullptr;


    float g_shiftSpeedX = 0.0f;
    float g_shiftSpeedY = 0.0f;
    int EvFunctionNumber[kScriptCallbackSlotCount] = {};
    std::uint32_t PrevRealCurrentTime = 0;
    std::uint32_t RealCurrentTime = 0;
    std::uint32_t g_bucketTimingSnapshotMilliseconds = 0;
    std::uint32_t g_demoRealTimeBaseMilliseconds = 0;
    std::uint32_t g_demoRecordedTimeBaseMilliseconds = 0;
    std::uint32_t g_childRotationCorrectionPending = 0;

    namespace
    {


    }


    namespace
    {
        template <class T>
        T& applicationSlot(void* owner, std::size_t offset) noexcept
        {

            return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(owner) + offset);
        }

        template <class T>
        const T& applicationSlot(const void* owner, std::size_t offset) noexcept
        {

            return *reinterpret_cast<const T*>(static_cast<const std::uint8_t*>(owner) + offset);
        }
    }


    namespace
    {
        __forceinline int drawPassConvertFloatToInt32(float value) noexcept
        {
            const long double d = static_cast<long double>(value);
            if (!std::isfinite(d) || d >= 9223372036854775808.0L || d < -9223372036854775808.0L)
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        __forceinline int drawPassMultiplyAddAndConvertToInt32(float value, float scale, float addend) noexcept
        {
            const long double d = static_cast<long double>(value) * static_cast<long double>(scale) + static_cast<long double>(addend);
            if (!std::isfinite(d) || d >= 9223372036854775808.0L || d < -9223372036854775808.0L)
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        int drawPassSubtractAndConvertToInt32(float lhs, float rhs) noexcept
        {
            const long double d = static_cast<long double>(lhs) - static_cast<long double>(rhs);
            if (!std::isfinite(d) || d >= 9223372036854775808.0L || d < -9223372036854775808.0L)
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        int drawSpriteAndCaptureReturnValue(SPRITE* sprite) noexcept
        {
            sprite->Draw();
            return 0;
        }

        bool spriteVisibleForDrawPass(const SPRITE* sprite) noexcept
        {
            return sprite && !sprite->isDrawSuppressed();
        }
    }


    void Application::LoadVid(RESOURCE* resource)
    {
        const std::uint32_t loadStart = static_cast<std::uint32_t>(::timeGetTime());
        auto* const applicationBytes = reinterpret_cast<std::uint8_t*>(this);
        WEAPON* const currentWeaponTable = *reinterpret_cast<WEAPON* const*>(
            applicationBytes + application_layout::WeaponTable);
        if (!currentWeaponTable)
        {
            LoadWeapon(resource);
            const int count = *reinterpret_cast<const int*>(applicationBytes + application_layout::WeaponCount);
            if (count != 0)
                writeLogLine(g_fileLogger, "LoadWeapon::No=%-5i             sizeof(WEAPON)=%-4i",
                           count, static_cast<int>(WEAPON::RUNTIME_RECORD_SIZE));
        }

        if (resource->GoBegin(RESOURCE::ResTypes::OBJECT))
        {
            logFileLoggerResourceError(g_fileLogger, "%s", 11, "load 'VID'", 0, "");
            return;
        }

        const bool extraDepot = vidCount() != 0;
        do
        {
            int nvid = 0;
            (void)resource->read(&nvid, sizeof(nvid));
            if (nvid >= static_cast<int>(ApplicationVidTable::kCapacity))
                logFileLoggerResourceError(g_fileLogger, "%s", 4, "nvid > MAX_VID", nvid, "");

            VID** const table = reinterpret_cast<VID**>(applicationBytes + application_layout::VidTable);
            if (VID* const previous = table[nvid])
            {
                delete previous;
                table[nvid] = nullptr;
                logFileLoggerResourceError(g_fileLogger, "%s", 5, "this VID already loaded", nvid, "");
            }

            VID* const created = reinterpret_cast<MAP*>(this)->CreateVid(resource, nvid);
            table[nvid] = created;
            if (!created)
                continue;

            int& count = *reinterpret_cast<int*>(applicationBytes + application_layout::VidCount);
            if (nvid >= count)
                count = nvid + 1;
            if (extraDepot)
                created->type = static_cast<WORD>(created->type | VID_TYPE_EXTRA);

            WEAPON* const weapons = *reinterpret_cast<WEAPON* const*>(applicationBytes + application_layout::WeaponTable);
            const int weaponCount = *reinterpret_cast<const int*>(applicationBytes + application_layout::WeaponCount);
            const int weaponIndex = created->nWeapon;
            if (weaponIndex < weaponCount)
                created->weapon = weapons + weaponIndex;
            else
            {
                created->logVidResourceError(10, "nWeapon > noWeapon", weaponIndex);
                created->weapon = weapons;
            }
            Graph->DrawLoadBar(table[0]);
        }
        while (!resource->GoNextSub(RESOURCE::ResTypes::OBJECT));

        int maxSizeX = 0;
        int maxSizeY = 0;
        const int count = vidCount();
        for (int index = 0; index < count; ++index)
        {
            VID* const vid = vidAt(index);
            if (!vid)
                continue;
            vid->SetChildAndLink();
            const int sizeX = static_cast<int>(vid->vidSizeX);
            const int sizeY = static_cast<int>(vid->vidSizeY);
            if (sizeX > maxSizeX)
                maxSizeX = sizeX;
            if (sizeY > maxSizeY)
                maxSizeY = sizeY;
        }

        const std::uint32_t elapsed = static_cast<std::uint32_t>(::timeGetTime()) - loadStart;
        writeLogLine(g_fileLogger, "LoadVid::No   =%-15i   sizeof(VID)   =%-5i    load time     =%ims   MaxSizeX,Y=%i,%i",
                   count, static_cast<int>(sizeof(VID)), static_cast<int>(elapsed), maxSizeX, maxSizeY);
    }


    void Application::DeleteExtraVid()
    {
        auto* const applicationBytes = reinterpret_cast<std::uint8_t*>(this);

        for (int pass = 0; pass < ApplicationDrawDispatcherState::PassCount; ++pass)
        {
            ApplicationDrawPassBucket& bucket = *reinterpret_cast<ApplicationDrawPassBucket*>(
                applicationBytes + application_layout::DrawLayerOwners +
                static_cast<std::size_t>(pass) * application_layout::DrawLayerStride);
            int cursor = bucket.count() - 1;
            while (cursor >= 0)
            {
                SPRITE* sprite = bucket.spriteAt(cursor);
                while (!sprite && --cursor >= 0)
                    sprite = bucket.spriteAt(cursor);
                if (!sprite)
                    break;

                if ((sprite->Vid()->formatFlags() & VID_TYPE_EXTRA) != 0u)
                    delete sprite;
                --cursor;
            }
        }

        int& count = *reinterpret_cast<int*>(applicationBytes + application_layout::VidCount);
        VID** const table = reinterpret_cast<VID**>(applicationBytes + application_layout::VidTable);
        for (int index = count - 1; index >= 0; --index)
        {
            VID* const vid = table[index];
            if (!vid || (vid->formatFlags() & VID_TYPE_EXTRA) == 0u)
                continue;

            delete vid;
            table[index] = nullptr;
        }

        while (count > 0 && table[count - 1] == nullptr)
            --count;
    }


    void Application::ExchangeVid(VID* first, VID* second)
    {
        VID* const nullVid = EmptyVid;
        if (!first || !second || first == second || first == nullVid || second == nullVid)
            return;

        const int count = vidCount();
        for (int index = 0; index < count; ++index)
        {
            VID* const vid = vidAt(index);
            if (!vid)
                continue;

            if (vid->linkVid == first)
                vid->linkVid = second;
            else if (vid->linkVid == second)
                vid->linkVid = first;

            for (int slot = 0; slot < VID::NO_ANIMATION; ++slot)
            {
                if (vid->childVid[slot] == first)
                    vid->childVid[slot] = second;
                else if (vid->childVid[slot] == second)
                    vid->childVid[slot] = first;
            }
        }

        auto* const applicationBytes = reinterpret_cast<std::uint8_t*>(this);
        VID** const table = reinterpret_cast<VID**>(applicationBytes + application_layout::VidTable);
        const int firstNvid = first->nVid;
        const int secondNvid = second->nVid;
        table[firstNvid] = second;
        table[secondNvid] = first;

        std::swap(first->exchangedVid, second->exchangedVid);
        std::swap(first->nVid, second->nVid);

        for (int slot = 0; slot < VID::ScriptFunctionSlotCount; ++slot)
            std::swap(first->scriptFunction[slot], second->scriptFunction[slot]);
        std::swap(first->scriptAux0, second->scriptAux0);
        std::swap(first->scriptAux1, second->scriptAux1);
    }


    void Application::LoadWeapon(RESOURCE* resource)
    {
        auto* const applicationBytes = reinterpret_cast<std::uint8_t*>(this);
        WEAPON*& table = *reinterpret_cast<WEAPON**>(applicationBytes + application_layout::WeaponTable);
        int& count = *reinterpret_cast<int*>(applicationBytes + application_layout::WeaponCount);

        if (table)
            ::operator delete(table);
        table = nullptr;

        void* loaded = nullptr;
        count = resource->Load(RESOURCE::ResTypes::WEAPON, &loaded,
                               static_cast<int>(WEAPON::RECORD_SIZE));
        table = static_cast<WEAPON*>(loaded);

        if (count > 0)
        {
            constexpr std::uint32_t kUnlimitedBits = 0x497423F0u;
            for (int record = 0; record < count; ++record)
            {
                std::uint8_t* const recordBytes = table[record].recordData.data();
                for (int index = 0; index < 8; ++index)
                {
                    const std::size_t delta = static_cast<std::size_t>(index) * sizeof(float);
                    for (const std::size_t base : { std::size_t(0x224u), std::size_t(0x244u) })
                    {
                        std::uint32_t bits = 0;
                        std::memcpy(&bits, recordBytes + base + delta, sizeof(bits));
                        if (bits == kUnlimitedBits)
                            continue;

                        float value = 0.0f;
                        std::memcpy(&value, &bits, sizeof(value));
                        value *= 0.001f;
                        std::memcpy(recordBytes + base + delta, &value, sizeof(value));
                    }
                }
            }
        }

        EmptyVid->setWeaponRecord(table);
    }


    void Application::ReloadVid()
    {
        RESOURCE resource;

        int count = vidCount();
        for (int index = 0; index < count; ++index)
        {
            VID* const tableVid = vidAt(index);
            if (!tableVid)
                continue;

            VID* const exchanged = tableVid->exchangedVidRef();
            if (exchanged != tableVid)
                ExchangeVid(exchanged, tableVid);
            count = vidCount();
        }

        const auto* const applicationBytes = reinterpret_cast<const std::uint8_t*>(this);
        const STRING& resourceName = *reinterpret_cast<const STRING*>(applicationBytes + application_layout::ResourceName);
        if (resource.openFile(&resourceName, RESOURCE::ResTypes::DATA) != 0)
        {
            logFileLoggerResourceError(g_fileLogger, "%s", 7, "resource file", 0, "");
            return;
        }

        LoadWeapon(&resource);

        if (resource.GoBegin(RESOURCE::ResTypes::OBJECT))
        {
            logFileLoggerResourceError(g_fileLogger, "%s", 11, "load 'VID'", 0, "");
            return;
        }

        do
        {
            int objectNvid = 0;
            (void)resource.read(&objectNvid, sizeof(objectNvid));
            if (objectNvid >= static_cast<int>(ApplicationVidTable::kCapacity))
                logFileLoggerResourceError(g_fileLogger, "%s", 4, "nvid > MAX_VID", objectNvid, "");

            VID* const slotVid = vidAt(objectNvid);
            if (!slotVid)
                continue;

            VID* const exchanged = slotVid->exchangedVidRef();
            VID* const target = vidAt(exchanged->nvid());
            target->name.Read(&resource);
            target->LoadParameters(&resource);

            const int weaponIndex = target->nWeapon;
            WEAPON* const weaponTable = *reinterpret_cast<WEAPON* const*>(applicationBytes + application_layout::WeaponTable);
            const int weaponCount = *reinterpret_cast<const int*>(applicationBytes + application_layout::WeaponCount);
            target->weapon = weaponIndex < weaponCount ? weaponTable + weaponIndex : weaponTable;
        }
        while (!resource.GoNextSub(RESOURCE::ResTypes::OBJECT));

        count = vidCount();
        for (int index = 0; index < count; ++index)
        {
            VID* const vid = vidAt(index);
            if (vid && (vid->formatFlags() & VID_TYPE_EXTRA) == 0u)
                vid->SetChildAndLink();
        }
    }


    int Application::beginBucketTimingSnapshot()
    {
        auto* const applicationBytes = reinterpret_cast<std::uint8_t*>(this);
        auto& flags = *reinterpret_cast<std::uint32_t*>(applicationBytes + application_layout::Flags);
        constexpr std::uint32_t flag = ApplicationDrawDispatcherState::BucketTimingFlag;
        if ((flags & flag) == 0u)
            g_bucketTimingSnapshotMilliseconds = CurrentTimeMilliseconds();
        flags |= flag;
        return static_cast<int>(flag);
    }


    int Application::endBucketTimingSnapshot()
    {
        auto* const applicationBytes = reinterpret_cast<std::uint8_t*>(this);
        auto& flags = *reinterpret_cast<std::uint32_t*>(applicationBytes + application_layout::Flags);
        constexpr std::uint32_t flag = ApplicationDrawDispatcherState::BucketTimingFlag;
        if ((flags & flag) != 0u)
        {
            for (int pass = 0; pass < ApplicationDrawDispatcherState::PassCount; ++pass)
            {
                auto* const bucket = reinterpret_cast<ApplicationDrawPassBucket*>(
                    applicationBytes + application_layout::DrawLayerOwners +
                    static_cast<std::size_t>(pass) * application_layout::DrawLayerStride);
                int cursor = bucket->count() - 1;
                while (cursor >= 0)
                {
                    if (SPRITE* const sprite = bucket->spriteAt(cursor))
                        sprite->setApplicationBucketTime(g_bucketTimingSnapshotMilliseconds);
                    --cursor;
                }
            }
            SetCurrentTimeMilliseconds(g_bucketTimingSnapshotMilliseconds);
            SetPreviousWorldTimeMilliseconds(g_bucketTimingSnapshotMilliseconds - 10u);
        }
        flags &= ~flag;
        return static_cast<int>(flags);
    }


    void Application::removeSpriteFromApplicationLists(SPRITE* sprite)
    {
        if (!sprite)
            return;
        const int layer = sprite->Vid()->renderLayer();
        auto* const bucket = reinterpret_cast<ApplicationDrawPassBucket*>(
            reinterpret_cast<std::uint8_t*>(this) + application_layout::DrawLayerOwners +
            static_cast<std::size_t>(layer) * application_layout::DrawLayerStride);
        (void)bucket->findAndNull(sprite);
    }


    char* Application::appendSpriteToApplicationListsAndReleaseReference(SPRITE* sprite)
    {
        if (!sprite)
            return nullptr;
        const int layer = sprite->Vid()->renderLayer();
        auto* const bucket = reinterpret_cast<ApplicationDrawPassBucket*>(
            reinterpret_cast<std::uint8_t*>(this) + application_layout::DrawLayerOwners +
            static_cast<std::size_t>(layer) * application_layout::DrawLayerStride);
        bucket->append(sprite);

        const int refs = sprite->listReferenceCount() - 1;
        sprite->setListReferenceCount(refs);
        if (refs > 0)
            return reinterpret_cast<char*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(refs)));
        if (refs == 0)
        {
            SPRITE* const deletedOwner = sprite;
            delete sprite;
            return reinterpret_cast<char*>(deletedOwner);
        }
        const int nvid = sprite->Vid() ? sprite->Vid()->nVid : -1;
        const std::intptr_t logged = logFileLoggerResourceError(
            g_fileLogger, "SPRITE %i", 4, "noRef at Release", refs, nvid);
        return reinterpret_cast<char*>(static_cast<std::uintptr_t>(logged));
    }


    SPRITE* Application::previousSpriteInDrawPassByVidProperties(
        int pass, int* cursor, std::uint32_t requiredVidProperties)
    {
        const auto* const bucket = reinterpret_cast<const ApplicationDrawPassBucket*>(
            reinterpret_cast<const std::uint8_t*>(this) + application_layout::DrawLayerOwners +
            static_cast<std::size_t>(pass) * application_layout::DrawLayerStride);
        SPRITE* const* const items = bucket->data();

        int index = --(*cursor);
        while (index >= 0)
        {
            SPRITE* candidate = items[index];
            while (!candidate)
            {
                index = --(*cursor);
                if (index < 0)
                    return nullptr;
                candidate = items[index];
            }
            if ((candidate->Vid()->properties() & requiredVidProperties) != 0u)
                return candidate;
            index = --(*cursor);
        }
        return nullptr;
    }


    SPRITE* Application::findSpriteAtPointByBounds(int filter, float x, float y, SPRITE* previous)
    {
        SPRITE* const candidate = findNearestSpriteByFilter(filter, x, y, 300.0f, previous);
        if (!candidate)
            return nullptr;
        VID* const vid = candidate->Vid();
        const float halfX = vid->halfSizeX();
        if (candidate->X() - halfX > x || x > candidate->X() + halfX)
            return nullptr;
        const float halfY = vid->halfSizeY();
        if (candidate->Y() - halfY > y || y > candidate->Y() + halfY)
            return nullptr;
        return candidate;
    }


    SPRITE* Application::findSpriteAtPointByFilter(int filter, float x, float y)
    {
        if (!Map)
            return nullptr;
        MAP& map = *Map;
        ApplicationDrawDispatcherState& state = GlobalApplicationDrawDispatcherState();


        const int originalFilter = filter;
        int bucketMask = filter & 0x000F0000;
        if (bucketMask == 0)
            bucketMask = 0x000F0000;

        ApplicationVidTable& vidTable = GlobalApplicationVidTable();
        VID* requestedVid = EmptyVid;
        const int exactNvid = filter & 0x07FF;
        const bool exactNvidFilter = (filter & 0x00000800) != 0;
        int typeMask = 0;
        if (exactNvidFilter)
        {


            if (exactNvid < vidTable.count())
            {
                if (VID* const slot = vidTable.slot(exactNvid))
                    requestedVid = slot;
            }
            if ((requestedVid->properties() & 0x40u) != 0u)
                filter |= 0x00008000;
            typeMask = static_cast<int>(requestedVid->spriteTypeId());
        }
        else
        {
            typeMask = (filter >> 20) & 0x67F;
            if (typeMask == 0)
                typeMask = 1663;
        }

        SPRITE* selected = nullptr;
        auto preferCandidate = [&](SPRITE* candidate) noexcept
        {


            if (!selected ||
                selected->Vid()->sizeX() > candidate->Vid()->sizeX() ||
                selected->Vid()->sizeY() > candidate->Vid()->sizeY())
            {
                selected = candidate;
            }
        };

        auto passesFilter = [&](SPRITE* candidate) noexcept -> bool
        {
            VID* const candidateVid = candidate->Vid();
            if ((typeMask & static_cast<int>(candidateVid->spriteTypeId())) == 0)
                return false;
            const int candidateBucketBit = 0x10000 << candidate->armyIndex();
            if ((candidateBucketBit & bucketMask) == 0)
                return false;
            if ((filter & static_cast<int>(0x80000000u)) != 0 && (candidate->runtimeFlags() & SPRITE::CommandBitsMask) != 0u)
                return false;
            if ((filter & 0x1000) != 0 && candidateVid->spriteClassId() != static_cast<DWORD>(filter & 0x7FF))
                return false;
            if (exactNvidFilter && candidateVid->nvid() != exactNvid)
                return false;
            return true;
        };

        auto standardHit = [&](SPRITE* candidate) noexcept -> bool
        {
            VID* const candidateVid = candidate->Vid();


            const float centerX = candidate->X();
            const float halfX = candidateVid->halfSizeX();
            const float lowerX = centerX - halfX;
            const float upperX = centerX + halfX;
            if (!(x >= lowerX) || !(upperX >= x))
                return false;

            const float baseY = candidate->Y() - candidate->Z();
            const float halfY = candidateVid->halfSizeY();
            const float lowerY = baseY - candidateVid->sizeZ() - halfY;
            const float upperY = baseY + halfY;
            return y > lowerY && upperY > y;
        };

        auto regionAwareHit = [&](SPRITE* candidate) noexcept -> bool
        {
            VID* const candidateVid = candidate->Vid();
            if (candidateVid->spriteClassId() == 23u)
            {
                const REGION* const region = static_cast<const REGION*>(candidate);


                const float centerX = candidate->X();
                const float halfX = region->regionWidth() * 0.5f;
                const float lowerX = centerX - halfX;
                const float upperX = centerX + halfX;
                if (!(x >= lowerX) || !(upperX >= x))
                    return false;

                const float baseY = candidate->Y() - candidate->Z();
                const float halfY = region->regionHeight() * 0.5f;
                const float lowerY = baseY - candidateVid->sizeZ() - halfY;
                const float upperY = baseY + halfY;
                return y > lowerY && upperY > y;
            }
            return standardHit(candidate);
        };

        if ((filter & 0x8000) != 0)
        {
            const float maxY = map.GetGroundZ(x, y) + y + 300.0f;
            SPRITE_COLLECTOR* const hash = GlobalSpriteCollector();
            for (SPRITE* candidate = hash->FirstHashInBox(x - 300.0f, y - 300.0f, x + 300.0f, maxY);
                 candidate;
                 candidate = hash->NextHashInBox())
            {
                if (candidate->childBacklink() != nullptr)
                    continue;
                if (!passesFilter(candidate) || !standardHit(candidate))
                    continue;
                preferCandidate(candidate);
            }
            return selected;
        }

        if ((typeMask & 0x0C) == 0 || (typeMask & 0x673) != 0)
        {


            if (exactNvidFilter &&
                (requestedVid->spriteClassId() == 10u || requestedVid->spriteClassId() == 19u))
            {
                BaseSpriteList<0>& frameList = applicationFrameSpriteList();
                int index = static_cast<int>(frameList.count()) - 1;
                while (index >= 0)
                {
                    SPRITE* const candidate = frameList.at(static_cast<std::size_t>(index));
                    if (!candidate)
                        return selected;
                    if (candidate->childBacklink() == nullptr && passesFilter(candidate) && standardHit(candidate))
                        preferCandidate(candidate);
                    --index;
                }
                return selected;
            }

            int firstPass = 0;


            int endPass = ApplicationDrawDispatcherState::PassCount - 1;
            if (exactNvidFilter)
            {
                firstPass = requestedVid->renderLayer();
                endPass = firstPass + 1;
            }

            if ((typeMask & 0x40) == 0)
            {
                for (int pass = firstPass; pass < endPass; ++pass)
                {
                    int cursor = state.drawPassBucket(pass).count();
                    for (SPRITE* candidate = previousSpriteInDrawPass(pass, &cursor);
                         candidate;
                         candidate = previousSpriteInDrawPass(pass, &cursor))
                    {
                        if (candidate->childBacklink() != nullptr)
                            continue;
                        if (!passesFilter(candidate) || !standardHit(candidate))
                            continue;
                        preferCandidate(candidate);
                    }
                }
                return selected;
            }

            for (int pass = firstPass; pass < endPass; ++pass)
            {
                int cursor = state.drawPassBucket(pass).count();
                for (SPRITE* candidate = previousSpriteInDrawPass(pass, &cursor);
                     candidate;
                     candidate = previousSpriteInDrawPass(pass, &cursor))
                {
                    if (candidate->childBacklink() != nullptr)
                        continue;
                    if (!passesFilter(candidate) || !regionAwareHit(candidate))
                        continue;
                    preferCandidate(candidate);
                }
            }
            return selected;
        }

        SPRITE_COLLECTOR* const hash = GlobalSpriteCollector();
        core::List<SPRITE*>& overflow = hash->mutableOverflowList();
        int* const cursor = hash->reverseCursorAddress();
        for (SPRITE* candidate = overflow.BeginIterate(cursor);
             candidate;
             candidate = overflow.NextIterate(cursor))
        {
            if (!passesFilter(candidate) || !standardHit(candidate))
                continue;
            preferCandidate(candidate);
        }
        (void)originalFilter;
        return selected;

    }


    SPRITE* Application::findNearestSpriteByFilter(int filter, float x, float y, float radius, SPRITE* previous)
    {
        if (!Map)
            return nullptr;
        MAP& map = *Map;
        ApplicationDrawDispatcherState& state = GlobalApplicationDrawDispatcherState();


        const int originalFilter = filter;
        int bucketMask = filter & 0x000F0000;
        if (bucketMask == 0)
            bucketMask = 0x000F0000;

        ApplicationVidTable& vidTable = GlobalApplicationVidTable();
        VID* requestedVid = EmptyVid;
        const int exactNvid = filter & 0x07FF;
        const bool exactNvidFilter = (filter & 0x00000800) != 0;
        int typeMask = 0;
        if (exactNvidFilter)
        {


            if (exactNvid < vidTable.count())
            {
                if (VID* const slot = vidTable.slot(exactNvid))
                    requestedVid = slot;
            }
            if ((requestedVid->properties() & 0x40u) != 0u)
                filter |= 0x00008000;
            typeMask = static_cast<int>(requestedVid->spriteTypeId());
        }
        else
        {
            typeMask = (filter >> 20) & 0x67F;
            if (typeMask == 0)
                typeMask = 1663;
        }

        SPRITE* selected = nullptr;
        const long double previousDistance = previous
            ? as1::approximatePlanarDistance(x - previous->X(), y - previous->Y())
            : -1.0L;

        float bestDistance = radius;

        auto passesFilter = [&](SPRITE* candidate) noexcept -> bool
        {
            VID* const candidateVid = candidate->Vid();
            if ((typeMask & static_cast<int>(candidateVid->spriteTypeId())) == 0)
                return false;
            const int candidateBucketBit = 0x10000 << candidate->armyIndex();
            if ((candidateBucketBit & bucketMask) == 0)
                return false;
            if ((filter & static_cast<int>(0x80000000u)) != 0 && (candidate->runtimeFlags() & SPRITE::CommandBitsMask) != 0u)
                return false;
            if ((filter & 0x1000) != 0 && candidateVid->spriteClassId() != static_cast<DWORD>(filter & 0x7FF))
                return false;
            if (exactNvidFilter && candidateVid->nvid() != exactNvid)
                return false;
            return true;
        };

        auto acceptMetric = [&](SPRITE* candidate, long double metric) noexcept
        {
            if (!(metric > previousDistance))
                return;
            if (metric < static_cast<long double>(bestDistance) ||
                std::isnan(bestDistance))
            {
                bestDistance = static_cast<float>(metric);
                selected = candidate;
            }
        };

        auto considerDistance = [&](SPRITE* candidate) noexcept
        {
            if (candidate->childBacklink() != nullptr || !passesFilter(candidate))
                return;

            const float dx = x - candidate->X();
            const float dy = y - candidate->Y();
            acceptMetric(candidate, as1::approximatePlanarDistance(dx, dy));
        };

        auto considerWeighted = [&](SPRITE* candidate) noexcept
        {
            if (candidate->childBacklink() != nullptr || !passesFilter(candidate))
                return;


            const float dx = x - candidate->X();
            const float dy = y - candidate->Y();
            acceptMetric(candidate, as1::approximatePlanarDistance(dx, dy));
        };

        if ((filter & 0x8000) != 0)
        {
            SPRITE_COLLECTOR* const hash = GlobalSpriteCollector();
            for (SPRITE* candidate = hash->FirstHashInBox(x - radius, y - radius, x + radius, y + radius);
                 candidate;
                 candidate = hash->NextHashInBox())
            {
                considerDistance(candidate);
            }
            return selected;
        }

        if ((typeMask & 0x0C) == 0 || (typeMask & 0x673) != 0)
        {


            if (exactNvidFilter &&
                (requestedVid->spriteClassId() == 10u || requestedVid->spriteClassId() == 19u))
            {
                BaseSpriteList<0>& frameList = applicationFrameSpriteList();
                int index = static_cast<int>(frameList.count()) - 1;
                if (index < 0)
                    return selected;
                SPRITE* candidate = frameList.at(static_cast<std::size_t>(index));
                if (!candidate)
                    return selected;
                for (;;)
                {
                    considerWeighted(candidate);
                    --index;
                    if (index < 0)
                        break;
                    candidate = frameList.at(static_cast<std::size_t>(index));
                    if (!candidate)
                        return selected;
                }
                return selected;
            }

            int firstPass = 0;


            int endPass = ApplicationDrawDispatcherState::PassCount - 1;
            if (exactNvidFilter)
            {
                firstPass = requestedVid->renderLayer();
                endPass = firstPass + 1;
            }

            for (int pass = firstPass; pass < endPass; ++pass)
            {
                int cursor = state.drawPassBucket(pass).count();
                for (SPRITE* candidate = previousSpriteInDrawPass(pass, &cursor);
                     candidate;
                     candidate = previousSpriteInDrawPass(pass, &cursor))
                {
                    considerWeighted(candidate);
                }
            }
            return selected;
        }

        SPRITE_COLLECTOR* const hash = GlobalSpriteCollector();
        core::List<SPRITE*>& overflow = hash->mutableOverflowList();
        int* const cursor = hash->reverseCursorAddress();
        for (SPRITE* candidate = overflow.BeginIterate(cursor);
             candidate;
             candidate = overflow.NextIterate(cursor))
        {
            considerWeighted(candidate);
        }
        (void)originalFilter;
        return selected;

    }


    SPRITE* Application::previousSpriteInDrawPass(int pass, int* cursor)
    {
        int index = --(*cursor);
        if (index < 0)
            return nullptr;

        const auto* const applicationBytes = reinterpret_cast<const std::uint8_t*>(this);
        const ApplicationDrawPassBucket& bucket = *reinterpret_cast<const ApplicationDrawPassBucket*>(
            applicationBytes + application_layout::DrawLayerOwners +
            static_cast<std::size_t>(pass) * application_layout::DrawLayerStride);
        SPRITE* const* const items = bucket.data();
        while (!items[index])
        {
            index = --(*cursor);
            if (index < 0)
                return nullptr;
        }
        return items[index];
    }


    int Application::drawSpritePass(int pass)
    {
        const int currentPass = pass;
        int cursor = 0;
        if (pass != 0 && pass != 10)
        {
            GRAPH* const graph = Graph;
            constexpr float half = 0.5f;
            const int viewCenterX = drawPassMultiplyAddAndConvertToInt32(
                graph->screenWidth(), half, *reinterpret_cast<const float*>(reinterpret_cast<const std::uint8_t*>(this) + application_layout::CameraShiftX));
            const int viewCenterY = drawPassMultiplyAddAndConvertToInt32(
                graph->screenHeight(), half, *reinterpret_cast<const float*>(reinterpret_cast<const std::uint8_t*>(this) + application_layout::CameraShiftY));

            const auto* const applicationBytes = reinterpret_cast<const std::uint8_t*>(this);
            const ApplicationDrawPassBucket& drawBucket = *reinterpret_cast<const ApplicationDrawPassBucket*>(
                applicationBytes + application_layout::DrawLayerOwners +
                static_cast<std::size_t>(pass) * application_layout::DrawLayerStride);
            cursor = drawBucket.count();
            SPRITE* const* const drawItems = drawBucket.data();
            SPRITE* sprite = previousSpriteInDrawPass(pass, &cursor);
            while (sprite)
            {
                if (spriteVisibleForDrawPass(sprite))
                {
                    const std::uint32_t xMaskValue =
                        static_cast<std::uint32_t>(drawPassConvertFloatToInt32(sprite->X())) -
                        static_cast<std::uint32_t>(viewCenterX) + 0x400u;

                    bool draw = false;
                    if ((xMaskValue & 0xFFFFF800u) != 0u)
                    {
                        const std::int32_t topYDelta = static_cast<std::int32_t>(
                            static_cast<std::uint32_t>(drawPassConvertFloatToInt32(sprite->Y())) -
                            static_cast<std::uint32_t>(viewCenterY));
                        if (topYDelta >= 0x200)
                            draw = true;
                    }
                    else
                    {
                        const std::uint32_t baseYMaskValue =
                            static_cast<std::uint32_t>(drawPassSubtractAndConvertToInt32(sprite->Y(), sprite->Z())) -
                            static_cast<std::uint32_t>(viewCenterY) + 0x200u;
                        if ((baseYMaskValue & 0xFFFFFC00u) == 0u)
                        {
                            draw = true;
                        }
                        else
                        {
                            const std::int32_t topYDelta = static_cast<std::int32_t>(
                                static_cast<std::uint32_t>(drawPassConvertFloatToInt32(sprite->Y())) -
                                static_cast<std::uint32_t>(viewCenterY));
                            if (topYDelta >= 0x200)
                                draw = true;
                        }
                    }

                    if (draw)
                        sprite->Draw();
                }

                --cursor;
                while (cursor >= 0 && !drawItems[cursor])
                    --cursor;
                sprite = cursor >= 0 ? drawItems[cursor] : nullptr;
            }
        }
        else
        {
            const auto* const applicationBytes = reinterpret_cast<const std::uint8_t*>(this);
            const ApplicationDrawPassBucket& bucket = *reinterpret_cast<const ApplicationDrawPassBucket*>(
                applicationBytes + application_layout::DrawLayerOwners +
                static_cast<std::size_t>(pass) * application_layout::DrawLayerStride);
            SPRITE* const* const items = bucket.data();
            cursor = bucket.count() - 1;
            while (cursor >= 0 && !items[cursor])
                --cursor;
            SPRITE* sprite = cursor >= 0 ? items[cursor] : nullptr;
            while (sprite)
            {
                if (spriteVisibleForDrawPass(sprite))
                    sprite->Draw();
                --cursor;
                while (cursor >= 0 && !items[cursor])
                    --cursor;
                sprite = cursor >= 0 ? items[cursor] : nullptr;
            }
        }

        MOUSE* const mouse = mouseInstanceRef();
        int result = mouse->hardwareCursorEnabled();
        VID* const rootMouseVid = mouse->Vid();
        if (result == 0 && (rootMouseVid->properties() & 0x00008000u) == 0u && mouse)
        {
            for (SPRITE* node = mouse; node; node = node->childChain())
            {
                VID* const vid = node->Vid();
                if (vid->renderLayer() != currentPass)
                    continue;

                result = static_cast<int>(node->runtimeFlags());
                if (!node->isDrawSuppressed())
                    result = drawSpriteAndCaptureReturnValue(node);
            }
        }
        return result;
    }


    int Application::callScriptFunctionInternal(int functionIndex, int firstArgument, int secondArgument, int thirdArgument)
    {
        auto* const applicationBytes = reinterpret_cast<std::uint8_t*>(this);
        const std::uint32_t flags = *reinterpret_cast<const std::uint32_t*>(
            applicationBytes + application_layout::Flags);
        if ((flags & application_flags::ScriptCallbacksDisabled) != 0u)
            return 0;

        auto* const scriptOwner = reinterpret_cast<SCRIPT*>(
            applicationBytes + application_layout::ScriptRuntime);
        return scriptOwner->callFunction(functionIndex, "ppi", firstArgument, secondArgument, thirdArgument);
    }


    SPRITE* Application::CreateSprite(VID* vid, VECTOR xyz, ANGLE direction, SPRITE* parent)
    {


        if (!vid)
            return nullptr;
        VID* selectedVid = vid;
        if ((selectedVid->runtimeAuxFlags() & 0x10u) != 0u)
            selectedVid = resolveRegionMappedVid(selectedVid, xyz.x, xyz.y, xyz.z);

        SPRITE* sprite = nullptr;
        switch (selectedVid->spriteClassId())
        {
        case B_UNIT:
            sprite = new UNIT(selectedVid, xyz.x, xyz.y, xyz.z, direction, parent);
            break;
        case B_AVIA:
            sprite = new AVIA(selectedVid, xyz.x, xyz.y, xyz.z, direction, parent);
            break;
        case B_CANNON:
            sprite = new CANNON(selectedVid, xyz.x, xyz.y, xyz.z, direction, parent);
            break;
        case B_PRIMITIVE:


            sprite = new FRAME(selectedVid, xyz.x, xyz.y, xyz.z, direction, parent);
            break;
        case B_MAN:
            sprite = new MAN(selectedVid, xyz.x, xyz.y, xyz.z, direction, parent);
            break;
        case B_BUILDEDTERRAIN:


            sprite = new BUILDED_TERRAIN(selectedVid, xyz.x, xyz.y, xyz.z, direction, parent);
            break;
        case B_SPRITE:
            sprite = new SPRITE(selectedVid, xyz.x, xyz.y, xyz.z, direction, parent);
            break;
        case B_FRAME:


            sprite = new PRIMITIVE(selectedVid, xyz.x, xyz.y, xyz.z, direction, parent);
            break;
        case B_LINKER:
            sprite = new LINKER(selectedVid, xyz.x, xyz.y, xyz.z, direction, parent);
            break;
        case B_TEXT:
            sprite = new STEXT(selectedVid, xyz.x, xyz.y, xyz.z, direction, parent);
            break;
        case B_REGION:
            sprite = new REGION(selectedVid, xyz.x, xyz.y, xyz.z, direction, parent);
            break;
        default:

            logFileLoggerResourceError(g_fileLogger, "MAP", 3, "sprite - Behave is invalidate",
                               static_cast<int>(selectedVid->spriteClassId()));
            return nullptr;
        }

        const std::uint32_t flags = *reinterpret_cast<const std::uint32_t*>(
            reinterpret_cast<const std::uint8_t*>(this) + application_layout::Flags);


        if (sprite && (flags & 0x21u) == 0u)
        {
            const int functionIndex = sprite->Vid()->birthScriptFunction();
            if (functionIndex >= 0)
            {
                const int spriteArg = static_cast<int>(static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(sprite)));
                callScriptFunctionInternal(functionIndex, spriteArg, 0);
            }
        }
        return sprite;
    }


} }
