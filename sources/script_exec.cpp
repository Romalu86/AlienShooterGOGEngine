#include "script.h"
#include "script/native_function_codes.h"
#include "script/vid_data_codes.h"

#include "core/application.h"
#include "base_sprite_list.h"
#include "menu.h"
#include "core/configuration.h"
#include "core/crc32.h"
#include "core/file_stream.h"
#include "core/file_logger.h"
#include "core/log.h"
#include "core/profile_p.h"
#include "mouse.h"
#include "input.h"
#include "map.h"
#include "player_arcade.h"
#include "engine.h"
#include "graph.h"
#include "core/weak_controller.h"
#include "sprite.h"
#include "sprite_collector.h"
#include "sound/sound_engine.h"
#include "vid/vid.h"
#include "win/application_win.h"
#include "registration_export.h"
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <string>
#include <io.h>

namespace as1
{

    int ScriptExecFunc(int opcode);

    namespace
    {
        std::uint32_t fileLength32(const FSTREAM& stream) noexcept
        {
            const std::FILE* file = stream.nativeFile();
            if (!file)
                return 0xFFFFFFFFu;
            const int fd = _fileno(const_cast<std::FILE*>(file));
            if (fd < 0)
                return 0xFFFFFFFFu;
            return static_cast<std::uint32_t>(_filelength(fd));
        }

        const char Class[] = "";

        int nmonster = 0;
        int typeunit = 0;
        int g_scriptSpriteIteratorPass = 0;
        int n_sprite = 0;


        std::FILE* scriptNativeFileFromInt(int value)
        {
            return reinterpret_cast<std::FILE*>(static_cast<std::intptr_t>(value));
        }

        int scriptNativeIntFromFile(std::FILE* file)
        {
            return static_cast<int>(reinterpret_cast<std::intptr_t>(file));
        }

        const input::InputMessageState& scriptApplicationInputState() noexcept
        {

            const auto* const owner = static_cast<const std::uint8_t*>(core::ApplicationOwner());
            return *reinterpret_cast<const input::InputMessageState*>(
                owner + core::application_layout::InputState);
        }

        void scriptNativeDecodeGammaIndex(int value, std::uint32_t& diffuse, std::uint32_t& specular)
        {
            diffuse = 0;
            specular = 0;
            const std::uint32_t packed = static_cast<std::uint32_t>(value);
            for (int shift = 0; shift < 32; shift += 8)
            {
                const std::uint32_t byteValue = (packed >> shift) & 0xFFu;
                const std::uint32_t component = ((byteValue & 0x80u) != 0)
                    ? (((~byteValue) & 0x7Fu) << 1)
                    : ((byteValue & 0x7Fu) << 1);
                if ((byteValue & 0x80u) != 0)
                    specular |= (component & 0xFFu) << shift;
                else
                    diffuse |= (component & 0xFFu) << shift;
            }
        }

        unsigned char interpolateGammaByte(int fromComponent, int toComponent, int time)
        {
            int first = fromComponent;
            if (first >= 0x80)
                first -= 0xFE;
            int second = toComponent;
            if (second >= 0x80)
                second -= 0xFE;
            const std::uint32_t deltaBits =
                static_cast<std::uint32_t>(second - first) * static_cast<std::uint32_t>(time);
            std::int32_t delta = 0;
            std::memcpy(&delta, &deltaBits, sizeof(delta));
            const int step = delta / 255;
            return (step + first) & 0xFF;
        }

        int CountGamma(int fromGamma, int toGamma, int time)
        {
            const int lowByte = interpolateGammaByte(fromGamma & 0xFF, toGamma & 0xFF, time) & 0xFF;
            const int middleByte = interpolateGammaByte((fromGamma >> 8) & 0xFF, (toGamma >> 8) & 0xFF, time) & 0xFF;
            const int highByte = interpolateGammaByte((fromGamma >> 16) & 0xFF, (toGamma >> 16) & 0xFF, time) & 0xFF;
            return lowByte | (middleByte << 8) | (highByte << 16);
        }

    }

    int SCRIPT::compile(const STRING& scriptFile, const STRING& gameRoot)
    {
        (void)gameRoot;

        FSTREAM stream(std::string(scriptFile.c_str()), "rb");
        resetScriptVmState();
        (m_data.scriptFile).Assign(scriptFile);
        if (!stream.isOpen())
        {
            reportCompileError(7, "", 0);
            return 1;
        }

        const std::uint32_t fileLength = fileLength32(stream);
        try
        {
            m_data.bytecodeBufferToken = pointerToken(::operator new(TemporaryBytecodeCapacity));
        }
        catch (...)
        {
            reportCompileError(2, "data", 0);
            std::exit(1);
        }
        m_data.bytecodeEnd = 0;

        try
        {
            const std::uint32_t allocationSize = fileLength + static_cast<std::uint32_t>(SourceBufferPadding);
            m_data.sourceBufferToken = pointerToken(::operator new(static_cast<std::size_t>(allocationSize)));
        }
        catch (...)
        {
            reportCompileError(2, "ini", 0);
            std::exit(1);
        }
        setSourceCursorOffset(static_cast<int>(SourcePayloadOffset));
        const std::uint32_t sourceEndOffset = static_cast<std::uint32_t>(SourcePayloadOffset) + fileLength;
        setSourceEndOffset(static_cast<std::int32_t>(sourceEndOffset));
        if (fileLength != 0)
            stream.read(sourceStorage() + SourcePayloadOffset, static_cast<unsigned>(fileLength));

        if (executionStackCapacity() < static_cast<int>(InitialListCapacity))
            m_data.stack.reserveExact(static_cast<int>(InitialListCapacity));
        m_data.stack.count = 0;

        m_data.functions.reserveExact(static_cast<int>(InitialListCapacity));

        m_data.sourceLine = 0;
        m_data.conditionalDepth = 0;
        m_data.parseMode = 0;

        int status = func();
        while (status == 0)
            status = func();

        writeLogLine(
            g_fileLogger,
            "LoadScript::ByteCode=%i varNo=%i DefineNo=%i stackNo=%i",
            m_data.bytecodeEnd,
            functionCount(),
            defineCount(),
            executionStackCount());

        clearDefines();

        if (m_data.bytecodeEnd != 0)
        {
            if (m_data.bytecodeEnd > static_cast<int>(TemporaryBytecodeCapacity))
            {
                reportCompileError(2, "byte code size", m_data.bytecodeEnd);
            }

            try
            {
                const std::size_t finalSize = static_cast<std::size_t>(
                    static_cast<std::uint32_t>(m_data.bytecodeEnd));
                void* const finalBytecode = ::operator new(finalSize);
                if (finalSize != 0)
                    std::memcpy(finalBytecode, bytecodeStorage(), finalSize);
                ::operator delete(static_cast<void*>(bytecodeStorage()));
                m_data.bytecodeBufferToken = pointerToken(finalBytecode);
            }
            catch (...)
            {

                reportCompileError(2, "tmp", 0);
                std::exit(1);
            }
        }
        else
        {
            resetScriptVmState();
        }

        ::operator delete(static_cast<void*>(sourceStorage()));
        m_data.sourceBufferToken = 0u;
        stream.close();
        return 0;
    }

    void SCRIPT::writeExecutionStackToStream(BaseStream* stream)
    {
        for (int i = 0; i < m_data.stack.count; ++i)
            executionStackStorage()[static_cast<std::size_t>(i)].Write(stream);

        stream->write(&m_data.functions.count, 4);
        const script::LogicFunctionRecord* const records = functionRecordStorage();
        for (int i = 0; i < m_data.functions.count; ++i)
        {
            const script::LogicFunctionRecord& rec = records[static_cast<std::size_t>(i)];
            stream->write(rec.name.c_str(), static_cast<unsigned>(std::strlen(rec.name.c_str()) + 1u));
            stream->write(reinterpret_cast<const std::uint8_t*>(&rec) + 4u, 20u);
        }
        for (int i = 0; i < m_data.functions.count; ++i)
        {
            const char* const text = records[static_cast<std::size_t>(i)].text.c_str();
            stream->write(text, static_cast<unsigned>(std::strlen(text) + 1u));
        }
    }

    void SCRIPT::readExecutionStackFromStream(BaseStream* stream)
    {
        for (int i = 0; i < m_data.stack.count; ++i)
            executionStackStorage()[static_cast<std::size_t>(i)].Read(stream);

        if (stream)
        {
            stream->read(&m_data.functions.count, 4);
            m_data.functions.reserveExact(m_data.functions.count);
            for (int i = 0; i < m_data.functions.count; ++i)
            {
                functionRecordStorage()[static_cast<std::size_t>(i)].name.Read(stream);
                stream->read(reinterpret_cast<std::uint8_t*>(
                    &functionRecordStorage()[static_cast<std::size_t>(i)]) + 4u, 20u);
            }
        }

        for (int i = 0; i < m_data.functions.count; ++i)
        {
            functionRecordStorage()[static_cast<std::size_t>(i)].text.ResetSharedEmptyWithoutRelease();
            functionRecordStorage()[static_cast<std::size_t>(i)].text.Read(stream);
        }
    }

