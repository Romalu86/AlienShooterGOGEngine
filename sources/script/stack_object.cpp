#include "stack_object.h"
#include "../core/base_stream.h"
#include "../core/log.h"
#include "../core/file_logger.h"
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <new>
#include <cstddef>

namespace as1 { namespace script
{
    namespace
    {
        constexpr int DivideByZeroSentinel = 0x0FFFFFFF;

        bool isStringObject(const StackObject& value)
        {
            return (value.flags & STACK_OBJECT_STRING) != 0;
        }

    }

    StackObjectList::StackObjectList() noexcept = default;

    StackObjectList::~StackObjectList()
    {
        releaseStorage();
    }

    void StackObjectList::reserveExact(int requestedCapacity)
    {
        if (requestedCapacity <= capacity)
            return;
        StackObject* const oldRecords = table;
        StackObject* replacement = nullptr;
        try
        {
            replacement = new StackObject[requestedCapacity];
        }
        catch (...)
        {
        }
        table = replacement;
        if (!table)
            fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", requestedCapacity);
        if (oldRecords)
        {
            for (int i = 0; i < capacity; ++i)
                table[i] = oldRecords[i];
            delete[] oldRecords;
        }
        capacity = requestedCapacity;
    }

    void StackObjectList::appendFields(std::uint8_t flags, int value, STRING text)
    {
        if (count >= capacity)
        {
            const std::uint32_t expandedBits = static_cast<std::uint32_t>(capacity) * 2u + 4u;
            int expandedCapacity;
            std::memcpy(&expandedCapacity, &expandedBits, sizeof(expandedCapacity));
            reserveExact(expandedCapacity);
        }

        const int writeIndex = count;
        ++count;
        StackObject& destination = table[static_cast<std::size_t>(writeIndex)];
        destination.flags = flags;
        destination.intValue = value;
        destination.text.Assign(text);
    }

    void StackObjectList::Insert(StackObject item)
    {
        appendFields(item.flags, item.intValue, item.text);
    }

    STRING IntToStackString(int value)
    {
        char buffer[0x80];
        const char* const result = _itoa(value, buffer, 10);
        if (!result || result[0] == '\0')
            return STRING();
        return STRING(result);
    }

    StackObject::StackObject()
        : flags(0), intValue(0), text()
    {
    }

    StackObject::StackObject(const StackObject& other)
        : flags(other.flags), intValue(other.intValue), text(other.text)
    {
    }

    StackObject& StackObject::operator=(const StackObject& other)
    {
        flags = other.flags;
        intValue = other.intValue;
        text.Assign(other.text);
        return *this;
    }

    StackObject::StackObject(int value)
        : flags(STACK_OBJECT_INT), intValue(value), text()
    {
    }

    int StackObject::Int() const
    {
        if (flags & STACK_OBJECT_STRING)
            return text.Int();
        return intValue;
    }

    const STRING* StackObject::String()
    {
        if (flags & STACK_OBJECT_INT)
        {
            char buffer[0x80];
            text = STRING(_itoa(intValue, buffer, 10));
        }
        return &text;
    }

    StackObject* StackObject::initializeReferenceValue(int value) noexcept
    {
        flags = static_cast<std::uint8_t>(STACK_OBJECT_INT | STACK_OBJECT_REF);
        intValue = value;
        text.ResetSharedEmptyWithoutRelease();
        if (value == 0)
            flags = STACK_OBJECT_INT;
        return this;
    }

    void StackObject::Read(BaseStream* stream)
    {
        stream->read(&flags, 1);
        if ((flags & STACK_OBJECT_HAS_PAYLOAD) == 0)
            return;
        if (flags & STACK_OBJECT_STRING)
        {
            text.Read(stream);
            char* const bytes = const_cast<char*>(text.c_str());
            const std::size_t length = std::strlen(bytes);
            for (std::size_t i = 0; i < length; ++i)
                bytes[i] = static_cast<char>(static_cast<unsigned char>(bytes[i]) ^ 0x17u);
        }
        else
        {
            stream->read(&intValue, 4);
        }
    }

