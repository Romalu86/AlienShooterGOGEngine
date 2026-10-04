#include "as_string.h"
#include "base_stream.h"
#include <algorithm>
#include <cstdarg>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string.h>
#include <limits>
#include <new>
#include <cstdio>
#include <ctime>
#include <cerrno>
#include <cwchar>
#include <string>

namespace as1
{
    char gSharedEmptyStringStorage = '\0';
    namespace
    {
        unsigned char gRegistryIntegerData[0x200]{};
        unsigned char gRegistryStringData[0x200]{};
        constexpr char g_letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        const char* safeCString(const char* text)
        {
            return text ? text : "";
        }

        __forceinline
        std::size_t stringAllocationSize(std::size_t textLength) noexcept
        {
            return (textLength & ~static_cast<std::size_t>(0x0Fu)) + 0x10u;
        }

        __forceinline
        bool sameStringBucket(std::size_t oldLength, std::size_t newLength) noexcept
        {
            return ((oldLength ^ newLength) & ~static_cast<std::size_t>(0x0Fu)) == 0u;
        }


        std::size_t fileLengthFromStart(FILE* file)
        {
            if (!file)
                return std::numeric_limits<std::size_t>::max();

            if (std::fseek(file, 0, SEEK_END) != 0)
                return std::numeric_limits<std::size_t>::max();

            const long len = std::ftell(file);
            if (len < 0)
                return std::numeric_limits<std::size_t>::max();

            if (std::fseek(file, 0, SEEK_SET) != 0)
                return std::numeric_limits<std::size_t>::max();

            return static_cast<std::size_t>(len);
        }

        FILE* openBinaryFileForRead(const char* path)
        {
            FILE* file = nullptr;
            return fopen_s(&file, path, "rb") == 0 ? file : nullptr;
        }

        bool getLocalTime(std::time_t timestamp, std::tm& out)
        {
            return localtime_s(&out, &timestamp) == 0;
        }

        void copyCStringBounded(char* destination, std::size_t capacity, const char* source)
        {
            if (!destination || capacity == 0)
                return;
            source = safeCString(source);
            const std::size_t len = std::min<std::size_t>(std::strlen(source), capacity - 1);
            std::memcpy(destination, source, len);
            destination[len] = '\0';
        }

        char* createTemporaryNameOwner(const char* directory, const char* prefix)
        {
            return ::_tempnam(directory, prefix);
        }
    }


    STRING::STRING(const char* text)
    {
        if (!text || *text == '\0')
        {
            m_text = SharedEmptyText();
            return;
        }
        const std::size_t length = std::strlen(text);
        m_text = static_cast<char*>(::operator new(stringAllocationSize(length)));
        std::memcpy(m_text, text, length);
        m_text[length] = '\0';
    }

    STRING::STRING(const char* left, const char* right)
    {
        const std::size_t leftLen = std::strlen(left);
        const std::size_t rightLen = std::strlen(right);
        m_text = static_cast<char*>(::operator new(stringAllocationSize(leftLen + rightLen)));
        if (leftLen != 0)
            std::memcpy(m_text, left, leftLen);
        std::memcpy(m_text + leftLen, right, rightLen + 1);
    }

    STRING::STRING(const char* text, int length)
    {
        const std::size_t size = static_cast<std::size_t>(length);
        m_text = static_cast<char*>(::operator new(stringAllocationSize(size)));
        std::memcpy(m_text, text, size);
        m_text[size] = '\0';
    }


    STRING::~STRING()
    {
        if (m_text != SharedEmptyText())
            ::operator delete(m_text);
    }


    int STRING::Length() const
    {
        return static_cast<int>(std::strlen(c_str()));
    }

    int STRING::Int() const
    {
        if (c_str()[1] != 'x')
            return std::atoi(c_str());
        int value;
        std::sscanf(c_str(), "%i", &value);
        return value;
    }


    STRING STRING::ToLower() const
    {
        STRING out(*this);
        ::_strlwr(out.m_text);
        return out;
    }


    STRING STRING::ToUpper() const
    {
        STRING out(*this);
        ::_strupr(out.m_text);
        return out;
    }


