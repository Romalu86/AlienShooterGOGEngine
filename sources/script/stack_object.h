#pragma once

#include "../core/as_string.h"
#include <cstdint>
#include <cstddef>

namespace as1
{
    class BaseStream;

    namespace script
    {
        enum class BinaryCommand : std::uint8_t
        {
            Divide        = 6,
            Modulo        = 7,
            Add           = 8,
            Subtract      = 9,
            BitwiseXor    = 10,
            BitwiseOr     = 11,
            BitwiseAnd    = 12,
            Equal         = 13,
            LogicalOr     = 14,
            Greater       = 15,
            Less          = 16,
            GreaterEqual  = 17,
            LessEqual     = 18,
            Multiply      = 19,
            NotEqual      = 20,
            LogicalAnd    = 21,
            ShiftRight    = 22,
            ShiftLeft     = 23,
        };

        constexpr std::uint8_t opcodeValue(BinaryCommand command) noexcept
        {
            return static_cast<std::uint8_t>(command);
        }

        enum StackObjectFlags : std::uint8_t
        {
            STACK_OBJECT_STRING      = 1u << 0,
            STACK_OBJECT_INT         = 1u << 1,
            STACK_OBJECT_ARRAY       = 1u << 2,
            STACK_OBJECT_HAS_PAYLOAD = 1u << 3,
            STACK_OBJECT_REF         = 1u << 4,
            STACK_OBJECT_DYNAMIC     = 1u << 5,
            STACK_OBJECT_CHAR_WRITE  = 1u << 6,
        };

        struct StackObject
        {
            std::uint8_t flags = 0;
            int intValue = 0;
            STRING text;

            StackObject();
            StackObject& operator=(const StackObject& other);
            StackObject(const StackObject& other);
            explicit StackObject(int value);
            __forceinline StackObject(int value, const STRING* context) : flags(STACK_OBJECT_INT), intValue(value), text() { (void)context; }
            __forceinline StackObject(const void* object, const STRING* context) : flags(static_cast<std::uint8_t>(STACK_OBJECT_INT | (object ? STACK_OBJECT_REF : 0))), intValue(static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object)))), text() { (void)context; }
            __forceinline StackObject(std::uint8_t initialFlags, int value, const STRING& initialText) : flags(0), intValue(0), text() { assignFields(initialFlags, value, initialText); }

            int Int() const;
            const STRING* String();
            __forceinline void assignInt(int value) { flags = STACK_OBJECT_INT; intValue = value; text = STRING(); }
            __forceinline void assignFields(std::uint8_t initialFlags, int value, const STRING& initialText) { flags = initialFlags; intValue = value; text.Assign(initialText); }
            StackObject* initializeReferenceValue(int value) noexcept;
            __forceinline void copyStorageFrom(const StackObject& other) { *this = other; }
            __forceinline void copyFrom(const StackObject& other) { *this = other; }
            void Read(BaseStream* stream);
            void Write(BaseStream* stream) const;
            void BinarOperator(int operation, const StackObject& rhs);
        };

        class StackObjectList
        {
        public:


            StackObjectList() noexcept;
            virtual ~StackObjectList();

            __forceinline void releaseStorage() noexcept { delete[] table; table = nullptr; count = 0; }
            void reserveExact(int requestedCapacity);
            void appendFields(std::uint8_t flags, int value, STRING text);
            void Insert(StackObject item);
            __forceinline void Push(const StackObject* item) { Insert(*item); }

            StackObject* data() noexcept { return table; }
            const StackObject* data() const noexcept { return table; }
            const StackObject& operator[](int index) const { return table[static_cast<std::size_t>(index)]; }
            StackObject& operator[](int index) { return table[static_cast<std::size_t>(index)]; }


            int count = 0;
            int capacity = 0;
            StackObject* table = nullptr;
        };

#if defined(_M_IX86) || defined(__i386__)
        static_assert(sizeof(StackObject) == 0x0C, "StackObject size 0x0C");
        static_assert(sizeof(StackObjectList) == 0x10, "StackObjectList size 0x10");
#endif

        STRING IntToStackString(int value);
    }
}
