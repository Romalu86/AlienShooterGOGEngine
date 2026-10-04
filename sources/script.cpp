#include "script.h"
#include "core/file_stream.h"
#include "core/file_logger.h"
#include "core/log.h"
#include "map.h"
#include "vid/vid.h"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <io.h>
#include <new>

namespace as1
{
    int ScriptExecFunc(int opcode);

    namespace script
    {
        LogicFunctionRecord::LogicFunctionRecord()
            : name(), flags(0), text()
        {
        }

        void LogicFunctionRecord::copyFrom(const LogicFunctionRecord& other)
        {
            name = other.name;
            flags = other.flags;
            text = other.text;
            codeOffsetOrStackIndex = other.codeOffsetOrStackIndex;
            stackBase = other.stackBase;
            itemCount = other.itemCount;
        }


    }

    namespace
    {
        std::uint32_t scriptFileLength32(const FSTREAM& stream) noexcept
        {
            const std::FILE* file = stream.nativeFile();
            if (!file)
                return 0xFFFFFFFFu;
            const int fd = _fileno(const_cast<std::FILE*>(file));
            if (fd < 0)
                return 0xFFFFFFFFu;
            return static_cast<std::uint32_t>(_filelength(fd));
        }

        bool readScriptDword(const std::uint8_t* bytecode, int offset, int& value)
        {
            std::uint32_t encodedValueBits = 0;
            std::memcpy(&encodedValueBits, bytecode + static_cast<std::size_t>(offset), sizeof(encodedValueBits));
            value = static_cast<int>(encodedValueBits);
            return true;
        }

        int scriptStackValueToInteger(const script::StackObject& value)
        {
            return (value.flags & script::STACK_OBJECT_STRING)
                ? value.text.Int()
                : value.intValue;
        }


    }

    script::LogicFunctionList::LogicFunctionList() noexcept = default;

    script::LogicFunctionList::~LogicFunctionList()
    {
        releaseStorage();
    }

    void script::LogicFunctionList::reserveExact(int requestedCapacity)
    {
        if (requestedCapacity <= capacity)
            return;
        LogicFunctionRecord* const oldRecords = table;
        LogicFunctionRecord* replacement = nullptr;
        try
        {
            replacement = new LogicFunctionRecord[requestedCapacity];
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
                table[i].copyFrom(oldRecords[i]);
            delete[] oldRecords;
        }
        capacity = requestedCapacity;
    }

    void script::LogicFunctionList::Insert(STRING name, std::uint8_t flags, STRING text,
                                            int bytecodeStart, int stackBase, int argumentCount)
    {
        if (count >= capacity)
        {
            const std::uint32_t expandedBits = static_cast<std::uint32_t>(capacity) * 2u + 4u;
            int expandedCapacity;
            std::memcpy(&expandedCapacity, &expandedBits, sizeof(expandedCapacity));
            reserveExact(expandedCapacity);
        }
        table[count].name = name;
        LogicFunctionRecord& destination = table[count];
        ++count;
        destination.flags = flags;
        destination.text = text;
        destination.codeOffsetOrStackIndex = bytecodeStart;
        destination.stackBase = stackBase;
        destination.itemCount = argumentCount;
    }

    ScriptDefineList::ScriptDefineList() noexcept = default;

    SCRIPT::SCRIPT() = default;

    script::LogicFunctionRecord* SCRIPT::functionRecordStorage() noexcept
    {
        return m_data.functions.table;
    }

    const script::LogicFunctionRecord* SCRIPT::functionRecordStorage() const noexcept
    {
        return m_data.functions.table;
    }

    SCRIPT::~SCRIPT()
    {
        resetScriptVmState();
    }