    STRING STRING::ToBase64(int add) const
    {
        char alphabet[65];
        std::memcpy(alphabet, g_letters, sizeof(alphabet));

        const std::size_t sourceLength = std::strlen(m_text);
        STRING result;
        std::size_t outputLength = 0;

        for (std::size_t sourceOffset = 0; sourceOffset < sourceLength; sourceOffset += 57u)
        {
            const std::size_t chunkLength = std::min<std::size_t>(57u, sourceLength - sourceOffset);
            unsigned char chunk[57];
            std::memcpy(chunk, m_text + sourceOffset, chunkLength);
            std::memset(chunk + chunkLength, 0, sizeof(chunk) - chunkLength);

            const unsigned char byteAdd = static_cast<unsigned char>(add);
            for (std::size_t i = 0; i < chunkLength; ++i)
                chunk[i] = static_cast<unsigned char>(chunk[i] + byteAdd);

            char encoded[77];
            std::size_t encodedLength = 0;
            for (std::size_t i = 0; i < chunkLength; i += 3u)
            {
                unsigned int value = static_cast<unsigned int>(chunk[i]);
                value = (value << 8u) + static_cast<unsigned int>(chunk[i + 1u]);
                value = (value << 8u) + static_cast<unsigned int>(chunk[i + 2u]);

                encoded[encodedLength + 3u] = alphabet[value & 0x3Fu];
                value >>= 6u;
                encoded[encodedLength + 2u] = alphabet[value & 0x3Fu];
                value >>= 6u;
                encoded[encodedLength + 1u] = alphabet[value & 0x3Fu];
                value >>= 6u;
                encoded[encodedLength] = alphabet[value & 0x3Fu];
                encodedLength += 4u;
            }
            encoded[encodedLength] = '\0';
            outputLength += encodedLength;
            result.append(encoded);
        }

        std::size_t padding = (3u - (sourceLength % 3u)) % 3u;
        while (padding != 0u)
        {
            result.m_text[outputLength - padding] = '=';
            --padding;
        }

        STRING out(result.c_str());
        return out;
    }


    void STRING::Read(BaseStream* stream)
    {

        Assign("");
        char chunk[256];
        int count = 0;
        int value = 0;
        for (;;)
        {
            if (count == 255)
            {
                chunk[count] = '\0';
                append(chunk);
                count = 0;
            }

            value = 0;
            if (stream->read(&value, 1u) != 0)
                value = 0;
            if (value == '\n')
                value = 0;
            if (value != '\r')
                chunk[count++] = static_cast<char>(value);
            if (value <= 0)
                break;
        }

        append(chunk);
    }

    void STRING::Write(BaseStream* stream) const
    {
        const char* text = c_str();
        stream->write(text, static_cast<unsigned>(std::strlen(text) + 1u));
    }

    void STRING::Write(FILE* file) const
    {
        std::fwrite(c_str(), std::strlen(c_str()) + 1u, 1u, file);
    }

    STRING& STRING::ReadLine(FILE* file)
    {
        Assign("");

        char chunk[256];
        int count = 0;
        int ch = 0;

        do
        {
            if (count == 255)
            {
                chunk[255] = '\0';
                append(chunk);
                count = 0;
            }

            ch = std::fgetc(file);
            if (ch < 0 || ch == '\n')
                ch = 0;
            else if (ch == '\r')
                continue;

            chunk[count++] = static_cast<char>(ch);
        } while (ch > 0);

        return append(chunk);
    }

    int STRING::loadFromFile(const STRING& pathOwner)
    {
        const char* const path = pathOwner.c_str();
        FILE* file = (*path != '\0') ? openBinaryFileForRead(path) : nullptr;
        if (!file)
        {
            Assign("");
            return 0;
        }

        const int fileLength = static_cast<int>(fileLengthFromStart(file));
        if (m_text != SharedEmptyText())
            ::operator delete(m_text);

        m_text = static_cast<char*>(::operator new(static_cast<unsigned int>(fileLength + 1), std::nothrow));
        if (!m_text)
        {
            m_text = SharedEmptyText();
            std::fclose(file);
            return 0;
        }

        std::fread(m_text, 1u, static_cast<std::size_t>(fileLength), file);
        m_text[fileLength] = '\0';
        std::fclose(file);
        return fileLength;
    }