    int SCRIPT::executionStackCount() const
    {
        return m_data.stack.count;
    }

int SCRIPT::DeletePointerToObject(void* object)
    {
        const int target = static_cast<int>(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(object) & 0xFFFFFFFFu));
        int cleared = 0;
        for (int i = 0; i < m_data.stack.count; ++i)
        {
            script::StackObject& value = executionStackStorage()[static_cast<std::size_t>(i)];
            if ((value.flags & script::STACK_OBJECT_REF) != 0 && value.intValue == target)
            {
                value.intValue = 0;
                script::StackObject& current = executionStackStorage()[static_cast<std::size_t>(i)];
                current.flags = static_cast<std::uint8_t>(current.flags & ~script::STACK_OBJECT_REF);
                ++cleared;
            }
        }
        return cleared;
    }

    ScriptDefineList::~ScriptDefineList()
    {
        releaseStorage();
    }

    void ScriptDefineList::Insert(STRING name, STRING value)
    {
        if (count >= capacity)
        {
            const std::uint32_t expandedBits = static_cast<std::uint32_t>(capacity) * 2u + 4u;
            int expandedCapacity;
            std::memcpy(&expandedCapacity, &expandedBits, sizeof(expandedCapacity));
            if (expandedCapacity > capacity)
            {
                ScriptDefinePairRecord* const oldRecords = table;
                ScriptDefinePairRecord* replacement = nullptr;
                try
                {
                    replacement = new ScriptDefinePairRecord[expandedCapacity];
                }
                catch (...)
                {
                }
                table = replacement;
                if (!table)
                    fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", expandedCapacity);
                if (oldRecords)
                {
                    for (int i = 0; i < capacity; ++i)
                    {
                        ScriptDefinePairRecord& destination = table[i];
                        destination.name = oldRecords[i].name;
                        destination.value = oldRecords[i].value;
                    }
                    delete[] oldRecords;
                }
                capacity = expandedCapacity;
            }
        }
        table[count].name = name;
        STRING& destination = table[count].value;
        ++count;
        destination = value;
    }

    void SCRIPT::resetScriptVmState()
    {
        if (bytecodeStorage())
            ::operator delete(static_cast<void*>(bytecodeStorage()));
        m_data.bytecodeBufferToken = 0u;
        if (sourceStorage())
            ::operator delete(static_cast<void*>(sourceStorage()));
        m_data.sourceBufferToken = 0u;

        {
            script::StackObject* const records = m_data.stack.table;
            m_data.stack.capacity = 0;
            m_data.stack.count = 0;
            delete[] records;
            m_data.stack.table = nullptr;
        }

        {
            script::LogicFunctionRecord* const records = m_data.functions.table;
            m_data.functions.capacity = 0;
            m_data.functions.count = 0;
            delete[] records;
            m_data.functions.table = nullptr;
        }

        {
            ScriptDefinePairRecord* const records = m_data.defines.table;
            m_data.defines.capacity = 0;
            m_data.defines.count = 0;
            delete[] records;
            m_data.defines.table = nullptr;
        }

        m_data.bytecodeEnd = 0;
        m_data.conditionalDepth = 0;
        m_data.sourceCursor = 0;
        m_data.parseMode = 0;
        m_data.fallbackFunction = -1;

    }

    void SCRIPT::reportCompileError(int errorCode, const char* detailText, int detailValue)
    {
        logFileLoggerResourceError(g_fileLogger,
            "LOGIC '%s' line %i",
            errorCode,
            detailText,
            detailValue,
            m_data.scriptFile.c_str(),
            m_data.sourceLine + 1);

        if (m_data.sourceCursor == 0)
            return;

        char window[61];
        const int start = sourceCursorOffset() - 30;
        for (int i = 0; i < 60; ++i)
        {
            unsigned char c = sourceStorage()[static_cast<std::size_t>(start + i)];
            if (c == '\n' || c == '\r' || c == '\t')
                c = '?';
            window[i] = static_cast<char>(c);
        }
        window[60] = '\0';
        logFileLoggerResourceError(g_fileLogger, "LOGIC", 10, window, 0);

        for (int i = 0; i < 60; ++i)
            window[i] = (i == 30) ? '^' : ' ';
        window[60] = '\0';
        logFileLoggerResourceError(g_fileLogger, "LOGIC", 10, window, 0);
    }

    namespace
    {
        int ctypeArgument(unsigned char c) noexcept
        {
            return static_cast<int>(static_cast<signed char>(c));
        }

        bool isIdentifierStart(unsigned char c)
        {
            return std::isalpha(ctypeArgument(c)) != 0 || c == '_';
        }

        bool isIdentifierChar(unsigned char c)
        {
            return std::isalnum(ctypeArgument(c)) != 0 || c == '_';
        }

        bool isScriptWhitespace(unsigned char c)
        {
            return std::isspace(ctypeArgument(c)) != 0;
        }
    }

    int SCRIPT::skipempty2()
    {
        int skipDepth = 0;
        int commentState = 0;

        while (sourceCursorOffset() < sourceEndOffset())
        {
            if (commentState != 0)
            {
                const unsigned char c = sourceByteAtCursor();
                if (commentState == 1)
                {
                    if (c == '\n')
                        commentState = 0;
                }
                else if (commentState == 2)
                {
                    if (c == '/' &&
                        sourceStorage()[static_cast<std::size_t>(sourceCursorOffset() - 1)] == '*')
                        commentState = 0;
                }
                setSourceCursorOffset(sourceCursorOffset() + 1);
                if (c == '\n')
                    ++m_data.sourceLine;
                continue;
            }

            unsigned char c = sourceByteAtCursor();

            if (isIdentifierStart(c) && m_data.parseMode == 0)
            {
                const int tokenStart = sourceCursorOffset();
                int tokenLength = 0;
                while (isIdentifierChar(sourceStorage()[static_cast<std::size_t>(tokenStart + tokenLength)]))
                {
                    if (tokenLength >= 0x0FFF)
                    {
                        reportCompileError(10, "Very long name", 0);
                        std::exit(1);
                    }
                    ++tokenLength;
                }
                int found;
                {
                    const STRING tokenName(reinterpret_cast<const char*>(sourceStorage()) + tokenStart,
                                           tokenLength);
                    found = findDefine(tokenName);
                }
                if (found >= 0)
                {
                    const int valueLength = static_cast<int>(
                        std::strlen(m_data.defines.table[found].value.c_str()));
                    setSourceCursorOffset(tokenStart + tokenLength - valueLength);
                    std::memcpy(sourceStorage() + sourceCursorOffset(),
                                m_data.defines.table[found].value.c_str(),
                                static_cast<std::size_t>(valueLength));
                }
                c = sourceByteAtCursor();
            }

            if (c == '#')
            {
                const char* cur = reinterpret_cast<const char*>(sourceStorage()) + sourceCursorOffset();

                if (std::strncmp(cur, "#ifdef", 6) == 0)
                {
                    setSourceCursorOffset(sourceCursorOffset() + 6);
                    ++m_data.conditionalDepth;
                    if (skipDepth == 0)
                    {
                        STRING name;
                        m_data.parseMode = 1;
                        GetName(name);
                        m_data.parseMode = 0;
                        if (getFunctionIndex(name) < 0 && findDefine(name) < 0)
                            skipDepth = m_data.conditionalDepth;
                    }
                    continue;
                }

                if (std::strncmp(cur, "#ifndef", 7) == 0)
                {
                    setSourceCursorOffset(sourceCursorOffset() + 7);
                    ++m_data.conditionalDepth;
                    if (skipDepth == 0)
                    {
                        STRING name;
                        m_data.parseMode = 1;
                        GetName(name);
                        m_data.parseMode = 0;
                        if (getFunctionIndex(name) >= 0 || findDefine(name) >= 0)
                            skipDepth = m_data.conditionalDepth;
                    }
                    continue;
                }

                if (std::strncmp(cur, "#endif", 6) == 0)
                {
                    setSourceCursorOffset(sourceCursorOffset() + 6);
                    if (skipDepth == m_data.conditionalDepth)
                        skipDepth = 0;
                    --m_data.conditionalDepth;
                    if (m_data.conditionalDepth < 0)
                        reportCompileError(10, "#endif without #ifdef", 0);
                    continue;
                }

                if (std::strncmp(cur, "#else", 5) == 0)
                {
                    setSourceCursorOffset(sourceCursorOffset() + 5);
                    if (skipDepth == 0)
                    {
                        if (m_data.conditionalDepth > 0)
                            skipDepth = m_data.conditionalDepth;
                    }
                    else if (skipDepth == m_data.conditionalDepth)
                        skipDepth = 0;

                    if (m_data.conditionalDepth <= 0)
                        reportCompileError(10, "#else without #ifdef", 0);
                    continue;
                }
            }

            if (c == '/')
            {
                const std::size_t nextOff = static_cast<std::size_t>(sourceCursorOffset() + 1);
                const unsigned char next = sourceStorage()[nextOff];
                if (next == '/')
                {
                    commentState = 1;
                    setSourceCursorOffset(sourceCursorOffset() + 1);
                    continue;
                }
                if (next == '*')
                {
                    commentState = 2;
                    setSourceCursorOffset(sourceCursorOffset() + 1);
                    continue;
                }
            }

            if (c == '?')
            {
                reportCompileError(10, "?: not supported in this version", 0);
                std::exit(1);
            }

            if (skipDepth == 0 && !isScriptWhitespace(c) && c != 0)
                return 0;

            setSourceCursorOffset(sourceCursorOffset() + 1);
            if (c == '\n')
                ++m_data.sourceLine;
        }

        if (m_data.conditionalDepth > 0)
            reportCompileError(10, "#ifdef without #endif", m_data.conditionalDepth);
        return 1;
    }

    int SCRIPT::skipempty()
    {
        if (skipempty2())
        {
            reportCompileError(10, "End of file", 0);
            std::exit(1);
        }
        return 0;
    }

    int SCRIPT::GetLine(STRING& outLine)
    {

        if (skipempty())
            return 1;

        char line[0x1000];
        int length = 0;
        for (;;)
        {
            const unsigned char c = sourceByteAtCursor();
            if (c == '\n' || c == '\r')
                break;
            if (length >= 0x0FFF)
            {
                reportCompileError(10, "Very long line", 0);
                std::exit(1);
            }
            line[length++] = static_cast<char>(c);
            setSourceCursorOffset(sourceCursorOffset() + 1);
        }
        line[length] = '\0';

        if (length == 0)
        {
            reportCompileError(10, "empty line", 0);
            std::exit(1);
        }

        if (char* const comment = std::strstr(line, "//"))
            *comment = '\0';

        int trimmed = static_cast<int>(std::strlen(line));
        while (trimmed > 0)
        {
            const unsigned char c = static_cast<unsigned char>(line[trimmed - 1]);
            if (c != ' ' && c != '\n' && c != '\r' && c != '\t')
                break;
            --trimmed;
        }
        line[trimmed] = '\0';

        outLine = STRING(line);
        return skipempty();
    }

    int SCRIPT::GetName(STRING& outName)
    {

        if (skipempty())
            return 0;

        char name[0x1000];
        int length = 0;
        for (;;)
        {
            const unsigned char c = sourceByteAtCursor();
            if (!isIdentifierChar(c))
                break;
            if (length >= 0x0FFF)
            {
                reportCompileError(10, "Very long name", 0);
                std::exit(1);
            }
            name[length++] = static_cast<char>(c);
            setSourceCursorOffset(sourceCursorOffset() + 1);
        }
        name[length] = '\0';
        outName = STRING(name);
        if (length == 0)
        {
            reportCompileError(4, "name", 0);
            std::exit(1);
        }
        skipempty();
        return length;
    }

    int SCRIPT::Word(const char* token)
    {
        const std::size_t len = std::strlen(token);
        skipempty();
        const char* cur = reinterpret_cast<const char*>(sourceStorage() + sourceCursorOffset());
        if (std::strncmp(cur, token, len) != 0)
            return 0;
        const unsigned char first = static_cast<unsigned char>(token[0]);
        const unsigned char after = static_cast<unsigned char>(cur[len]);
        if ((std::isalpha(ctypeArgument(first)) != 0 || first == '#') &&
            isIdentifierChar(after))
            return 0;

        setSourceCursorOffset(sourceCursorOffset() + static_cast<int>(len));
        skipempty2();
        return 1;
    }

    int SCRIPT::WordEnd(const char* token)
    {
        if (Word(token))
            return 1;
        reportCompileError(13, token, 0);
        std::exit(1);
    }

    int SCRIPT::GetInt()
    {
        skipempty();
        const int bytecodeStart = m_data.bytecodeEnd;
        vyrag();

        if (bytecodeStorage()[static_cast<std::size_t>(bytecodeStart)] == 1 &&
            m_data.bytecodeEnd - bytecodeStart == 5)
        {
            int value = 0;
            std::memcpy(&value,
                bytecodeStorage() + static_cast<std::size_t>(bytecodeStart + 1),
                sizeof(value));
            m_data.bytecodeEnd -= 5;
            return value;
        }

        reportCompileError(4, "constant int value", 0);
        std::exit(1);
    }

    int SCRIPT::PutString(char* outText)
    {
        char* dst = outText;
        if (sourceByteAtCursor() != '"')
            return 0;

        setSourceCursorOffset(sourceCursorOffset() + 1);
        if (sourceByteAtCursor() != '"')
        {
            while (true)
            {
                if (sourceCursorOffset() >= sourceEndOffset())
                    break;

                unsigned char c = sourceByteAtCursor();
                if (c == '\\')
                {
                    const int nextOffset = sourceCursorOffset() + 1;
                    const unsigned char next = sourceStorage()[static_cast<std::size_t>(nextOffset)];

                    if (next == '\r')
                    {
                        const int afterCrOffset = sourceCursorOffset() + 2;
                        const unsigned char afterCr = sourceStorage()[static_cast<std::size_t>(afterCrOffset)];
                        if (afterCr == '\n')
                        {
                            setSourceCursorOffset(afterCrOffset);
                            ++m_data.sourceLine;
                        }
                        else
                        {
                            setSourceCursorOffset(nextOffset);
                            *dst++ = static_cast<char>(next);
                        }
                    }
                    else if (next == '\n')
                    {
                        setSourceCursorOffset(nextOffset);
                        ++m_data.sourceLine;
                    }
                    else if (next == 'n')
                    {
                        *dst++ = '\n';
                        setSourceCursorOffset(sourceCursorOffset() + 1);
                    }
                    else if (next == 'r')
                    {
                        *dst++ = '\r';
                        setSourceCursorOffset(sourceCursorOffset() + 1);
                    }
                    else if (next == 't')
                    {
                        *dst++ = '\t';
                        setSourceCursorOffset(sourceCursorOffset() + 1);
                    }
                    else
                    {
                        const int secondOffset = sourceCursorOffset() + 2;
                        const int thirdOffset = sourceCursorOffset() + 3;
                        const unsigned char second = sourceStorage()[static_cast<std::size_t>(secondOffset)];
                        const unsigned char third = sourceStorage()[static_cast<std::size_t>(thirdOffset)];
                        if (std::isdigit(ctypeArgument(next)) &&
                            std::isdigit(ctypeArgument(second)) &&
                            std::isdigit(ctypeArgument(third)))
                        {
                            *dst++ = static_cast<char>(
                                ((next - '0') << 6) +
                                ((second - '0') << 3) +
                                (third - '0'));
                            setSourceCursorOffset(thirdOffset);
                        }
                        else
                        {
                            setSourceCursorOffset(nextOffset);
                            *dst++ = static_cast<char>(next);
                        }
                    }
                }
                else
                {
                    *dst++ = static_cast<char>(c);
                }

                setSourceCursorOffset(sourceCursorOffset() + 1);
                if (sourceByteAtCursor() == '"')
                    break;
            }
        }

        const int quoteOffset = sourceCursorOffset();
        setSourceCursorOffset(sourceCursorOffset() + 1);
        if (quoteOffset >= sourceEndOffset())
        {
            reportCompileError(10, "End of file", 0);
            std::exit(1);
        }

        *dst++ = '\0';
        return static_cast<int>(dst - outText);
    }

    int SCRIPT::SetNoElement(int elementCount)
    {

        functionRecordStorage()[static_cast<std::size_t>(m_data.functions.count - 1)].itemCount = elementCount;
        return m_data.functions.count * 3;
    }

    void SCRIPT::IntVar()
    {
        int declaredCount = 1;
        int objectFlags = 0;
        STRING name;

        skipempty();
        if (sourceByteAtCursor() == '*')
        {
            setSourceCursorOffset(sourceCursorOffset() + 1);
            objectFlags = script::STACK_OBJECT_DYNAMIC;
        }

        GetName(name);
        if (getFunctionIndex(name) >= 0)
        {
            char buffer[512];
            std::snprintf(buffer, sizeof(buffer), "int redefinition '%s'", name.c_str());
            reportCompileError(10, buffer, 0);
            std::exit(1);
        }

        m_data.functions.Insert(name, 1, STRING(), executionStackCount(), 0, 0);

        if (Word("["))
        {
            objectFlags |= script::STACK_OBJECT_ARRAY;
            if (sourceByteAtCursor() == ']')
            {
                setSourceCursorOffset(sourceCursorOffset() + 1);
                if (!Word("="))
                {
                    reportCompileError(10, "for [] need initialisation", 0);
                    std::exit(1);
                }

                WordEnd("{");
                int initializerCount = 0;
                for (;;)
                {
                    script::StackObject value;
                    value.assignFields(script::STACK_OBJECT_INT, 0, STRING());
                    appendExecutionStackObject(value);

                    script::StackObject* const target =
                        mutableExecutionStackStorageAt(executionStackCount() - 1);
                    target->flags = static_cast<std::uint8_t>(target->flags | objectFlags);
                    target->intValue = GetInt();
                    target->flags = static_cast<std::uint8_t>(
                        target->flags | script::STACK_OBJECT_HAS_PAYLOAD);
                    ++initializerCount;

                    if (!Word(","))
                        break;
                }
                WordEnd("}");
                SetNoElement(initializerCount);
                return;
            }

            declaredCount = GetInt();
            WordEnd("]");
        }

        for (int i = 0; i < declaredCount; ++i)
        {
            script::StackObject value;
            value.assignFields(script::STACK_OBJECT_INT, 0, STRING());
            appendExecutionStackObject(value);
            script::StackObject* const target =
                mutableExecutionStackStorageAt(executionStackCount() - 1);
            target->flags = static_cast<std::uint8_t>(
                target->flags | objectFlags | script::STACK_OBJECT_CHAR_WRITE);
        }

        if (Word("="))
        {
            if ((objectFlags & script::STACK_OBJECT_ARRAY) != 0)
            {
                WordEnd("{");
                int initialized = 0;
                if (declaredCount <= 0)
                {
                    reportCompileError(10, "too many initializers", 0);
                    std::exit(1);
                }
                while (initialized < declaredCount)
                {
                    script::StackObject* const target =
                        mutableExecutionStackStorageAt(executionStackCount() - declaredCount + initialized);
                    target->flags = static_cast<std::uint8_t>(
                        target->flags & ~script::STACK_OBJECT_CHAR_WRITE);
                    target->intValue = GetInt();
                    target->flags = static_cast<std::uint8_t>(
                        target->flags | script::STACK_OBJECT_HAS_PAYLOAD);
                    ++initialized;

                    if (!Word(","))
                    {
                        WordEnd("}");
                        break;
                    }
                    if (initialized >= declaredCount)
                    {
                        reportCompileError(10, "too many initializers", 0);
                        std::exit(1);
                    }
                }
            }
            else
            {
                script::StackObject* const target =
                    mutableExecutionStackStorageAt(executionStackCount() - 1);
                target->intValue = GetInt();
                target->flags = static_cast<std::uint8_t>(
                    (target->flags | script::STACK_OBJECT_HAS_PAYLOAD) &
                    ~script::STACK_OBJECT_CHAR_WRITE);
            }
        }

        SetNoElement(declaredCount);
    }

    void SCRIPT::StringVar()
    {
        int declaredCount = 1;
        int objectFlags = 0;
        STRING name;

        GetName(name);
        if (getFunctionIndex(name) >= 0)
        {
            char buffer[512];
            std::snprintf(buffer, sizeof(buffer), "string redifinition '%s'", name.c_str());
            reportCompileError(10, buffer, 0);
            std::exit(1);
        }

        if (Word("["))
        {
            objectFlags = script::STACK_OBJECT_ARRAY;
            declaredCount = GetInt();
            WordEnd("]");
        }

        m_data.functions.Insert(name, 1, STRING(), executionStackCount(), 0, 0);

        for (int i = 0; i < declaredCount; ++i)
        {
            script::StackObject value;
            value.assignFields(script::STACK_OBJECT_STRING, 0, STRING());
            appendExecutionStackObject(value);
            script::StackObject* const target =
                mutableExecutionStackStorageAt(executionStackCount() - 1);
            target->flags = static_cast<std::uint8_t>(target->flags | objectFlags);
        }

        if (Word("="))
        {
            char text[0x1000];
            PutString(text);

            script::StackObject* const target =
                mutableExecutionStackStorageAt(executionStackCount() - 1);
            target->text.Assign(text);
            target->flags = static_cast<std::uint8_t>(
                (target->flags | script::STACK_OBJECT_HAS_PAYLOAD) &
                ~script::STACK_OBJECT_CHAR_WRITE);
        }

        SetNoElement(declaredCount);
    }

    int SCRIPT::mnog()
    {
        std::uint8_t unaryOpcode = 0;
        std::uint8_t prefixOpcode = script::opcodeValue(script::VmOpcode::ReadVariable);
        std::uint8_t castOpcode = 0;

        if (Word("(int)"))
            castOpcode = script::opcodeValue(script::VmOpcode::ConvertToInteger);
        else if (Word("(string)"))
            castOpcode = script::opcodeValue(script::VmOpcode::ConvertToString);
        else if (Word("(sprite)"))
            castOpcode = script::opcodeValue(script::VmOpcode::ConvertToObject);

        const int cursorBeforeUnary = sourceCursorOffset();
        const bool nextIsMinus =
            sourceStorage()[static_cast<std::size_t>(cursorBeforeUnary + 1)] == '-';
        const bool nextIsPlus =
            sourceStorage()[static_cast<std::size_t>(cursorBeforeUnary + 1)] == '+';
        if (!nextIsMinus && Word("-"))
            unaryOpcode = script::opcodeValue(script::VmOpcode::Negate);
        else if (!nextIsPlus && Word("+"))
            unaryOpcode = 0;
        else if (Word("~"))
            unaryOpcode = script::opcodeValue(script::VmOpcode::BitwiseNot);
        else if (Word("!"))
            unaryOpcode = script::opcodeValue(script::VmOpcode::LogicalNot);

        if (Word("--"))
            prefixOpcode = script::opcodeValue(script::VmOpcode::PreDecrement);
        else if (Word("++"))
            prefixOpcode = script::opcodeValue(script::VmOpcode::PreIncrement);
        else if (Word("&"))
            prefixOpcode = script::opcodeValue(script::VmOpcode::AddressOf);

        auto canWrite = [&](int byteCount) -> bool
        {
            (void)byteCount;
            return true;
        };
        auto emitByte = [&](std::uint8_t value) -> bool
        {
            EmitByteIfNoError(value);
            return true;
        };
        auto emitIntObject = [&](int value) -> bool
        {
            const std::size_t off = static_cast<std::size_t>(m_data.bytecodeEnd);
            bytecodeStorage()[off] = script::opcodeValue(script::VmOpcode::PushInteger);
            std::memcpy(bytecodeStorage() + off + 1, &value, sizeof(value));
            m_data.bytecodeEnd += 5;
            return true;
        };
        auto emitIntPayload = [&](int value) -> bool
        {
            const std::size_t off = static_cast<std::size_t>(m_data.bytecodeEnd);
            std::memcpy(bytecodeStorage() + off, &value, sizeof(value));
            m_data.bytecodeEnd += 4;
            return true;
        };
        auto emitByteAndIntPayload = [&](std::uint8_t opcode, int value) -> bool
        {
            const std::size_t offset = static_cast<std::size_t>(m_data.bytecodeEnd);
            bytecodeStorage()[offset] = opcode;
            ++m_data.bytecodeEnd;
            std::memcpy(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd),
                        &value, sizeof(value));
            m_data.bytecodeEnd += 4;
            return true;
        };
        auto emitTrailingUnary = [&]() -> bool
        {

            if (unaryOpcode != 0 && !emitByte(static_cast<std::uint8_t>(unaryOpcode)))
                return false;
            if (castOpcode != 0 && !emitByte(static_cast<std::uint8_t>(castOpcode)))
                return false;
            return true;
        };

        const unsigned char current = sourceByteAtCursor();
        if (std::isdigit(current))
        {
            STRING token;
            GetName(token);
            int value = 0;
            std::sscanf(token.c_str(), "%i", &value);
            if (unaryOpcode == script::opcodeValue(script::VmOpcode::Negate))
            {
                value = -value;
                unaryOpcode = 0;
            }
            else if (unaryOpcode == script::opcodeValue(script::VmOpcode::BitwiseNot))
            {
                value = ~value;
                unaryOpcode = 0;
            }
            else if (unaryOpcode == script::opcodeValue(script::VmOpcode::LogicalNot))
            {
                value = value ? 0 : 1;
                unaryOpcode = 0;
            }
            if (!emitIntObject(value))
                return 1;
            return emitTrailingUnary() ? 0 : 1;
        }

        if (current == '"')
        {
            if (!emitByte(script::opcodeValue(script::VmOpcode::PushString)))
                return 1;
            const std::size_t outOff = static_cast<std::size_t>(m_data.bytecodeEnd);
            const int written = PutString(reinterpret_cast<char*>(bytecodeStorage() + outOff));
            m_data.bytecodeEnd += written;
            return emitTrailingUnary() ? 0 : 1;
        }

        if (current == '\'')
        {
            setSourceCursorOffset(sourceCursorOffset() + 1);
            const signed char ch = static_cast<signed char>(sourceByteAtCursor());
            setSourceCursorOffset(sourceCursorOffset() + 1);
            if (!emitIntObject(static_cast<int>(ch)))
                return 1;
            if (sourceByteAtCursor() != '\'')
            {
                reportCompileError(13, "second '", 0);
                std::exit(1);
            }
            setSourceCursorOffset(sourceCursorOffset() + 1);
            return emitTrailingUnary() ? 0 : 1;
        }

        if (Word("sizeof"))
        {
            if (!Word("("))
            {
                reportCompileError(13, "'(' for sizeof", 0);
                std::exit(1);
            }

            if (!emitByte(script::opcodeValue(script::VmOpcode::PushInteger)))
                return 1;

            int sizeofValue = 4;
            if (!Word("int") && !Word("string"))
            {
                STRING sizeofName;
                GetName(sizeofName);
                int foundIndex = -1;
                const int count = functionCount();
                const script::LogicFunctionRecord* const records = functionRecordStorage();
                for (int i = count - 1; i >= 0; --i)
                {
                    if (std::strcmp(records[static_cast<std::size_t>(i)].name.c_str(), sizeofName.c_str()) == 0)
                    {
                        foundIndex = i;
                        break;
                    }
                }
                if (foundIndex < 0 || records[static_cast<std::size_t>(foundIndex)].flags != 1)
                {
                    reportCompileError(4, "sizeof parameter", 0);
                    std::exit(1);
                }
                sizeofValue = records[static_cast<std::size_t>(foundIndex)].itemCount << 2;
            }

            if (!emitIntPayload(sizeofValue))
                return 1;
            WordEnd(")");
            return emitTrailingUnary() ? 0 : 1;
        }

        if (Word("static"))
        {
            if (Word("int"))
            {
                do
                {
                    IntVar();
                }
                while (Word(","));
                return emitTrailingUnary() ? 0 : 1;
            }
            if (Word("string"))
            {
                do
                {
                    StringVar();
                }
                while (Word(","));
                return emitTrailingUnary() ? 0 : 1;
            }
            reportCompileError(4, "static variable", 0);
            std::exit(1);
        }

        if (Word("int"))
        {
            do
            {
                IntVar();
            }
            while (Word(","));
            return emitTrailingUnary() ? 0 : 1;
        }

        if (Word("string"))
        {
            do
            {
                StringVar();
            }
            while (Word(","));
            return emitTrailingUnary() ? 0 : 1;
        }

        if (Word("return"))
        {
            vyrag();
            if (!emitByte(script::opcodeValue(script::VmOpcode::Return)))
                return 1;
            return emitTrailingUnary() ? 0 : 1;
        }

        if (Word("("))
        {
            vyrag();
            WordEnd(")");
            return emitTrailingUnary() ? 0 : 1;
        }

        if (std::isalpha(current))
        {
            auto sourceCursorChar = [&]() -> int
            {
                if (sourceCursorOffset() < 0 || sourceCursorOffset() >= sourceEndOffset())
                    return -1;
                return sourceByteAtCursor();
            };
            auto emitFormattedPrimaryDiagnostic = [&](int code, const char* fmt, const STRING& name)
            {
                char buffer[512];
                std::snprintf(buffer, sizeof(buffer), fmt, name.c_str());
                reportCompileError(code, buffer, 0);
                std::exit(1);
            };

            STRING name;
            GetName(name);

            const int foundIndex = getFunctionIndex(name);
            if (foundIndex < 0)
            {
                if (sourceCursorChar() == ':')
                {
                    setSourceCursorOffset(sourceCursorOffset() + 1);
                    m_data.functions.Insert(name, 7, STRING(), m_data.bytecodeEnd, 0, 0);
                    return mnog();
                }
                emitFormattedPrimaryDiagnostic(10, "Undeclared identifier '%s'", name);
            }

            const script::LogicFunctionRecord* const records = functionRecordStorage();
            const std::uint8_t flags = records[static_cast<std::size_t>(foundIndex)].flags;
            if (flags == 8)
            {
                if (sourceCursorChar() != ':')
                    emitFormattedPrimaryDiagnostic(10, "Incorrect use label '%s'", name);
                setSourceCursorOffset(sourceCursorOffset() + 1);
                int patchOffset = 0;
                script::LogicFunctionRecord* const label = mutableFunctionRecordAt(foundIndex);
                if (label)
                {
                    patchOffset = label->codeOffsetOrStackIndex;
                    label->flags = 7;
                    label->codeOffsetOrStackIndex = m_data.bytecodeEnd;
                    const int delta = m_data.bytecodeEnd - patchOffset;
                    std::memcpy(bytecodeStorage() + patchOffset, &delta, sizeof(delta));
                }
                return mnog();
            }

            if (flags == 7)
            {
                if (sourceCursorChar() == ':')
                    emitFormattedPrimaryDiagnostic(10, "Label redefinition '%s'", name);
                emitFormattedPrimaryDiagnostic(10, "Incorrect use label '%s'", name);
            }

            if (flags == 2)
            {
                const int parameterBase = records[static_cast<std::size_t>(foundIndex)].stackBase;
                const int parameterCount = records[static_cast<std::size_t>(foundIndex)].itemCount;
                const int externOpcode = records[static_cast<std::size_t>(foundIndex)].codeOffsetOrStackIndex;

                WordEnd("(");
                int parsedCount = 0;
                if (!Word(")"))
                {
                    for (;;)
                    {
                        vyrag();
                        Word(",");
                        ++parsedCount;
                        if (Word(")"))
                            break;
                    }
                }

                while (parsedCount < parameterCount)
                {
                    const script::StackObject* defaultRecord = mutableExecutionStackStorageAt(parameterBase + parsedCount);
                    if ((defaultRecord->flags & script::STACK_OBJECT_HAS_PAYLOAD) == 0)
                        break;
                    if (!emitByteAndIntPayload(script::opcodeValue(script::VmOpcode::ReadVariable), parameterBase + parsedCount))
                        return 1;
                    ++parsedCount;
                }

                if (parsedCount != parameterCount)
                {
                    reportCompileError(4, "extern function parameters number", 0);
                    std::exit(1);
                }

                if (!emitByte(static_cast<std::uint8_t>(externOpcode)))
                    return 1;
                return emitTrailingUnary() ? 0 : 1;
            }

            if (flags == 3)
            {
                const int parameterBase = records[static_cast<std::size_t>(foundIndex)].stackBase;
                const int parameterCount = records[static_cast<std::size_t>(foundIndex)].itemCount;

                WordEnd("(");
                int parsedCount = 0;
                if (!Word(")"))
                {
                    for (;;)
                    {
                        vyrag();
                        Word(",");
                        if (!emitByteAndIntPayload(script::opcodeValue(script::VmOpcode::Assign), parameterBase + parsedCount))
                            return 1;
                        if (!emitByte(script::opcodeValue(script::VmOpcode::Pop)))
                            return 1;
                        ++parsedCount;
                        if (Word(")"))
                            break;
                    }
                }

                while (parsedCount < parameterCount)
                {
                    const script::StackObject* defaultRecord = mutableExecutionStackStorageAt(parameterBase + parsedCount);
                    if ((defaultRecord->flags & script::STACK_OBJECT_HAS_PAYLOAD) == 0)
                        break;

                    if ((defaultRecord->flags & script::STACK_OBJECT_STRING) != 0)
                    {
                        const std::size_t textLen = std::strlen(defaultRecord->text.c_str()) + 1;
                        if (!emitByte(script::opcodeValue(script::VmOpcode::PushString)))
                            return 1;
                        if (!canWrite(static_cast<int>(textLen)))
                            return 1;
                        std::memcpy(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd),
                            defaultRecord->text.c_str(), textLen);
                        m_data.bytecodeEnd += static_cast<int>(textLen);
                    }
                    else
                    {
                        if (!emitIntObject(defaultRecord->intValue))
                            return 1;
                    }

                    if (!emitByteAndIntPayload(script::opcodeValue(script::VmOpcode::Assign), parameterBase + parsedCount))
                        return 1;
                    if (!emitByte(script::opcodeValue(script::VmOpcode::Pop)))
                        return 1;
                    ++parsedCount;
                }

                if (parsedCount != parameterCount)
                {
                    reportCompileError(4, "function parameters number", 0);
                    std::exit(1);
                }

                if (!emitByteAndIntPayload(script::opcodeValue(script::VmOpcode::CallScriptFunction), foundIndex))
                    return 1;
                return emitTrailingUnary() ? 0 : 1;
            }

            if (flags == 4)
            {
                const STRING& textValue = records[static_cast<std::size_t>(foundIndex)].text;
                const std::size_t textLen = std::strlen(textValue.c_str()) + 1;
                if (!emitByte(script::opcodeValue(script::VmOpcode::PushString)))
                    return 1;
                if (!canWrite(static_cast<int>(textLen)))
                    return 1;
                std::memcpy(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd),
                    textValue.c_str(), textLen);
                m_data.bytecodeEnd += static_cast<int>(textLen);
                return emitTrailingUnary() ? 0 : 1;
            }

            if (flags == 5)
            {
                const int value = records[static_cast<std::size_t>(foundIndex)].codeOffsetOrStackIndex;
                if (!emitIntObject(value))
                    return 1;
                return emitTrailingUnary() ? 0 : 1;
            }

            if (flags == 1)
            {
                const int stackIndex = records[static_cast<std::size_t>(foundIndex)].codeOffsetOrStackIndex;
                const script::StackObject* stackRecord = mutableExecutionStackStorageAt(stackIndex);

                int indexBytecodeStart = 0;
                int savedIndexBytecodeSize = 0;
                std::uint8_t* savedIndexBytecode = nullptr;
                if (Word("["))
                {
                    indexBytecodeStart = m_data.bytecodeEnd;
                    if ((stackRecord->flags & (script::STACK_OBJECT_STRING |
                                               script::STACK_OBJECT_ARRAY |
                                               script::STACK_OBJECT_DYNAMIC)) == 0)
                    {
                        reportCompileError(10, "[] for not array", 0);
                        std::exit(1);
                    }

                    vyrag();
                    if (!emitByte(script::opcodeValue(script::VmOpcode::ArrayIndex)))
                        return 1;
                    savedIndexBytecodeSize = m_data.bytecodeEnd - indexBytecodeStart;
                    savedIndexBytecode = static_cast<std::uint8_t*>(
                        ::operator new(static_cast<std::size_t>(savedIndexBytecodeSize)));
                    std::memcpy(savedIndexBytecode,
                        bytecodeStorage() + static_cast<std::size_t>(indexBytecodeStart),
                        static_cast<std::size_t>(savedIndexBytecodeSize));
                    m_data.bytecodeEnd = indexBytecodeStart;
                    WordEnd("]");
                }

                std::uint8_t assignmentOpcode = prefixOpcode;
                script::BinaryCommand compoundCommand = script::BinaryCommand::Add;
                auto sourceCharAtOffset = [&](int delta) -> int
                {
                    const int pos = sourceCursorOffset() + delta;
                    return sourceStorage()[static_cast<std::size_t>(pos)];
                };

                if (sourceCharAtOffset(1) != '=' && Word("="))
                {
                    vyrag();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::Assign);
                }
                else if (Word("+="))
                {
                    vyrag();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::Add;
                }
                else if (Word("-="))
                {
                    vyrag();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::Subtract;
                }
                else if (Word("/="))
                {
                    vyrag();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::Divide;
                }
                else if (Word("*="))
                {
                    vyrag();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::Multiply;
                }
                else if (Word("%="))
                {
                    vyrag();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::Modulo;
                }
                else if (Word("&="))
                {
                    vyrag();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::BitwiseAnd;
                }
                else if (Word("|="))
                {
                    vyrag();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::BitwiseOr;
                }
                else if (Word("^="))
                {
                    vyrag();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::BitwiseXor;
                }
                else if (Word("<<="))
                {
                    vyrag();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::ShiftLeft;
                }
                else if (Word(">>="))
                {
                    vyrag();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::ShiftRight;
                }
                else if (Word("++"))
                {
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::PostIncrement);
                }
                else if (Word("--"))
                {
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::PostDecrement);
                }

                if (savedIndexBytecode)
                {
                    std::memcpy(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd),
                        savedIndexBytecode, static_cast<std::size_t>(savedIndexBytecodeSize));
                    m_data.bytecodeEnd += savedIndexBytecodeSize;
                    ::operator delete(savedIndexBytecode);
                    savedIndexBytecode = nullptr;
                }
                else if ((stackRecord->flags & (script::STACK_OBJECT_ARRAY | script::STACK_OBJECT_DYNAMIC)) != 0)
                {
                    if (assignmentOpcode == script::opcodeValue(script::VmOpcode::PostIncrement) ||
                        assignmentOpcode == script::opcodeValue(script::VmOpcode::PostDecrement) ||
                        assignmentOpcode == script::opcodeValue(script::VmOpcode::PreIncrement) ||
                        assignmentOpcode == script::opcodeValue(script::VmOpcode::PreDecrement))
                    {
                        reportCompileError(10, "Increment or decrement for array", 0);
                        std::exit(1);
                    }
                    if (assignmentOpcode != script::opcodeValue(script::VmOpcode::ReadVariable))
                    {
                        reportCompileError(4, "operation for array", assignmentOpcode);
                        std::exit(1);
                    }
                }

                if (!emitByteAndIntPayload(static_cast<std::uint8_t>(assignmentOpcode), stackIndex))
                    return 1;
                if (assignmentOpcode == script::opcodeValue(script::VmOpcode::CompoundAssign) && !emitByte(script::opcodeValue(compoundCommand)))
                    return 1;
                return emitTrailingUnary() ? 0 : 1;
            }

            return emitTrailingUnary() ? 0 : 1;
        }

        if (unaryOpcode != 0)
        {
            reportCompileError(10, "error symbol", 0);
            std::exit(1);
        }
        return 0;
    }

    void SCRIPT::SetOperation(int byteCodePos, int operation)
    {
        std::uint8_t* const code = bytecodeStorage();
        const std::size_t start = static_cast<std::size_t>(byteCodePos);
        const bool canReadTwoConstants =
            code[start] == script::opcodeValue(script::VmOpcode::PushInteger) &&
            code[start + 5] == script::opcodeValue(script::VmOpcode::PushInteger) &&
            m_data.bytecodeEnd - byteCodePos == 10;

        if (canReadTwoConstants)
        {
            int lhs = 0;
            int rhs = 0;
            std::memcpy(&lhs, code + start + 1, sizeof(lhs));
            std::memcpy(&rhs, code + start + 6, sizeof(rhs));

            bool hasFoldedValue = true;
            int folded = lhs;
            switch (static_cast<script::BinaryCommand>(operation))
            {
            case script::BinaryCommand::Multiply:
                folded = lhs * rhs;
                break;
            case script::BinaryCommand::Divide:
                folded = lhs / rhs;
                break;
            case script::BinaryCommand::Modulo:
                folded = lhs % rhs;
                break;
            case script::BinaryCommand::Add:
                folded = lhs + rhs;
                break;
            case script::BinaryCommand::Subtract:
                folded = lhs - rhs;
                break;
            case script::BinaryCommand::ShiftRight:
                folded = lhs >> (rhs & 31);
                break;
            case script::BinaryCommand::ShiftLeft:
                folded = lhs << (rhs & 31);
                break;
            case script::BinaryCommand::BitwiseXor:
                folded = lhs ^ rhs;
                break;
            case script::BinaryCommand::BitwiseAnd:
                folded = lhs & rhs;
                break;
            case script::BinaryCommand::BitwiseOr:
                folded = lhs | rhs;
                break;
            default:
                hasFoldedValue = false;
                break;
            }

            if (hasFoldedValue)
                std::memcpy(code + start + 1, &folded, sizeof(folded));

            m_data.bytecodeEnd -= 5;
            return;
        }

        EmitByteIfNoError(static_cast<std::uint8_t>(operation));
    }

    void SCRIPT::slag()
    {
        const int bytecodeStart = m_data.bytecodeEnd;
        mnog();
        for (;;)
        {
            if (Word("*"))
            {
                mnog();
                SetOperation(bytecodeStart, script::opcodeValue(script::BinaryCommand::Multiply));
                continue;
            }
            if (Word("/"))
            {
                mnog();
                SetOperation(bytecodeStart, script::opcodeValue(script::BinaryCommand::Divide));
                continue;
            }
            if (Word("%"))
            {
                mnog();
                SetOperation(bytecodeStart, script::opcodeValue(script::BinaryCommand::Modulo));
                continue;
            }
            break;
        }
    }

    void SCRIPT::cmpslag()
    {
        const int bytecodeStart = m_data.bytecodeEnd;
        slag();
        for (;;)
        {
            if (Word("+"))
            {
                slag();
                SetOperation(bytecodeStart, script::opcodeValue(script::BinaryCommand::Add));
                continue;
            }
            if (Word("-"))
            {
                slag();
                SetOperation(bytecodeStart, script::opcodeValue(script::BinaryCommand::Subtract));
                continue;
            }
            break;
        }
    }

    void SCRIPT::logicslag()
    {
        const int bytecodeStart = m_data.bytecodeEnd;
        cmpslag();
        for (;;)
        {
            if (Word(">="))
            {
                cmpslag();
                bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::GreaterEqual);
                continue;
            }
            if (Word(">>"))
            {
                cmpslag();
                SetOperation(bytecodeStart, script::opcodeValue(script::BinaryCommand::ShiftRight));
                continue;
            }
            if (Word(">"))
            {
                cmpslag();
                bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::Greater);
                continue;
            }
            if (Word("<="))
            {
                cmpslag();
                bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::LessEqual);
                continue;
            }
            if (Word("<<"))
            {
                cmpslag();
                SetOperation(bytecodeStart, script::opcodeValue(script::BinaryCommand::ShiftLeft));
                continue;
            }
            if (Word("<"))
            {
                cmpslag();
                bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::Less);
                continue;
            }
            if (Word("=="))
            {
                cmpslag();
                bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::Equal);
                continue;
            }
            if (Word("!="))
            {
                cmpslag();
                bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::NotEqual);
                continue;
            }
            break;
        }
    }

    void SCRIPT::vyrag()
    {
        const int bytecodeStart = m_data.bytecodeEnd;
        logicslag();
        for (;;)
        {
            if (Word("^"))
            {
                logicslag();
                SetOperation(bytecodeStart, script::opcodeValue(script::BinaryCommand::BitwiseXor));
                continue;
            }
            if (Word("&&"))
            {
                logicslag();
                bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] =
                    script::opcodeValue(script::BinaryCommand::LogicalAnd);
                continue;
            }
            if (Word("&"))
            {
                logicslag();
                SetOperation(bytecodeStart, script::opcodeValue(script::BinaryCommand::BitwiseAnd));
                continue;
            }
            if (Word("||"))
            {
                logicslag();
                bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] =
                    script::opcodeValue(script::BinaryCommand::LogicalOr);
                continue;
            }
            if (Word("|"))
            {
                logicslag();
                SetOperation(bytecodeStart, script::opcodeValue(script::BinaryCommand::BitwiseOr));
                continue;
            }
            break;
        }
    }

    int SCRIPT::vyrag_oper()
    {
        for (;;)
        {
            vyrag();
            const int sourceLine = m_data.sourceLine + 1;
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd)] =
                script::opcodeValue(script::VmOpcode::StatementEnd);
            ++m_data.bytecodeEnd;
            std::memcpy(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd),
                        &sourceLine, sizeof(sourceLine));
            m_data.bytecodeEnd += 4;
            if (!Word(","))
                return 0;
        }
    }

    void SCRIPT::oper(std::int32_t* breakPatchList)
    {
        const int iffMatched = Word("iff");
        if (iffMatched || Word("if"))
        {
            WordEnd("(");
            vyrag();
            WordEnd(")");

            const script::VmOpcode branchOpcode = iffMatched
                ? script::VmOpcode::IfFalseChain
                : script::VmOpcode::If;
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(branchOpcode);
            const int firstPatchOffset = m_data.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd)) = (0);
            m_data.bytecodeEnd += 4;

            oper(breakPatchList);
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(firstPatchOffset)) = (m_data.bytecodeEnd - firstPatchOffset);

            if (!Word("else"))
                return;

            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(firstPatchOffset)) = (m_data.bytecodeEnd - firstPatchOffset + 5);
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int elsePatchOffset = m_data.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd)) = (0);
            m_data.bytecodeEnd += 4;

            oper(breakPatchList);
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(elsePatchOffset)) = (m_data.bytecodeEnd - elsePatchOffset);
            return;
        }

        if (Word("while"))
        {
            std::int32_t localBreakPatchList[0x80] = {};
            WordEnd("(");
            const int loopConditionStart = m_data.bytecodeEnd;
            vyrag();
            WordEnd(")");
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::If);
            const int conditionPatchOffset = m_data.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd)) = (0);
            m_data.bytecodeEnd += 4;

            oper(localBreakPatchList);
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int jumpBackPatchOffset = m_data.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd)) = (loopConditionStart - jumpBackPatchOffset);
            m_data.bytecodeEnd += 4;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(conditionPatchOffset)) = (m_data.bytecodeEnd - conditionPatchOffset);

            for (int slot = 0; slot < 0x80 && localBreakPatchList[slot] != 0; ++slot)
            {
                const int patchOffset = localBreakPatchList[slot];
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(patchOffset)) = (m_data.bytecodeEnd - patchOffset);
            }
            return;
        }

        if (Word("do"))
        {
            std::int32_t localBreakPatchList[0x80] = {};
            const int loopBodyStart = m_data.bytecodeEnd;
            oper(localBreakPatchList);

            WordEnd("while");
            WordEnd("(");
            vyrag();
            WordEnd(")");
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::LogicalNot);
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::If);
            const int branchBackPatchOffset = m_data.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd)) = (loopBodyStart - branchBackPatchOffset);
            m_data.bytecodeEnd += 4;

            for (int slot = 0; slot < 0x80 && localBreakPatchList[slot] != 0; ++slot)
            {
                const int patchOffset = localBreakPatchList[slot];
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(patchOffset)) = (m_data.bytecodeEnd - patchOffset);
            }
            return;
        }

        if (Word("for"))
        {
            std::int32_t localBreakPatchList[0x80] = {};

            WordEnd("(");
            vyrag_oper();
            WordEnd(";");

            const int conditionStart = m_data.bytecodeEnd;
            vyrag();
            WordEnd(";");
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::If);
            const int conditionPatchOffset = m_data.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd)) = (0);
            m_data.bytecodeEnd += 4;
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int skipUpdatePatchOffset = m_data.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd)) = (0);
            m_data.bytecodeEnd += 4;

            const int updateStart = m_data.bytecodeEnd;
            vyrag_oper();
            WordEnd(")");
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int updateJumpBackPatchOffset = m_data.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd)) = (conditionStart - updateJumpBackPatchOffset);
            m_data.bytecodeEnd += 4;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(skipUpdatePatchOffset)) = (m_data.bytecodeEnd - skipUpdatePatchOffset);

            oper(localBreakPatchList);
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int bodyJumpBackPatchOffset = m_data.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd)) = (updateStart - bodyJumpBackPatchOffset);
            m_data.bytecodeEnd += 4;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(conditionPatchOffset)) = (m_data.bytecodeEnd - conditionPatchOffset);

            for (int slot = 0; slot < 0x80 && localBreakPatchList[slot] != 0; ++slot)
            {
                const int patchOffset = localBreakPatchList[slot];
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(patchOffset)) = (m_data.bytecodeEnd - patchOffset);
            }
            return;
        }

        if (Word("break"))
        {
            WordEnd(";");
            if (!breakPatchList)
            {
                reportCompileError(10, "'break' without loop", 0);
                std::exit(1);
            }

            int slot = 0;
            while (slot < 0x80 && breakPatchList[slot] != 0)
                ++slot;
            if (slot >= 0x80)
            {
                reportCompileError(10, "Too many 'break'", 0);
                std::exit(1);
            }
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            breakPatchList[slot] = m_data.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd)) = (0);
            m_data.bytecodeEnd += 4;
            breakPatchList[slot + 1] = 0;
            return;
        }

        if (Word("goto"))
        {
            STRING name;
            GetName(name);

            int labelIndex = getFunctionIndex(name);
            const bool appendedPendingLabel = labelIndex < 0;
            if (appendedPendingLabel)
            {
                m_data.functions.Insert(name, 8, STRING(), m_data.bytecodeEnd + 1, 0, 0);
                labelIndex = functionCount() - 1;
            }

            const script::LogicFunctionRecord* const records = functionRecordStorage();
            const script::LogicFunctionRecord& labelRecord = records[static_cast<std::size_t>(labelIndex)];

            if (labelRecord.flags == 8 && !appendedPendingLabel)
            {
                char buffer[512];
                std::snprintf(buffer, sizeof(buffer), "second use undefined label '%s'", name.c_str());
                reportCompileError(10, buffer, 0);
                std::exit(1);
            }
            if (labelRecord.flags != 7 && labelRecord.flags != 8)
            {
                char buffer[512];
                std::snprintf(buffer, sizeof(buffer), "'%s' is not label", name.c_str());
                reportCompileError(10, buffer, 0);
                std::exit(1);
            }
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int payloadOffset = m_data.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd)) = (labelRecord.codeOffsetOrStackIndex - payloadOffset);
            m_data.bytecodeEnd += 4;
            return;
        }

        if (Word("{"))
        {
            const int savedFunctionCount = functionCount();
            while (!Word("}"))
                oper(breakPatchList);
            m_data.functions.count = savedFunctionCount;
            return;
        }

        vyrag_oper();
        WordEnd(";");
    }

    int SCRIPT::func()
    {
        if (skipempty2())
            return 1;

        const std::size_t cursor = static_cast<std::size_t>(sourceCursorOffset());
        auto starts = [&](const char* token) -> bool
        {
            return std::strncmp(reinterpret_cast<const char*>(sourceStorage() + cursor),
                token, std::strlen(token)) == 0;
        };

        if (starts("#define"))
        {
        setSourceCursorOffset(sourceCursorOffset() + 7);
        STRING name;
        m_data.parseMode = 1;
        GetName(name);
        m_data.parseMode = 0;

        STRING value;
        GetLine(value);

        const int existing = findDefine(name);
        if (existing >= 0)
            m_data.defines.table[existing].value = value;
        else
            m_data.defines.Insert(name, value);

        value.ReleaseOwnedStorage();
        return skipempty2();

        }
        if (starts("#undef"))
        {
        setSourceCursorOffset(sourceCursorOffset() + 6);
        STRING name;
        m_data.parseMode = 1;
        GetName(name);
        m_data.parseMode = 0;

        const int index = findDefine(name);
        if (index < 0)
        {
            reportCompileError(4, "#undef parameters", 0);
            std::exit(1);
        }
        if (index < m_data.defines.count)
        {
            --m_data.defines.count;
            for (int i = index; i < m_data.defines.count; ++i)
            {
                ScriptDefinePairRecord* const records = m_data.defines.table;
                records[i].name = records[i + 1].name;
                records[i].value = records[i + 1].value;
            }
        }
        if (m_data.defines.count == 0)
            clearDefines();
        return skipempty2();

        }
        if (Word("#include"))
        {
        STRING savedScriptFile = m_data.scriptFile;

        const unsigned char delimiter = sourceByteAtCursor();
        if (delimiter != '"' && delimiter != '<')
        {
            reportCompileError(13, "include file name", 0);
            std::exit(1);
        }

        setSourceCursorOffset(sourceCursorOffset() + 1);

        char includeName[0x400];
        int includeNameLength = 0;
        for (;;)
        {
            const unsigned char c = sourceByteAtCursor();
            if (c == '"' || c == '>')
                break;
            if (sourceCursorOffset() >= sourceEndOffset())
            {
                reportCompileError(10, "End of file", 0);
                std::exit(1);
            }
            includeName[includeNameLength++] = static_cast<char>(c);
            setSourceCursorOffset(sourceCursorOffset() + 1);
        }
        includeName[includeNameLength] = '\0';
        setSourceCursorOffset(sourceCursorOffset() + 1);

        FSTREAM includeStream(includeName, "rb");
        if (!includeStream.isOpen())
        {
            reportCompileError(7, includeName, 0);
            std::exit(1);
        }

        const std::uint32_t includeLength = fileLength32(includeStream);
        const int savedCursor = sourceCursorOffset();
        const int savedEnd = sourceEndOffset();
        const int savedLine = m_data.sourceLine;
        const int savedConditionalDepth = m_data.conditionalDepth;

        void* includeSourceOwner = nullptr;
        try
        {
            const std::uint32_t includeAllocationSize = includeLength + static_cast<std::uint32_t>(SourceBufferPadding);
            includeSourceOwner = ::operator new(static_cast<std::size_t>(includeAllocationSize));
        }
        catch (...)
        {
            reportCompileError(2, "include", 0);
            std::exit(1);
        }
        const std::uint32_t savedSourceToken = m_data.sourceBufferToken;
        m_data.sourceBufferToken = pointerToken(includeSourceOwner);
        setSourceCursorOffset(static_cast<int>(SourcePayloadOffset));
        const std::uint32_t includeEndOffset = static_cast<std::uint32_t>(SourcePayloadOffset) + includeLength;
        setSourceEndOffset(static_cast<std::int32_t>(includeEndOffset));
        if (includeLength != 0)
        {
            includeStream.read(sourceStorage() + SourcePayloadOffset,
                includeLength);
        }

        m_data.sourceLine = 0;
        (m_data.scriptFile).Assign(includeName);
        m_data.conditionalDepth = 0;

        int status = func();
        while (status == 0)
            status = func();

        includeStream.close();

        ::operator delete(static_cast<void*>(sourceStorage()));
        m_data.sourceBufferToken = savedSourceToken;
        setSourceCursorOffset(savedCursor);
        setSourceEndOffset(savedEnd);
        m_data.sourceLine = savedLine;
        m_data.conditionalDepth = savedConditionalDepth;
        (m_data.scriptFile).Assign(savedScriptFile);

        savedScriptFile.ReleaseOwnedStorage();
        return skipempty2();

        }
        if (Word("extern"))
        {
        const int savedFunctionCount = functionCount();

        STRING name;
        GetName(name);

        const script::LogicFunctionRecord* const records = functionRecordStorage();
        for (int i = savedFunctionCount - 1; i >= 0; --i)
        {
            if (std::strcmp(records[static_cast<std::size_t>(i)].name.c_str(), name.c_str()) == 0)
            {
                reportCompileError(10, "function redefinition", 0);
                std::exit(1);
            }
        }

        const int stackBase = executionStackCount();
        WordEnd("(");
        for (;;)
        {
            if (Word("int"))
            {
                IntVar();
            }
            else if (Word("string"))
            {
                StringVar();
            }

            if (!Word(","))
                break;
        }
        WordEnd(")");

        const int parameterCount = executionStackCount() - stackBase;
        const int nativeCode = GetInt();

        const unsigned char previous = sourceStorage()[static_cast<std::size_t>(sourceCursorOffset() - 1)];
        if (!std::isdigit(ctypeArgument(previous)))
        {
            reportCompileError(13, "extern function code", 0);
            std::exit(1);
        }

        m_data.functions.count = savedFunctionCount;
        m_data.functions.Insert(name, 2, STRING(), nativeCode, stackBase, parameterCount);
        WordEnd(";");
        return skipempty2();

        }

        if (Word("static"))
        {
            if (Word("int"))
            {
                do
                {
                    IntVar();
                }
                while (Word(","));
                WordEnd(";");
                return skipempty2();
            }
            if (Word("string"))
            {
                do
                {
                    StringVar();
                }
                while (Word(","));
                WordEnd(";");
                return skipempty2();
            }
            reportCompileError(4, "static variable", 0);
            std::exit(1);
        }

        if (Word("int"))
        {
            do
            {
                IntVar();
            }
            while (Word(","));
            WordEnd(";");
            return skipempty2();
        }
        if (Word("string"))
        {
            do
            {
                StringVar();
            }
            while (Word(","));
            WordEnd(";");
            return skipempty2();
        }

        {

        const int savedFunctionCount = functionCount();
        STRING name;
        GetName(name);

        int existingFunction = -1;
        const script::LogicFunctionRecord* records = functionRecordStorage();
        for (int i = savedFunctionCount - 1; i >= 0; --i)
        {
            if (std::strcmp(records[static_cast<std::size_t>(i)].name.c_str(), name.c_str()) == 0)
            {
                existingFunction = i;
                break;
            }
        }

        const int stackBase = executionStackCount();
        const int bytecodeStart = m_data.bytecodeEnd;
        int parameterOrdinal = 0;

        WordEnd("(");
        for (;;)
        {
            bool declaredParameter = false;
            if (Word("int"))
            {
                IntVar();
                declaredParameter = true;
            }
            else if (Word("string"))
            {
                StringVar();
                declaredParameter = true;
            }

            if (declaredParameter && existingFunction >= 0 && functionCount() > 0)
            {
                records = functionRecordStorage();
                script::LogicFunctionRecord* const lastParameterSymbol =
                    mutableFunctionRecordAt(functionCount() - 1);
                if (lastParameterSymbol)
                {
                    lastParameterSymbol->codeOffsetOrStackIndex =
                        records[static_cast<std::size_t>(existingFunction)].stackBase + parameterOrdinal;
                }
            }

            ++parameterOrdinal;
            if (!Word(","))
                break;
        }
        WordEnd(")");

        const int parameterCount = executionStackCount() - stackBase;
        if (existingFunction >= 0)
        {
            records = functionRecordStorage();
            const script::LogicFunctionRecord& previous =
                records[static_cast<std::size_t>(existingFunction)];
            if (previous.itemCount != parameterCount || previous.codeOffsetOrStackIndex != -1)
            {
                reportCompileError(10, "function redefinition", 0);
                std::exit(1);
            }

            if (stackBase <= 0)
                clearExecutionStack();
            else if (m_data.stack.count > stackBase)
                m_data.stack.count = stackBase;
        }

        int functionBytecodeStart = bytecodeStart;
        if (Word(";"))
        {
            functionBytecodeStart = -1;
        }
        else
        {

            if (existingFunction >= 0)
            {
                script::LogicFunctionRecord* const previous =
                    mutableFunctionRecordAt(existingFunction);
                if (previous)
                    previous->codeOffsetOrStackIndex = bytecodeStart;
            }

            WordEnd("{");
            while (!Word("}"))
                oper(nullptr);

            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] =
                script::opcodeValue(script::VmOpcode::Return);
        }

        m_data.functions.count = savedFunctionCount;

        if (existingFunction >= 0)
            return skipempty2();

        m_data.functions.Insert(name, 3, STRING(), functionBytecodeStart, stackBase, parameterCount);
        const int newFunctionIndex = functionCount() - 1;

        if (std::strcmp(name.c_str(), "main") == 0)
        {
            m_data.fallbackFunction = newFunctionIndex;
            return skipempty2();
        }

        return skipempty2();

        }
    }

    namespace
    {
        bool readVmDword(const std::uint8_t* bytecode, int offset, int& value)
        {
            std::uint32_t encodedValueBits = 0;
            std::memcpy(&encodedValueBits, bytecode + static_cast<std::size_t>(offset), sizeof(encodedValueBits));
            value = static_cast<int>(encodedValueBits);
            return true;
        }

        int stackValueToInteger(const script::StackObject& value)
        {
            return (value.flags & script::STACK_OBJECT_STRING)
                ? value.text.Int()
                : value.intValue;
        }
        int scriptSpritePointerValue(SPRITE* sprite) noexcept
        {
            return sprite
                ? static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(sprite)))
                : 0;
        }

        SPRITE* scriptResolveSpriteReference(int value) noexcept
        {
            if (value == 0)
                return nullptr;
            return reinterpret_cast<SPRITE*>(
                static_cast<std::uintptr_t>(static_cast<std::uint32_t>(value)));
        }

        

        

        PLAYER* scriptPlayerSlot(int index) noexcept
        {

            return Map->Player(index);
        }

        int g_unitIteratorArmyBucket = 0;
        int g_unitIteratorOrdinal = 0;

        ENGINE* findTrainEngineByArmyAndOrdinal(int armyBucket, int targetOrdinal, int* outScanOrdinal)
        {
            SPRITE_COLLECTOR* const map = GlobalSpriteCollector();
            int skipped = 0;
            int matched = 0;
            int count = map->overflowCount();
            if (!count)
            {
                *outScanOrdinal = 0;
                return nullptr;
            }

            int index = count - 1;
            SPRITE* sprite = map->overflowSpriteAt(index);
            if (!sprite)
            {
                *outScanOrdinal = 0;
                return nullptr;
            }

            for (;;)
            {
                if (sprite->Vid()->spriteClassId() == 21 && !sprite->engineChainPrevious())
                {
                    if (armyBucket == 4 || sprite->armyIndex() == armyBucket)
                    {
                        ++skipped;
                        if (++matched == targetOrdinal)
                        {
                            *outScanOrdinal = skipped;
                            SPRITE* const special = static_cast<ENGINE*>(sprite)->findEngineChainSpecialWeaponNode();
                            if (!special)
                                return static_cast<ENGINE*>(sprite);
                            if ((sprite->runtimeFlags() ^ special->runtimeFlags()) & SPRITE::ArmyBitsMask)
                                --matched;
                            else
                                return static_cast<ENGINE*>(special);
                        }
                    }
                    else
                    {
                        ++skipped;
                    }
                }

                if (index > map->overflowCount())
                    index = map->overflowCount();
                --index;
                if (index < 0)
                {
                    *outScanOrdinal = 0;
                    return nullptr;
                }
                sprite = map->overflowSpriteAt(index);
                if (!sprite)
                {
                    *outScanOrdinal = 0;
                    return nullptr;
                }
            }
        }

        ENGINE* FirstTrain(int armyBucket)
        {
            if (armyBucket < 0)
                return nullptr;
            int bucket = armyBucket;
            if (bucket >= 4)
                bucket = 3;
            g_unitIteratorArmyBucket = bucket;
            g_unitIteratorOrdinal = 1;
            int scanOrdinal = armyBucket;
            return findTrainEngineByArmyAndOrdinal(bucket, 1, &scanOrdinal);
        }

        ENGINE* NextTrain()
        {
            int scanOrdinal = 0;
            return findTrainEngineByArmyAndOrdinal(g_unitIteratorArmyBucket, ++g_unitIteratorOrdinal, &scanOrdinal);
        }

        constexpr std::uint32_t scriptSinTableBits[256] =
        {
        0x00000000u, 0x3CC90AB0u, 0x3D48FB2Fu, 0x3D96A905u, 0x3DC8BD36u, 0x3DFAB273u, 0x3E164083u, 0x3E2F10A2u,
        0x3E47C5C2u, 0x3E605C13u, 0x3E78CFCCu, 0x3E888E93u, 0x3E94A031u, 0x3EA09AE5u, 0x3EAC7CD4u, 0x3EB8442Au,
        0x3EC3EF15u, 0x3ECF7BCAu, 0x3EDAE880u, 0x3EE63375u, 0x3EF15AEAu, 0x3EFC5D27u, 0x3F039C3Du, 0x3F08F59Bu,
        0x3F0E39DAu, 0x3F13682Au, 0x3F187FC0u, 0x3F1D7FD1u, 0x3F226799u, 0x3F273656u, 0x3F2BEB4Au, 0x3F3085BBu,
        0x3F3504F3u, 0x3F396842u, 0x3F3DAEF9u, 0x3F41D870u, 0x3F45E403u, 0x3F49D112u, 0x3F4D9F02u, 0x3F514D3Du,
        0x3F54DB31u, 0x3F584853u, 0x3F5B941Au, 0x3F5EBE05u, 0x3F61C598u, 0x3F64AA59u, 0x3F676BD8u, 0x3F6A09A7u,
        0x3F6C835Eu, 0x3F6ED89Eu, 0x3F710908u, 0x3F731447u, 0x3F74FA0Bu, 0x3F76BA07u, 0x3F7853F8u, 0x3F79C79Du,
        0x3F7B14BEu, 0x3F7C3B28u, 0x3F7D3AACu, 0x3F7E1324u, 0x3F7EC46Du, 0x3F7F4E6Du, 0x3F7FB10Fu, 0x3F7FEC43u,
        0x3F800000u, 0x3F7FEC43u, 0x3F7FB10Fu, 0x3F7F4E6Du, 0x3F7EC46Du, 0x3F7E1324u, 0x3F7D3AACu, 0x3F7C3B28u,
        0x3F7B14BEu, 0x3F79C79Du, 0x3F7853F8u, 0x3F76BA07u, 0x3F74FA0Bu, 0x3F731447u, 0x3F710908u, 0x3F6ED89Eu,
        0x3F6C835Eu, 0x3F6A09A7u, 0x3F676BD8u, 0x3F64AA59u, 0x3F61C598u, 0x3F5EBE05u, 0x3F5B941Au, 0x3F584853u,
        0x3F54DB31u, 0x3F514D3Du, 0x3F4D9F02u, 0x3F49D112u, 0x3F45E403u, 0x3F41D870u, 0x3F3DAEF9u, 0x3F396842u,
        0x3F3504F3u, 0x3F3085BBu, 0x3F2BEB4Au, 0x3F273656u, 0x3F226799u, 0x3F1D7FD1u, 0x3F187FC0u, 0x3F13682Au,
        0x3F0E39DAu, 0x3F08F59Bu, 0x3F039C3Du, 0x3EFC5D27u, 0x3EF15AEAu, 0x3EE63375u, 0x3EDAE880u, 0x3ECF7BCAu,
        0x3EC3EF15u, 0x3EB8442Au, 0x3EAC7CD4u, 0x3EA09AE5u, 0x3E94A031u, 0x3E888E93u, 0x3E78CFCCu, 0x3E605C13u,
        0x3E47C5C2u, 0x3E2F10A2u, 0x3E164083u, 0x3DFAB273u, 0x3DC8BD36u, 0x3D96A905u, 0x3D48FB2Fu, 0x3CC90AB0u,
        0x00000000u, 0xBCC90AAFu, 0xBD48FB2Fu, 0xBD96A905u, 0xBDC8BD36u, 0xBDFAB273u, 0xBE164083u, 0xBE2F10A2u,
        0xBE47C5C2u, 0xBE605C13u, 0xBE78CFCCu, 0xBE888E93u, 0xBE94A031u, 0xBEA09AE5u, 0xBEAC7CD4u, 0xBEB8442Au,
        0xBEC3EF15u, 0xBECF7BCAu, 0xBEDAE880u, 0xBEE63375u, 0xBEF15AEAu, 0xBEFC5D27u, 0xBF039C3Du, 0xBF08F59Bu,
        0xBF0E39DAu, 0xBF13682Au, 0xBF187FC0u, 0xBF1D7FD1u, 0xBF226799u, 0xBF273656u, 0xBF2BEB4Au, 0xBF3085BBu,
        0xBF3504F3u, 0xBF396842u, 0xBF3DAEF9u, 0xBF41D870u, 0xBF45E403u, 0xBF49D112u, 0xBF4D9F02u, 0xBF514D3Du,
        0xBF54DB31u, 0xBF584853u, 0xBF5B941Au, 0xBF5EBE05u, 0xBF61C598u, 0xBF64AA59u, 0xBF676BD8u, 0xBF6A09A7u,
        0xBF6C835Eu, 0xBF6ED89Eu, 0xBF710908u, 0xBF731447u, 0xBF74FA0Bu, 0xBF76BA07u, 0xBF7853F8u, 0xBF79C79Du,
        0xBF7B14BEu, 0xBF7C3B28u, 0xBF7D3AACu, 0xBF7E1324u, 0xBF7EC46Du, 0xBF7F4E6Du, 0xBF7FB10Fu, 0xBF7FEC43u,
        0xBF800000u, 0xBF7FEC43u, 0xBF7FB10Fu, 0xBF7F4E6Du, 0xBF7EC46Du, 0xBF7E1324u, 0xBF7D3AACu, 0xBF7C3B28u,
        0xBF7B14BEu, 0xBF79C79Du, 0xBF7853F8u, 0xBF76BA07u, 0xBF74FA0Bu, 0xBF731447u, 0xBF710908u, 0xBF6ED89Eu,
        0xBF6C835Eu, 0xBF6A09A7u, 0xBF676BD8u, 0xBF64AA59u, 0xBF61C598u, 0xBF5EBE05u, 0xBF5B941Au, 0xBF584853u,
        0xBF54DB31u, 0xBF514D3Du, 0xBF4D9F02u, 0xBF49D112u, 0xBF45E403u, 0xBF41D870u, 0xBF3DAEF9u, 0xBF396842u,
        0xBF3504F3u, 0xBF3085BBu, 0xBF2BEB4Au, 0xBF273656u, 0xBF226799u, 0xBF1D7FD1u, 0xBF187FC0u, 0xBF13682Au,
        0xBF0E39DAu, 0xBF08F59Bu, 0xBF039C3Du, 0xBEFC5D27u, 0xBEF15AEAu, 0xBEE63375u, 0xBEDAE880u, 0xBECF7BCAu,
        0xBEC3EF15u, 0xBEB8442Au, 0xBEAC7CD4u, 0xBEA09AE5u, 0xBE94A031u, 0xBE888E93u, 0xBE78CFCCu, 0xBE605C13u,
        0xBE47C5C2u, 0xBE2F10A2u, 0xBE164083u, 0xBDFAB273u, 0xBDC8BD36u, 0xBD96A905u, 0xBD48FB30u, 0xBCC90AB0u,
        };

        constexpr std::uint32_t scriptCosTableBits[256] =
        {
        0x3F800000u, 0x3F7FEC43u, 0x3F7FB10Fu, 0x3F7F4E6Du, 0x3F7EC46Du, 0x3F7E1324u, 0x3F7D3AACu, 0x3F7C3B28u,
        0x3F7B14BEu, 0x3F79C79Du, 0x3F7853F8u, 0x3F76BA07u, 0x3F74FA0Bu, 0x3F731447u, 0x3F710908u, 0x3F6ED89Eu,
        0x3F6C835Eu, 0x3F6A09A7u, 0x3F676BD8u, 0x3F64AA59u, 0x3F61C598u, 0x3F5EBE05u, 0x3F5B941Au, 0x3F584853u,
        0x3F54DB31u, 0x3F514D3Du, 0x3F4D9F02u, 0x3F49D112u, 0x3F45E403u, 0x3F41D870u, 0x3F3DAEF9u, 0x3F396842u,
        0x3F3504F3u, 0x3F3085BBu, 0x3F2BEB4Au, 0x3F273656u, 0x3F226799u, 0x3F1D7FD1u, 0x3F187FC0u, 0x3F13682Au,
        0x3F0E39DAu, 0x3F08F59Bu, 0x3F039C3Du, 0x3EFC5D27u, 0x3EF15AEAu, 0x3EE63375u, 0x3EDAE880u, 0x3ECF7BCAu,
        0x3EC3EF15u, 0x3EB8442Au, 0x3EAC7CD4u, 0x3EA09AE5u, 0x3E94A031u, 0x3E888E93u, 0x3E78CFCCu, 0x3E605C13u,
        0x3E47C5C2u, 0x3E2F10A2u, 0x3E164083u, 0x3DFAB273u, 0x3DC8BD36u, 0x3D96A905u, 0x3D48FB2Fu, 0x3CC90AB0u,
        0x00000000u, 0xBCC90AAFu, 0xBD48FB2Fu, 0xBD96A905u, 0xBDC8BD36u, 0xBDFAB273u, 0xBE164083u, 0xBE2F10A2u,
        0xBE47C5C2u, 0xBE605C13u, 0xBE78CFCCu, 0xBE888E93u, 0xBE94A031u, 0xBEA09AE5u, 0xBEAC7CD4u, 0xBEB8442Au,
        0xBEC3EF15u, 0xBECF7BCAu, 0xBEDAE880u, 0xBEE63375u, 0xBEF15AEAu, 0xBEFC5D27u, 0xBF039C3Du, 0xBF08F59Bu,
        0xBF0E39DAu, 0xBF13682Au, 0xBF187FC0u, 0xBF1D7FD1u, 0xBF226799u, 0xBF273656u, 0xBF2BEB4Au, 0xBF3085BBu,
        0xBF3504F3u, 0xBF396842u, 0xBF3DAEF9u, 0xBF41D870u, 0xBF45E403u, 0xBF49D112u, 0xBF4D9F02u, 0xBF514D3Du,
        0xBF54DB31u, 0xBF584853u, 0xBF5B941Au, 0xBF5EBE05u, 0xBF61C598u, 0xBF64AA59u, 0xBF676BD8u, 0xBF6A09A7u,
        0xBF6C835Eu, 0xBF6ED89Eu, 0xBF710908u, 0xBF731447u, 0xBF74FA0Bu, 0xBF76BA07u, 0xBF7853F8u, 0xBF79C79Du,
        0xBF7B14BEu, 0xBF7C3B28u, 0xBF7D3AACu, 0xBF7E1324u, 0xBF7EC46Du, 0xBF7F4E6Du, 0xBF7FB10Fu, 0xBF7FEC43u,
        0xBF800000u, 0xBF7FEC43u, 0xBF7FB10Fu, 0xBF7F4E6Du, 0xBF7EC46Du, 0xBF7E1324u, 0xBF7D3AACu, 0xBF7C3B28u,
        0xBF7B14BEu, 0xBF79C79Du, 0xBF7853F8u, 0xBF76BA07u, 0xBF74FA0Bu, 0xBF731447u, 0xBF710908u, 0xBF6ED89Eu,
        0xBF6C835Eu, 0xBF6A09A7u, 0xBF676BD8u, 0xBF64AA59u, 0xBF61C598u, 0xBF5EBE05u, 0xBF5B941Au, 0xBF584853u,
        0xBF54DB31u, 0xBF514D3Du, 0xBF4D9F02u, 0xBF49D112u, 0xBF45E403u, 0xBF41D870u, 0xBF3DAEF9u, 0xBF396842u,
        0xBF3504F3u, 0xBF3085BBu, 0xBF2BEB4Au, 0xBF273656u, 0xBF226799u, 0xBF1D7FD1u, 0xBF187FC0u, 0xBF13682Au,
        0xBF0E39DAu, 0xBF08F59Bu, 0xBF039C3Du, 0xBEFC5D27u, 0xBEF15AEAu, 0xBEE63375u, 0xBEDAE880u, 0xBECF7BCAu,
        0xBEC3EF15u, 0xBEB8442Au, 0xBEAC7CD4u, 0xBEA09AE5u, 0xBE94A031u, 0xBE888E93u, 0xBE78CFCCu, 0xBE605C13u,
        0xBE47C5C2u, 0xBE2F10A2u, 0xBE164083u, 0xBDFAB273u, 0xBDC8BD36u, 0xBD96A905u, 0xBD48FB2Fu, 0xBCC90AB0u,
        0x00000000u, 0x3CC90AAFu, 0x3D48FB2Fu, 0x3D96A905u, 0x3DC8BD36u, 0x3DFAB273u, 0x3E164083u, 0x3E2F10A2u,
        0x3E47C5C2u, 0x3E605C13u, 0x3E78CFCCu, 0x3E888E93u, 0x3E94A031u, 0x3EA09AE5u, 0x3EAC7CD4u, 0x3EB8442Au,
        0x3EC3EF15u, 0x3ECF7BCAu, 0x3EDAE880u, 0x3EE63375u, 0x3EF15AEAu, 0x3EFC5D27u, 0x3F039C3Du, 0x3F08F59Bu,
        0x3F0E39DAu, 0x3F13682Au, 0x3F187FC0u, 0x3F1D7FD1u, 0x3F226799u, 0x3F273656u, 0x3F2BEB4Au, 0x3F3085BBu,
        0x3F3504F3u, 0x3F396842u, 0x3F3DAEF9u, 0x3F41D870u, 0x3F45E403u, 0x3F49D112u, 0x3F4D9F02u, 0x3F514D3Du,
        0x3F54DB31u, 0x3F584853u, 0x3F5B941Au, 0x3F5EBE05u, 0x3F61C598u, 0x3F64AA59u, 0x3F676BD8u, 0x3F6A09A7u,
        0x3F6C835Eu, 0x3F6ED89Eu, 0x3F710908u, 0x3F731447u, 0x3F74FA0Bu, 0x3F76BA07u, 0x3F7853F8u, 0x3F79C79Du,
        0x3F7B14BEu, 0x3F7C3B28u, 0x3F7D3AACu, 0x3F7E1324u, 0x3F7EC46Du, 0x3F7F4E6Du, 0x3F7FB10Fu, 0x3F7FEC43u,
        };

        float scriptNativeFloatFromBits(std::uint32_t bits)
        {
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        int scriptNativeX87Int64Low32(long double value) noexcept
        {
            if (!std::isfinite(value) ||
                value < -9223372036854775808.0L ||
                value >= 9223372036854775808.0L)
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(value));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        int scriptNativeTable1024ToInt(float value)
        {
            return scriptNativeX87Int64Low32(
                static_cast<long double>(value) * 1024.0L);
        }

        int scriptNativeSin1024(int angle)
        {
            return scriptNativeTable1024ToInt(SPRITE::directionSinValue(angle));
        }

        int scriptNativeCos1024(int angle)
        {
            return scriptNativeTable1024ToInt(SPRITE::directionCosValue(angle));
        }
    }

    int SCRIPT::popSpriteReferenceValue()
    {
        const int oldIndex = m_data.stack.count - 1;
        script::StackObject* top = mutableExecutionStackStorageAt(oldIndex);

        if (top->intValue != 0 && (top->flags & script::STACK_OBJECT_REF) == 0)
            logFileLoggerResourceError(g_fileLogger, "LOGIC", 10, "this variable is not unit", 0);

        const int index = --m_data.stack.count;
        return stackValueToInteger(*mutableExecutionStackStorageAt(index));
    }