    void StackObject::Write(BaseStream* stream) const
    {
        stream->write(&flags, 1);
        if ((flags & STACK_OBJECT_HAS_PAYLOAD) == 0)
            return;
        if (flags & STACK_OBJECT_STRING)
        {
            char* const bytes = const_cast<char*>(text.c_str());
            const std::size_t length = std::strlen(bytes);
            for (std::size_t i = 0; i < length; ++i)
                bytes[i] = static_cast<char>(static_cast<unsigned char>(bytes[i]) ^ 0x17u);
            stream->write(bytes, static_cast<unsigned>(length + 1u));
            for (std::size_t i = 0; i < length; ++i)
                bytes[i] = static_cast<char>(static_cast<unsigned char>(bytes[i]) ^ 0x17u);
        }
        else
        {
            stream->write(&intValue, 4);
        }
    }

    void StackObject::BinarOperator(int operation, const StackObject& rhs)
    {
        const auto setNumericResult = [this](int value)
        {
            flags = STACK_OBJECT_INT;
            intValue = value;
        };

        if ((flags & STACK_OBJECT_STRING) != 0 &&
            (rhs.flags & STACK_OBJECT_STRING) != 0)
        {
            switch (static_cast<BinaryCommand>(operation))
            {
            case BinaryCommand::Add:
                text += rhs.text;
                flags = STACK_OBJECT_STRING;
                return;
            case BinaryCommand::Subtract:

                text.Replace(rhs.text.c_str(), "");
                flags = STACK_OBJECT_STRING;
                return;
            case BinaryCommand::Equal:
                setNumericResult(std::strcmp(text.c_str(), rhs.text.c_str()) == 0 ? 1 : 0);
                return;
            case BinaryCommand::NotEqual:
                setNumericResult(std::strcmp(text.c_str(), rhs.text.c_str()) != 0 ? 1 : 0);
                return;
            default:
                break;
            }
        }

        const int rhsValue = rhs.Int();
        if (flags & STACK_OBJECT_STRING)
            intValue = Int();

        switch (static_cast<BinaryCommand>(operation))
        {
        case BinaryCommand::Add:
            setNumericResult(intValue + rhsValue);
            break;
        case BinaryCommand::Subtract:
            setNumericResult(intValue - rhsValue);
            break;
        case BinaryCommand::Multiply:
            setNumericResult(intValue * rhsValue);
            break;
        case BinaryCommand::Divide:
            setNumericResult(rhsValue != 0 ? intValue / rhsValue : DivideByZeroSentinel);
            break;
        case BinaryCommand::Modulo:

            setNumericResult(intValue % rhsValue);
            break;
        case BinaryCommand::BitwiseOr:
            setNumericResult(intValue | rhsValue);
            break;
        case BinaryCommand::BitwiseXor:
            setNumericResult(intValue ^ rhsValue);
            break;
        case BinaryCommand::BitwiseAnd:
            setNumericResult(intValue & rhsValue);
            break;
        case BinaryCommand::ShiftLeft:
            setNumericResult(intValue << (rhsValue & 31));
            break;
        case BinaryCommand::ShiftRight:
            setNumericResult(intValue >> (rhsValue & 31));
            break;
        case BinaryCommand::LogicalAnd:
            setNumericResult((intValue != 0 && rhsValue != 0) ? 1 : 0);
            break;
        case BinaryCommand::LogicalOr:
            setNumericResult((intValue != 0 || rhsValue != 0) ? 1 : 0);
            break;
        case BinaryCommand::Less:
            setNumericResult(intValue < rhsValue ? 1 : 0);
            break;
        case BinaryCommand::LessEqual:
            setNumericResult(intValue <= rhsValue ? 1 : 0);
            break;
        case BinaryCommand::Greater:
            setNumericResult(intValue > rhsValue ? 1 : 0);
            break;
        case BinaryCommand::GreaterEqual:
            setNumericResult(intValue >= rhsValue ? 1 : 0);
            break;
        case BinaryCommand::Equal:
            if (isStringObject(rhs) && isStringObject(*this))
                setNumericResult(std::strcmp(text.c_str(), rhs.text.c_str()) == 0 ? 1 : 0);
            else
                setNumericResult(intValue == rhsValue ? 1 : 0);
            break;
        case BinaryCommand::NotEqual:
            if (isStringObject(rhs) && isStringObject(*this))
                setNumericResult(std::strcmp(text.c_str(), rhs.text.c_str()) != 0 ? 1 : 0);
            else
                setNumericResult(intValue != rhsValue ? 1 : 0);
            break;
        default:
            writeLogLine(g_fileLogger, "!!!ERROE!!!LOGIC::Unknown Binary command %i", static_cast<int>(operation));
            flags = STACK_OBJECT_INT;
            break;
        }
    }

}}
