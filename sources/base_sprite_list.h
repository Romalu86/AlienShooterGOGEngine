#pragma once

#include <cstddef>
#include <cstdint>
#include <new>

#include "core/as_string.h"

namespace as1
{
    class SPRITE;
    class SPRITE_COLLECTOR;
    class RelationTable;
    namespace input { struct InputMessageState; }

    template <int Indexed>
    class BaseSpriteList;


    namespace core
    {
        struct ApplicationDrawPassBucket;

        template <class T>
        class List;


        template <>
        class List<SPRITE*>
        {
        public:
            __forceinline List() noexcept
                : m_count(0), m_capacity(0), m_items(nullptr) {}
            __forceinline ~List()
            {
                ::operator delete(m_items);
                m_items = nullptr;
                m_count = 0;
            }

            virtual void* deletingDestructor(unsigned char deletingDestructorFlags) noexcept;

            List(const List&) = delete;
            List& operator=(const List&) = delete;

            __forceinline SPRITE* BeginIterate(int* cursor) const noexcept
            {
                if (m_count == 0)
                    return nullptr;
                const int index = m_count - 1;
                *cursor = index;
                return m_items[index];
            }
            __forceinline SPRITE* NextIterate(int* cursor) const noexcept
            {
                if (*cursor > m_count)
                    *cursor = m_count;
                const int index = *cursor - 1;
                *cursor = index;
                return index >= 0 ? m_items[index] : nullptr;
            }

            __forceinline
            std::size_t count() const noexcept
            {
                return m_count > 0 ? static_cast<std::size_t>(m_count) : 0u;
            }
            __forceinline
            bool empty() const noexcept { return m_count == 0; }
            __forceinline
            SPRITE* at(std::size_t index) const noexcept
            {
                return m_items && index < static_cast<std::size_t>(m_count) ? m_items[index] : nullptr;
            }
            __forceinline
            int activeCount() const noexcept { return m_count; }
            __forceinline
            SPRITE* const* data() const noexcept { return m_items; }
            __forceinline
            bool contains(SPRITE* sprite) const noexcept
            {
                if (!m_items)
                    return false;
                for (int i = 0; i < m_count; ++i)
                    if (m_items[i] == sprite)
                        return true;
                return false;
            }

        protected:
            friend class ::as1::SPRITE_COLLECTOR;
            friend class ::as1::RelationTable;
            friend struct ApplicationDrawPassBucket;
            template <int> friend class ::as1::BaseSpriteList;
            int m_count = 0;
            int m_capacity = 0;
            SPRITE** m_items = nullptr;
        };
    }


    template <>
    class BaseSpriteList<0> : public core::List<SPRITE*>
    {
    public:
        __forceinline BaseSpriteList() noexcept = default;
        __forceinline ~BaseSpriteList() = default;

        void* deletingDestructor(unsigned char deletingDestructorFlags) noexcept override;

        BaseSpriteList(const BaseSpriteList&) = delete;
        BaseSpriteList& operator=(const BaseSpriteList&) = delete;


        void append(SPRITE* sprite);

        int removeSorted(SPRITE* sprite);


        int DeleteSpriteNumber(int index);

        void deleteAllSprites();

        void clear();
    };


    extern BaseSpriteList<0> g_spriteWorkList;

}