int SCRIPT::IsLastStackString() const
    {
        const script::StackObject& top = executionStackStorage()[static_cast<std::size_t>(m_data.stack.count - 1)];
        return (top.flags & script::STACK_OBJECT_STRING) != 0 ? 1 : 0;
    }

    int MAP::PopObject()
    {
        SCRIPT& runtime = getScript();
        const int oldIndex = runtime.m_data.stack.count - 1;
        script::StackObject& top = runtime.m_data.stack[oldIndex];

        if (top.intValue != 0 && (top.flags & script::STACK_OBJECT_REF) == 0)
            logFileLoggerResourceError(g_fileLogger, "LOGIC", 10, "this variable is not unit", 0);

        const int index = --runtime.m_data.stack.count;
        return stackValueToInteger(runtime.m_data.stack[index]);
    }

    void MAP::PushObject(int value)
    {
        SCRIPT* const script = reinterpret_cast<SCRIPT*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::ScriptRuntime);
        script::StackObject obj;
        obj.initializeReferenceValue(value);
        script->m_data.stack.appendFields(
            obj.flags, obj.intValue, obj.text);
    }

    int ScriptExecFunc(int opcode)
    {

        return Map->ExecFunc(opcode);
    }

    int MAP::ExecFunc(int opcode)
    {
        static STRING str;
        SCRIPT* const script = reinterpret_cast<SCRIPT*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::ScriptRuntime);

        core::ApplicationDrawDispatcherState& drawState =
            core::GlobalApplicationDrawDispatcherState();
        core::Application* const application = reinterpret_cast<core::Application*>(core::ApplicationOwner());
        switch (static_cast<script::NativeFunctionCode>(opcode))
        {
        case script::NativeFunctionCode::CreateSprite:
{
                const int parentHandle = script->popSpriteReferenceValue();
                const int direction = PopInt();
                const int z = PopInt();
                const int y = PopInt();
                const int x = PopInt();
                VID* const vid = PopVid("for CreateSprite()");
                if (vid == EmptyVid)
                {
                    PushInt(0);
                    return 0;
                }

                SPRITE* const parent = scriptResolveSpriteReference(parentHandle);
                SPRITE* const created = reinterpret_cast<core::Application*>(core::ApplicationOwner())->CreateSprite(
                    vid,
                    VECTOR(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                    ANGLE(direction),
                    parent);
                PushObject(scriptSpritePointerValue(created));
                return 0;
            }
        case script::NativeFunctionCode::Flagman:
{
                        const int army = PopInt();
                        SPRITE* sprite = nullptr;
                        sprite = reinterpret_cast<MAP*>(win::applicationWinInstance())->Flagman(army);
                        PushObject(scriptSpritePointerValue(sprite));
                        return 0;
            }
        case script::NativeFunctionCode::FirstUnit:
{
                typeunit = PopInt();
                nmonster = 0;
                SPRITE_COLLECTOR* const hash = GlobalSpriteCollector();
                core::List<SPRITE*>& list = hash->mutableOverflowList();
                SPRITE* sprite = list.BeginIterate(&nmonster);
                while (sprite && (!sprite->Vid() || (static_cast<int>(sprite->Vid()->spriteType) & typeunit) == 0))
                    sprite = list.NextIterate(&nmonster);
                PushObject(scriptSpritePointerValue(sprite));
                return 0;
            }
        case script::NativeFunctionCode::NextUnit:
{
                SPRITE_COLLECTOR* const hash = GlobalSpriteCollector();
                core::List<SPRITE*>& list = hash->mutableOverflowList();
                SPRITE* sprite = list.NextIterate(&nmonster);
                while (sprite && (!sprite->Vid() || (static_cast<int>(sprite->Vid()->spriteType) & typeunit) == 0))
                    sprite = list.NextIterate(&nmonster);
                PushObject(scriptSpritePointerValue(sprite));
                return 0;
            }
        case script::NativeFunctionCode::GetSprite:
{
                SPRITE* const previous = scriptResolveSpriteReference(script->popSpriteReferenceValue());
                const int y = PopInt();
                const int x = PopInt();
                const int type = PopInt();
                auto* const application = reinterpret_cast<core::Application*>(core::ApplicationOwner());

                SPRITE* const sprite = application->findSpriteAtPointByBounds(
                    type, static_cast<float>(x), static_cast<float>(y), previous);
                PushObject(scriptSpritePointerValue(sprite));
                return 0;
            }
        case script::NativeFunctionCode::GetSpriteScr:
{
                const int screenY = PopInt();
                const int screenX = PopInt();
                const int type = PopInt();
                auto* const application = reinterpret_cast<core::Application*>(core::ApplicationOwner());

                SPRITE* const sprite = application->findSpriteAtPointByFilter(
                    type, static_cast<float>(screenX), static_cast<float>(screenY));
                PushObject(scriptSpritePointerValue(sprite));
                return 0;
            }
        case script::NativeFunctionCode::FindNearestSprite:
{
                SPRITE* const previous = scriptResolveSpriteReference(script->popSpriteReferenceValue());
                const int radius = PopInt();
                const int y = PopInt();
                const int x = PopInt();
                const int type = PopInt();
                auto* const application = reinterpret_cast<core::Application*>(core::ApplicationOwner());

                SPRITE* const sprite = application->findNearestSpriteByFilter(
                    type, static_cast<float>(x), static_cast<float>(y), static_cast<float>(radius), previous);
                PushObject(scriptSpritePointerValue(sprite));
                return 0;
            }
        case script::NativeFunctionCode::FirstInBox:
{
                const int bottom = PopInt();
                const int right = PopInt();
                const int top = PopInt();
                const int left = PopInt();
                SPRITE* sprite = GlobalSpriteCollectorFirstHashInBox(
                    static_cast<float>(left),
                    static_cast<float>(top),
                    static_cast<float>(right),
                    static_cast<float>(bottom));
                PushObject(scriptSpritePointerValue(sprite));
                return 0;
            }
        case script::NativeFunctionCode::NextInBox:
{
                SPRITE* sprite = GlobalSpriteCollectorNextHashInBox();
                PushObject(scriptSpritePointerValue(sprite));
                return 0;
            }
        case script::NativeFunctionCode::FirstSprite:
            {
                g_scriptSpriteIteratorPass = 0;
                n_sprite = drawState.drawPassBucket(0).count();
                SPRITE* const sprite = application->previousSpriteInDrawPass(0, &n_sprite);
                PushObject(scriptSpritePointerValue(sprite));
                return 0;
            }
        case script::NativeFunctionCode::NextSprite:
{
                SPRITE* sprite = nullptr;

                --n_sprite;
                if (n_sprite >= 0)
                {
                    const core::ApplicationDrawPassBucket& bucket = drawState.drawPassBucket(g_scriptSpriteIteratorPass);
                    while (n_sprite >= 0)
                    {
                        sprite = bucket.spriteAt(n_sprite);
                        if (sprite)
                            break;
                        --n_sprite;
                    }
                }

                while (!sprite && g_scriptSpriteIteratorPass < core::ApplicationDrawDispatcherState::PassCount - 1)
                {
                    ++g_scriptSpriteIteratorPass;
                    n_sprite = drawState.drawPassBucket(g_scriptSpriteIteratorPass).count();
                    sprite = application->previousSpriteInDrawPass(g_scriptSpriteIteratorPass, &n_sprite);
                }

                PushObject(scriptSpritePointerValue(sprite));
                return 0;
            }
        case script::NativeFunctionCode::Action:
{
                const int argument3 = PopInt();
                const int argument2 = PopInt();
                const int argument1 = PopInt();
                const int actionCode = PopInt();
                SPRITE* const actionSprite = scriptResolveSpriteReference(script->popSpriteReferenceValue());

                if (!actionSprite)
                {
                    PushInt(0);
                    return 0;
                }

                if (actionCode < 17)
                {
                    actionSprite->ChangeAnimation(actionCode);
                    PushInt(0);
                    return 0;
                }

                if (actionCode == 121 || actionCode == 124 || actionCode == 125)
                {

                    const int stringPointerValue = actionSprite->dispatchVirtualAction(
                        static_cast<std::uint32_t>(actionCode), argument1, argument2, argument3);
                    const STRING* const owner = reinterpret_cast<const STRING*>(
                        static_cast<std::uintptr_t>(static_cast<std::uint32_t>(stringPointerValue)));
                    PushStr(*owner);
                    return 0;
                }

                if (actionCode == 90 || actionCode == 156 || actionCode == 155 || actionCode == 154 || actionCode == 101 || actionCode == 103)
                {
                    const int result = actionSprite->dispatchVirtualAction(
                        static_cast<std::uint32_t>(actionCode), argument1, argument2, argument3);
                    PushObject(result);
                    return 0;
                }

                if ((actionCode == 33 || actionCode == 32 || actionCode == 36 || actionCode == 34 || actionCode == 150 || actionCode == 151) &&
                    actionSprite->Vid()->spriteClassId() == 21u &&
                    static_cast<unsigned char>(actionSprite->runtimeFlags() & SPRITE::CommandBitsMask) == 104u)
                {
                    PushInt(0);
                    return 0;
                }

                const int result = actionSprite->dispatchVirtualAction(
                    static_cast<std::uint32_t>(actionCode), argument1, argument2, argument3);
                PushInt(result);
                return 0;
            }
        case script::NativeFunctionCode::SizeTo:
{
                const int y = PopInt();
                const int x = PopInt();
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                int value = 0xEA60;
                if (sprite)
                {

                    const float distance = static_cast<float>(approximatePlanarDistance(
                        static_cast<float>(x) - sprite->X(),
                        static_cast<float>(y) - sprite->Y()));
                    value = scriptNativeX87Int64Low32(static_cast<long double>(distance));
                }
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::AddCommand:
{
                const int argument3 = PopInt();
                const int argument2 = PopInt();
                const int argument1 = PopInt();
                const int actionCode = PopInt();
                const int handle = script->popSpriteReferenceValue();
                if (handle == 0)
                    return 0;
                scriptResolveSpriteReference(handle)->AddActionAfterStop(actionCode, argument1, argument2, argument3);
                return 0;
            }
        case script::NativeFunctionCode::GetUnitVid:
{
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                PushInt(sprite && sprite->vidPointer() ? sprite->vidPointer()->nVid : 0);
                return 0;
            }
        case script::NativeFunctionCode::Destroy:
{
                const int handle = script->popSpriteReferenceValue();
                if (handle == 0)
                    return 0;
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                delete sprite;
                return 0;
            }
        case script::NativeFunctionCode::GetX:
{
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                PushInt(sprite ? scriptNativeX87Int64Low32(static_cast<long double>(sprite->xCoordinateValue())) : 0);
                return 0;
            }
        case script::NativeFunctionCode::GetY:
{
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                PushInt(sprite ? scriptNativeX87Int64Low32(static_cast<long double>(sprite->yCoordinateValue())) : 0);
                return 0;
            }
        case script::NativeFunctionCode::GetZ:
{
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                PushInt(sprite ? scriptNativeX87Int64Low32(static_cast<long double>(sprite->Z())) : 0);
                return 0;
            }
        case script::NativeFunctionCode::GetDirection:
{
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                PushInt(sprite ? sprite->Direction().Int() : 0);
                return 0;
            }
        case script::NativeFunctionCode::GetAnimation:
{
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                PushInt(sprite ? sprite->Animation() : 0);
                return 0;
            }
        case script::NativeFunctionCode::DirectionTo:
{
                const int y = PopInt();
                const int x = PopInt();
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                int value = 0;
                if (sprite)
                {
                    const float dy = static_cast<float>(static_cast<double>(y) - sprite->Y());
                    const float dx = static_cast<float>(static_cast<double>(x) - sprite->X());
                    value = ANGLE(dx, dy).Int();
                }
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::GetCommands:
{
                (str).Assign(Class);
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                if (!sprite)
                    return (PushStr(str), 0);

                const DWORD spriteClass = sprite->Vid()->spriteClass;
                if (spriteClass == 2u || spriteClass == 0x18u || spriteClass == 3u || spriteClass == 7u)
                {
                    STRING commandWordsText = sprite->GetTextItems();
                    (str).Assign(commandWordsText);
                    commandWordsText.ReleaseOwnedStorage();
                }

                STRING commandRecordsText = sprite->GetTextActions();
                (str).append(commandRecordsText);
                commandRecordsText.ReleaseOwnedStorage();

                return (PushStr(str), 0);
            }
        case script::NativeFunctionCode::SetCommands:
{
                (str).Assign(*PopStr());

                const int handle = script->popSpriteReferenceValue();
                if (handle == 0)
                    return 0;

                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                if (!sprite)
                    return 0;

                const DWORD spriteClass = sprite->Vid()->spriteClass;
                if (spriteClass == 2u || spriteClass == 0x18u || spriteClass == 3u || spriteClass == 7u)
                    sprite->SetTextItems(&str);

                static const char kCommandSectionDelimiter[] = { '\x02', '\0' };
                if (std::strstr(str.c_str(), kCommandSectionDelimiter))
                {
                    STRING commandRecordsOnlyText;
                    commandRecordsOnlyText.Assign(str.After(kCommandSectionDelimiter));
                    (str).Assign(commandRecordsOnlyText);
                    commandRecordsOnlyText.ReleaseOwnedStorage();
                }

                sprite->SetTextActions(&str);

                return 0;
            }
        case script::NativeFunctionCode::Load:
{

                        const STRING* const path = PopStr();
                        auto* const owner = reinterpret_cast<std::uint8_t*>(this);
                        auto& flags = *reinterpret_cast<std::uint32_t*>(
                            owner + core::application_layout::Flags);
                        flags |= application_flags::PendingCommandOrLoad;
                        reinterpret_cast<STRING*>(
                            owner + core::application_layout::PendingCommand)->Assign(*path);
                        return 0;
            }
        case script::NativeFunctionCode::Save:
{
                        const STRING path = *PopStr();
                        win::applicationWinInstance()->saveMap(path);
                        return 0;
            }
        case script::NativeFunctionCode::SaveDemo:
{
                RESOURCE& demoResource = Map->demoResource();
                if (demoResource.isOpen())
                    return 0;

                const STRING& path = *PopStr();
                demoResource.OpenForWrite(&path, RESOURCE::ResTypes::DEMO);
                return 0;
            }
        case script::NativeFunctionCode::MenuFind:
{
                const int ndir = PopInt();
                VID* const vid = PopVid("for MenuFind");

                SPRITE* found = nullptr;
                if (vid != EmptyVid && vid->totalSpriteCount() != 0)
                {
                    BaseSpriteList<0>& list = applicationFrameSpriteList();
                    const int count = list.activeCount();
                    for (int i = 0; i < count; ++i)
                    {
                        SPRITE* const sprite = list.at(i);
                        if (!sprite || sprite->Vid() != vid)
                            continue;
                        if (ndir != 999999)
                        {
                            const std::uint32_t directionByte =
                                static_cast<std::uint32_t>(sprite->directionIndex()
                                    + vid->directionQuantizationOffset()) & 0xFFu;
                            const int directionIndex = static_cast<int>(
                                (directionByte * static_cast<std::uint32_t>(vid->directionCount())) >> 8);
                            const int encodedDirectionSelector = 999000 + sprite->directionIndex();
                            if (directionIndex != ndir && encodedDirectionSelector != ndir)
                                continue;
                        }
                        found = sprite;
                        break;
                    }
                }
                PushObject(scriptSpritePointerValue(found));
                return 0;
            }
        case script::NativeFunctionCode::MenuLoad:
{

                const STRING path = *PopStr();
                (void)applicationMenu().Load(path);
                return 0;
            }
        case script::NativeFunctionCode::MenuRelease:
{
                const STRING path = *PopStr();
                if (path.isEmpty())
                    applicationFrameSpriteList().deleteAllSprites();
                else
                    (void)applicationMenu().DeleteFromFile(path);
                return 0;
            }
        case script::NativeFunctionCode::MenuNvidUnderCursor:
{
                PushInt(applicationMenu().NVidUnderCursor());
                return 0;
            }
        case script::NativeFunctionCode::MenuNdirUnderCursor:
{
                PushInt(applicationMenu().NDirUnderCursor());
                return 0;
            }
        case script::NativeFunctionCode::MenuAction:
{
                const int argument3 = PopInt();
                const int argument2 = PopInt();
                const int argument1 = PopInt();
                const int action = PopInt();
                const int ndir = PopInt();
                VID* const vid = PopVid("for MenuAction");
                if (vid == EmptyVid)
                    return 0;

                BaseSpriteList<0>& list = applicationFrameSpriteList();
                const int count = list.activeCount();
                for (int i = 0; i < count; ++i)
                {
                    SPRITE* const sprite = list.at(i);
                    if (!sprite || sprite->Vid() != vid)
                        continue;
                    if (ndir != 999999)
                    {
                        const std::uint32_t directionByte =
                            static_cast<std::uint32_t>(sprite->directionIndex()
                                + vid->directionQuantizationOffset()) & 0xFFu;
                        const int directionIndex = static_cast<int>(
                            (directionByte * static_cast<std::uint32_t>(vid->directionCount())) >> 8);
                        const int encodedDirectionSelector = 999000 + sprite->directionIndex();
                        if (directionIndex != ndir && encodedDirectionSelector != ndir)
                            continue;
                    }

                    if (action < 0x11)
                        sprite->ChangeAnimation(action);
                    else
                        (void)sprite->dispatchVirtualAction(
                            static_cast<std::uint32_t>(action), argument1, argument2, argument3);
                }
                return 0;
            }
        case script::NativeFunctionCode::MenuCreate:
{
                const int z = PopInt();
                const int yDelta = PopInt();
                const int y = z + yDelta;
                const int x = PopInt();
                const int directionSource = PopInt();
                VID* const vid = PopVid("for MenuCreate");
                if (vid == EmptyVid)
                {
                    PushInt(0);
                    return 0;
                }

                const int direction = ((directionSource << 8) / static_cast<int>(vid->directionCount())) & 0xFF;
                SPRITE* const created = reinterpret_cast<core::Application*>(core::ApplicationOwner())->CreateSprite(
                    vid, VECTOR(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                    ANGLE(direction), nullptr);
                PushObject(scriptSpritePointerValue(created));
                return 0;
            }
        case script::NativeFunctionCode::MenuLeftClick:
{
                MENU& list = applicationMenu();
                SPRITE* const selected = (list.controlFlags() & 1u) != 0u
                    ? list.selectedSprite()
                    : nullptr;
                PushObject(scriptSpritePointerValue(selected));
                return 0;
            }
        case script::NativeFunctionCode::GetInputX:
{
                PushInt(scriptNativeX87Int64Low32(
                    static_cast<long double>(scriptApplicationInputState().clientX)));
                return 0;
            }
        case script::NativeFunctionCode::GetInputY:
{
                PushInt(scriptNativeX87Int64Low32(
                    static_cast<long double>(scriptApplicationInputState().clientY)));
                return 0;
            }
        case script::NativeFunctionCode::GetKey:
{
                const int value = static_cast<int>(scriptApplicationInputState().lastCode);
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::MenuMode:
{
                const int value = PopInt();
                auto* const application = reinterpret_cast<core::Application*>(core::ApplicationOwner());
                if (value != 0)
                {

                    application->beginBucketTimingSnapshot();
                    mouseInstanceRef()->ChangeAnimation(0);
                }
                else
                {

                    application->endBucketTimingSnapshot();
                }
                return 0;
            }
        case script::NativeFunctionCode::SetCursor:
{
                const int cursorId = PopInt();
                MOUSE* mouse = mouseInstanceRef();

                if (cursorId == -1)
                {
                    mouse->Disable();
                    return 0;
                }
                if (cursorId == 0x100)
                {
                    mouse->HardwareOn();
                    return 0;
                }
                if (cursorId == 0x101)
                {
                    mouse->HardwareOff();
                    return 0;
                }

                if (!mouse->cursorHandlesLoaded())
                    mouse->Enable();
                mouse = mouseInstanceRef();
                mouse->ChangeAnimation(cursorId);
                return 0;
            }
        case script::NativeFunctionCode::MessageText:
{
                        const int y = PopInt();
                        const int x = PopInt();
                        STRING text = *PopStr();
                        win::ApplicationWin* const app = win::applicationWinInstance();
                        PLAYER* const player = app->startupPlayerSlotByIndex(
                            static_cast<int>(app->activeStartupPlayerIndex()));

                        player->PutMessage(&text, static_cast<float>(x), static_cast<float>(y));
                        return 0;
            }
        case script::NativeFunctionCode::GetInputState:
{
                const std::uint32_t state = scriptApplicationInputState().flags;
                const int bitOrder[] = {15, 14, 9, 10, 8, 7, 12, 11, 6, 5, 2, 0};
                int packed = 0;
                for (int bit : bitOrder)
                    packed = (packed << 1) | static_cast<int>((state >> bit) & 1u);
                PushInt(packed);
                return 0;
            }
        case script::NativeFunctionCode::SetShiftCoor:
{
                const int y = PopInt();
                const int x = PopInt();
                Map->SetShiftCoor(static_cast<float>(x), static_cast<float>(y), 0);
                return 0;
            }
        case script::NativeFunctionCode::SetScrollType:
{
                core::SetApplicationScrollType(static_cast<std::uint32_t>(PopInt()));
                return 0;
            }
        case script::NativeFunctionCode::GetScrollType:
{
                PushInt(static_cast<int>(core::ApplicationScrollType()));
                return 0;
            }
        case script::NativeFunctionCode::ScreenX:
{
                const int value = scriptNativeX87Int64Low32(static_cast<long double>(Graph->SizeX()));
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::ScreenY:
{
                const int value = scriptNativeX87Int64Low32(static_cast<long double>(Graph->SizeY()));
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::SetApplicationFlag7:
{
                const int value = PopInt();
                std::uint32_t flags = core::ApplicationFlags();
                flags = (flags & ~application_flags::ScriptControlBit7) |
                    (value != 0 ? application_flags::ScriptControlBit7 : 0u);
                core::SetApplicationFlags(flags);
                return 0;
            }
        case script::NativeFunctionCode::PlayerNoop:
{
                const int value = PopInt();
                PLAYER* const player = scriptPlayerSlot(static_cast<int>(core::ActivePlayerIndex()));
                if (value != 0)
                    player->onScriptModeEnabled();
                else
                    player->onScriptModeDisabled();
                return 0;
            }
        case script::NativeFunctionCode::GetString:
{
                (str).Assign(STRING());
                (str).Assign(*PopStr());

                const STRING section = *PopStr();

                const STRING& profilePath = *core::g_startupStringsIniPathOwner;

                STRING defaultValue;
                STRING profileValue;
                core::profile_p::readProfileStringInto(profileValue, profilePath, section, str, defaultValue);
                PushStr(profileValue);
                profileValue.ReleaseOwnedStorage();
                if (defaultValue.isEmpty())
                    return 0;
                defaultValue.ReleaseOwnedStorage();
                return 0;
            }
        case script::NativeFunctionCode::Exit:
{
                        const STRING reason = *PopStr();
                        (void)reason;
                        if (win::ApplicationWin* const app = win::applicationWinInstance())
                            if (HWND hwnd = app->nativeWindow())
                                ::PostMessageA(hwnd, WM_CLOSE, 0, 0);
                        return 0;
            }
        case script::NativeFunctionCode::ToScreenX:
{
                const int x = PopInt();
                const int value = scriptNativeX87Int64Low32(
                    static_cast<long double>(x) -
                    static_cast<long double>(core::GlobalApplicationDrawDispatcherState().cameraShiftX()));
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::ToScreenY:
{
                const int z = PopInt();
                const int y = PopInt();
                const int value = scriptNativeX87Int64Low32(
                    static_cast<long double>(y) - static_cast<long double>(z) -
                    static_cast<long double>(core::GlobalApplicationDrawDispatcherState().cameraShiftY()));
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::MenuRightClick:
{
                MENU& list = applicationMenu();
                SPRITE* const selected = (list.controlFlags() & 2u) != 0u
                    ? list.selectedSprite()
                    : nullptr;
                PushObject(scriptSpritePointerValue(selected));
                return 0;
            }
        case script::NativeFunctionCode::SetMouseClick:
{
                const int vkButton = PopInt();
                const int firstOrSecond = PopInt();
                input::InputControlKeys& keys = input::g_inputControlKeys;
                if (firstOrSecond == 0x400)
                {
                    keys.firstActionPrimary = static_cast<std::uint32_t>(vkButton);
                    keys.firstActionAlternate = static_cast<std::uint32_t>(vkButton);
                    return 0;
                }
                if (firstOrSecond == 0x800)
                {
                    keys.secondActionPrimary = static_cast<std::uint32_t>(vkButton);
                    keys.secondActionAlternate = static_cast<std::uint32_t>(vkButton);
                    return 0;
                }
                return 0;
            }
        case script::NativeFunctionCode::CursorAction:
{
                const int argument1 = PopInt();
                const int argument2 = PopInt();
                Mouse->Action(63, static_cast<std::intptr_t>(argument2), argument1, 0);
                return 0;
            }
        case script::NativeFunctionCode::SetSoundVolume:
{
                const int volume = PopInt();
                sound::g_globalSoundEngine->applyMasterVolumePercent(volume);
                return 0;
            }
        case script::NativeFunctionCode::SetMusicVolume:
{
                const int volume = PopInt();
                sound::g_globalSoundEngine->applyMusicVolumePercent(volume);
                return 0;
            }
        case script::NativeFunctionCode::PlaySfx:
{
                const int nsfx = PopInt();
                sound::g_globalSoundEngine->enqueueSoundRequest(nsfx, 0, 0);
                return 0;
            }
        case script::NativeFunctionCode::StopSfx:
{
                const int nsfx = PopInt();
                sound::g_globalSoundEngine->stopSoundNumber(nsfx);
                return 0;
            }
        case script::NativeFunctionCode::StopMusic:
{
                sound::g_globalSoundEngine->stopMusic();
                return 0;
            }
        case script::NativeFunctionCode::PlaySfxFromCoor:
{
                const int y = PopInt();
                const int x = PopInt();
                const int nsfx = PopInt();
                GRAPH* const graph = Graph;
                const core::ApplicationDrawDispatcherState& drawState = core::GlobalApplicationDrawDispatcherState();
                const float halfScreenX = static_cast<float>(graph->SizeX()) * 0.5f;
                const float halfScreenY = static_cast<float>(graph->SizeY()) * 0.5f;
                const float soundX = static_cast<float>(x) - drawState.cameraShiftX() - halfScreenX;
                const float soundY = static_cast<float>(y) - drawState.cameraShiftY() - halfScreenY;
                sound::g_globalSoundEngine->enqueueSoundRequestFromCoordinates(nsfx, soundX, soundY);
                return 0;
            }
        case script::NativeFunctionCode::PlayMusicFile:
{
                const int loop = PopInt();
                const STRING filename = *PopStr();
                sound::g_globalSoundEngine->playMusicFile(filename.c_str(), loop);
                return 0;
            }
        case script::NativeFunctionCode::Effect:
{
                const int duration = PopInt();
                const int argument2 = PopInt();
                const int argument1 = PopInt();
                const int effect = PopInt();
                (void)Graph->Effect(effect, argument1, argument2, duration);
                return 0;
            }
        case script::NativeFunctionCode::SetEnvironment:
{
                const int value = PopInt();
                Graph->SetEnvironment(static_cast<std::uint32_t>(value));
                return 0;
            }
        case script::NativeFunctionCode::SetGraphDetail:
        case script::NativeFunctionCode::SetAutoReBirth:
{
                (void)PopInt();
                return 0;
            }
        case script::NativeFunctionCode::SetGamma:
{
                const int gammaIndex = PopInt();
                std::uint32_t diffuse = 0;
                std::uint32_t specular = 0;
                scriptNativeDecodeGammaIndex(gammaIndex, diffuse, specular);
                Graph->setGamma(Gamma{diffuse, specular});
                return 0;
            }
        case script::NativeFunctionCode::SetWind:
{
                const int direct = PopInt();
                const int wind = PopInt();
                Graph->SetWind(wind, ANGLE(static_cast<unsigned char>(direct)));
                return 0;
            }
        case script::NativeFunctionCode::PlayMovie:
{
                const STRING filename = *PopStr();
                Graph->PlayMovie(&filename);
                return 0;
            }
        case script::NativeFunctionCode::IsPlayMovie:
{
                const int value = Graph->movieComObject(0) != nullptr ? 1 : 0;
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::StopMovie:
{
                Graph->releaseMoviePlayback();
                return 0;
            }
        case script::NativeFunctionCode::IsPlayMusic:
{
                const std::uint8_t* const soundOwner = reinterpret_cast<const std::uint8_t*>(
                    sound::g_globalSoundEngine);
                void* const streamOwner =
                    *reinterpret_cast<void* const*>(soundOwner + 0x414u);
                int value = 0;
                if (streamOwner != nullptr)
                {
                    using StreamStatusMethod = int(__thiscall*)(void*);
                    void* const* const streamVtable = *reinterpret_cast<void* const* const*>(streamOwner);
                    const auto streamStatus = reinterpret_cast<StreamStatusMethod>(streamVtable[1]);
                    if (streamStatus(streamOwner) != 0 ||
                        *reinterpret_cast<const std::uint32_t*>(soundOwner + 0x410u) != 0u)
                    {
                        value = 1;
                    }
                }
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::CountGamma:
{
                const int time = PopInt();
                const int toGamma = PopInt();
                const int fromGamma = PopInt();
                PushInt(CountGamma(fromGamma, toGamma, time));
                return 0;
            }
        case script::NativeFunctionCode::GetGamma:
{
                const Gamma& gamma = Graph->gammaPair();
                std::uint32_t out = (gamma.first >> 1) & 0x7F7F7F7Fu;
                for (int shift = 0; shift < 32; shift += 8)
                {
                    const std::uint32_t specByte = (gamma.second >> shift) & 0xFFu;
                    if (specByte != 0)
                    {
                        const std::uint32_t packedByte = 0x80u | (((~specByte) & 0xFEu) >> 1);
                        out = (out & ~(0xFFu << shift)) | ((packedByte & 0xFFu) << shift);
                    }
                }
                PushInt(static_cast<int>(out));
                return 0;
            }
        case script::NativeFunctionCode::GetEffectState:
{
                const int effect = PopInt();
                PushInt(Graph->GetEffectState(effect));
                return 0;
            }
        case script::NativeFunctionCode::GetPreviousMapName:
{
                PushStr(core::ApplicationPreviousMapName());
                return 0;
            }
        case script::NativeFunctionCode::GetASProtectUserName:
{
                const char* const information = g_registrationInformation;
                PushStr((information && information[0] != '\0') ? STRING(information) : STRING());
                return 0;
            }
        case script::NativeFunctionCode::GetMapName:
{
                PushStr(core::ApplicationCurrentMapName());
                return 0;
            }
        case script::NativeFunctionCode::Exec:
{
                str.Assign(PopStr()->c_str());
                writeLogLine(g_fileLogger, "Exec '%s'", str.c_str());

                STRING shellParameters = str.After(" ");
                STRING shellExecutable = str.Before(" ");
                ShellExecuteA(nullptr,
                              nullptr,
                              shellExecutable.c_str(),
                              shellParameters.c_str(),
                              nullptr,
                              5);
                return 0;
            }
        case script::NativeFunctionCode::CharAt:
{
                const int index = PopInt();
                const STRING text = *PopStr();

                const int value = static_cast<int>(static_cast<signed char>(text.c_str()[index]));
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::Log:
{

                const STRING text = *PopStr();
                writeLogLine(g_fileLogger, text.c_str());
                return 0;
            }
        case script::NativeFunctionCode::Random:
{

                const int maxValue = PopInt();
                const int randomValue = std::rand();
                const std::int32_t divisor = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(maxValue) + 1u);
                const int value = randomValue % divisor;
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::ChangeZUnit:
{
                const int z = PopInt();
                VID* const vid = PopVid("for ChangeZUnit");
                if (vid == EmptyVid)
                    return 0;

                const int pass = vid->renderLayer();
                int cursor = drawState.drawPassBucket(pass).count();
                SPRITE* sprite = application->previousSpriteInDrawPass(pass, &cursor);
                while (sprite)
                {
                    if (sprite->Vid() == vid)
                        sprite->ChangeCoor(sprite->X(), sprite->Y(), static_cast<float>(z));
                    sprite = application->previousSpriteInDrawPass(pass, &cursor);
                }
                return 0;
            }
        case script::NativeFunctionCode::GetTime:
{
                PushInt(static_cast<int>(core::CurrentTimeMilliseconds()));
                return 0;
            }
        case script::NativeFunctionCode::GetGroundZ:
{
                const int y = PopInt();
                const int x = PopInt();
                const int value = scriptNativeX87Int64Low32(static_cast<long double>(
                    Map->GetGroundZ(static_cast<float>(x), static_cast<float>(y))));
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::StringLength:
        case script::NativeFunctionCode::StringLengthCompat:
{
                const STRING& text = *PopStr();
                PushInt(text.Length());
                return 0;
            }
        case script::NativeFunctionCode::SetFlagman:
{
                const int spriteHandle = script->popSpriteReferenceValue();
                const int playerIndex = PopInt();
                Map->SetFlagman(playerIndex, scriptResolveSpriteReference(spriteHandle));
                return 0;
            }
        case script::NativeFunctionCode::AskPlace:
{
                const int z = PopInt();
                const int y = PopInt();
                const int x = PopInt();
                VID* const vid = PopVid("for CanPlace");

                int handle = 0;
                if (SPRITE* const hit = GlobalSpriteCollectorCanPlace(
                        *Map, vid, static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)))
                {
                    handle = scriptSpritePointerValue(hit);
                }
                PushObject(handle);
                return 0;
            }
        case script::NativeFunctionCode::GetVidData:
{
                const int type = PopInt();

                VID* const vid = PopVid("for GetVid");
                if (vid == EmptyVid)
                {
                    PushInt(0);
                    return 0;
                }

                switch (static_cast<script::VidDataCode>(type))
                {
                case script::VidDataCode::MaxHp:
                    PushInt(static_cast<int>(vid->maxHp));
                    return 0;
                case script::VidDataCode::BattleRange:
                    PushInt(scriptNativeX87Int64Low32(static_cast<long double>(vid->weaponBattleRange())));
                    return 0;
                case script::VidDataCode::Ammo:
                    PushInt(vid->GetMaxAmmo());
                    return 0;
                case script::VidDataCode::Name:
                    PushStr(vid->scriptName());
                    return 0;
                case script::VidDataCode::Count:
                    PushInt(vid->totalSpriteCount());
                    return 0;
                case script::VidDataCode::KilledUnit:
                    PushInt(vid->totalKilledUnitCount());
                    return 0;
                case script::VidDataCode::KilledUnitArmy0:
                case script::VidDataCode::KilledUnitArmy1:
                case script::VidDataCode::KilledUnitArmy2:
                case script::VidDataCode::KilledUnitArmy3:
                    PushInt(vid->killedUnitCountForArmy(type - script::toInt(script::VidDataCode::KilledUnitArmy0)));
                    return 0;
                case script::VidDataCode::CountArmy0:
                case script::VidDataCode::CountArmy1:
                case script::VidDataCode::CountArmy2:
                case script::VidDataCode::CountArmy3:
                    PushInt(vid->spriteCountForBucket(type - script::toInt(script::VidDataCode::CountArmy0)));
                    return 0;
                case script::VidDataCode::MaxHpArmy0:
                case script::VidDataCode::MaxHpArmy1:
                case script::VidDataCode::MaxHpArmy2:
                case script::VidDataCode::MaxHpArmy3:
                    PushInt(vid->GetMaxHp(type - script::toInt(script::VidDataCode::MaxHpArmy0)));
                    return 0;
                case script::VidDataCode::SpriteType:
                    PushInt(static_cast<int>(vid->spriteTypeId()));
                    return 0;
                case script::VidDataCode::Class:
                    PushInt(static_cast<int>(vid->spriteClassId()));
                    return 0;
                case script::VidDataCode::Speed:
                    PushInt(vid->maxSpeedValue() == 999999.0f
                        ? 999999
                        : scriptNativeX87Int64Low32(
                            static_cast<long double>(vid->maxSpeedValue()) * 1000.0L));
                    return 0;
                case script::VidDataCode::Lifetime:
                    PushInt(vid->lifetimeValue());
                    return 0;
                case script::VidDataCode::DetectRange:
                    PushInt(scriptNativeX87Int64Low32(static_cast<long double>(vid->weaponDetectRange())));
                    return 0;
                case script::VidDataCode::WeaponAim:
                    PushInt(scriptNativeX87Int64Low32(static_cast<long double>(vid->weaponAim())));
                    return 0;
                case script::VidDataCode::ExchangeVid:
                    PushInt(vid->exchangedVidRef()->nvid());
                    return 0;
                case script::VidDataCode::DirectionCount:
                    PushInt(vid->directionCount());
                    return 0;
                case script::VidDataCode::MoveMask:
                    PushInt(static_cast<int>(vid->movementMask()));
                    return 0;
                case script::VidDataCode::BuildTime:
                    PushInt(vid->weaponBuildTime());
                    return 0;
                case script::VidDataCode::Hide:
                    PushInt(vid->PropHide());
                    return 0;
                case script::VidDataCode::NotCreateAsChild:
                    PushInt(vid->PropNotCreateAsChild());
                    return 0;
                case script::VidDataCode::FrameSpeed:
                    PushInt(static_cast<int>(vid->defaultFrameSpeed()));
                    return 0;
                case script::VidDataCode::Link:
                    PushInt(vid->linkedVid() ? vid->linkedVid()->nvid() : 0);
                    return 0;
                case script::VidDataCode::Damage:
                    PushInt(vid->deathDamageMinimumBits());
                    return 0;
                case script::VidDataCode::RecolorUnit:
                    PushInt(vid->totalRecolorUnitCount());
                    return 0;
                case script::VidDataCode::RecolorUnitArmy0:
                case script::VidDataCode::RecolorUnitArmy1:
                case script::VidDataCode::RecolorUnitArmy2:
                case script::VidDataCode::RecolorUnitArmy3:
                    PushInt(vid->recolorUnitCountForArmy(type - script::toInt(script::VidDataCode::RecolorUnitArmy0)));
                    return 0;
                default:
                    break;
                }

                if (type >= script::VidChildFirst && type < script::VidChildEnd)
                {
                    VID* child = vid->childVidForDataCode(type);
                    if (!child)
                        return (PushInt(0), 0);
                    PushInt(child->nvid());
                    return 0;
                }

                if (type >= script::VidNoChildFirst && type < script::VidNoChildEnd)
                {
                    PushInt(vid->noChildValueForDataCode(type));
                    return 0;
                }

                script->RunTimeError(0x0E, "GetVid type", type);
                return 0;

            }
        case script::NativeFunctionCode::SetVidData:
{
                const int value = PopInt();
                const int type = PopInt();
                VID* vid = PopVid("for SetVid");
                if (vid == EmptyVid)
                    return 0;

                switch (static_cast<script::VidDataCode>(type))
                {
                case script::VidDataCode::MaxHp:
                    vid->maxHp = value;
                    return 0;
                case script::VidDataCode::Ammo:
                {
                    VID* target = vid;
                    VID* link = vid->linkedVid();
                    if (link && link->CanFight() != 0)
                        target = link;
                    target->setWeaponRecordAmmoCapacity(value);
                    return 0;
                }
                case script::VidDataCode::KilledUnit:
                    vid->setKilledUnitCountForArmy(3, value);
                    vid->setKilledUnitCountForArmy(2, value);
                    vid->setKilledUnitCountForArmy(1, value);
                case script::VidDataCode::KilledUnitArmy0:
                    vid->setKilledUnitCountForArmy(0, value);
                    return 0;
                case script::VidDataCode::KilledUnitArmy1:
                    vid->setKilledUnitCountForArmy(1, value);
                    return 0;
                case script::VidDataCode::KilledUnitArmy2:
                    vid->setKilledUnitCountForArmy(2, value);
                    return 0;
                case script::VidDataCode::KilledUnitArmy3:
                    vid->setKilledUnitCountForArmy(3, value);
                    return 0;
                case script::VidDataCode::MaxHpArmy0:
                case script::VidDataCode::MaxHpArmy1:
                case script::VidDataCode::MaxHpArmy2:
                case script::VidDataCode::MaxHpArmy3:
                    vid->SetMaxHp(type - script::toInt(script::VidDataCode::MaxHpArmy0), value);
                    return 0;
                case script::VidDataCode::HpCoeffArmy0:
                case script::VidDataCode::HpCoeffArmy1:
                case script::VidDataCode::HpCoeffArmy2:
                case script::VidDataCode::HpCoeffArmy3:
                    vid->SetHpCoeff(type - script::toInt(script::VidDataCode::HpCoeffArmy0), value);
                    return 0;
                case script::VidDataCode::Speed:
                {

                    const float speedValue =
                        value == 999999 ? 999999.0f : static_cast<float>(value) * 0.001f;
                    vid->setScriptSpeedValue(speedValue);
                    const core::ApplicationDrawPassBucket& bucket =
                        core::GlobalApplicationDrawDispatcherState().drawPassBucket(vid->renderLayer());
                    SPRITE* const* slots = bucket.data();
                    for (int i = bucket.count() - 1; i >= 0; --i)
                    {
                        SPRITE* const sprite = slots ? slots[i] : nullptr;
                        if (sprite && sprite->Vid() == vid)
                            sprite->syncExDataMaxSpeedFromVid();
                    }
                    return 0;
                }
                case script::VidDataCode::Lifetime:
                {
                    if (vid->nvid() == 0)
                        return 0;
                    vid->setLifetimeValue(value);
                    vid->setActionAuxStateRequired(1);
                    return 0;
                }
                case script::VidDataCode::DetectRange:
                    vid->setWeaponDetectRange(static_cast<float>(value));
                    return 0;
                case script::VidDataCode::WeaponAim:
                    vid->setWeaponAim(static_cast<float>(value));
                    return 0;
                case script::VidDataCode::ExchangeVid:
                    if (!reinterpret_cast<MAP*>(core::ApplicationOwner())->ValidateVid(value))
                    {
                        script->RunTimeError(4, "SetVid get_image", value);
                        return 0;
                    }
                    reinterpret_cast<core::Application*>(core::ApplicationOwner())->ExchangeVid(vid, reinterpret_cast<MAP*>(core::ApplicationOwner())->Vid(value));
                    return 0;
                case script::VidDataCode::MoveMask:
                    vid->setMovementMask(static_cast<DWORD>(value));
                    return 0;
                case script::VidDataCode::BuildTime:
                    vid->setWeaponBuildTime(value);
                    return 0;
                case script::VidDataCode::Hide:
                    vid->SetPropHide(value);
                    return 0;
                case script::VidDataCode::NotCreateAsChild:
                    vid->SetPropNotCreateAsChild(value);
                    return 0;
                case script::VidDataCode::FrameSpeed:
                    vid->setAllFrameSpeeds(value);
                    return 0;
                case script::VidDataCode::Link:
                {
                    vid->setLinkedVid(reinterpret_cast<MAP*>(core::ApplicationOwner())->Vid(value));
                    return 0;
                }
                case script::VidDataCode::Damage:
                    vid->setDeathDamageMinimumBits(value);
                    return 0;
                default:
                    break;
                }

                if (!vid)
                    return 0;

                if (type >= script::VidChildFirst && type < script::VidChildEnd)
                {
                    if (value == 0)
                    {
                        vid->setChildNvidForDataCode(type, 0);
                        vid->setChildVidForDataCode(type, nullptr);
                        return 0;
                    }

                    const int absValue = value < 0 ? -value : value;
                    const bool validChildSlot = reinterpret_cast<MAP*>(core::ApplicationOwner())->ValidateVid(absValue);
                    if (!validChildSlot)
                    {
                        script->RunTimeError(4, "SetVid child", value);
                        return 0;
                    }

                    VID* childForStore = reinterpret_cast<MAP*>(core::ApplicationOwner())->Vid(absValue);
                    vid->setChildNvidForDataCode(type, value);
                    vid->setChildVidForDataCode(type, childForStore);
                    VID* childForFlag = reinterpret_cast<MAP*>(core::ApplicationOwner())->Vid(absValue);
                    if (!childForFlag || childForFlag->PropBirthAsSmoke() == 0)
                        return 0;
                    vid->setActionAuxStateRequired(vid->actionAuxStateRequired() | 1);
                    return 0;
                }

                if (type >= script::toInt(script::VidDataCode::Gamma0) && type <= script::toInt(script::VidDataCode::Gamma3))
                {
                    const Gamma gamma(Gamma::DECODE, static_cast<DWORD>(value));
                    vid->SetGamma(gamma, static_cast<unsigned>(type - script::toInt(script::VidDataCode::Gamma0)));
                    return 0;
                }

                if (type >= script::VidNoChildFirst && type < script::VidNoChildEnd)
                {
                    vid->setNoChildValueForDataCode(type, value);
                    return 0;
                }

                script->RunTimeError(0x0E, "SetVid type", type);
                return 0;

            }
        case script::NativeFunctionCode::IntToString:
{
                const int value = PopInt();
                char buffer[128];
                std::snprintf(buffer, sizeof(buffer), "%d", value);
                PushStr(STRING(buffer));
                return 0;
            }
        case script::NativeFunctionCode::Sin:
{
                const int angle = PopInt();
                PushInt(scriptNativeSin1024(angle));
                return 0;
            }
        case script::NativeFunctionCode::Cos:
{
                const int angle = PopInt();
                PushInt(scriptNativeCos1024(angle));
                return 0;
            }
        case script::NativeFunctionCode::MapSizeX:
{
                        const int value = scriptNativeX87Int64Low32(
                            static_cast<long double>(win::applicationWinInstance()->mapExtentX()));
                        PushInt(value);
                        return 0;
            }
        case script::NativeFunctionCode::MapSizeY:
{
                        const int value = scriptNativeX87Int64Low32(
                            static_cast<long double>(win::applicationWinInstance()->mapExtentY()));
                        PushInt(value);
                        return 0;
            }
        case script::NativeFunctionCode::Genocide:
{
                VID* const vid = PopVid("for Genocide");
                if (!vid || vid == EmptyVid)
                    return 0;

                const int pass = vid->renderLayer();
                const core::ApplicationDrawPassBucket& bucket = drawState.drawPassBucket(pass);
                int cursor = bucket.count() - 1;
                while (cursor >= 0 && !bucket.spriteAt(cursor))
                    --cursor;
                SPRITE* sprite = cursor >= 0 ? bucket.spriteAt(cursor) : nullptr;
                while (sprite)
                {
                    if (sprite->Vid() == vid)
                        delete sprite;
                    sprite = application->previousSpriteInDrawPass(pass, &cursor);
                }
                return 0;
            }
        case script::NativeFunctionCode::ReplaceUnit:
{
                VID* const replacement = PopVid("for Replace Unit 2");
                VID* const source = PopVid("for Replace Unit 1");
                if (replacement == EmptyVid || source == EmptyVid)
                    return 0;

                const int pass = source->renderLayer();
                const core::ApplicationDrawPassBucket& bucket = drawState.drawPassBucket(pass);
                int cursor = bucket.count() - 1;
                while (cursor >= 0 && !bucket.spriteAt(cursor))
                    --cursor;
                SPRITE* sprite = cursor >= 0 ? bucket.spriteAt(cursor) : nullptr;
                while (sprite)
                {
                    if (sprite->Vid() == source)
                    {
                        reinterpret_cast<core::Application*>(core::ApplicationOwner())->CreateSprite(
                            replacement, VECTOR(sprite->X(), sprite->Y(), sprite->Z()),
                            ANGLE(sprite->directionIndex()), nullptr);
                        delete sprite;
                    }
                    sprite = application->previousSpriteInDrawPass(pass, &cursor);
                }
                return 0;
            }
        case script::NativeFunctionCode::Crc:
{
                const STRING text = *PopStr();
                const Crc32 crc(text.c_str(), static_cast<unsigned int>(text.Length()));
                PushInt(static_cast<int>(static_cast<unsigned int>(crc)));
                return 0;
            }
        case script::NativeFunctionCode::Printf:
{
                if (script->IsLastStackString())
                {
                    const STRING value = *PopStr();
                    STRING localValue;
                    ::new (static_cast<void*>(&(localValue))) as1::STRING(value);

                    const char* valueText = localValue.c_str();
                    const STRING format = *PopStr();
                    const char* formatText = format.c_str();

                    STRING formatted;
                    constructFormattedString(formatted, formatText, valueText);
                    PushStr(formatted);
                    return 0;
                }

                const int value = PopInt();
                const STRING format = *PopStr();
                const char* formatText = format.c_str();
                STRING formatted;
                constructFormattedString(formatted, formatText, value);
                PushStr(formatted);
                return 0;
            }
        case script::NativeFunctionCode::ReloadVid:
{

                reinterpret_cast<core::Application*>(core::ApplicationOwner())->ReloadVid();
                return 0;
            }
        case script::NativeFunctionCode::FileWrite:
{
                const STRING value = *PopStr();
                const int fileValue = PopInt();
                if (fileValue == 0)
                    return 0;

                std::FILE* file = scriptNativeFileFromInt(fileValue);
                if (!file)
                    return 0;

                value.Write(file);
                std::fseek(file, -1, SEEK_CUR);
                std::fputs("\n", file);
                return 0;
            }
        case script::NativeFunctionCode::FileRead:
{
                const int fileValue = PopInt();
                STRING lpFile;
                RESOURCE& demoResource = Map->demoResource();

                if ((core::ApplicationFlags() & application_flags::DemoUseResource) != 0)
                {
                    (lpFile).Read(&demoResource);
                }
                else if (fileValue != 0)
                {
                    (lpFile).ReadLine(scriptNativeFileFromInt(fileValue));
                }

                if ((core::ApplicationFlags() & application_flags::DemoWriteToResource) != 0)
                    lpFile.Write(&demoResource);

                PushStr(lpFile);
                return 0;
            }
        case script::NativeFunctionCode::FileOpen:
{
                const STRING filename = *PopStr();
                if ((core::ApplicationFlags() & application_flags::DemoUseResource) != 0)
                {
                    PushInt(0);
                    return 0;
                }

                std::FILE* file = FOpen(&filename, "r+t");
                if (!file)
                    script->RunTimeError(7, filename.c_str(), 0);
                PushInt(scriptNativeIntFromFile(file));
                return 0;
            }
        case script::NativeFunctionCode::FileClose:
{
                const int fileValue = PopInt();
                if (fileValue == 0)
                    return 0;
                if (std::FILE* file = scriptNativeFileFromInt(fileValue))
                    std::fclose(file);
                return 0;
            }
        case script::NativeFunctionCode::FileCreate:
{
                const STRING filename = *PopStr();
                if ((core::ApplicationFlags() & application_flags::DemoUseResource) != 0)
                    return (PushInt(0), 0);

                std::FILE* file = FOpen(&filename, "w+t");
                PushInt(scriptNativeIntFromFile(file));
                return 0;
            }
        case script::NativeFunctionCode::FileEof:
{
                const int fileValue = PopInt();
                if (fileValue == 0)
                    return (PushInt(1), 0);

                std::FILE* file = scriptNativeFileFromInt(fileValue);
                PushInt(file && std::feof(file) ? 0x10 : 0);
                return 0;
            }
        case script::NativeFunctionCode::RegistryGetString:
{

                const STRING defaultValue = *PopStr();
                const STRING valueName = *PopStr();
                const STRING registryPath = *PopStr();
                PushStr(registryPath.ReadRegistryString(valueName, defaultValue));
                return 0;
            }
        case script::NativeFunctionCode::RegistrySetString:
{

                const STRING value = *PopStr();
                const STRING valueName = *PopStr();
                const STRING registryPath = *PopStr();
                registryPath.WriteRegistryString(valueName, value);
                return 0;
            }
        case script::NativeFunctionCode::RegistryDeleteValue:
{

                const STRING valueName = *PopStr();
                const STRING registryPath = *PopStr();
                registryPath.DeleteRegistryValue(valueName);
                return 0;
            }
        case script::NativeFunctionCode::RegistryPath:
{

                PushStr(*core::g_startupRegistryPathOwner->Path());
                return 0;
            }
        case script::NativeFunctionCode::StringLower:
{
                PushStr(PopStr()->ToLower());
                return 0;
            }
        case script::NativeFunctionCode::StringUpper:
{
                PushStr(PopStr()->ToUpper());
                return 0;
            }
        case script::NativeFunctionCode::ToBase64:
{
                const int key = PopInt();
                const STRING text = *PopStr();
                PushStr(text.ToBase64(key));
                return 0;
            }
        case script::NativeFunctionCode::TrainProperty:
{

                const int query = PopInt();
                SPRITE* const sprite = scriptResolveSpriteReference(PopObject());
                if (!sprite || !sprite->Vid() || sprite->Vid()->spriteClassId() != 21u)
                {
                    PushInt(0);
                    return 0;
                }

                TRAIN_INFO metrics(static_cast<ENGINE*>(sprite));

                switch (query)
                {
                case 1:
                    PushInt(metrics.speed);
                    break;
                case 2:
                    PushInt(metrics.weapon);
                    break;
                case 3:
                    PushInt((metrics.hp * 100) / metrics.max_hp);
                    break;
                case 4:
                    PushInt(metrics.hp);
                    break;
                case 5:
                    PushInt(metrics.percentAmmo);
                    break;
                case 6:
                    PushInt(metrics.Acceleration());
                    break;
                case 7:
                    PushInt(metrics.build_time);
                    break;
                case 8:
                    PushInt(0);
                    break;
                case 9:
                {

                    for (SPRITE* node = sprite->engineChainHead(); node; node = node->engineChainNext())
                    {
                        if (node->commandIndex() != 0)
                            PushInt(0);
                    }
                    PushInt(1);
                    break;
                }
                case 10:
                    PushInt(metrics.ammo);
                    break;
                case 11:
                    PushInt(metrics.maxAmmo);
                    break;
                default:
                    PushInt(0);
                    break;
                }
                return 0;
            }
        case script::NativeFunctionCode::SetSemaphore:
{
                const int army = PopInt();
                const int value = PopInt();
                const int y = PopInt();
                const int x = PopInt();
                core::g_rMap.SetSemaphore(x, y, value, army);
                return 0;
            }
        case script::NativeFunctionCode::BreakTrain:
{
                const int y = PopInt();
                const int x = PopInt();
                if (SPRITE* const sprite = scriptResolveSpriteReference(PopObject()))
                    sprite->splitEngineChainAtPosition(static_cast<float>(x), static_cast<float>(y));
                return 0;
            }
        case script::NativeFunctionCode::FirstTrain:
{
                PushObject(scriptSpritePointerValue(FirstTrain(PopInt())));
                return 0;
            }
        case script::NativeFunctionCode::NextTrain:
{
                PushObject(scriptSpritePointerValue(NextTrain()));
                return 0;
            }
        case script::NativeFunctionCode::PatrolEngine:
{
                const int y = PopInt();
                const int x = PopInt();
                SPRITE* const sprite = scriptResolveSpriteReference(PopObject());
                if (sprite && sprite->Vid()->spriteClassId() == 21)
                {
                    static_cast<ENGINE*>(sprite)->SetCommandToTrain(25, x, y);
                    return 0;
                }
                writeLogLine(g_fileLogger, "\xC1\xEE\xF0\xE8\xF1, \xF3 \xF2\xE5\xE1\xFF \xE2 PatrolTrain - train \xED\xE5\xE2\xE5\xF0\xED\xFB\xE9 %X",
                           static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(sprite)));
                return 0;
            }
        case script::NativeFunctionCode::SetPushLine:
{
                const int value = PopInt();
                const int y2 = PopInt();
                const int x2 = PopInt();
                const int y1 = PopInt();
                const int x1 = PopInt();
                core::g_rMap.SetPushLine(x1, y1, x2, y2, value);
                return 0;
            }
        case script::NativeFunctionCode::GetScreenInputX:
{
                PushInt(scriptNativeX87Int64Low32(
                    static_cast<long double>(scriptApplicationInputState().clientX)));
                return 0;
            }
        case script::NativeFunctionCode::GetScreenInputY:
{
                PushInt(scriptNativeX87Int64Low32(
                    static_cast<long double>(scriptApplicationInputState().clientY)));
                return 0;
            }
        case script::NativeFunctionCode::SetCleverEnemyAttack:
{

                const int value = PopInt();
                static_cast<PLAYER_ARCADE*>(scriptPlayerSlot(1))->SetCleverEnemyAttack(value);
                return 0;
            }
        case script::NativeFunctionCode::AddUnitLimit:
{
                const int index = PopInt();
                VID* const vid = PopVid("for AddUnitLimit");
                const int value = PopInt();
                if (vid != EmptyVid)
                    vid->setUnitLimit(index, value);
                return 0;
            }
        case script::NativeFunctionCode::SetEnemyCanAttackNeutralTrains:
{
                const int value = PopInt();
                const std::uint32_t bit = (value != 0) ? application_flags::EnemyCanAttackNeutralTrains : 0u;
                const std::uint32_t flags = (core::ApplicationFlags() & ~application_flags::EnemyCanAttackNeutralTrains) | bit;
                core::SetApplicationFlags(flags);
                return 0;
            }
        case script::NativeFunctionCode::SetMoney:
{
                const int value = PopInt();
                const int playerIndex = PopInt();
                scriptPlayerSlot(playerIndex)->SetMoney(value);
                return 0;
            }
        case script::NativeFunctionCode::GetMoney:
{
                const int playerIndex = PopInt();
                PushInt(static_cast<int>(scriptPlayerSlot(playerIndex)->getMoney()));
                return 0;
            }
        case script::NativeFunctionCode::CanMoveEngineTo:
{

                (void)PopInt();
                (void)PopInt();
                (void)PopObject();
                return 0;
            }
        case script::NativeFunctionCode::CanAttackEngine:
{

                (void)PopObject();
                (void)PopObject();
                return 0;
            }
        default:
            writeLogLine(g_fileLogger, "!!!ERROR!!!LOGIC: Unknown extern Function %i", opcode);
            return 0;
        }
    }

}
