#pragma once

#include "base_sprite_list.h"
#include "core/types.h"
#include <cstddef>
#include <cstdint>

namespace as1
{
    class MAP;
    class SPRITE;
    class Group;
    class RESOURCE;

    class Group : public core::List<SPRITE*>
    {
    public:
        Group(Group* insertAfter = nullptr, SPRITE* firstSprite = nullptr) noexcept;
        ~Group();

        void* deletingDestructor(unsigned char deleteSelfFlag) noexcept override;

        Group(const Group&) = delete;
        Group& operator=(const Group&) = delete;

        void DrawNumber(int number);
        void append(SPRITE* sprite);

        float X() const noexcept { return m_centerX; }
        float Y() const noexcept { return m_centerY; }
        Group* nextGroup() const noexcept { return m_next; }
        void setNextGroup(Group* next) noexcept { m_next = next; }

    private:
        friend class GROUPS;

        float m_centerX;
        float m_centerY;
        std::uint32_t m_unusedGroupPrimaryState;
        std::uint32_t m_unusedGroupSecondaryState;
        Group* m_next;
    };

    class GROUPS : public Group
    {
    public:
        __forceinline GROUPS() noexcept : Group(nullptr, nullptr) {}
        __forceinline ~GROUPS() = default;

        GROUPS(const GROUPS&) = delete;
        GROUPS& operator=(const GROUPS&) = delete;

        void Load(RESOURCE* map);
        void Save(RESOURCE* map);
        void DeletePointerToSprite(SPRITE* sprite);
        void DrawNumber();
        __forceinline
        Group* Next(const Group* group) noexcept
        {
            if (!group)
                return nullptr;
            Group* const next = group->nextGroup();
            return next != this ? next : nullptr;
        }

        __forceinline
        Group* First() noexcept
        {
            Group* const node = nextGroup();
            return node != this ? node : nullptr;
        }

        __forceinline
        const Group* First() const noexcept
        {
            const Group* const node = nextGroup();
            return node != this ? node : nullptr;
        }

        __forceinline
        std::size_t size() const noexcept
        {
            std::size_t result = 0;
            for (const Group* node = First(); node; ++result)
            {
                const Group* const next = node->nextGroup();
                node = next != this ? next : nullptr;
            }
            return result;
        }

        __forceinline
        bool empty() const noexcept
        {
            return nextGroup() == this;
        }

        __forceinline
        std::size_t refCount() const noexcept
        {
            std::size_t result = 0;
            for (const Group* node = First(); node;)
            {
                result += static_cast<std::size_t>(node->count());
                const Group* const next = node->nextGroup();
                node = next != this ? next : nullptr;
            }
            return result;
        }
    };

}