    int SCRIPT::Load(const STRING& scriptFile)
    {
        std::uint32_t firstDword = static_cast<std::uint32_t>(m_data.stack.count);
        FSTREAM stream(std::string(scriptFile.c_str()), "rb");
        resetScriptVmState();
        (m_data.scriptFile).Assign(scriptFile);
        if (!stream.isOpen())
        {
            reportCompileError(7, "", 0);
            return 1;
        }

        stream.read_new(&firstDword, sizeof(firstDword));

        if ((firstDword & 0xFF000000u) != 0)
        {
            const int result = compile(m_data.scriptFile, STRING());
            stream.close();
            return result;
        }

        const int stackCount = static_cast<int>(firstDword);

        if (m_data.stack.capacity < static_cast<int>(InitialListCapacity))
            m_data.stack.reserveExact(static_cast<int>(InitialListCapacity));
        m_data.stack.count = stackCount;
        if (stackCount > m_data.stack.capacity)
            m_data.stack.reserveExact(stackCount);
        readExecutionStackFromStream(&stream);

        stream.read_new(&m_data.bytecodeEnd, sizeof(m_data.bytecodeEnd));
        void* const bytecodeOwner = ::operator new(
            static_cast<std::uint32_t>(m_data.bytecodeEnd), std::nothrow);
        m_data.bytecodeBufferToken = pointerToken(bytecodeOwner);
        if (!bytecodeOwner)
        {
            reportCompileError(2, "data2", 0);
            std::exit(1);
        }
        stream.read_new(bytecodeOwner, static_cast<std::uint32_t>(m_data.bytecodeEnd));

        for (int i = 0; i < m_data.functions.count; ++i)
        {
            const script::LogicFunctionRecord* const rec = functionRecordAt(i);
            if (rec && std::strcmp(rec->name.c_str(), "main") == 0)
                m_data.fallbackFunction = i;
        }

        stream.close();
        return 0;
    }


    STRING SCRIPT::GetVariableStr(const STRING& expression)
    {
        const int functionIndex = getFunctionIndex(expression.Before("["));
        if (functionIndex < 0)
        {
            writeLogLine(g_fileLogger, "!!!ERROR!!! SCRIPT Can't find variable '%s' in GetVariableString", expression.c_str());
            return STRING();
        }

        const int elementIndex = expression.After("[").Int();
        const script::LogicFunctionRecord* const records = functionRecordStorage();
        const script::LogicFunctionRecord& record = records[static_cast<std::size_t>(functionIndex)];


        const int stackIndex = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(record.codeOffsetOrStackIndex) +
            static_cast<std::uint32_t>(elementIndex));
        script::StackObject* value = mutableExecutionStackStorageAt(stackIndex);

