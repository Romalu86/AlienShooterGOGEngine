#include "groups.h"

#include "core/resource.h"
#include "core/application.h"
#include "graph.h"
#include "map.h"
#include "sprite.h"

#include <cstdint>
#include <new>

namespace as1
{
#if defined(_M_IX86)
    static_assert(sizeof(Group) == 0x24u, "Group layout 0x24 bytes");
    static_assert(sizeof(GROUPS) == 0x24u, "GROUPS sentinel layout 0x24 bytes");
#endif
    namespace
    {
        constexpr std::int32_t END_GROUP_INT = -1;
    }


    Group::Group(Group* insertAfter, SPRITE* firstSprite) noexcept
    {
        if (insertAfter)
        {
            m_next = insertAfter->m_next;
            insertAfter->m_next = this;
        }
        else
        {
            m_next = this;
        }
        if (firstSprite)
            append(firstSprite);
    }


    Group::~Group()
    {
        Group* const next = m_next;
        Group* predecessor = next;
        for (Group* cursor = next->m_next; cursor != this; cursor = cursor->m_next)
            predecessor = cursor;
        predecessor->m_next = next;
    }


    void* Group::deletingDestructor(unsigned char deleteSelfFlag) noexcept
    {
        Group* const self = this;
        self->~Group();
        if ((deleteSelfFlag & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }


    void Group::DrawNumber(int number)
    {
        const auto& drawState = core::GlobalApplicationDrawDispatcherState();
        for (int index = 0; index < m_count; ++index)
        {
            SPRITE* const sprite = m_items[static_cast<std::size_t>(index)];
            Graph->PrintfXY(sprite->X() - drawState.cameraShiftX(),
                            sprite->Y() - sprite->Z() - drawState.cameraShiftY(),
                            "%i", number);
        }
    }


    void Group::append(SPRITE* sprite)
    {
        if (m_count != 0)
        {
            m_centerX = (sprite->X() + m_centerX) * 0.5f;
            m_centerY = (sprite->Y() + m_centerY) * 0.5f;
        }
        else
        {
            m_centerX = sprite->X();
            m_centerY = sprite->Y();
        }


        reinterpret_cast<BaseSpriteList<0>*>(this)->append(sprite);
    }



    void GROUPS::Load(RESOURCE* resource)
    {
        for (;;)
        {
            SPRITE* const firstSprite = Map->ReadPointer(resource);
            if (firstSprite == reinterpret_cast<SPRITE*>(static_cast<std::intptr_t>(-1)))
                break;

            void* const memory = ::operator new(sizeof(Group), std::nothrow);
            Group* const group = memory ? new (memory) Group(this, firstSprite) : nullptr;
            if (!group)
                return;

            for (;;)
            {
                SPRITE* const sprite = Map->ReadPointer(resource);
                if (sprite == reinterpret_cast<SPRITE*>(static_cast<std::intptr_t>(-1)))
                    break;
                group->append(sprite);
            }
        }
    }


    void GROUPS::Save(RESOURCE* resource)
    {
        for (Group* group = First(); group;)
        {
            if (group->m_count != 0)
            {
                for (int index = 0; index < group->m_count; ++index)
                {
                    const std::uint32_t spritePointerValue = static_cast<std::uint32_t>(
                        reinterpret_cast<std::uintptr_t>(group->m_items[index]) & 0xFFFFFFFFu);
                    resource->write(&spritePointerValue, 4u);
                }
                const std::int32_t end = END_GROUP_INT;
                resource->write(&end, 4u);
            }

            Group* const next = group->m_next;
            group = next != this ? next : nullptr;
        }

        const std::int32_t end = END_GROUP_INT;
        resource->write(&end, 4u);
    }


    void GROUPS::DeletePointerToSprite(SPRITE* sprite)
    {
        Group* group = First();
        while (group)
        {


            if (reinterpret_cast<BaseSpriteList<0>*>(group)->removeSorted(sprite) == 0 && group->m_count == 0)
            {
                Group* const dead = group;
                Group* const next = group->m_next;
                group = next != this ? next : nullptr;
                delete dead;
            }
            else
            {
                Group* const next = group->m_next;
                group = next != this ? next : nullptr;
            }
        }
    }


    void GROUPS::DrawNumber()
    {
        Group* group = First();
        int number = 0;
        while (group)
        {
            group->DrawNumber(number++);
            Group* const next = group->m_next;
            group = next != this ? next : nullptr;
        }
    }

}
