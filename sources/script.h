#pragma once
#include "core/as_string.h"
#include "graphics/gamma.h"
#include "script/stack_object.h"
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace as1
{
    class VID;
    class SPRITE;
    struct WEAPON;
    namespace core { struct ApplicationDrawDispatcherState; }

    namespace script
    {
        enum class VmOpcode : std::uint8_t
        {
            PushInteger        = 1,
            PushString         = 2,
            Negate             = 3,
            BitwiseNot         = 4,
            LogicalNot         = 5,
            If                 = 24,
            StatementEnd       = 25,
            Pop                = 26,
            Jump               = 28,
            IfFalseChain       = 29,
            CallScriptFunction = 30,
            Return             = 31,
            PostIncrement      = 32,
            PostDecrement      = 33,
            PreIncrement       = 34,
            PreDecrement       = 35,
            ReadVariable       = 36,
            AddressOf          = 37,
            Assign             = 38,
            CompoundAssign     = 39,
            ArrayIndex         = 40,
            ConvertToString    = 41,
            ConvertToInteger   = 42,
            ConvertToObject    = 43,
            BranchIfTrue       = 44,
            BranchIfFalse      = 45,
        };

        constexpr std::uint8_t opcodeValue(VmOpcode command) noexcept
        {
            return static_cast<std::uint8_t>(command);
        }

        struct LogicFunctionRecord
        {
            STRING name;
            std::uint8_t flags;
            std::uint8_t statusFlags;
            std::uint8_t padding0;
            std::uint8_t padding1;
            STRING text;
            int codeOffsetOrStackIndex;
            int stackBase;
            int itemCount;

            LogicFunctionRecord();
            void copyFrom(const LogicFunctionRecord& other);
        };

        class LogicFunctionList
        {
        public:


            LogicFunctionList() noexcept;
            virtual ~LogicFunctionList();

            __forceinline void releaseStorage() noexcept { delete[] table; table = nullptr; count = 0; }
            void reserveExact(int requestedCapacity);
            void Insert(STRING name, std::uint8_t flags, STRING text,
                        int bytecodeStart, int stackBase, int argumentCount);
            LogicFunctionRecord* data() noexcept { return table; }
            const LogicFunctionRecord* data() const noexcept { return table; }

            int count = 0;
            int capacity = 0;
            LogicFunctionRecord* table = nullptr;
        };
    }

    struct ScriptDefinePairRecord
    {
        STRING name;
        STRING value;
    };

    class ScriptDefineList
    {
    public:


        ScriptDefineList() noexcept;
        virtual ~ScriptDefineList();

        __forceinline void releaseStorage() noexcept { delete[] table; table = nullptr; count = 0; }
        void Insert(STRING name, STRING value);
        ScriptDefinePairRecord* data() noexcept { return table; }
        const ScriptDefinePairRecord* data() const noexcept { return table; }

        int count = 0;
        int capacity = 0;
        ScriptDefinePairRecord* table = nullptr;
    };

    struct ScriptData
    {
        script::StackObjectList stack;
        script::LogicFunctionList functions;
        ScriptDefineList defines;
        STRING scriptFile;
        std::uint32_t bytecodeBufferToken = 0;
        std::int32_t bytecodeEnd = 0;
        std::uint32_t sourceCursor;
        std::uint32_t sourceEnd;
        std::uint32_t sourceBufferToken = 0;
        std::int32_t sourceLine = -1;
        std::int32_t conditionalDepth;
        std::int32_t fallbackFunction;
        std::int32_t parseMode;
    };


#if defined(_M_IX86) || defined(__i386__)
    static_assert(sizeof(script::LogicFunctionRecord) == 0x18, "LogicFunctionRecord size 0x18");
    static_assert(sizeof(script::LogicFunctionList) == 0x10, "LogicFunctionList size 0x10");
    static_assert(sizeof(ScriptDefinePairRecord) == 0x08, "ScriptDefinePairRecord size 0x08");
    static_assert(sizeof(ScriptDefineList) == 0x10, "ScriptDefineList size 0x10");
    static_assert(offsetof(ScriptData, stack) == 0x00, "SCRIPT stack-list offset +0x00");
    static_assert(offsetof(ScriptData, functions) == 0x10, "SCRIPT function-list offset +0x10");
    static_assert(offsetof(ScriptData, defines) == 0x20, "SCRIPT define-list offset +0x20");
    static_assert(offsetof(ScriptData, scriptFile) == 0x30, "SCRIPT file STRING offset +0x30");
    static_assert(sizeof(ScriptData) == 0x58, "SCRIPT data size 0x58");
#endif

    class SCRIPT
    {
    public:
        SCRIPT();
        ~SCRIPT();
        SCRIPT(const SCRIPT&) = delete;
        SCRIPT& operator=(const SCRIPT&) = delete;

        int compile(const STRING& scriptFile, const STRING& gameRoot);
        int Load(const STRING& scriptFile);
        void writeExecutionStackToStream(BaseStream* stream);
        void readExecutionStackFromStream(BaseStream* stream);
        bool isLoaded() const { return m_data.bytecodeEnd != 0; }

        static constexpr std::uint32_t InitialListCapacity = 0x80u;
        static constexpr std::uint32_t TemporaryBytecodeCapacity = 0x3E800u;
        static constexpr std::uint32_t SourceBufferPadding = 0x1000u;
        static constexpr std::uint32_t SourcePayloadOffset = 0x0FE2u;
        __forceinline void clearExecutionStack() { m_data.stack.releaseStorage(); m_data.stack.capacity = 0; }
        int executionStackCount() const;
        __forceinline int executionStackCapacity() const { return m_data.stack.capacity; }
        __forceinline script::StackObject* mutableExecutionStackStorageAt(int index) { return &executionStackStorage()[static_cast<std::size_t>(index)]; }
        __forceinline void appendExecutionStackObject(const script::StackObject& value) { m_data.stack.Push(&value); }

        int DeletePointerToObject(void* object);
        __forceinline int functionCount() const { return m_data.functions.count; }
        __forceinline int functionCapacity() const { return m_data.functions.capacity; }
        __forceinline const script::LogicFunctionRecord* functionRecordAt(int index) const noexcept { return (index < 0 || index >= m_data.functions.count || functionRecordStorage() == nullptr) ? nullptr : functionRecordStorage() + index; }
        __forceinline script::LogicFunctionRecord* mutableFunctionRecordAt(int index) noexcept { return (index < 0 || index >= m_data.functions.count || functionRecordStorage() == nullptr) ? nullptr : functionRecordStorage() + index; }
        __forceinline int getFunctionIndex(const STRING& name) const noexcept
        {
            const script::LogicFunctionRecord* const records = functionRecordStorage();
            if (!records)
                return -1;
            for (int i = m_data.functions.count - 1; i >= 0; --i)
            {
                if (std::strcmp(records[static_cast<std::size_t>(i)].name.c_str(), name.c_str()) == 0)
                    return i;
            }
            return -1;
        }
        __forceinline int defineCount() const { return m_data.defines.count; }
        __forceinline int defineCapacity() const { return m_data.defines.capacity; }
        __forceinline const STRING& scriptFile() const { return m_data.scriptFile; }
        __forceinline int bytecodeEnd() const { return m_data.bytecodeEnd; }
        __forceinline int sourceCursorOffset() const { return (m_data.sourceBufferToken == 0u || m_data.sourceCursor == 0u) ? 0 : static_cast<int>(m_data.sourceCursor - m_data.sourceBufferToken); }
        __forceinline int sourceEndOffset() const { return (m_data.sourceBufferToken == 0u || m_data.sourceEnd == 0u) ? 0 : static_cast<int>(m_data.sourceEnd - m_data.sourceBufferToken); }
        __forceinline int conditionalDepth() const { return m_data.conditionalDepth; }
        __forceinline int parseMode() const { return m_data.parseMode; }
        __forceinline std::uint8_t sourceByteAtCursor() const { return sourceStorage()[static_cast<std::size_t>(sourceCursorOffset())]; }
        __forceinline void setSourceCursorOffset(int offset) { m_data.sourceCursor = m_data.sourceBufferToken == 0u ? 0u : m_data.sourceBufferToken + static_cast<std::uint32_t>(offset); }
        void reportCompileError(int errorCode, const char* detailText, int detailValue);

        __forceinline void EmitByteIfNoError(std::uint8_t opcode) { bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd)] = opcode; ++m_data.bytecodeEnd; }

        int skipempty2();
        int skipempty();
        int GetLine(STRING& outLine);
        int GetName(STRING& outName);
        int Word(const char* token);
        int WordEnd(const char* token);
        int GetInt();
        int PutString(char* outText);
        int SetNoElement(int elementCount);
        void IntVar();
        void StringVar();

        int mnog();

        void SetOperation(int byteCodePos, int operation);

        void slag();

        void cmpslag();

        void logicslag();


        void vyrag();

        int vyrag_oper();
        void oper(std::int32_t* breakPatchList);
        int func();
        void resetScriptVmState();
        __forceinline void clearFunctionTable() { m_data.functions.releaseStorage(); m_data.functions.capacity = 0; }
        __forceinline void clearDefines()
        {
            ScriptDefinePairRecord* const records = m_data.defines.table;
            m_data.defines.capacity = 0;
            m_data.defines.count = 0;
            delete[] records;
            m_data.defines.table = nullptr;
        }
        __forceinline int findDefine(const STRING& name) const
        {
            const ScriptDefinePairRecord* const records = defineRecordStorage();
            for (int i = m_data.defines.count - 1; i >= 0; --i)
            {
                if (std::strcmp(records[static_cast<std::size_t>(i)].name.c_str(), name.c_str()) == 0)
                    return i;
            }
            return -1;
        }
        int popSpriteReferenceValue();
        void pushSpriteReference(SPRITE* sprite);
        void PushInt(int value);
        void PushStr(const STRING& value);
        as1::VID* popVidValue(const char* errorContext);
        void RunTimeError(int errorCode, const char* text, int value);
        int IsLastStackString() const;
        int topValueIsString() const { return IsLastStackString(); }


        STRING GetVariableStr(const STRING& expression);


        int callFunction(int functionIndex, const char* argumentTypes, int firstArgument, int secondArgument, int thirdArgument);

    private:
        friend class MAP;
        static __forceinline std::uint32_t pointerToken(const void* pointer) noexcept { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer) & 0xFFFFFFFFu); }
        __forceinline script::StackObject* executionStackStorage() noexcept { return m_data.stack.table; }
        __forceinline const script::StackObject* executionStackStorage() const noexcept { return m_data.stack.table; }
        script::LogicFunctionRecord* functionRecordStorage() noexcept;
        const script::LogicFunctionRecord* functionRecordStorage() const noexcept;
        __forceinline ScriptDefinePairRecord* defineRecordStorage() noexcept { return m_data.defines.table; }
        __forceinline const ScriptDefinePairRecord* defineRecordStorage() const noexcept { return m_data.defines.table; }
        __forceinline std::uint8_t* bytecodeStorage() noexcept { return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(m_data.bytecodeBufferToken)); }
        __forceinline const std::uint8_t* bytecodeStorage() const noexcept { return reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(m_data.bytecodeBufferToken)); }
        __forceinline std::uint8_t* sourceStorage() noexcept { return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(m_data.sourceBufferToken)); }
        __forceinline const std::uint8_t* sourceStorage() const noexcept { return reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(m_data.sourceBufferToken)); }
        __forceinline void setSourceEndOffset(int offset) noexcept { m_data.sourceEnd = m_data.sourceBufferToken == 0u ? 0u : m_data.sourceBufferToken + static_cast<std::uint32_t>(offset < 0 ? 0 : offset); }
        ScriptData m_data;

    };

#if defined(_M_IX86) || defined(__i386__)
#endif

}