    STRING& constructCurrentTimeString(STRING& destination)
    {

        std::time_t timestamp = 0;
        std::time(&timestamp);
        std::tm* localTime = std::localtime(&timestamp);
        char timeText[0x100];
        std::strftime(timeText, sizeof(timeText), "%H:%M:%S", localTime);

        if (timeText[0] == '\0')
        {
            destination.m_text = STRING::SharedEmptyText();
            return destination;
        }

        const std::size_t textLen = std::strlen(timeText);
        char* owner = static_cast<char*>(::operator new(stringAllocationSize(textLen)));
        destination.m_text = owner;
        std::memcpy(owner, timeText, textLen);
        owner[textLen] = '\0';
        return destination;
    }


    STRING& constructCurrentDateString(STRING& destination)
    {

        std::time_t timestamp = 0;
        std::time(&timestamp);
        std::tm* localTime = std::localtime(&timestamp);
        char dateText[0x100];
        std::strftime(dateText, sizeof(dateText), "%Y-%m-%d", localTime);

        if (dateText[0] == '\0')
        {
            destination.m_text = STRING::SharedEmptyText();
            return destination;
        }

        const std::size_t textLen = std::strlen(dateText);
        char* owner = static_cast<char*>(::operator new(stringAllocationSize(textLen)));
        destination.m_text = owner;
        std::memcpy(owner, dateText, textLen);
        owner[textLen] = '\0';
        return destination;
    }


