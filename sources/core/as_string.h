#pragma once
#include "types.h"
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <iosfwd>
#include <string>
#include <cwchar>
#include <cstring>
#include <new>

namespace as1
{
    class BaseStream;
    extern char gSharedEmptyStringStorage;

    class STRING
    {
    public:
        __forceinline STRING() : m_text(SharedEmptyText()) {}
        STRING(const char* s);
        STRING(const char* left, const char* right);
        STRING(const char* text, int length);
        __forceinline STRING(const STRING& other)
        {
            const char* const source = other.m_text;
            if (*source == '\0')
            {
                m_text = SharedEmptyText();
                return;
            }
            const std::size_t length = std::strlen(source);
            m_text = static_cast<char*>(::operator new((length & ~std::size_t(0x0F)) + 0x10));
            std::memcpy(m_text, source, length);
            m_text[length] = '\0';
        }
        ~STRING();

        __forceinline STRING& operator=(const STRING& other) { return Assign(other); }
        __forceinline STRING& operator=(const char* text) { return Assign(text); }

        __forceinline const char* c_str() const noexcept { return m_text; }
        __forceinline bool isEmpty() const noexcept { return *m_text == '\0'; }

        int Length() const;

        int Int() const;
        STRING ToLower() const;
        STRING ToUpper() const;
        STRING ToBase64(int add) const;

        void Read(BaseStream* stream);

        void Write(BaseStream* stream) const;

        void Write(FILE* file) const;

        STRING& ReadLine(FILE* file);


        int loadFromFile(const STRING& path);



        __forceinline STRING operator+(const char* rhs) const { return STRING(m_text, rhs); }

        __forceinline int operator!=(const char* rhs) const { return std::strcmp(m_text, rhs) != 0; }

        STRING& Assign(const STRING& other);
        STRING& Assign(const char* text);

        STRING& append(const STRING& other);
        STRING& append(const char* text);
        __forceinline STRING& operator+=(const STRING& other) { return append(other); }
        __forceinline STRING& operator+=(const char* text) { return append(text); }

        STRING& RemoveEndChars(const char* chars);
        int Replace(const STRING& source, const STRING& dest) { return Replace(source.c_str(), dest.c_str()); }
        int Replace(const STRING* source, const STRING* dest) { return Replace(source->c_str(), dest->c_str()); }

        int Replace(const char* source, const char* dest);


        STRING Before(const char* marker) const;

        STRING After(const char* marker) const;

        STRING BeforeLast(const char* marker) const;

        STRING AfterLast(const char* marker) const;
        STRING IncrementTrailingNumber(int delta) const;

        STRING ExtractRegistrySubkey(DWORD* rootKey) const;
        STRING ReadRegistryString(const STRING& valueName, const STRING& defaultValue) const;
        int ReadRegistryInt(const STRING& valueName, int defaultValue) const;
        void WriteRegistryString(const STRING& valueName, const STRING& value) const;
        void WriteRegistryInt(const STRING& valueName, int value) const;
        void DeleteRegistryValue(const STRING& valueName) const;
        void ToWideChar(wchar_t* out, int count) const;
        int ReadProfileInt(const STRING& section, const STRING& key, int defaultValue) const;
        STRING ReadProfileString(const STRING& section, const STRING& key, const STRING& defaultValue) const;
        int ResetAndAssign(const STRING& other);


        static __forceinline char* SharedEmptyText() { return &gSharedEmptyStringStorage; }

        friend STRING& constructFormattedString(STRING& destination, const char* format, ...);
        friend STRING& constructCurrentTimeString(STRING& destination);
        friend STRING& constructCurrentDateString(STRING& destination);
        friend STRING& constructTemporaryNameString(STRING& destination, const char* directory, const char* prefix);

        __forceinline void ReleaseOwnedStorage()
        {
            if (m_text && m_text != SharedEmptyText())
                ::operator delete(m_text);
            m_text = SharedEmptyText();
        }


        __forceinline char* DetachOwnedStorage() noexcept
        {
            char* const owner = m_text;
            m_text = SharedEmptyText();
            return owner;
        }
        __forceinline void AdoptOwnedStorage(char* owner) noexcept { m_text = owner; }

        __forceinline void ResetSharedEmptyWithoutRelease() noexcept { m_text = SharedEmptyText(); }


    private:
        char* m_text;
    };

    STRING& constructFormattedString(STRING& destination, const char* format, ...);
    STRING& constructCurrentTimeString(STRING& destination);
    STRING& constructCurrentDateString(STRING& destination);
    STRING& constructFileTimestampString(STRING& destination, const STRING& fileName);
    STRING& constructTemporaryNameString(STRING& destination, const char* directory, const char* prefix);
    __forceinline STRING operator+(const char* lhs, const STRING& rhs) { return STRING(lhs, rhs.c_str()); }

    FILE* FOpen(const STRING* name, const char* mode);

}
