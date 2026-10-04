#pragma once
#include "core/resource_filter.h"
#include <cstdint>
#include <cstdio>

namespace as1
{
    namespace script
    {
        class R_CODER
        {
        public:
            void start_encoding(std::uint8_t firstByte, int position, FILE* file);
            int EncodeShift(int lowCount, int base, int shift);
            int EndEncoding();

            int StartDecoding(FILE* file);
            int DecodeCulShift(int shift);
            int DecodeUpdate(int count, int base, unsigned total);

            std::uint32_t low() const { return m_low; }
            std::uint32_t range() const { return m_range; }
            std::uint32_t step() const { return m_step; }
            std::uint32_t position() const { return m_position; }
            std::uint8_t currentByte() const { return m_currentByte; }
            FILE* file() const { return m_file; }

        private:
            friend class QS1_CODER;
            __forceinline void OutByte(int value)
            {
                std::fputc(value & 0xFF, m_file);
            }

            __forceinline int InByte()
            {
                return std::fgetc(m_file);
            }

            __forceinline void enc_normalize()
            {
                while (m_range <= 0x800000u)
                {
                    std::uint32_t low = m_low;
                    if (low >= 0x7F800000u)
                    {
                        if (low & 0x80000000u)
                        {
                            OutByte(static_cast<int>(m_currentByte) + 1);
                            while (m_step)
                            {
                                OutByte(0);
                                --m_step;
                            }
                            low = m_low;
                            m_currentByte = static_cast<std::uint8_t>(low >> 23);
                        }
                        else
                        {
                            ++m_step;
                        }
                    }
                    else
                    {
                        OutByte(m_currentByte);
                        while (m_step)
                        {
                            OutByte(0xFF);
                            --m_step;
                        }
                        low = m_low;
                        m_currentByte = static_cast<std::uint8_t>(low >> 23);
                    }
                    m_range <<= 8;
                    m_low = (low & 0x7FFFFFu) << 8;
                    ++m_position;
                }
            }

            __forceinline void dec_normalize()
            {
                while (m_range <= 0x800000u)
                {
                    m_low = ((2u * m_low) | (m_currentByte & 1u)) << 7;
                    const int ch = InByte();
                    m_currentByte = static_cast<std::uint8_t>(ch);
                    m_low |= static_cast<std::uint32_t>(m_currentByte) >> 1;
                    m_range <<= 8;
                }
            }

            std::uint32_t m_low = 0;
            std::uint32_t m_range = 0;
            std::uint32_t m_step = 0;
            std::uint8_t m_currentByte = 0;
            std::uint32_t m_position = 0;
            FILE* m_file = nullptr;
        };


        class QSMODEL
        {
        public:
            int noSym;
            int left;
            int nextLeft;
            int rescaleInterval;
            int targetRescale;
            int increment;
            int searchShift;
            std::uint16_t* cumulative;
            std::uint16_t* frequency;
            std::uint16_t* lookup;


            QSMODEL();

            ~QSMODEL();

            void dorescale();

            void Init(int symbols, int shift, int rescale, int* initial);

            void Reset(int* initial);

            int GetSym(int count);

            void GetFreq(int sym, int* freq, int* cumulativeFreq);

            void Update(int sym);
        };

        class QS1_CODER : public as1::Filter
        {
        public:
            int byteInWord;
            QSMODEL model[256];

            __forceinline explicit QS1_CODER(int mode) : byteInWord(mode) {}
            __forceinline ~QS1_CODER() override {}
            void Reset() override;
            int Encode(const void* data, unsigned long size, FILE* file) override;
            int Decode(void* data, unsigned long size, FILE* file) override;
        };

    }
}
