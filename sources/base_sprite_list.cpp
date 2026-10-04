#include "base_sprite_list.h"

#include <cmath>
#include <algorithm>
#include <new>
#include <cstring>
#include "sprite.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "core/resource.h"
#include "core/application.h"
#include "graph.h"
#include "map.h"
#include "menu.h"
#include "input.h"
#include "vid/vid.h"

namespace as1
{

#if defined(_M_IX86)
    static_assert(sizeof(core::List<SPRITE*>) == 0x10u, "core::List<SPRITE*> layout 0x10 bytes");
    static_assert(sizeof(BaseSpriteList<0>) == 0x10u, "BaseSpriteList<0> layout 0x10 bytes");
#endif




    void BaseSpriteList<0>::append(SPRITE* sprite)
    {
        if (!sprite)
            return;
        (void)sprite->AddListReference();

        if (m_count >= m_capacity)
        {
            const int nextCapacity = m_capacity * 2 + 4;
            if (nextCapacity > m_capacity)
            {
                SPRITE** const oldItems = m_items;
                SPRITE** const nextItems = static_cast<SPRITE**>(
                    ::operator new(sizeof(SPRITE*) * static_cast<std::size_t>(nextCapacity), std::nothrow));
                m_items = nextItems;
                if (!nextItems)
                    fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", nextCapacity);

                if (oldItems)
                {
                    for (int i = 0; i < m_capacity; ++i)
                        nextItems[i] = oldItems[i];
                    ::operator delete(oldItems);
                }
                m_capacity = nextCapacity;
            }
        }

        m_items[m_count++] = sprite;
    }


    int BaseSpriteList<0>::removeSorted(SPRITE* sprite)
    {
        if (!sprite)
            return 1;

        int index = m_count;
        while (index != 0)
        {
            --index;
            if (m_items[index] == sprite)
                break;
        }
        if (index < 0 || index >= m_count || m_items[index] != sprite)
            return 1;

        --m_count;
        m_items[index] = m_items[m_count];

        const int refs = sprite->listReferenceCount() - 1;
        sprite->setListReferenceCount(refs);
        if (refs > 0)
            return 0;
        if (refs < 0)
        {
            VID* const vid = sprite->Vid();
            logFileLoggerResourceError(g_fileLogger, "SPRITE %i", 4, "noRef at Release", refs, vid ? vid->nvid() : -1);
            return 0;
        }
        delete sprite;
        return 0;
    }


    int BaseSpriteList<0>::DeleteSpriteNumber(int index)
    {
        if (index < 0 || index >= m_count)
            return 1;

        SPRITE* const sprite = m_items[index];
        --m_count;
        m_items[index] = m_items[m_count];

        const int refs = sprite->listReferenceCount() - 1;
        sprite->setListReferenceCount(refs);
        if (refs < 0)
        {
            VID* const vid = sprite->Vid();
            logFileLoggerResourceError(g_fileLogger, "SPRITE %i", 4, "noRef at Release", refs, vid ? vid->nvid() : -1);
            return 0;
        }
        if (sprite)
            delete sprite;
        return 0;
    }


    void BaseSpriteList<0>::deleteAllSprites()
    {
        for (int first = 0; first < m_count; ++first)
        {
            for (int scan = m_count - 1; scan > first; --scan)
            {
                SPRITE* const sprite = m_items[first];
                if (!sprite || sprite != m_items[scan])
                    continue;

                const int refs = sprite->listReferenceCount() - 1;
                sprite->setListReferenceCount(refs);
                if (refs < 0)
                {
                    VID* const vid = sprite->Vid();
                    logFileLoggerResourceError(g_fileLogger, "SPRITE %i", 4, "noRef at Release", refs, vid ? vid->nvid() : -1);
                }
                else if (refs == 0)
                {
                    delete sprite;
                }

                --m_count;
                m_items[scan] = m_items[m_count];
            }
        }

        for (int index = m_count - 1; index >= 0; --index)
        {
            if (m_items[index])
                (void)DeleteSpriteNumber(index);
        }


        SPRITE** const storage = m_items;
        m_capacity = 0;
        m_count = 0;
        if (storage)
            ::operator delete(storage);
        m_items = nullptr;
    }


    void BaseSpriteList<0>::clear()
    {
        for (int index = m_count - 1; index >= 0; --index)
        {
            SPRITE* const sprite = m_items[index];
            if (!sprite)
                continue;

            const int refs = sprite->listReferenceCount() - 1;
            sprite->setListReferenceCount(refs);
            if (refs > 0)
            {
                --m_count;
                m_items[index] = m_items[m_count];
                continue;
            }

            if (refs < 0)
            {
                VID* const vid = sprite->Vid();
                logFileLoggerResourceError(g_fileLogger, "SPRITE %i", 4, "noRef at Release", refs, vid ? vid->nvid() : -1);
                continue;
            }

            delete sprite;
        }


        SPRITE** const storage = m_items;
        m_capacity = 0;
        m_count = 0;
        if (storage)
            ::operator delete(storage);
        m_items = nullptr;
    }


    BaseSpriteList<0> g_spriteWorkList;




    void* core::List<SPRITE*>::deletingDestructor(unsigned char deletingDestructorFlags) noexcept
    {
        core::List<SPRITE*>* const self = this;
        ::operator delete(m_items);
        m_items = nullptr;
        m_count = 0;
        if ((deletingDestructorFlags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }


    void* BaseSpriteList<0>::deletingDestructor(unsigned char deletingDestructorFlags) noexcept
    {
        if ((deletingDestructorFlags & 2u) != 0u)
        {
            unsigned char* const first = reinterpret_cast<unsigned char*>(this);
            auto* const cookie = reinterpret_cast<std::uint32_t*>(first) - 1;
            const std::uint32_t count = *cookie;
            for (std::uint32_t i = count; i != 0u; --i)
            {
                auto* const record = reinterpret_cast<BaseSpriteList<0>*>(
                    first + static_cast<std::size_t>(i - 1u) * 0x10u);
                record->~BaseSpriteList<0>();
            }
            void* const allocation = static_cast<void*>(cookie);
            if ((deletingDestructorFlags & 1u) != 0u)
                ::operator delete(allocation);
            return allocation;
        }

        BaseSpriteList<0>* const self = this;
        self->~BaseSpriteList<0>();
        if ((deletingDestructorFlags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }


}