    STRING& constructFileTimestampString(STRING& destination, const STRING& fileName)
    {
        FILETIME creationTime{};
        FILETIME lastAccessTime{};
        FILETIME lastWriteTime{};
        HANDLE const file = ::CreateFileA(fileName.c_str(), GENERIC_READ, 0, nullptr,
                                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        ::GetFileTime(file, &creationTime, &lastAccessTime, &lastWriteTime);
        ::CloseHandle(file);

        destination.Assign("Cr-");

        FILETIME localTime{};
        SYSTEMTIME systemTime{};
        char text[0x50]{};

        auto appendStamp = [&](const FILETIME& sourceTime, const char* suffix)
        {
            ::FileTimeToLocalFileTime(&sourceTime, &localTime);
            ::FileTimeToSystemTime(&localTime, &systemTime);
            ::GetDateFormatA(0x400, 0, &systemTime, "yyyy-MM-dd", text, 0x50);
            destination.append(text);
            destination.append(" ");
            ::GetTimeFormatA(0x400, 0, &systemTime, "hh:mm:ss", text, 0x50);
            destination.append(text);
            if (suffix)
                destination.append(suffix);
        };


        appendStamp(creationTime, " La-");
        appendStamp(lastAccessTime, " Lw-");
        appendStamp(lastWriteTime, nullptr);
        return destination;
    }


    STRING& constructTemporaryNameString(STRING& destination, const char* directory, const char* prefix)
    {

        char temporaryName[0x1000];
        std::memset(temporaryName, 0, sizeof(temporaryName));

        char* tempOwner = createTemporaryNameOwner(directory, prefix);


        if (!tempOwner)
        {
            destination.m_text = STRING::SharedEmptyText();
            return destination;
        }
        std::strcpy(temporaryName, tempOwner);
        std::free(tempOwner);

        if (temporaryName[0] == '\0')
        {
            destination.m_text = STRING::SharedEmptyText();
            return destination;
        }

        const std::size_t textLen = std::strlen(temporaryName);
        char* owner = static_cast<char*>(::operator new(stringAllocationSize(textLen)));
        destination.m_text = owner;
        std::memcpy(owner, temporaryName, textLen);
        owner[textLen] = '\0';
        return destination;
    }


    int STRING::Replace(const char* search, const char* replacement)
    {

        char* current = m_text;
        char* found = std::strstr(current, search);
        if (!found)
            return 0;

        const std::size_t searchLen = std::strlen(search);
        const std::size_t replacementLen = std::strlen(replacement);

        if (searchLen < replacementLen)
        {
            char* oldOwner = m_text;
            const std::size_t prefixLen = static_cast<std::size_t>(found - oldOwner);
            const std::size_t oldLen = std::strlen(oldOwner);
            const std::size_t newLen = oldLen - searchLen + replacementLen;
            char* newOwner = static_cast<char*>(::operator new(stringAllocationSize(newLen)));
            m_text = newOwner;

            if (prefixLen != 0)
                std::strncpy(newOwner, oldOwner, prefixLen);
            if (replacementLen != 0)
                std::strncpy(newOwner + prefixLen, replacement, replacementLen);
            std::strncpy(newOwner + prefixLen + replacementLen, found + searchLen, oldLen - prefixLen - searchLen + 1);

            if (oldOwner && oldOwner != SharedEmptyText())

                ::operator delete(oldOwner);
            return 1;
        }

        if (replacementLen != 0)
            std::strncpy(found, replacement, replacementLen);

        const std::size_t tailLen = std::strlen(found + searchLen);
        std::memcpy(found + replacementLen, found + searchLen, tailLen + 1);
        return 1;
    }

    STRING& STRING::RemoveEndChars(const char* chars)
    {
        char* const begin = m_text;
        char* cursor = begin + std::strlen(begin);
        if (cursor == begin)
            return *this;

        --cursor;
        while (cursor >= begin)
        {
            const int ch = static_cast<unsigned char>(*cursor);
            if (std::strchr(chars, ch) == nullptr)
                break;
            *cursor = '\0';
            if (cursor == begin)
                break;
            --cursor;
        }
        return *this;
    }


    STRING STRING::Before(const char* marker) const
    {
        STRING out;
        const char* const text = m_text;
        const char* const found = std::strstr(text, marker);
        std::size_t textLen = 0;

        if (found)
            textLen = static_cast<std::size_t>(found - text);
        else if (*text != '\0')
            textLen = std::strlen(text);
        else
            return out;

        if (textLen == 0)
            return out;

        char* owner = static_cast<char*>(::operator new(stringAllocationSize(textLen)));
        out.m_text = owner;
        std::memcpy(owner, text, textLen);
        owner[textLen] = '\0';
        return out;
    }


    STRING STRING::After(const char* marker) const
    {
        STRING out;
        const char* const found = std::strstr(m_text, marker);
        if (!found)
            return out;

        const char* const right = found + std::strlen(marker);
        if (*right == '\0')
            return out;

        const std::size_t textLen = std::strlen(right);
        char* owner = static_cast<char*>(::operator new(stringAllocationSize(textLen)));
        out.m_text = owner;
        std::memcpy(owner, right, textLen);
        owner[textLen] = '\0';
        return out;
    }


    STRING STRING::BeforeLast(const char* marker) const
    {
        STRING out;
        const char* const text = m_text;
        const char* last = nullptr;
        const char* cursor = std::strstr(text, marker);
        while (cursor)
        {
            last = cursor;
            cursor = std::strstr(cursor + 1, marker);
        }

        std::size_t textLen = 0;
        if (last)
            textLen = static_cast<std::size_t>(last - text);
        else if (*text != '\0')
            textLen = std::strlen(text);
        else
            return out;

        if (textLen == 0)
            return out;

        char* owner = static_cast<char*>(::operator new(stringAllocationSize(textLen)));
        out.m_text = owner;
        std::memcpy(owner, text, textLen);
        owner[textLen] = '\0';
        return out;
    }


    STRING STRING::AfterLast(const char* marker) const
    {
        STRING out;
        const char* last = nullptr;
        const char* cursor = std::strstr(m_text, marker);
        while (cursor)
        {
            last = cursor;
            cursor = std::strstr(cursor + 1, marker);
        }

        if (!last)
            return out;

        const char* const right = last + std::strlen(marker);
        if (*right == '\0')
            return out;

        const std::size_t textLen = std::strlen(right);
        char* owner = static_cast<char*>(::operator new(stringAllocationSize(textLen)));
        out.m_text = owner;
        std::memcpy(owner, right, textLen);
        owner[textLen] = '\0';
        return out;
    }


    STRING STRING::IncrementTrailingNumber(int delta) const
    {
        STRING out;
        const char* sourceText = m_text;
        char* tempOwner = nullptr;

        if (*sourceText != '\0')
        {
            const std::size_t sourceLen = std::strlen(sourceText);
            tempOwner = static_cast<char*>(::operator new(stringAllocationSize(sourceLen)));
            std::memcpy(tempOwner, sourceText, sourceLen);
            tempOwner[sourceLen] = '\0';
        }
        else
        {
            tempOwner = SharedEmptyText();
        }

        if (delta != 0)
        {
            int lastDigit = static_cast<int>(std::strlen(sourceText)) - 2;
            while (lastDigit >= 0 && !std::isdigit(static_cast<int>(static_cast<signed char>(sourceText[lastDigit]))))
                --lastDigit;

            if (lastDigit < 0)
            {
                out.m_text = SharedEmptyText();
                if (tempOwner && tempOwner != SharedEmptyText())
                    ::operator delete(tempOwner);
                return out;
            }

            int firstDigit = lastDigit - 1;
            while (firstDigit >= 0 && std::isdigit(static_cast<int>(static_cast<signed char>(sourceText[firstDigit]))))
                --firstDigit;
            if (firstDigit < 0 || sourceText[firstDigit] != '-')
                ++firstDigit;

            const int width = lastDigit - firstDigit + 1;
            const int oldValue = std::atoi(sourceText + firstDigit);
            char formattedNumber[0x80];
            std::sprintf(formattedNumber, "%0*i", width, oldValue + delta);
            std::strncpy(tempOwner + firstDigit, formattedNumber, static_cast<std::size_t>(width));
        }

        if (*tempOwner != '\0')
        {
            const std::size_t textLen = std::strlen(tempOwner);
            char* owner = static_cast<char*>(::operator new(stringAllocationSize(textLen)));
            out.m_text = owner;
            std::memcpy(owner, tempOwner, textLen);
            owner[textLen] = '\0';
        }
        else
        {
            out.m_text = SharedEmptyText();
        }

        if (tempOwner && tempOwner != SharedEmptyText())

            ::operator delete(tempOwner);
        return out;
    }

    STRING STRING::ExtractRegistrySubkey(DWORD* rootKey) const
    {
        const char* const sourceText = m_text;

        if (std::strncmp(sourceText, "HKEY_USERS\\", sizeof("HKEY_USERS\\") - 1u) == 0)
        {
            *rootKey = 0x80000003u;
            return After("\\");
        }

        if (std::strncmp(sourceText, "HKEY_CURRENT_USER\\", sizeof("HKEY_CURRENT_USER\\") - 1u) == 0)
        {
            *rootKey = 0x80000001u;
            return After("\\");
        }

        if (std::strncmp(sourceText, "HKEY_CLASSES_ROOT\\", sizeof("HKEY_CLASSES_ROOT\\") - 1u) == 0)
        {
            *rootKey = 0x80000000u;
            return After("\\");
        }

        if (std::strncmp(sourceText, "HKEY_CURRENT_CONFIG\\", sizeof("HKEY_CURRENT_CONFIG\\") - 1u) == 0)
        {
            *rootKey = 0x80000005u;
            return After("\\");
        }

        if (std::strncmp(sourceText, "HKEY_LOCAL_MACHINE\\", sizeof("HKEY_LOCAL_MACHINE\\") - 1u) == 0)
        {
            *rootKey = 0x80000002u;
            return After("\\");
        }

        *rootKey = 0x80000002u;

        STRING out;
        if (*sourceText == '\0')
            return out;

        const std::size_t textLen = std::strlen(sourceText);
        out.m_text = static_cast<char*>(::operator new(stringAllocationSize(textLen)));
        std::memcpy(out.m_text, sourceText, textLen);
        out.m_text[textLen] = '\0';
        return out;
    }


    STRING STRING::ReadRegistryString(const STRING& valueName, const STRING& defaultValue) const
    {
        DWORD root = 0x80000002u;
        STRING subKey = ExtractRegistrySubkey(&root);

        HKEY opened = nullptr;
        if (::RegOpenKeyExA(reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(root)),
                            subKey.c_str(), 0, KEY_QUERY_VALUE, &opened) == ERROR_SUCCESS)
        {
            DWORD type = 0;
            DWORD cbData = 0x1FFu;
            (void)::RegQueryValueExA(opened, valueName.c_str(), nullptr, &type,
                                     gRegistryStringData, &cbData);
            ::RegCloseKey(opened);

            if (type == REG_DWORD || type == REG_BINARY)
            {
                DWORD registryValue = 0;
                std::memcpy(&registryValue, gRegistryStringData, sizeof(registryValue));
                ::_itoa(static_cast<int>(registryValue),
                        reinterpret_cast<char*>(gRegistryStringData), 10);
                return gRegistryStringData[0]
                    ? STRING(reinterpret_cast<const char*>(gRegistryStringData))
                    : STRING();
            }

            if (type == REG_SZ)
            {
                return gRegistryStringData[0]
                    ? STRING(reinterpret_cast<const char*>(gRegistryStringData))
                    : STRING();
            }
        }

        return defaultValue.isEmpty() ? STRING() : STRING(defaultValue);
    }