        if ((value->flags & script::STACK_OBJECT_INT) != 0)
        {
            value->text = script::IntToStackString(value->intValue);
        }
        return STRING(value->text.c_str());
    }


    int SCRIPT::callFunction(int functionIndex, const char* argumentTypes, int firstArgument, int secondArgument, int thirdArgument)
    {


        if (m_data.bytecodeBufferToken == 0u)
            return 0;

        int resolvedFunction = functionIndex;
        if (resolvedFunction < 0)
            resolvedFunction = m_data.fallbackFunction;

        if (resolvedFunction < 0 || resolvedFunction >= m_data.functions.count)
        {
            writeLogLine(g_fileLogger, "!!!ERROR!!! SCRIPT Call unexisted function %i", resolvedFunction);
            return 0;
        }

        const script::LogicFunctionRecord& fn = functionRecordStorage()[static_cast<std::size_t>(resolvedFunction)];
        if (fn.flags != 3)
        {
            writeLogLine(g_fileLogger, "!!!ERROR!!!LOGIC: Call unexisted function %s()", fn.name.c_str());
            return 0;
        }

        const int savedStackCount = m_data.stack.count;
        const int bytecodeLimit = m_data.bytecodeEnd;
        int cursor = fn.codeOffsetOrStackIndex;
        int controlAnchor = cursor;
        int result = 0;
        int pointerSentinel = 0x7FFFFFFF;

        PushInt(savedStackCount);
        PushInt(bytecodeLimit);
        int frameBase = static_cast<int>(
            static_cast<std::uint32_t>(savedStackCount) + 2u);


        const char* const types = argumentTypes;
        const int argumentValues[3] = { firstArgument, secondArgument, thirdArgument };
        const int marshaledCount = fn.itemCount < 3 ? fn.itemCount : 3;
        for (int i = 0; i < marshaledCount; ++i)
        {
            script::StackObject* const arg = mutableExecutionStackStorageAt(fn.stackBase + i);
            const char type = types[i];
            const int value = argumentValues[i];
            if (type == 's' || type == 'p')
            {
                arg->assignFields(
                    static_cast<std::uint8_t>(
                        value != 0
                            ? (script::STACK_OBJECT_INT | script::STACK_OBJECT_REF)
                            : script::STACK_OBJECT_INT),
                    value,
                    STRING());
            }
            else
            {
                arg->assignInt(value);
            }
        }


        if (types[0] == 'p')
            pointerSentinel = firstArgument;
        else if (types[1] == 'p')
            pointerSentinel = secondArgument;
        else if (types[2] == 'p')
            pointerSentinel = thirdArgument;

        int arrayVmActive = 0;
        int arrayVmOffset = 0;

        while (cursor < bytecodeLimit)
        {
            if (m_data.stack.count < frameBase)
                writeLogLine(g_fileLogger, "!!!ERROR!!!LOGIC: '%s' stack error %i", "pop, but not push", cursor);

            const std::uint8_t opcode = bytecodeStorage()[static_cast<std::size_t>(cursor++)];
            const bool isBinaryCommand =
                opcode >= script::opcodeValue(script::BinaryCommand::Divide) &&
                opcode <= script::opcodeValue(script::BinaryCommand::ShiftLeft);
            const bool isVariableCommand =
                opcode >= script::opcodeValue(script::VmOpcode::PostIncrement) &&
                opcode <= script::opcodeValue(script::VmOpcode::CompoundAssign);
            if (isBinaryCommand || isVariableCommand)
            {
                if (isBinaryCommand)
                {
                    script::StackObject* rhs = mutableExecutionStackStorageAt(m_data.stack.count - 1);
                    script::StackObject* lhs = mutableExecutionStackStorageAt(m_data.stack.count - 2);
                    lhs->BinarOperator(opcode, *rhs);
                    --m_data.stack.count;
                    continue;
                }

                int operandIndex = 0;
                readScriptDword(bytecodeStorage(), cursor, operandIndex);

                script::StackObject* operandRecord = mutableExecutionStackStorageAt(operandIndex);
                int targetIndex = operandIndex;
                if ((operandRecord->flags & script::STACK_OBJECT_DYNAMIC) != 0 && arrayVmActive != 0)
                    targetIndex = operandRecord->intValue;
                targetIndex += arrayVmOffset;

                script::StackObject* target = mutableExecutionStackStorageAt(targetIndex);

                const script::VmOpcode variableCommand = static_cast<script::VmOpcode>(opcode);

                switch (variableCommand)
                {
                case script::VmOpcode::PostIncrement:
                    m_data.stack.appendFields(
                        target->flags, target->intValue, target->text);
                    target->flags = static_cast<std::uint8_t>(target->flags & ~script::STACK_OBJECT_REF);
                    if ((target->flags & (script::STACK_OBJECT_INT | script::STACK_OBJECT_DYNAMIC)) != 0)
                    {
                        target->intValue += 1;
                    }
                    else if ((target->flags & script::STACK_OBJECT_STRING) != 0)
                    {
                        target->text = script::IntToStackString(scriptStackValueToInteger(*target) + 1);
                        target->flags = script::STACK_OBJECT_STRING;
                    }
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                case script::VmOpcode::PostDecrement:
                    m_data.stack.appendFields(
                        target->flags, target->intValue, target->text);
                    target->flags = static_cast<std::uint8_t>(target->flags & ~script::STACK_OBJECT_REF);
                    if ((target->flags & (script::STACK_OBJECT_INT | script::STACK_OBJECT_DYNAMIC)) != 0)
                    {
                        target->intValue -= 1;
                    }
                    else if ((target->flags & script::STACK_OBJECT_STRING) != 0)
                    {
                        target->text = script::IntToStackString(scriptStackValueToInteger(*target) - 1);
                        target->flags = script::STACK_OBJECT_STRING;
                    }
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                case script::VmOpcode::PreIncrement:
                    target->flags = static_cast<std::uint8_t>(target->flags & ~script::STACK_OBJECT_REF);
                    if ((target->flags & (script::STACK_OBJECT_INT | script::STACK_OBJECT_DYNAMIC)) != 0)
                    {
                        target->intValue += 1;
                    }
                    else if ((target->flags & script::STACK_OBJECT_STRING) != 0)
                    {
                        target->text = script::IntToStackString(scriptStackValueToInteger(*target) + 1);
                        target->flags = script::STACK_OBJECT_STRING;
                    }
                    m_data.stack.appendFields(
                        target->flags, target->intValue, target->text);
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                case script::VmOpcode::PreDecrement:
                    target->flags = static_cast<std::uint8_t>(target->flags & ~script::STACK_OBJECT_REF);
                    if ((target->flags & (script::STACK_OBJECT_INT | script::STACK_OBJECT_DYNAMIC)) != 0)
                    {
                        target->intValue -= 1;
                    }
                    else if ((target->flags & script::STACK_OBJECT_STRING) != 0)
                    {
                        target->text = script::IntToStackString(scriptStackValueToInteger(*target) - 1);
                        target->flags = script::STACK_OBJECT_STRING;
                    }
                    m_data.stack.appendFields(
                        target->flags, target->intValue, target->text);
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                case script::VmOpcode::ReadVariable:
                    if (arrayVmActive == 0 && (target->flags & script::STACK_OBJECT_ARRAY) != 0)
                    {
                        PushInt(operandIndex);
                    }
                    else
                    {
                        m_data.stack.appendFields(
                            target->flags, target->intValue, target->text);
                    }
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                case script::VmOpcode::AddressOf:
                {
                    const std::uintptr_t addressValue =
                        (target->flags & script::STACK_OBJECT_STRING) != 0
                            ? reinterpret_cast<std::uintptr_t>(&target->text)
                            : reinterpret_cast<std::uintptr_t>(&target->intValue);
                    PushInt(static_cast<int>(addressValue & 0xFFFFFFFFu));
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                }
                case script::VmOpcode::Assign:
                {
                    script::StackObject* source = mutableExecutionStackStorageAt(m_data.stack.count - 1);

                    script::StackObject* baseRecord = mutableExecutionStackStorageAt(operandIndex);
                    if ((baseRecord->flags & script::STACK_OBJECT_STRING) != 0)
                    {
                        if (arrayVmActive != 0 && (baseRecord->flags & script::STACK_OBJECT_ARRAY) == 0)
                        {
                            std::string text = std::string(baseRecord->text.c_str());
                            const int numeric = scriptStackValueToInteger(*source);
                            text[static_cast<std::size_t>(arrayVmOffset)] = static_cast<char>(numeric & 0xFF);
                            baseRecord->text.Assign(text.c_str());
                        }
                        else
                        {
                            target->flags = static_cast<std::uint8_t>(
                                target->flags & ~script::STACK_OBJECT_CHAR_WRITE);
                            if ((source->flags & script::STACK_OBJECT_INT) != 0)
                                target->text = script::IntToStackString(source->intValue);
                            else
                                target->text = source->text;
                        }
                    }
                    else
                    {
                        const int numeric = scriptStackValueToInteger(*source);
                        target->flags = static_cast<std::uint8_t>(target->flags & ~static_cast<std::uint8_t>(script::STACK_OBJECT_REF | script::STACK_OBJECT_CHAR_WRITE));
                        target->intValue = numeric;
                        if ((source->flags & script::STACK_OBJECT_REF) != 0)
                            target->flags = static_cast<std::uint8_t>(target->flags | script::STACK_OBJECT_REF);
                    }
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                }
                case script::VmOpcode::CompoundAssign:
                {
                    --m_data.stack.count;
                    script::StackObject* rhs = mutableExecutionStackStorageAt(m_data.stack.count);
                    const std::uint8_t compoundOpcode = bytecodeStorage()[static_cast<std::size_t>(cursor + 4)];
                    target->BinarOperator(compoundOpcode, *rhs);
                    m_data.stack.appendFields(
                        target->flags, target->intValue, target->text);
                    cursor += 5;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                }
                default:
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                }
            }

            switch (static_cast<script::VmOpcode>(opcode))
            {
            case script::VmOpcode::PushInteger:
            {
                int value = 0;
                readScriptDword(bytecodeStorage(), cursor, value);
                PushInt(value);
                cursor += 4;
                break;
            }
            case script::VmOpcode::PushString:
            {
                const char* text = reinterpret_cast<const char*>(bytecodeStorage() + static_cast<std::size_t>(cursor));
                PushStr(STRING(text));
                cursor += static_cast<int>(std::strlen(text)) + 1;
                break;
            }
            case script::VmOpcode::Negate:
            case script::VmOpcode::BitwiseNot:
            case script::VmOpcode::LogicalNot:
            {
                script::StackObject* top = mutableExecutionStackStorageAt(m_data.stack.count - 1);
                const int value = scriptStackValueToInteger(*top);
                top->flags = script::STACK_OBJECT_INT;
                if (opcode == script::opcodeValue(script::VmOpcode::Negate))
                    top->intValue = -value;
                else if (opcode == script::opcodeValue(script::VmOpcode::BitwiseNot))
                    top->intValue = ~value;
                else
                    top->intValue = value == 0 ? 1 : 0;
                break;
            }
            case script::VmOpcode::If:
            {
                --m_data.stack.count;
                script::StackObject* cond = mutableExecutionStackStorageAt(m_data.stack.count);
                const int condValue = scriptStackValueToInteger(*cond);
                int payload = 0;
                readScriptDword(bytecodeStorage(), cursor, payload);
                cursor += condValue ? 4 : payload;
                if (m_data.stack.count - frameBase > 1)
                    writeLogLine(g_fileLogger, "!!!ERROR!!!LOGIC: '%s' stack error %i", "if", cursor);
                controlAnchor = cursor;
                m_data.stack.count = frameBase;
                break;
            }
            case script::VmOpcode::StatementEnd:
            {
                int sourceLine = 0;
                readScriptDword(bytecodeStorage(), cursor, sourceLine);
                cursor += 4;
                controlAnchor = cursor;
                if (m_data.stack.count - frameBase > 1)
                    writeLogLine(g_fileLogger, "!!!ERROR!!!LOGIC: '%s' stack error %i", ";", cursor);
                m_data.stack.count = frameBase;
                break;
            }
            case script::VmOpcode::Pop:

                --m_data.stack.count;
                break;
            case script::VmOpcode::Jump:
            {
                int payload = 0;
                readScriptDword(bytecodeStorage(), cursor, payload);
                cursor += payload;
                break;
            }
            case script::VmOpcode::IfFalseChain:
            {
                --m_data.stack.count;
                script::StackObject* cond = mutableExecutionStackStorageAt(m_data.stack.count);
                const int condValue = scriptStackValueToInteger(*cond);
                int payload = 0;
                readScriptDword(bytecodeStorage(), cursor, payload);

                if (condValue != 0)
                {
                    const int previousAnchor = controlAnchor;
                    bytecodeStorage()[static_cast<std::size_t>(previousAnchor)] = script::opcodeValue(script::VmOpcode::Jump);
                    const int patchedRelative = payload - previousAnchor + cursor - 1;
                    std::memcpy(bytecodeStorage() + static_cast<std::size_t>(previousAnchor + 1),
                                &patchedRelative, sizeof(patchedRelative));
                    cursor += 4;
                }
                else
                {
                    cursor += payload;
                }

                controlAnchor = cursor;
                if (m_data.stack.count - frameBase > 1)
                    writeLogLine(g_fileLogger, "!!!ERROR!!!LOGIC: '%s' stack error %i", "iff", cursor);
                m_data.stack.count = frameBase;
                break;
            }
            case script::VmOpcode::CallScriptFunction:
            {


                PushInt(frameBase);
                PushInt(cursor + 4);

                int calleeIndex = 0;
                readScriptDword(bytecodeStorage(), cursor, calleeIndex);
                frameBase = m_data.stack.count;

                const script::LogicFunctionRecord& callee =
                    functionRecordStorage()[static_cast<std::size_t>(calleeIndex)];
                if (callee.codeOffsetOrStackIndex < 0)
                {
                    writeLogLine(g_fileLogger, "!!!ERROR!!!LOGIC: Call unexisted function %s()", callee.name.c_str());
                    controlAnchor = cursor;
                    break;
                }

                cursor = callee.codeOffsetOrStackIndex;
                controlAnchor = cursor;
                break;
            }
            case script::VmOpcode::Return:
            {
                if (m_data.stack.count - frameBase > 1)
                    writeLogLine(g_fileLogger, "!!!ERROR!!!LOGIC: '%s' stack error %i", "return", cursor - 1);

                script::StackObject returnedValue;
                bool hasReturnedValue = false;
                if (m_data.stack.count > frameBase)
                {
                    script::StackObject* top = mutableExecutionStackStorageAt(m_data.stack.count - 1);
                    returnedValue.copyFrom(*top);
                    hasReturnedValue = true;
                    --m_data.stack.count;
                }

                m_data.stack.count = frameBase;

                --m_data.stack.count;
                script::StackObject* returnCursorObject = mutableExecutionStackStorageAt(m_data.stack.count);
                const int returnCursor = scriptStackValueToInteger(*returnCursorObject);

                --m_data.stack.count;
                script::StackObject* savedFrameObject = mutableExecutionStackStorageAt(m_data.stack.count);
                frameBase = scriptStackValueToInteger(*savedFrameObject);
                cursor = returnCursor;
                controlAnchor = cursor;

                if (hasReturnedValue)
                {
                    m_data.stack.appendFields(
                        returnedValue.flags, returnedValue.intValue, returnedValue.text);

                    if (returnCursor >= bytecodeLimit && result == 0 &&
                        (returnedValue.flags & script::STACK_OBJECT_STRING) == 0)
                        result = scriptStackValueToInteger(returnedValue);
                }
                break;
            }
            case script::VmOpcode::ArrayIndex:
            {
                --m_data.stack.count;
                script::StackObject* const indexObject = mutableExecutionStackStorageAt(m_data.stack.count);
                arrayVmOffset = scriptStackValueToInteger(*indexObject);
                arrayVmActive = 1;
                break;
            }
            case script::VmOpcode::ConvertToString:
            {
                script::StackObject* const top = mutableExecutionStackStorageAt(m_data.stack.count - 1);
                if ((top->flags & script::STACK_OBJECT_INT) != 0)
                    top->text = script::IntToStackString(top->intValue);
                top->flags = script::STACK_OBJECT_STRING;
                break;
            }
            case script::VmOpcode::ConvertToInteger:
            {
                script::StackObject* const top = mutableExecutionStackStorageAt(m_data.stack.count - 1);
                top->intValue = scriptStackValueToInteger(*top);
                top->flags = script::STACK_OBJECT_INT;
                break;
            }
            case script::VmOpcode::ConvertToObject:
            {
                script::StackObject* const top = mutableExecutionStackStorageAt(m_data.stack.count - 1);
                top->intValue = scriptStackValueToInteger(*top);
                top->flags = static_cast<std::uint8_t>(script::STACK_OBJECT_INT |
                    (top->intValue != 0 ? script::STACK_OBJECT_REF : 0));
                break;
            }
            case script::VmOpcode::BranchIfTrue:
            case script::VmOpcode::BranchIfFalse:
            {
                const script::StackObject* const top =
                    mutableExecutionStackStorageAt(m_data.stack.count - 1);
                const bool condition = scriptStackValueToInteger(*top) != 0;
                int relative = 0;
                readScriptDword(bytecodeStorage(), cursor, relative);
                const bool takeFallthrough =
                    (opcode == script::opcodeValue(script::VmOpcode::BranchIfTrue))
                        ? condition
                        : !condition;
                cursor += takeFallthrough ? 4 : relative;
                break;
            }
            default:
            {

                constexpr std::uint8_t kResultProbeOpcode = 84;
                if (opcode == kResultProbeOpcode)
                {
                    const script::StackObject* top = mutableExecutionStackStorageAt(m_data.stack.count - 1);
                    if (scriptStackValueToInteger(*top) == pointerSentinel)
                        result = 1;
                }
                ScriptExecFunc(static_cast<int>(opcode));
                break;
            }
            }
        }

        m_data.stack.count = savedStackCount;
        if (savedStackCount > m_data.stack.capacity)
        {
            auto* const stackList = &m_data.stack;
            stackList->reserveExact(savedStackCount);
        }
        return result;
    }


    void SCRIPT::PushInt(int value)
    {
        script::StackObject obj;
        obj.assignFields(static_cast<std::uint8_t>(script::STACK_OBJECT_INT), value, STRING());
        appendExecutionStackObject(obj);
    }


    void SCRIPT::PushStr(const STRING& value)
    {
        STRING ebxTemp;
        ::new (static_cast<void*>(&ebxTemp)) as1::STRING(value.c_str());

        m_data.stack.appendFields(
            static_cast<std::uint8_t>(script::STACK_OBJECT_STRING),
            0,
            ebxTemp);

        ebxTemp.ReleaseOwnedStorage();
    }


    void SCRIPT::RunTimeError(int errorCode, const char* text, int value)
    {


        logFileLoggerResourceError(g_fileLogger, "MAP", errorCode, text, value);
    }



}