    int STRING::ReadRegistryInt(const STRING& valueName, int defaultValue) const
    {
        DWORD root = 0x80000002u;
        STRING subKey = ExtractRegistrySubkey(&root);
        int result = defaultValue;

        HKEY opened = nullptr;
        if (::RegOpenKeyExA(reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(root)),
                            subKey.c_str(), 0, KEY_QUERY_VALUE, &opened) == ERROR_SUCCESS)
        {
            DWORD type = 0;
            DWORD cbData = 0x1FFu;
            (void)::RegQueryValueExA(opened, valueName.c_str(), nullptr, &type,
                                     gRegistryIntegerData, &cbData);

            if (type == REG_SZ)
                result = std::atoi(reinterpret_cast<const char*>(gRegistryIntegerData));
            else if (type == REG_DWORD || type == REG_BINARY)
                std::memcpy(&result, gRegistryIntegerData, sizeof(result));

            ::RegCloseKey(opened);
        }

        return result;
    }


    void STRING::WriteRegistryString(const STRING& valueName, const STRING& value) const
    {
        DWORD root = 0x80000002u;
        STRING subKey = ExtractRegistrySubkey(&root);

        HKEY opened = nullptr;
        DWORD disposition = 0;
        if (::RegCreateKeyExA(reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(root)),
                              subKey.c_str(), 0, const_cast<LPSTR>(""), 0,
                              0xF003Fu, nullptr, &opened, &disposition) == ERROR_SUCCESS)
        {
            const char* const valueText = value.c_str();
            (void)::RegSetValueExA(opened, valueName.c_str(), 0, REG_SZ,
                                   reinterpret_cast<const BYTE*>(valueText),
                                   static_cast<DWORD>(std::strlen(valueText) + 1u));
            ::RegCloseKey(opened);
        }
    }

    void STRING::WriteRegistryInt(const STRING& valueName, int value) const
    {
        DWORD root = 0x80000002u;
        STRING subKey = ExtractRegistrySubkey(&root);

        HKEY opened = nullptr;
        DWORD disposition = 0;
        if (::RegCreateKeyExA(reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(root)),
                              subKey.c_str(), 0, const_cast<LPSTR>(""), 0,
                              0xF003Fu, nullptr, &opened, &disposition) == ERROR_SUCCESS)
        {
            const DWORD data = static_cast<DWORD>(value);
            (void)::RegSetValueExA(opened, valueName.c_str(), 0, REG_DWORD,
                                   reinterpret_cast<const BYTE*>(&data), 4u);
            ::RegCloseKey(opened);
        }
    }


    void STRING::DeleteRegistryValue(const STRING& valueName) const
    {
        DWORD root = 0x80000002u;
        STRING subKey = ExtractRegistrySubkey(&root);

        HKEY opened = nullptr;
        if (::RegOpenKeyExA(reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(root)),
                            subKey.c_str(), 0, 0xF003Fu, &opened) == ERROR_SUCCESS)
        {
            (void)::RegDeleteValueA(opened, valueName.c_str());
            ::RegCloseKey(opened);
        }
    }


    void STRING::ToWideChar(wchar_t* out, int count) const
    {
        (void)::MultiByteToWideChar(0, 0, m_text, -1, out, count);
    }


    int STRING::ResetAndAssign(const STRING& other)
    {
        Assign("");
        Assign(other);
        return 0;
    }


    int STRING::ReadProfileInt(const STRING& section, const STRING& key, int defaultValue) const
    {
        return static_cast<int>(::GetPrivateProfileIntA(section.c_str(), key.c_str(), defaultValue, c_str()));
    }


    STRING STRING::ReadProfileString(const STRING& section, const STRING& key, const STRING& defaultValue) const
    {
        char buffer[0x8000];
        ::GetPrivateProfileStringA(section.c_str(), key.c_str(), defaultValue.c_str(), buffer, 0x7FFFu, c_str());
        return buffer[0] ? STRING(buffer) : STRING();
    }
    STRING& STRING::Assign(const STRING& other)
    {
        if (this == &other)
            return *this;

        const char* const sourceText = other.m_text;
        const std::size_t sourceLen = std::strlen(sourceText);
        char* const oldText = m_text;

        if (oldText == SharedEmptyText())
        {
            if (sourceLen == 0u)
                return *this;
            m_text = static_cast<char*>(::operator new(stringAllocationSize(sourceLen)));
        }
        else if (sourceLen == 0u)
        {
            ::operator delete(oldText);
            m_text = SharedEmptyText();
            return *this;
        }
        else
        {
            const std::size_t oldLen = std::strlen(oldText);
            if (!sameStringBucket(oldLen, sourceLen))
            {
                ::operator delete(oldText);
                m_text = static_cast<char*>(::operator new(stringAllocationSize(sourceLen)));
            }
        }

        std::memcpy(m_text, sourceText, sourceLen + 1u);
        return *this;
    }

    STRING& constructFormattedString(STRING& destination, const char* format, ...)
    {

        char formattedText[0x1000];
        formattedText[0] = '\0';
        std::memset(formattedText + 1, 0, sizeof(formattedText) - 1);

        va_list args;
        va_start(args, format);
        ::vsprintf(formattedText, format, args);
        va_end(args);

        if (formattedText[0] == '\0')
        {
            destination.m_text = STRING::SharedEmptyText();
            return destination;
        }

        const std::size_t textLen = std::strlen(formattedText);
        char* owner = static_cast<char*>(::operator new(stringAllocationSize(textLen)));
        destination.m_text = owner;
        std::memcpy(owner, formattedText, textLen);
        owner[textLen] = '\0';
        return destination;
    }


    STRING& STRING::Assign(const char* text)
    {
        const std::size_t newLen = std::strlen(text);
        char* const oldText = m_text;

        if (oldText == SharedEmptyText())
        {
            if (newLen == 0u)
                return *this;
            m_text = static_cast<char*>(::operator new(stringAllocationSize(newLen)));
        }
        else if (newLen == 0u)
        {
            ::operator delete(oldText);
            m_text = SharedEmptyText();
            return *this;
        }
        else
        {
            const std::size_t oldLen = std::strlen(oldText);
            if (!sameStringBucket(oldLen, newLen))
            {
                ::operator delete(oldText);
                m_text = static_cast<char*>(::operator new(stringAllocationSize(newLen)));
            }
        }

        std::memcpy(m_text, text, newLen + 1u);
        return *this;
    }


    STRING& STRING::append(const STRING& other)
    {
        const char* sourceText = other.m_text;
        if (*sourceText == '\0')
            return *this;

        char* const oldText = m_text;
        const std::size_t oldLen = std::strlen(oldText);
        const std::size_t sourceLen = std::strlen(sourceText);
        const std::size_t combinedLen = oldLen + sourceLen;

        if (oldText == SharedEmptyText() || !sameStringBucket(oldLen, combinedLen))
        {
            char* const replacement = static_cast<char*>(::operator new(stringAllocationSize(combinedLen)));
            if (oldLen != 0u)
                std::memcpy(replacement, oldText, oldLen);
            m_text = replacement;
            if (oldText != SharedEmptyText())
                ::operator delete(oldText);
        }

        sourceText = other.m_text;
        std::memcpy(m_text + oldLen, sourceText, sourceLen + 1u);
        return *this;
    }


    STRING& STRING::append(const char* text)
    {
        if (*text == '\0')
            return *this;

        char* const oldText = m_text;
        const std::size_t oldLen = std::strlen(oldText);
        const std::size_t sourceLen = std::strlen(text);
        const std::size_t combinedLen = oldLen + sourceLen;

        if (oldText == SharedEmptyText() || !sameStringBucket(oldLen, combinedLen))
        {
            char* const replacement = static_cast<char*>(::operator new(stringAllocationSize(combinedLen)));
            if (oldLen != 0u)
                std::memcpy(replacement, oldText, oldLen);
            m_text = replacement;
            if (oldText != SharedEmptyText())
                ::operator delete(oldText);
        }

        std::memcpy(m_text + oldLen, text, sourceLen + 1u);
        return *this;
    }


    FILE* FOpen(const STRING* name, const char* mode)
    {
        return name->c_str()[0] ? std::fopen(name->c_str(), mode) : nullptr;
    }


}
