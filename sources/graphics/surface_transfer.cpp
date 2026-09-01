#include "graphics/surface_transfer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cfenv>
#include <limits>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace as1
{
#if defined(_MSC_VER) && defined(_M_IX86)
#define AS1_SURFACE_THISCALL_BRIDGE __fastcall
#elif defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
#define AS1_SURFACE_THISCALL_BRIDGE __attribute__((fastcall))
#else
#define AS1_SURFACE_THISCALL_BRIDGE
#endif
    namespace
    {
        constexpr DWORD kInvalidCall = 0x8876086Cu;
        constexpr DWORD kFail = 0x80004005u;
        constexpr DWORD kOutOfMemory = 0x8007000Eu;
        constexpr DWORD kNotImplemented = 0x80004001u;

        constexpr DWORD kFmtR8G8B8 = 20u;
        constexpr DWORD kFmtA8R8G8B8 = 21u;
        constexpr DWORD kFmtX8R8G8B8 = 22u;
        constexpr DWORD kFmtR5G6B5 = 23u;
        constexpr DWORD kFmtX1R5G5B5 = 24u;
        constexpr DWORD kFmtA1R5G5B5 = 25u;
        constexpr DWORD kFmtA4R4G4B4 = 26u;
        constexpr DWORD kFmtR3G3B2 = 27u;
        constexpr DWORD kFmtA8 = 28u;
        constexpr DWORD kFmtA8R3G3B2 = 29u;
        constexpr DWORD kFmtX4R4G4B4 = 30u;
        constexpr DWORD kFmtA8P8 = 40u;
        constexpr DWORD kFmtP8 = 41u;
        constexpr DWORD kFmtL8 = 50u;
        constexpr DWORD kFmtA8L8 = 51u;
        constexpr DWORD kFmtA4L4 = 52u;
        constexpr DWORD kFmtV8U8 = 60u;
        constexpr DWORD kFmtL6V5U5 = 61u;
        constexpr DWORD kFmtX8L8V8U8 = 62u;
        constexpr DWORD kFmtQ8W8V8U8 = 63u;
        constexpr DWORD kFmtV16U16 = 64u;
        constexpr DWORD kFmtW11V11U10 = 65u;
        constexpr DWORD kFmtDXT1 = 0x31545844u;
        constexpr DWORD kFmtDXT2 = 0x32545844u;
        constexpr DWORD kFmtYUY2 = 0x32595559u;
        constexpr DWORD kFmtDXT3 = 0x33545844u;
        constexpr DWORD kFmtDXT4 = 0x34545844u;
        constexpr DWORD kFmtDXT5 = 0x35545844u;
        constexpr DWORD kFmtUYVY = 0x59565955u;

        constexpr DWORD kFilterNone = 1u;
        constexpr DWORD kFilterPoint = 2u;
        constexpr DWORD kFilterLinear = 3u;
        constexpr DWORD kFilterTriangle = 4u;
        constexpr DWORD kFilterBox = 5u;
        constexpr DWORD kFilterDither = 0x00080000u;

        constexpr float kRoundMatrix[32] = {
            0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
            0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
            0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
            0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
        };
        constexpr float kDitherMatrix[32] = {
            0.96875f, 0.46875f, 0.84375f, 0.34375f, 0.96875f, 0.46875f, 0.84375f, 0.34375f,
            0.21875f, 0.71875f, 0.09375f, 0.59375f, 0.21875f, 0.71875f, 0.09375f, 0.59375f,
            0.78125f, 0.28125f, 0.90625f, 0.40625f, 0.78125f, 0.28125f, 0.90625f, 0.40625f,
            0.03125f, 0.53125f, 0.15625f, 0.65625f, 0.03125f, 0.53125f, 0.15625f, 0.65625f,
        };

        constexpr DWORD kPlainObjectSize = 0x1064u;
        constexpr DWORD kPackedYuvObjectSize = 0x1094u;
        constexpr DWORD kBlockObjectSize = 0x10A8u;
        constexpr DWORD kPlainConstructor = 0x00452588u;
        constexpr DWORD kPackedYuvConstructor = 0x00454398u;
        constexpr DWORD kBlockConstructor = 0x00454A14u;
        constexpr DWORD kBaseDescriptorVtable = 0x00473A9Cu;
        constexpr DWORD kPackedDescriptorVtable = 0x00473B10u;
        constexpr DWORD kBlockDescriptorVtable = 0x00473B50u;
        constexpr DWORD kRoundMatrixRetailAddress = 0x0047DB38u;
        constexpr DWORD kDitherMatrixRetailAddress = 0x0047DBB8u;

        struct Pixel
        {
            float r;
            float g;
            float b;
            float a;
        };


        struct FormatInfo
        {
            DWORD format = 0;
            int category = 0;
            int bitsPerPixel = 0;
            int bytesPerPixel = 0;
            int blockBytes = 0;
            bool compressed = false;
            bool packedYuv = false;
            bool palette = false;
            bool directRowMemory = false;
            bool supported = false;
            DWORD originalObjectSize = 0;
            DWORD originalVtable = 0;
            DWORD originalConstructor = 0;
        };

        struct MemoryView
        {
            BYTE* bits = nullptr;
            int pitch = 0;
            DWORD format = 0;
            RECTI rect{};
            DWORD fullWidth = 0;
            DWORD fullHeight = 0;
            const BYTE* palette = nullptr;
            bool dither = false;
        };

        struct TriangleAccumulationNode
        {
            Pixel* pixels = nullptr;
            float completion = 0.0f;
            TriangleAccumulationNode* next = nullptr;
        };

        struct TriangleWeightPair
        {
            DWORD index = 0;
            float weight = 0.0f;
        };


        struct LinearAxisEntry
        {
            DWORD firstIndex = 0;
            float firstWeight = 0.0f;
            DWORD secondIndex = 0;
            float secondWeight = 0.0f;
        };



        bool validRect(const RECTI& rect)
        {
            return rect.left >= 0 && rect.top >= 0 && rect.right >= rect.left && rect.bottom >= rect.top;
        }

        int rectWidth(const RECTI& rect)
        {
            return rect.right - rect.left;
        }

        int rectHeight(const RECTI& rect)
        {
            return rect.bottom - rect.top;
        }

        FormatInfo formatInfo(DWORD format)
        {

            FormatInfo out{};
            out.format = format;
            auto plain = [&](int category, int bits, int bytes, bool palette, DWORD vtable)
            {
                out.category = category;
                out.bitsPerPixel = bits;
                out.bytesPerPixel = bytes;
                out.palette = palette;
                out.directRowMemory = true;
                out.supported = true;
                out.originalObjectSize = kPlainObjectSize;
                out.originalVtable = vtable;
                out.originalConstructor = kPlainConstructor;
            };
            auto packedYuv = [&](DWORD vtable)
            {
                out.category = 1;
                out.bitsPerPixel = 16;
                out.bytesPerPixel = 2;
                out.packedYuv = true;
                out.supported = true;
                out.originalObjectSize = kPackedYuvObjectSize;
                out.originalVtable = vtable;
                out.originalConstructor = kPackedYuvConstructor;
            };
            auto block = [&](int blockBytes, DWORD vtable)
            {
                out.category = 1;
                out.blockBytes = blockBytes;
                out.compressed = true;
                out.supported = true;
                out.originalObjectSize = kBlockObjectSize;
                out.originalVtable = vtable;
                out.originalConstructor = kBlockConstructor;
            };

            switch (format)
            {
            case kFmtR8G8B8: plain(1, 24, 3, false, 0x00473B5Cu); break;
            case kFmtA8R8G8B8: plain(1, 32, 4, false, 0x00473B68u); break;
            case kFmtX8R8G8B8: plain(1, 32, 4, false, 0x00473B74u); break;
            case kFmtR5G6B5: plain(1, 16, 2, false, 0x00473B80u); break;
            case kFmtX1R5G5B5: plain(1, 16, 2, false, 0x00473B8Cu); break;
            case kFmtA1R5G5B5: plain(1, 16, 2, false, 0x00473B98u); break;
            case kFmtA4R4G4B4: plain(1, 16, 2, false, 0x00473BA4u); break;
            case kFmtR3G3B2: plain(1, 8, 1, false, 0x00473BB0u); break;
            case kFmtA8: plain(1, 8, 1, false, 0x00473BBCu); break;
            case kFmtA8R3G3B2: plain(1, 16, 2, false, 0x00473BC8u); break;
            case kFmtX4R4G4B4: plain(1, 16, 2, false, 0x00473BD4u); break;
            case kFmtA8P8: plain(2, 16, 2, true, 0x00473BE0u); break;
            case kFmtP8: plain(2, 8, 1, true, 0x00473BECu); break;
            case kFmtL8: plain(1, 8, 1, false, 0x00473BF8u); break;
            case kFmtA8L8: plain(1, 16, 2, false, 0x00473C04u); break;
            case kFmtA4L4: plain(1, 8, 1, false, 0x00473C10u); break;
            case kFmtV8U8: plain(3, 16, 2, false, 0x00473C1Cu); break;
            case kFmtL6V5U5: plain(3, 16, 2, false, 0x00473C28u); break;
            case kFmtX8L8V8U8: plain(3, 32, 4, false, 0x00473C34u); break;
            case kFmtQ8W8V8U8: plain(3, 32, 4, false, 0x00473C40u); break;
            case kFmtV16U16: plain(3, 32, 4, false, 0x00473C4Cu); break;
            case kFmtW11V11U10: plain(3, 32, 4, false, 0x00473C58u); break;
            case kFmtUYVY: packedYuv(0x00473C64u); break;
            case kFmtYUY2: packedYuv(0x00473C70u); break;
            case kFmtDXT1: block(8, 0x00473C7Cu); break;
            case kFmtDXT2: block(16, 0x00473C88u); break;
            case kFmtDXT3: block(16, 0x00473C94u); break;
            case kFmtDXT4: block(16, 0x00473CA0u); break;
            case kFmtDXT5: block(16, 0x00473CACu); break;
            default: break;
            }
            return out;
        }

        constexpr DWORD kSurfaceFormatDescriptors[30][10] = {
            {0x00000014u, 0x00000000u, 0x00000018u, 0x00000000u, 0x00000000u, 0x00000008u, 0x00000008u, 0x00000008u, 0x00000000u, 0x00000001u},
            {0x00000015u, 0x00000000u, 0x00000020u, 0x00000000u, 0x00000008u, 0x00000008u, 0x00000008u, 0x00000008u, 0x00000001u, 0x00000001u},
            {0x00000016u, 0x00000000u, 0x00000020u, 0x00000000u, 0x00000000u, 0x00000008u, 0x00000008u, 0x00000008u, 0x00000001u, 0x00000001u},
            {0x00000017u, 0x00000000u, 0x00000010u, 0x00000000u, 0x00000000u, 0x00000005u, 0x00000006u, 0x00000005u, 0x00000001u, 0x00000001u},
            {0x00000018u, 0x00000000u, 0x00000010u, 0x00000000u, 0x00000000u, 0x00000005u, 0x00000005u, 0x00000005u, 0x00000001u, 0x00000001u},
            {0x00000028u, 0x00000001u, 0x00000010u, 0x00000000u, 0x00000008u, 0x00000008u, 0x00000008u, 0x00000008u, 0x00000001u, 0x00000001u},
            {0x00000029u, 0x00000001u, 0x00000008u, 0x00000000u, 0x00000008u, 0x00000008u, 0x00000008u, 0x00000008u, 0x00000001u, 0x00000001u},
            {0x00000019u, 0x00000000u, 0x00000010u, 0x00000000u, 0x00000001u, 0x00000005u, 0x00000005u, 0x00000005u, 0x00000001u, 0x00000001u},
            {0x0000001Au, 0x00000000u, 0x00000010u, 0x00000000u, 0x00000004u, 0x00000004u, 0x00000004u, 0x00000004u, 0x00000001u, 0x00000001u},
            {0x0000001Bu, 0x00000000u, 0x00000008u, 0x00000000u, 0x00000000u, 0x00000003u, 0x00000003u, 0x00000002u, 0x00000001u, 0x00000001u},
            {0x0000001Cu, 0x00000000u, 0x00000008u, 0x00000000u, 0x00000008u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000001u, 0x00000001u},
            {0x0000001Du, 0x00000000u, 0x00000010u, 0x00000000u, 0x00000008u, 0x00000003u, 0x00000003u, 0x00000002u, 0x00000001u, 0x00000001u},
            {0x0000001Eu, 0x00000000u, 0x00000010u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000004u, 0x00000004u, 0x00000001u, 0x00000001u},
            {0x00000032u, 0x00000002u, 0x00000008u, 0x00000000u, 0x00000000u, 0x00000008u, 0x00000008u, 0x00000008u, 0x00000001u, 0x00000001u},
            {0x00000033u, 0x00000002u, 0x00000010u, 0x00000000u, 0x00000008u, 0x00000008u, 0x00000008u, 0x00000008u, 0x00000001u, 0x00000001u},
            {0x00000034u, 0x00000002u, 0x00000008u, 0x00000000u, 0x00000004u, 0x00000004u, 0x00000004u, 0x00000004u, 0x00000001u, 0x00000001u},
            {0x0000003Cu, 0x00000003u, 0x00000010u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000008u, 0x00000008u, 0x00000001u, 0x00000001u},
            {0x0000003Du, 0x00000003u, 0x00000010u, 0x00000006u, 0x00000000u, 0x00000000u, 0x00000005u, 0x00000005u, 0x00000001u, 0x00000001u},
            {0x0000003Eu, 0x00000003u, 0x00000020u, 0x00000008u, 0x00000000u, 0x00000000u, 0x00000008u, 0x00000008u, 0x00000001u, 0x00000001u},
            {0x0000003Fu, 0x00000003u, 0x00000020u, 0x00000000u, 0x00000008u, 0x00000008u, 0x00000008u, 0x00000008u, 0x00000001u, 0x00000001u},
            {0x00000040u, 0x00000003u, 0x00000020u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000010u, 0x00000010u, 0x00000001u, 0x00000001u},
            {0x00000041u, 0x00000003u, 0x00000020u, 0x00000000u, 0x00000000u, 0x0000000Bu, 0x0000000Bu, 0x0000000Au, 0x00000001u, 0x00000001u},
            {0x59565955u, 0x00000000u, 0x00000010u, 0x00000000u, 0x00000000u, 0x00000008u, 0x00000008u, 0x00000008u, 0x00000000u, 0x00000001u},
            {0x32595559u, 0x00000000u, 0x00000010u, 0x00000000u, 0x00000000u, 0x00000008u, 0x00000008u, 0x00000008u, 0x00000000u, 0x00000001u},
            {0x31545844u, 0x00000000u, 0x00000010u, 0x00000000u, 0x00000001u, 0x00000005u, 0x00000006u, 0x00000005u, 0x00000000u, 0x00000001u},
            {0x32545844u, 0x00000000u, 0x00000010u, 0x00000000u, 0x00000004u, 0x00000005u, 0x00000006u, 0x00000005u, 0x00000000u, 0x00000001u},
            {0x33545844u, 0x00000000u, 0x00000010u, 0x00000000u, 0x00000004u, 0x00000005u, 0x00000006u, 0x00000005u, 0x00000000u, 0x00000001u},
            {0x34545844u, 0x00000000u, 0x00000010u, 0x00000000u, 0x00000003u, 0x00000005u, 0x00000006u, 0x00000005u, 0x00000000u, 0x00000001u},
            {0x35545844u, 0x00000000u, 0x00000010u, 0x00000000u, 0x00000003u, 0x00000005u, 0x00000006u, 0x00000005u, 0x00000000u, 0x00000001u},
            {0x00000000u, 0x00000000u, 0x00000020u, 0x00000000u, 0x00000008u, 0x00000008u, 0x00000008u, 0x00000008u, 0x00000000u, 0x00000000u},
        };

        const DWORD* findSurfaceFormatEntry(DWORD format) noexcept
        {
            for (std::size_t i = 0; i < 29; ++i)
            {
                if (kSurfaceFormatDescriptors[i][0] == format)
                    return kSurfaceFormatDescriptors[i];
            }
            return kSurfaceFormatDescriptors[29];
        }

        bool wholeSurfaceLockFormat(DWORD format)
        {
            const FormatInfo info = formatInfo(format);
            return info.compressed || info.packedYuv;
        }

        DWORD normalizeFilter(DWORD filter, DWORD sourceFormat)
        {
            if (filter != 0xFFFFFFFFu)
                return filter;
            const FormatInfo source = formatInfo(sourceFormat);
            return kFilterDither | (source.category == 3 ? kFilterPoint : kFilterTriangle);
        }

        bool validFilter(DWORD filter)
        {
            const DWORD type = filter & 0xFFFFu;
            return type >= kFilterNone && type <= kFilterBox && (filter & 0xFFF00000u) == 0u;
        }

        float clamp01(float value)
        {
            return std::max(0.0f, std::min(1.0f, value));
        }

        int quantize(float value, int maximum, float dither)
        {
            const float scaled = value * static_cast<float>(maximum) + dither;
            const int converted = static_cast<int>(scaled);
            return std::max(0, std::min(maximum, converted));
        }

        int signExtend(unsigned value, unsigned bits)
        {
            const unsigned shift = 32u - bits;
            return static_cast<int>(value << shift) >> shift;
        }

        Pixel palettePixel(const BYTE* palette, unsigned index)
        {
            if (!palette)
                return {1.0f, 1.0f, 1.0f, 1.0f};
            const BYTE* entry = palette + (index & 0xFFu) * 4u;
            constexpr float scale = 1.0f / 255.0f;
            return {
                static_cast<float>(entry[0]) * scale,
                static_cast<float>(entry[1]) * scale,
                static_cast<float>(entry[2]) * scale,
                static_cast<float>(entry[3]) * scale,
            };
        }

        bool paletteEquivalent(const BYTE* left, const BYTE* right)
        {
            if (left == right)
                return true;
            if (!left || !right)
            {
                const BYTE* existing = left ? left : right;
                if (!existing)
                    return true;
                for (unsigned i = 0; i < 256u; ++i)
                {
                    const BYTE* entry = existing + i * 4u;
                    if (entry[0] != 255u || entry[1] != 255u || entry[2] != 255u || entry[3] != 255u)
                        return false;
                }
                return true;
            }
            return std::memcmp(left, right, 1024u) == 0;
        }

        Pixel colorKeyPixel(DWORD colorKey)
        {
            constexpr float scale = 1.0f / 255.0f;
            return {
                static_cast<float>((colorKey >> 16) & 0xFFu) * scale,
                static_cast<float>((colorKey >> 8) & 0xFFu) * scale,
                static_cast<float>(colorKey & 0xFFu) * scale,
                static_cast<float>((colorKey >> 24) & 0xFFu) * scale,
            };
        }

        void applyColorKey(Pixel& pixel, DWORD colorKey)
        {
            if (colorKey == 0u)
                return;
            const Pixel key = colorKeyPixel(colorKey);
            if (pixel.r == key.r && pixel.g == key.g && pixel.b == key.b && pixel.a == key.a)
                pixel = Pixel{};
        }

        Pixel decode565(WORD value)
        {
            return {
                static_cast<float>((value >> 11) & 31u) / 31.0f,
                static_cast<float>((value >> 5) & 63u) / 63.0f,
                static_cast<float>(value & 31u) / 31.0f,
                1.0f,
            };
        }

        Pixel decodeDxtColor(WORD value)
        {
            return decode565(value);
        }

        DWORD decodeRgb565ToBgr(const WORD* source565, DWORD* outputBgr) noexcept
        {
            const WORD value = *source565;
            const DWORD b5 = value & 0x1Fu;
            const DWORD g6 = (value >> 5) & 0x3Fu;
            const DWORD r5 = (value >> 11) & 0x1Fu;
            const DWORD result =
                ((b5 << 3) | (b5 >> 2)) |
                (((g6 << 2) | (g6 >> 4)) << 8) |
                (((r5 << 3) | (r5 >> 2)) << 16);
            *outputBgr = result;
            return result;
        }

        float* bgrToWeightedColor(float* result, const BYTE* sourceBgr) noexcept
        {
            static constexpr float weights[3] = {0.0820000023f, 0.609399974f, 0.308600008f};
            constexpr float inv255 = 0.0039215689f;
            for (int i = 0; i < 3; ++i)
            {
#if defined(_MSC_VER) && defined(_M_IX86)
                int component = static_cast<int>(sourceBgr[i]);
                const float weight = weights[i];
                float value = 0.0f;
                __asm {
                    fild component
                    fmul weight
                    fmul inv255
                    fstp value
                }
                result[i] = value;
#else
                result[i] = static_cast<float>(sourceBgr[i]) * weights[i] * inv255;
#endif
            }
            return result + 3;
        }

        long long weightedColorToBgr(const float* sourceWeighted, BYTE* outputBgr) noexcept
        {
            static constexpr float weights[3] = {0.0820000023f, 0.609399974f, 0.308600008f};
            long long result = 0;
            for (int i = 0; i < 3; ++i)
            {
#if defined(_MSC_VER) && defined(_M_IX86)
                constexpr float scale255 = 255.0f;
                const float source = sourceWeighted[i];
                const float weight = weights[i];
                unsigned short savedControl = 0;
                unsigned short chopControl = 0;
                __asm fnstcw savedControl
                chopControl = static_cast<unsigned short>(savedControl | 0x0C00u);
                __asm {
                    fldcw chopControl
                    fld source
                    fdiv weight
                    fmul scale255
                    fistp qword ptr [result]
                    fldcw savedControl
                }
#else
                const long double value = static_cast<long double>(sourceWeighted[i]) /
                    static_cast<long double>(weights[i]) * 255.0L;
                result = static_cast<long long>(value);
#endif
                outputBgr[i] = static_cast<BYTE>(result);
            }
            return result;
        }

        WORD* encodeBgrToRgb565(const BYTE* sourceBgr, WORD* output565) noexcept
        {
            const WORD b = static_cast<WORD>(sourceBgr[0] >> 3);
            const WORD g = static_cast<WORD>(sourceBgr[1] >> 2);
            const WORD r = static_cast<WORD>(sourceBgr[2] >> 3);
            *output565 = static_cast<WORD>(b | (g << 5) | (r << 11));
            return output565;
        }

        constexpr float kDxtColorWeights[3] = {0.0820000023f, 0.609399974f, 0.308600008f};
        constexpr DWORD kDxtSelector4[4] = {0u, 2u, 3u, 1u};
        constexpr DWORD kDxtSelector3[4] = {0u, 2u, 1u, 3u};

        void squareDxtColorMatrix(const float* source, float* destination) noexcept
        {
            // Source/destination use the retail 3x4 upper-triangle scratch
            // layout: 00,01,02 / --,11,12 / --,--,22.
            destination[0] = source[0] * source[0] + source[1] * source[1] + source[2] * source[2];
            destination[1] = (source[4] + source[0]) * source[1] + source[5] * source[2];
            destination[2] = (source[8] + source[0]) * source[2] + source[5] * source[1];
            destination[4] = source[5] * source[5] + source[4] * source[4] + source[1] * source[1];
            destination[5] = (source[8] + source[4]) * source[5] + source[2] * source[1];
            destination[8] = source[8] * source[8] + source[5] * source[5] + source[2] * source[2];
        }

        void clipDxtColorEndpoints(float* first, float* second) noexcept
        {

            for (int component = 0; component < 3; ++component)
            {
                const bool firstBelow = first[component] < 0.0f;
                const bool secondBelow = second[component] < 0.0f;
                if (firstBelow != secondBelow)
                {
                    double t = -(static_cast<double>(first[component]) /
                        (static_cast<double>(second[component]) - first[component]));
                    float* endpoint = first;
                    if (!firstBelow)
                    {
                        endpoint = second;
                        t -= 1.0;
                    }
                    for (int i = 0; i < 3; ++i)
                        endpoint[i] = static_cast<float>((second[i] - first[i]) * t + endpoint[i]);
                }

                const float upper = kDxtColorWeights[component];
                const bool firstAbove = first[component] > upper;
                const bool secondAbove = second[component] > upper;
                if (firstAbove != secondAbove)
                {
                    double t = (static_cast<double>(upper) - first[component]) /
                        (static_cast<double>(second[component]) - first[component]);
                    float* endpoint = first;
                    if (!firstAbove)
                    {
                        endpoint = second;
                        t -= 1.0;
                    }
                    for (int i = 0; i < 3; ++i)
                        endpoint[i] = static_cast<float>((second[i] - first[i]) * t + endpoint[i]);
                }
            }
        }

        float* encodeDxtColorEndpoints(BYTE* output, float* first, float* second, int selectedCount) noexcept
        {
            BYTE bgr[4]{};
            weightedColorToBgr(first, bgr);
            encodeBgrToRgb565(bgr, reinterpret_cast<WORD*>(output));
            weightedColorToBgr(second, bgr);
            encodeBgrToRgb565(bgr, reinterpret_cast<WORD*>(output + 2));

            WORD c0 = 0;
            WORD c1 = 0;
            std::memcpy(&c0, output, sizeof(c0));
            std::memcpy(&c1, output + 2, sizeof(c1));
            if ((selectedCount == 16) != (c1 < c0))
            {
                std::swap(c0, c1);
                std::memcpy(output, &c0, sizeof(c0));
                std::memcpy(output + 2, &c1, sizeof(c1));
            }

            DWORD decoded = 0;
            decodeRgb565ToBgr(&c0, &decoded);
            bgrToWeightedColor(first, reinterpret_cast<const BYTE*>(&decoded));
            decodeRgb565ToBgr(&c1, &decoded);
            return bgrToWeightedColor(second, reinterpret_cast<const BYTE*>(&decoded));
        }

        WORD encodeDxtSolidColorBlock(const DWORD* pixels, BYTE* output, WORD selectedMask) noexcept
        {
            DWORD representative = pixels[0];
            encodeBgrToRgb565(reinterpret_cast<const BYTE*>(&representative), reinterpret_cast<WORD*>(output));
            WORD c0 = 0;
            std::memcpy(&c0, output, sizeof(c0));
            std::memcpy(output + 2, &c0, sizeof(c0));
            DWORD selectors = 0u;

            if (selectedMask != 0xFFFFu)
            {
                DWORD selectorPattern = 3u;
                WORD bit = 1u;
                for (int pixel = 0; pixel < 16; ++pixel)
                {
                    if ((selectedMask & bit) != 0u)
                        representative = pixels[pixel];
                    else
                        selectors |= selectorPattern;
                    bit = static_cast<WORD>(bit << 1u);
                    selectorPattern <<= 2u;
                }
                encodeBgrToRgb565(reinterpret_cast<const BYTE*>(&representative), reinterpret_cast<WORD*>(output));
                std::memcpy(&c0, output, sizeof(c0));
                std::memcpy(output + 2, &c0, sizeof(c0));
            }
            std::memcpy(output + 4, &selectors, sizeof(selectors));
            return c0;
        }

        WORD encodeDxtColorBlockMasked(const DWORD* pixels, BYTE* output, DWORD lowerRgb, DWORD upperRgb) noexcept
        {
            if (!output)
                return 0u;

            WORD selectedMask = 0u;
            int selectedCount = 0;
            for (int pixel = 15; pixel >= 0; --pixel)
            {
                selectedMask = static_cast<WORD>(selectedMask << 1u);
                const DWORD value = pixels[pixel];
                const BYTE b = static_cast<BYTE>(value & 0xFFu);
                const BYTE g = static_cast<BYTE>((value >> 8u) & 0xFFu);
                const BYTE r = static_cast<BYTE>((value >> 16u) & 0xFFu);
                const BYTE lowB = static_cast<BYTE>(lowerRgb & 0xFFu);
                const BYTE lowG = static_cast<BYTE>((lowerRgb >> 8u) & 0xFFu);
                const BYTE lowR = static_cast<BYTE>((lowerRgb >> 16u) & 0xFFu);
                const BYTE highB = static_cast<BYTE>(upperRgb & 0xFFu);
                const BYTE highG = static_cast<BYTE>((upperRgb >> 8u) & 0xFFu);
                const BYTE highR = static_cast<BYTE>((upperRgb >> 16u) & 0xFFu);
                if (lowR > r || r > highR || lowG > g || g > highG || lowB > b || b > highB)
                {
                    selectedMask = static_cast<WORD>(selectedMask | 1u);
                    ++selectedCount;
                }
            }

            if (selectedCount == 0)
            {
                const WORD c0 = 0u;
                const WORD c1 = 0xFFFFu;
                const DWORD selectors = 0xFFFFFFFFu;
                std::memcpy(output, &c0, sizeof(c0));
                std::memcpy(output + 2, &c1, sizeof(c1));
                std::memcpy(output + 4, &selectors, sizeof(selectors));
                return static_cast<WORD>(reinterpret_cast<std::uintptr_t>(output));
            }

            bool allSame = true;
            for (int pixel = 1; pixel < 16; ++pixel)
            {
                if ((pixels[pixel] & 0x00FFFFFFu) != (pixels[pixel - 1] & 0x00FFFFFFu))
                {
                    allSame = false;
                    break;
                }
            }
            if (allSame)
                return encodeDxtSolidColorBlock(pixels, output, selectedMask);

            float weighted[16][4]{};
            for (int pixel = 0; pixel < 16; ++pixel)
                bgrToWeightedColor(weighted[pixel], reinterpret_cast<const BYTE*>(&pixels[pixel]));

            float mean[3]{};
            const float reciprocalCount = 1.0f / static_cast<float>(selectedCount);
            for (int component = 0; component < 3; ++component)
            {
                float sum = 0.0f;
                WORD bit = 1u;
                for (int pixel = 0; pixel < 16; ++pixel, bit = static_cast<WORD>(bit << 1u))
                {
                    if ((selectedMask & bit) != 0u)
                        sum += weighted[pixel][component];
                }
                mean[component] = reciprocalCount * sum;
            }
            for (int component = 0; component < 3; ++component)
                for (int pixel = 0; pixel < 16; ++pixel)
                    weighted[pixel][component] -= mean[component];

            float matrix[9]{};
            matrix[0] = matrix[1] = matrix[2] = matrix[4] = matrix[5] = matrix[8] = 0.0f;
            WORD bit = 1u;
            for (int pixel = 0; pixel < 16; ++pixel, bit = static_cast<WORD>(bit << 1u))
            {
                if ((selectedMask & bit) == 0u)
                    continue;
                const float x = weighted[pixel][0];
                const float y = weighted[pixel][1];
                const float z = weighted[pixel][2];
                matrix[0] += x * x;
                matrix[1] += y * x;
                matrix[2] += z * x;
                matrix[4] += y * y;
                matrix[5] += z * y;
                matrix[8] += z * z;
            }

            float temporary[9]{};
            for (int iteration = 0; iteration < 9; ++iteration)
            {
                squareDxtColorMatrix(matrix, temporary);
                squareDxtColorMatrix(temporary, matrix);
                const double trace = static_cast<double>(matrix[0]) + matrix[4] + matrix[8];
                if (trace == 0.0)
                    return encodeDxtSolidColorBlock(pixels, output, selectedMask);
                const double normalize = 3.0 / trace;
                matrix[0] = static_cast<float>(matrix[0] * normalize);
                matrix[1] = static_cast<float>(matrix[1] * normalize);
                matrix[2] = static_cast<float>(matrix[2] * normalize);
                matrix[4] = static_cast<float>(matrix[4] * normalize);
                matrix[5] = static_cast<float>(matrix[5] * normalize);
                matrix[8] = static_cast<float>(matrix[8] * normalize);
            }

            matrix[3] = matrix[1];
            matrix[6] = matrix[2];
            matrix[7] = matrix[5];
            float largestDiagonal = 0.0f;
            int column = 0;
            for (int i = 0; i < 3; ++i)
            {
                const float diagonal = matrix[i * 3 + i];
                if (largestDiagonal < diagonal)
                {
                    largestDiagonal = diagonal;
                    column = i;
                }
            }
            if (largestDiagonal <= 0.0f)
                return encodeDxtSolidColorBlock(pixels, output, selectedMask);

            const double inverseLength = 1.0 / std::sqrt(static_cast<double>(largestDiagonal));
            float axis[3]{};
            for (int i = 0; i < 3; ++i)
                axis[i] = static_cast<float>(matrix[i * 3 + column] * inverseLength);
            double axisLengthSquared = 0.0;
            for (float value : axis)
                axisLengthSquared += static_cast<double>(value) * value;
            if (axisLengthSquared == 0.0)
                return encodeDxtSolidColorBlock(pixels, output, selectedMask);

            float minimumProjection = 99999.0f;
            float maximumProjection = -99999.0f;
            bit = 1u;
            for (int pixel = 0; pixel < 16; ++pixel, bit = static_cast<WORD>(bit << 1u))
            {
                if ((selectedMask & bit) == 0u)
                    continue;
                double projection = 0.0;
                for (int i = 0; i < 3; ++i)
                    projection += static_cast<double>(axis[i]) * weighted[pixel][i];
                projection /= axisLengthSquared;
                if (projection < minimumProjection)
                    minimumProjection = static_cast<float>(projection);
                if (projection > maximumProjection)
                    maximumProjection = static_cast<float>(projection);
            }

            float first[3]{};
            float second[3]{};
            for (int i = 0; i < 3; ++i)
            {
                first[i] = static_cast<float>(minimumProjection * axis[i] + mean[i]);
                second[i] = static_cast<float>(maximumProjection * axis[i] + mean[i]);
            }
            clipDxtColorEndpoints(first, second);
            encodeDxtColorEndpoints(output, first, second, selectedCount);

            double endpointLengthSquared = 0.0;
            for (int i = 0; i < 3; ++i)
            {
                const double delta = static_cast<double>(second[i]) - first[i];
                endpointLengthSquared += delta * delta;
            }
            if (endpointLengthSquared == 0.0 && selectedCount == 16)
                return encodeDxtSolidColorBlock(pixels, output, selectedMask);

            DWORD selectors = 0u;
            bit = 0x8000u;
            for (int pixel = 15; pixel >= 0; --pixel, bit = static_cast<WORD>(bit >> 1u))
            {
                selectors <<= 2u;
                DWORD selector = 3u;
                if ((selectedMask & bit) != 0u)
                {
                    double numerator = 0.0;
                    for (int i = 0; i < 3; ++i)
                    {
                        weighted[pixel][i] += mean[i];
                        numerator += (static_cast<double>(second[i]) - first[i]) *
                            (static_cast<double>(weighted[pixel][i]) - first[i]);
                    }
                    double coordinate = numerator / endpointLengthSquared;
                    if (selectedCount == 16)
                    {
                        coordinate *= 4.0;
                        if (coordinate < 0.0)
                            coordinate = 0.0;
                        else if (coordinate >= 4.0)
                            coordinate = 3.0;
                        selector = kDxtSelector4[static_cast<unsigned>(static_cast<std::int64_t>(coordinate))];
                    }
                    else
                    {
                        coordinate *= 3.0;
                        if (coordinate < 0.0)
                            coordinate = 0.0;
                        else if (coordinate >= 3.0)
                            coordinate = 2.0;
                        selector = kDxtSelector3[static_cast<unsigned>(static_cast<std::int64_t>(coordinate))];
                    }
                }
                selectors |= selector;
            }
            std::memcpy(output + 4, &selectors, sizeof(selectors));
            return static_cast<WORD>(selectors & 0xFFFFu);
        }

        WORD encodeDxtColorBlock(const DWORD* pixels, BYTE* output) noexcept
        {
            return encodeDxtColorBlockMasked(pixels, output, 0x00FFFFFFu, 0x00000000u);
        }

        WORD encodeDxt1Block(DWORD* pixels, BYTE* output) noexcept
        {
            int firstTransparent = 0;
            while (firstTransparent < 16 && static_cast<std::int32_t>(pixels[firstTransparent]) < 0)
                ++firstTransparent;
            if (firstTransparent == 16)
                return encodeDxtColorBlock(pixels, output);

            DWORD transparentRgb = 0u;
            for (;; ++transparentRgb)
            {
                int pixel = 0;
                for (; pixel < 16; ++pixel)
                {
                    const DWORD value = pixels[pixel];
                    if (static_cast<std::int32_t>(value) < 0 && (value & 0x00FFFFFFu) == transparentRgb)
                        break;
                }
                if (pixel == 16)
                    break;
            }
            for (int pixel = 0; pixel < 16; ++pixel)
            {
                if (static_cast<std::int32_t>(pixels[pixel]) >= 0)
                    pixels[pixel] = transparentRgb;
            }
            return encodeDxtColorBlockMasked(pixels, output, transparentRgb, transparentRgb);
        }

        WORD encodeDxt3Block(const DWORD* pixels, BYTE* output) noexcept
        {
            for (int row = 0; row < 4; ++row)
            {
                WORD packed = 0u;
                for (int column = 3; column >= 0; --column)
                {
                    packed = static_cast<WORD>(packed << 4u);
                    const BYTE alpha = static_cast<BYTE>((pixels[row * 4 + column] >> 24u) & 0xFFu);
                    packed = static_cast<WORD>(packed | (alpha >> 4u));
                }
                std::memcpy(output + row * 2, &packed, sizeof(packed));
            }
            return encodeDxtColorBlock(pixels, output + 8);
        }

        WORD encodeDxt5Block(const DWORD* pixels, BYTE* output) noexcept
        {
            BYTE minimum = static_cast<BYTE>((pixels[0] >> 24u) & 0xFFu);
            BYTE maximum = minimum;
            for (int pixel = 1; pixel < 16; ++pixel)
            {
                const BYTE alpha = static_cast<BYTE>((pixels[pixel] >> 24u) & 0xFFu);
                if (alpha > maximum)
                    maximum = alpha;
                if (alpha < minimum)
                    minimum = alpha;
            }

            bool explicitZeroOneMode = false;
            if (maximum == 0xFFu && minimum == 0u)
            {
                BYTE smallestNonZero = maximum;
                BYTE largestNonFull = minimum;
                for (int pixel = 0; pixel < 16; ++pixel)
                {
                    const BYTE alpha = static_cast<BYTE>((pixels[pixel] >> 24u) & 0xFFu);
                    if (alpha < smallestNonZero && alpha != 0u)
                        smallestNonZero = alpha;
                    if (alpha > largestNonFull && alpha != 0xFFu)
                        largestNonFull = alpha;
                }
                if (smallestNonZero < largestNonFull)
                {
                    maximum = smallestNonZero;
                    minimum = largestNonFull;
                    explicitZeroOneMode = true;
                }
                else
                {
                    maximum = 0xFFu;
                    minimum = 0u;
                }
            }

            output[0] = maximum;
            output[1] = minimum;
            std::uint64_t packedSelectors = 0u;
            if (maximum != minimum)
            {
                const int difference = static_cast<int>(maximum) - static_cast<int>(minimum);
                const int halfDifference = difference >> 1;
                const int interpolationSteps = explicitZeroOneMode ? 5 : 7;
                for (int pixel = 15; pixel >= 0; --pixel)
                {
                    packedSelectors <<= 3u;
                    const BYTE alpha = static_cast<BYTE>((pixels[pixel] >> 24u) & 0xFFu);
                    int selector = 0;
                    if (explicitZeroOneMode && alpha == 0u)
                    {
                        selector = 6;
                    }
                    else if (explicitZeroOneMode && alpha == 0xFFu)
                    {
                        selector = 7;
                    }
                    else
                    {
                        const int candidate = (halfDifference + interpolationSteps *
                            (static_cast<int>(maximum) - static_cast<int>(alpha))) / difference;
                        if (candidate >= interpolationSteps)
                            selector = 1;
                        else if (candidate > 0)
                            selector = candidate + 1;
                    }
                    packedSelectors |= static_cast<std::uint64_t>(selector);
                }
            }
            for (int byte = 0; byte < 6; ++byte)
                output[2 + byte] = static_cast<BYTE>((packedSelectors >> (byte * 8)) & 0xFFu);
            return encodeDxtColorBlock(pixels, output + 8);
        }

        BYTE decodeDxtColorBlock(const BYTE* block, DWORD* outputBgra) noexcept
        {
            if (!block)
            {
                std::memset(outputBgra, 0, 0x40u);
                return 0u;
            }

            WORD c0 = 0;
            WORD c1 = 0;
            std::memcpy(&c0, block + 0, sizeof(c0));
            std::memcpy(&c1, block + 2, sizeof(c1));
            DWORD color0 = 0;
            DWORD color1 = 0;
            decodeRgb565ToBgr(&c0, &color0);
            decodeRgb565ToBgr(&c1, &color1);

            BYTE table[4][4]{};
            for (int channel = 0; channel < 3; ++channel)
            {
                const BYTE a = static_cast<BYTE>((color0 >> (channel * 8)) & 0xFFu);
                const BYTE b = static_cast<BYTE>((color1 >> (channel * 8)) & 0xFFu);
                table[0][channel] = a;
                table[1][channel] = b;
                if (c0 <= c1)
                {
                    table[2][channel] = static_cast<BYTE>((static_cast<unsigned>(a) + b) / 2u);
                    table[3][channel] = 0u;
                }
                else
                {
                    table[2][channel] = static_cast<BYTE>((2u * static_cast<unsigned>(a) + b + 1u) / 3u);
                    table[3][channel] = static_cast<BYTE>((static_cast<unsigned>(a) + 2u * b + 1u) / 3u);
                }
            }
            table[0][3] = 0xFFu;
            table[1][3] = 0xFFu;
            table[2][3] = 0xFFu;
            table[3][3] = c0 <= c1 ? 0u : 0xFFu;

            DWORD indices = 0;
            std::memcpy(&indices, block + 4, sizeof(indices));
            BYTE result = 0u;
            for (int i = 0; i < 16; ++i)
            {
                const unsigned selector = indices & 3u;
                indices >>= 2;
                DWORD pixel = 0;
                std::memcpy(&pixel, table[selector], sizeof(pixel));
                outputBgra[i] = pixel;
                result = table[selector][3];
            }
            return result;
        }

        int decodeDxt1Block(const BYTE* block, DWORD* outputBgra) noexcept
        {
            decodeDxtColorBlock(block, outputBgra);
            for (int i = 0; i < 16; ++i)
            {
                if ((outputBgra[i] & 0xFF000000u) == 0u)
                    outputBgra[i] = 0u;
            }
            return 16;
        }

        int decodeDxt3Block(const BYTE* block, DWORD* outputBgra) noexcept
        {
            decodeDxtColorBlock(block + 8, outputBgra);
            BYTE* alpha = reinterpret_cast<BYTE*>(outputBgra) + 3;
            int result = 0;
            for (int wordIndex = 0; wordIndex < 4; ++wordIndex)
            {
                WORD packed = 0;
                std::memcpy(&packed, block + wordIndex * 2, sizeof(packed));
                const WORD original = packed;
                result = original;
                for (int pixel = 0; pixel < 4; ++pixel)
                {
                    const BYTE nibble = static_cast<BYTE>(packed & 0x0Fu);
                    const BYTE replicated = static_cast<BYTE>((nibble << 4) | nibble);
                    *alpha = replicated;
                    alpha += 4;
                    packed = static_cast<WORD>(packed >> 4);

                    result = (static_cast<int>(original) & 0xFF00) | replicated;
                }
            }
            return result;
        }

        int decodeDxt5Block(const BYTE* block, DWORD* outputBgra) noexcept
        {
            decodeDxtColorBlock(block + 8, outputBgra);

            const BYTE a0 = block[0];
            const BYTE a1 = block[1];
            BYTE table[8]{};
            table[0] = a0;
            table[1] = a1;
            if (a0 <= a1)
            {
                table[2] = static_cast<BYTE>((a1 + 4u * a0) / 5u);
                table[3] = static_cast<BYTE>((3u * a0 + 2u * a1) / 5u);
                table[4] = static_cast<BYTE>((3u * a1 + 2u * a0) / 5u);
                table[5] = static_cast<BYTE>((a0 + 4u * a1) / 5u);
                table[6] = 0u;
                table[7] = 0xFFu;
            }
            else
            {
                table[2] = static_cast<BYTE>((a1 + 6u * a0) / 7u);
                table[3] = static_cast<BYTE>((5u * a0 + 2u * a1) / 7u);
                table[4] = static_cast<BYTE>((3u * a1 + 4u * a0) / 7u);
                table[5] = static_cast<BYTE>((3u * a0 + 4u * a1) / 7u);
                table[6] = static_cast<BYTE>((5u * a1 + 2u * a0) / 7u);
                table[7] = static_cast<BYTE>((a0 + 6u * a1) / 7u);
            }

            for (int group = 0; group < 2; ++group)
            {
                DWORD packed = static_cast<DWORD>(block[2 + group * 3]) |
                    (static_cast<DWORD>(block[3 + group * 3]) << 8) |
                    (static_cast<DWORD>(block[4 + group * 3]) << 16);
                for (int i = 0; i < 8; ++i)
                {
                    const int pixel = group * 8 + i;
                    reinterpret_cast<BYTE*>(&outputBgra[pixel])[3] = table[packed & 7u];
                    packed >>= 3;
                }
            }
            return 16;
        }

        Pixel interpolate(const Pixel& a, const Pixel& b, float wa, float wb)
        {
            return {
                a.r * wa + b.r * wb,
                a.g * wa + b.g * wb,
                a.b * wa + b.b * wb,
                a.a * wa + b.a * wb,
            };
        }

        Pixel decodeDxtPixel(const MemoryView& view, int x, int y)
        {
            const FormatInfo info = formatInfo(view.format);
            const int blockX = x >> 2;
            const int blockY = y >> 2;
            const BYTE* block = view.bits + blockY * view.pitch + blockX * info.blockBytes;
            DWORD decoded[16]{};
            switch (view.format)
            {
            case kFmtDXT1: decodeDxt1Block(block, decoded); break;
            case kFmtDXT2:
            case kFmtDXT3: decodeDxt3Block(block, decoded); break;
            case kFmtDXT4:
            case kFmtDXT5: decodeDxt5Block(block, decoded); break;
            default: return Pixel{};
            }

            const int pixelIndex = (y & 3) * 4 + (x & 3);
            const DWORD value = decoded[pixelIndex];
            constexpr float inv255 = 0.0039215689f;
            Pixel out{
                static_cast<float>((value >> 16) & 0xFFu) * inv255,
                static_cast<float>((value >> 8) & 0xFFu) * inv255,
                static_cast<float>(value & 0xFFu) * inv255,
                static_cast<float>((value >> 24) & 0xFFu) * inv255,
            };

            if ((view.format == kFmtDXT2 || view.format == kFmtDXT4) && out.a > 0.0f && out.a < 1.0f)
            {
                out.r = out.r >= out.a ? 1.0f : out.r / out.a;
                out.g = out.g >= out.a ? 1.0f : out.g / out.a;
                out.b = out.b >= out.a ? 1.0f : out.b / out.a;
            }
            return out;
        }

        Pixel decodeYuvPixel(const MemoryView& view, int x, int y)
        {
            const int pairX = x & ~1;
            const BYTE* pair = view.bits + y * view.pitch + pairX * 2;
            int yValue = 0;
            int uValue = 0;
            int vValue = 0;
            if (view.format == kFmtYUY2)
            {
                yValue = pair[(x & 1) ? 2 : 0];
                uValue = pair[1];
                vValue = pair[3];
            }
            else
            {
                uValue = pair[0];
                yValue = pair[(x & 1) ? 3 : 1];
                vValue = pair[2];
            }
            const float yf = 1.16438356f * static_cast<float>(yValue - 16);
            const float uf = static_cast<float>(uValue - 128);
            const float vf = static_cast<float>(vValue - 128);
            return {
                clamp01((yf + 1.59602678f * vf) / 255.0f),
                clamp01((yf - 0.39176229f * uf - 0.81296765f * vf) / 255.0f),
                clamp01((yf + 2.01723214f * uf) / 255.0f),
                1.0f,
            };
        }

        Pixel readPixel(const MemoryView& view, int x, int y, DWORD colorKey)
        {
            Pixel out{};
            const FormatInfo info = formatInfo(view.format);
            if (info.compressed)
            {
                out = decodeDxtPixel(view, x, y);
                applyColorKey(out, colorKey);
                return out;
            }
            if (info.packedYuv)
            {
                out = decodeYuvPixel(view, x, y);
                applyColorKey(out, colorKey);
                return out;
            }

            const BYTE* pixel = view.bits + y * view.pitch + x * info.bytesPerPixel;
            constexpr float inv255 = 1.0f / 255.0f;
            switch (view.format)
            {
            case kFmtR8G8B8: out = {pixel[2] * inv255, pixel[1] * inv255, pixel[0] * inv255, 1.0f}; break;
            case kFmtA8R8G8B8: out = {pixel[2] * inv255, pixel[1] * inv255, pixel[0] * inv255, pixel[3] * inv255}; break;
            case kFmtX8R8G8B8: out = {pixel[2] * inv255, pixel[1] * inv255, pixel[0] * inv255, 1.0f}; break;
            case kFmtR5G6B5: out = decode565(static_cast<WORD>(pixel[0] | (pixel[1] << 8))); break;
            case kFmtX1R5G5B5:
            {
                const WORD v = static_cast<WORD>(pixel[0] | (pixel[1] << 8));
                out = {static_cast<float>((v >> 10) & 31u) / 31.0f, static_cast<float>((v >> 5) & 31u) / 31.0f, static_cast<float>(v & 31u) / 31.0f, 1.0f};
                break;
            }
            case kFmtA1R5G5B5:
            {
                const WORD v = static_cast<WORD>(pixel[0] | (pixel[1] << 8));
                out = {static_cast<float>((v >> 10) & 31u) / 31.0f, static_cast<float>((v >> 5) & 31u) / 31.0f, static_cast<float>(v & 31u) / 31.0f, static_cast<float>((v >> 15) & 1u)};
                break;
            }
            case kFmtA4R4G4B4:
            {
                const WORD v = static_cast<WORD>(pixel[0] | (pixel[1] << 8));
                out = {static_cast<float>((v >> 8) & 15u) / 15.0f, static_cast<float>((v >> 4) & 15u) / 15.0f, static_cast<float>(v & 15u) / 15.0f, static_cast<float>((v >> 12) & 15u) / 15.0f};
                break;
            }
            case kFmtR3G3B2: out = {static_cast<float>((pixel[0] >> 5) & 7u) / 7.0f, static_cast<float>((pixel[0] >> 2) & 7u) / 7.0f, static_cast<float>(pixel[0] & 3u) / 3.0f, 1.0f}; break;
            case kFmtA8: out = {1.0f, 1.0f, 1.0f, pixel[0] * inv255}; break;
            case kFmtA8R3G3B2:
            {
                const WORD v = static_cast<WORD>(pixel[0] | (pixel[1] << 8));
                out = {static_cast<float>((v >> 5) & 7u) / 7.0f, static_cast<float>((v >> 2) & 7u) / 7.0f, static_cast<float>(v & 3u) / 3.0f, static_cast<float>((v >> 8) & 0xFFu) / 255.0f};
                break;
            }
            case kFmtX4R4G4B4:
            {
                const WORD v = static_cast<WORD>(pixel[0] | (pixel[1] << 8));
                out = {static_cast<float>((v >> 8) & 15u) / 15.0f, static_cast<float>((v >> 4) & 15u) / 15.0f, static_cast<float>(v & 15u) / 15.0f, 1.0f};
                break;
            }
            case kFmtA8P8:
                out = palettePixel(view.palette, pixel[0]);
                out.a = pixel[1] * inv255;
                break;
            case kFmtP8: out = palettePixel(view.palette, pixel[0]); break;
            case kFmtL8:
            {
                const float l = pixel[0] * inv255;
                out = {l, l, l, 1.0f};
                break;
            }
            case kFmtA8L8:
            {
                const float l = pixel[0] * inv255;
                out = {l, l, l, pixel[1] * inv255};
                break;
            }
            case kFmtA4L4:
            {
                const float l = static_cast<float>(pixel[0] & 15u) / 15.0f;
                out = {l, l, l, static_cast<float>((pixel[0] >> 4) & 15u) / 15.0f};
                break;
            }
            case kFmtV8U8: out = {static_cast<float>(static_cast<std::int8_t>(pixel[0])) / 128.0f, static_cast<float>(static_cast<std::int8_t>(pixel[1])) / 128.0f, 0.0f, 1.0f}; break;
            case kFmtL6V5U5:
            {
                const WORD v = static_cast<WORD>(pixel[0] | (pixel[1] << 8));
                out = {static_cast<float>(signExtend(v & 31u, 5)) / 16.0f, static_cast<float>(signExtend((v >> 5) & 31u, 5)) / 16.0f, 0.0f, static_cast<float>((v >> 10) & 63u) / 63.0f};
                break;
            }
            case kFmtX8L8V8U8: out = {static_cast<float>(static_cast<std::int8_t>(pixel[0])) / 128.0f, static_cast<float>(static_cast<std::int8_t>(pixel[1])) / 128.0f, 0.0f, pixel[2] * inv255}; break;
            case kFmtQ8W8V8U8: out = {static_cast<float>(static_cast<std::int8_t>(pixel[0])) / 128.0f, static_cast<float>(static_cast<std::int8_t>(pixel[1])) / 128.0f, static_cast<float>(static_cast<std::int8_t>(pixel[2])) / 128.0f, static_cast<float>(static_cast<std::int8_t>(pixel[3])) / 128.0f}; break;
            case kFmtV16U16:
            {
                const std::int16_t u = static_cast<std::int16_t>(pixel[0] | (pixel[1] << 8));
                const std::int16_t v = static_cast<std::int16_t>(pixel[2] | (pixel[3] << 8));
                out = {static_cast<float>(u) / 32768.0f, static_cast<float>(v) / 32768.0f, 0.0f, 1.0f};
                break;
            }
            case kFmtW11V11U10:
            {
                const DWORD v = static_cast<DWORD>(pixel[0]) | (static_cast<DWORD>(pixel[1]) << 8) | (static_cast<DWORD>(pixel[2]) << 16) | (static_cast<DWORD>(pixel[3]) << 24);
                out = {static_cast<float>(signExtend(v & 0x3FFu, 10)) / 512.0f, static_cast<float>(signExtend((v >> 10) & 0x7FFu, 11)) / 1024.0f, static_cast<float>(signExtend((v >> 21) & 0x7FFu, 11)) / 1024.0f, 1.0f};
                break;
            }
            default: break;
            }
            applyColorKey(out, colorKey);
            return out;
        }

        unsigned nearestPaletteIndex(const BYTE* palette, const Pixel& pixel, bool includeAlpha)
        {
            unsigned bestIndex = 0u;
            float bestDistance = std::numeric_limits<float>::max();
            for (unsigned i = 0; i < 256u; ++i)
            {
                const Pixel entry = palettePixel(palette, i);
                const float dr = pixel.r - entry.r;
                const float dg = pixel.g - entry.g;
                const float db = pixel.b - entry.b;
                const float da = includeAlpha ? pixel.a - entry.a : 0.0f;
                const float distance = dr * dr + dg * dg + db * db + da * da;
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    bestIndex = i;
                }
            }
            return bestIndex;
        }

        bool writePixel(const MemoryView& view, int x, int y, const Pixel& input)
        {
            const FormatInfo info = formatInfo(view.format);
            if (!info.supported || info.compressed || info.packedYuv)
                return false;
            BYTE* pixel = view.bits + y * view.pitch + x * info.bytesPerPixel;
            const std::size_t matrixIndex = static_cast<std::size_t>((x & 3) * 8 + (y & 3));
            const float dither = view.dither ? kDitherMatrix[matrixIndex] : kRoundMatrix[matrixIndex];
            const Pixel p{clamp01(input.r), clamp01(input.g), clamp01(input.b), clamp01(input.a)};
            const float luminance = p.r * 0.21250001f + p.g * 0.71539998f + p.b * 0.072099999f;
            switch (view.format)
            {
            case kFmtR8G8B8:
                pixel[0] = static_cast<BYTE>(quantize(p.b, 255, dither));
                pixel[1] = static_cast<BYTE>(quantize(p.g, 255, dither));
                pixel[2] = static_cast<BYTE>(quantize(p.r, 255, dither));
                return true;
            case kFmtA8R8G8B8:
            {
                const DWORD value = static_cast<DWORD>(quantize(p.b, 255, dither)) |
                                    (static_cast<DWORD>(quantize(p.g, 255, dither)) << 8) |
                                    (static_cast<DWORD>(quantize(p.r, 255, dither)) << 16) |
                                    (static_cast<DWORD>(quantize(p.a, 255, dither)) << 24);
                std::memcpy(pixel, &value, sizeof(value));
                return true;
            }
            case kFmtX8R8G8B8:
            {
                const DWORD value = static_cast<DWORD>(quantize(p.b, 255, dither)) |
                                    (static_cast<DWORD>(quantize(p.g, 255, dither)) << 8) |
                                    (static_cast<DWORD>(quantize(p.r, 255, dither)) << 16);
                std::memcpy(pixel, &value, sizeof(value));
                return true;
            }
            case kFmtR5G6B5:
            {
                const WORD value = static_cast<WORD>(quantize(p.b, 31, dither) |
                                                     (quantize(p.g, 63, dither) << 5) |
                                                     (quantize(p.r, 31, dither) << 11));
                std::memcpy(pixel, &value, sizeof(value));
                return true;
            }
            case kFmtX1R5G5B5:
            {
                const WORD value = static_cast<WORD>(quantize(p.b, 31, dither) |
                                                     (quantize(p.g, 31, dither) << 5) |
                                                     (quantize(p.r, 31, dither) << 10));
                std::memcpy(pixel, &value, sizeof(value));
                return true;
            }
            case kFmtA1R5G5B5:
            {
                const WORD value = static_cast<WORD>(quantize(p.b, 31, dither) |
                                                     (quantize(p.g, 31, dither) << 5) |
                                                     (quantize(p.r, 31, dither) << 10) |
                                                     (quantize(p.a, 1, dither) << 15));
                std::memcpy(pixel, &value, sizeof(value));
                return true;
            }
            case kFmtA4R4G4B4:
            {
                const WORD value = static_cast<WORD>(quantize(p.b, 15, dither) |
                                                     (quantize(p.g, 15, dither) << 4) |
                                                     (quantize(p.r, 15, dither) << 8) |
                                                     (quantize(p.a, 15, dither) << 12));
                std::memcpy(pixel, &value, sizeof(value));
                return true;
            }
            case kFmtR3G3B2: pixel[0] = static_cast<BYTE>(quantize(p.b, 3, dither) | (quantize(p.g, 7, dither) << 2) | (quantize(p.r, 7, dither) << 5)); return true;
            case kFmtA8: pixel[0] = static_cast<BYTE>(quantize(p.a, 255, dither)); return true;
            case kFmtA8R3G3B2:
            {
                const WORD value = static_cast<WORD>(quantize(p.b, 3, dither) |
                                                     (quantize(p.g, 7, dither) << 2) |
                                                     (quantize(p.r, 7, dither) << 5) |
                                                     (quantize(p.a, 255, dither) << 8));
                std::memcpy(pixel, &value, sizeof(value));
                return true;
            }
            case kFmtX4R4G4B4:
            {
                const WORD value = static_cast<WORD>(quantize(p.b, 15, dither) |
                                                     (quantize(p.g, 15, dither) << 4) |
                                                     (quantize(p.r, 15, dither) << 8));
                std::memcpy(pixel, &value, sizeof(value));
                return true;
            }
            case kFmtA8P8:
            {
                const WORD value = static_cast<WORD>(nearestPaletteIndex(view.palette, p, false) |
                                                     (quantize(p.a, 255, dither) << 8));
                std::memcpy(pixel, &value, sizeof(value));
                return true;
            }
            case kFmtP8: pixel[0] = static_cast<BYTE>(nearestPaletteIndex(view.palette, p, true)); return true;
            case kFmtL8: pixel[0] = static_cast<BYTE>(quantize(luminance, 255, dither)); return true;
            case kFmtA8L8:
            {
                const WORD value = static_cast<WORD>(quantize(luminance, 255, dither) |
                                                     (quantize(p.a, 255, dither) << 8));
                std::memcpy(pixel, &value, sizeof(value));
                return true;
            }
            case kFmtA4L4: pixel[0] = static_cast<BYTE>(quantize(luminance, 15, dither) | (quantize(p.a, 15, dither) << 4)); return true;
            case kFmtV8U8:
                pixel[0] = static_cast<BYTE>(static_cast<std::int8_t>(static_cast<int>(input.r * 128.0f + dither)));
                pixel[1] = static_cast<BYTE>(static_cast<std::int8_t>(static_cast<int>(input.g * 128.0f + dither)));
                return true;
            case kFmtL6V5U5:
            {
                const int u = static_cast<int>(input.r * 16.0f + dither) & 31;
                const int v = static_cast<int>(input.g * 16.0f + dither) & 31;
                const int l = quantize(p.a, 63, dither);
                const WORD value = static_cast<WORD>(u | (v << 5) | (l << 10));
                std::memcpy(pixel, &value, sizeof(value));
                return true;
            }
            case kFmtX8L8V8U8:
            {
                const DWORD value = static_cast<BYTE>(static_cast<std::int8_t>(static_cast<int>(input.r * 128.0f + dither))) |
                                    (static_cast<DWORD>(static_cast<BYTE>(static_cast<std::int8_t>(static_cast<int>(input.g * 128.0f + dither)))) << 8) |
                                    (static_cast<DWORD>(quantize(p.a, 255, dither)) << 16);
                std::memcpy(pixel, &value, sizeof(value));
                return true;
            }
            case kFmtQ8W8V8U8:
            {
                const DWORD value = static_cast<BYTE>(static_cast<std::int8_t>(static_cast<int>(input.r * 128.0f + dither))) |
                                    (static_cast<DWORD>(static_cast<BYTE>(static_cast<std::int8_t>(static_cast<int>(input.g * 128.0f + dither)))) << 8) |
                                    (static_cast<DWORD>(static_cast<BYTE>(static_cast<std::int8_t>(static_cast<int>(input.b * 128.0f + dither)))) << 16) |
                                    (static_cast<DWORD>(static_cast<BYTE>(static_cast<std::int8_t>(static_cast<int>(input.a * 128.0f + dither)))) << 24);
                std::memcpy(pixel, &value, sizeof(value));
                return true;
            }
            case kFmtV16U16:
            {
                const DWORD value = static_cast<WORD>(static_cast<std::int16_t>(static_cast<int>(input.r * 32768.0f + dither))) |
                                    (static_cast<DWORD>(static_cast<WORD>(static_cast<std::int16_t>(static_cast<int>(input.g * 32768.0f + dither)))) << 16);
                std::memcpy(pixel, &value, sizeof(value));
                return true;
            }
            case kFmtW11V11U10:
            {
                const DWORD u = static_cast<DWORD>(static_cast<int>(input.r * 512.0f + dither)) & 0x3FFu;
                const DWORD v = static_cast<DWORD>(static_cast<int>(input.g * 1024.0f + dither)) & 0x7FFu;
                const DWORD w = static_cast<DWORD>(static_cast<int>(input.b * 1024.0f + dither)) & 0x7FFu;
                const DWORD value = u | (v << 10) | (w << 21);
                std::memcpy(pixel, &value, sizeof(value));
                return true;
            }
            default: return false;
            }
        }

        bool writeYuvRow(const MemoryView& view, int y, const std::vector<Pixel>& pixels)
        {
            if ((view.rect.left & 1) != 0)
                return false;
            BYTE* row = view.bits + y * view.pitch + view.rect.left * 2;
            for (std::size_t x = 0; x < pixels.size(); x += 2)
            {
                const Pixel p0 = pixels[x];
                const Pixel p1 = pixels[std::min(x + 1, pixels.size() - 1)];
                const auto yFrom = [](const Pixel& p) {
                    return quantize(0.257f * clamp01(p.r) + 0.504f * clamp01(p.g) + 0.098f * clamp01(p.b) + 16.0f / 255.0f, 255, 0.5f);
                };
                const float r = (p0.r + p1.r) * 0.5f;
                const float g = (p0.g + p1.g) * 0.5f;
                const float b = (p0.b + p1.b) * 0.5f;
                const int y0 = yFrom(p0);
                const int y1 = yFrom(p1);
                const int u = quantize(-0.148f * r - 0.291f * g + 0.439f * b + 128.0f / 255.0f, 255, 0.5f);
                const int v = quantize(0.439f * r - 0.368f * g - 0.071f * b + 128.0f / 255.0f, 255, 0.5f);
                if (view.format == kFmtYUY2)
                {
                    row[0] = static_cast<BYTE>(y0); row[1] = static_cast<BYTE>(u); row[2] = static_cast<BYTE>(y1); row[3] = static_cast<BYTE>(v);
                }
                else
                {
                    row[0] = static_cast<BYTE>(u); row[1] = static_cast<BYTE>(y0); row[2] = static_cast<BYTE>(v); row[3] = static_cast<BYTE>(y1);
                }
                row += 4;
            }
            return true;
        }

        bool writeRowPixels(const MemoryView& destination, int y, const Pixel* pixels, int count)
        {
            const FormatInfo info = formatInfo(destination.format);
            if (info.compressed || !pixels || count < 0)
                return false;
            if (info.packedYuv)
            {
                std::vector<Pixel> row(pixels, pixels + count);
                return writeYuvRow(destination, y, row);
            }
            for (int x = 0; x < count; ++x)
            {
                if (!writePixel(destination, destination.rect.left + x, y, pixels[x]))
                    return false;
            }
            return true;
        }

        bool writeRow(const MemoryView& destination, int y, const std::vector<Pixel>& pixels)
        {
            return writeRowPixels(destination, y, pixels.data(), static_cast<int>(pixels.size()));
        }

        bool directCopy(const MemoryView& destination,
                        const MemoryView& source,
                        int width,
                        int height,
                        DWORD colorKey,
                        SurfaceTransferState* state)
        {
            if (destination.format != source.format || colorKey != 0u)
                return false;
            const FormatInfo info = formatInfo(source.format);
            if (info.palette && !paletteEquivalent(destination.palette, source.palette))
                return false;

            if (!info.directRowMemory)
                return false;

            const int bytesPerRow = width * info.bytesPerPixel;
            const BYTE* src = source.bits + source.rect.top * source.pitch + source.rect.left * info.bytesPerPixel;
            BYTE* dst = destination.bits + destination.rect.top * destination.pitch + destination.rect.left * info.bytesPerPixel;
            for (int row = 0; row < height; ++row)
                std::memcpy(dst + row * destination.pitch, src + row * source.pitch, static_cast<std::size_t>(bytesPerRow));
            if (state)
            {
                state->directCopyRoute = true;
                state->copiedRows = height;
                state->copiedPixels = width * height;
            }
            return true;
        }

        // beginTruncatingRoundingMode/restoreRasterRoundingMode save the x87 control word, switch the
        // rounding-control field to truncate, then restore the saved word.
        // The target is Win32/x87; the portable branch preserves the same
        // observable rounding-mode lifetime for verification builds.
#if defined(_MSC_VER) && defined(_M_IX86)
        std::uint32_t g_savedRasterRoundingMode = 0;

        void beginTruncatingRoundingMode()
        {
            std::uint32_t saved = 0;
            std::uint32_t truncating = 0;
            __asm fnstcw word ptr [saved]
            truncating = saved | 0x0C00u;
            __asm fldcw word ptr [truncating]
            g_savedRasterRoundingMode = saved;
        }

        void restoreRasterRoundingMode()
        {
            std::uint32_t saved = g_savedRasterRoundingMode;
            __asm fldcw word ptr [saved]
        }
#else
        int g_savedRasterRoundingMode = FE_TONEAREST;

        void beginTruncatingRoundingMode()
        {
            const int current = std::fegetround();
            g_savedRasterRoundingMode = current == -1 ? FE_TONEAREST : current;
            std::fesetround(FE_TOWARDZERO);
        }

        void restoreRasterRoundingMode()
        {
            std::fesetround(g_savedRasterRoundingMode);
        }
#endif

        int buildLinearAxisWeights_fistpDword(float value) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            int result = 0;
            __asm {
                fld value
                fistp result
            }
            return result;
#else
            return static_cast<int>(value);
#endif
        }

        LinearAxisEntry* buildLinearAxisWeightsBody(DWORD destinationSize, DWORD sourceSize, int wrapBorder)
        {

#if defined(_MSC_VER) && defined(_M_IX86)
            void* allocation = ::operator new(
                static_cast<std::size_t>(destinationSize) * sizeof(LinearAxisEntry));
#else
            void* allocation = ::operator new(
                static_cast<std::size_t>(destinationSize) * sizeof(LinearAxisEntry),
                std::nothrow);
#endif
            if (!allocation)
                return nullptr;

            LinearAxisEntry* const firstEntry = static_cast<LinearAxisEntry*>(allocation);
            LinearAxisEntry* entry = firstEntry;
            const float scale = static_cast<float>(
                static_cast<double>(sourceSize) / static_cast<double>(destinationSize));

            beginTruncatingRoundingMode();
            for (DWORD destination = 0; destination < destinationSize; ++destination, ++entry)
            {
                const long double x87Position =
                    static_cast<long double>(destination) * static_cast<long double>(scale) - 0.5L;
                const float storedPosition = static_cast<float>(x87Position);
                const float floorValue = static_cast<float>(std::floor(static_cast<double>(x87Position)));
                int first = buildLinearAxisWeights_fistpDword(floorValue);
                int second = first + 1;

                if (first < 0)
                    first = wrapBorder != 0 ? static_cast<int>(sourceSize - 1u) : 0;
                if (second >= static_cast<int>(sourceSize))
                    second = wrapBorder == 0 ? static_cast<int>(sourceSize - 1u) : 0;

                entry->firstIndex = static_cast<DWORD>(first);
                entry->secondIndex = static_cast<DWORD>(second);
                const float firstWeight = 1.0f - (storedPosition - floorValue);
                entry->firstWeight = firstWeight;
                entry->secondWeight = 1.0f - firstWeight;
            }
            restoreRasterRoundingMode();
            return firstEntry;
        }

#if defined(_MSC_VER) && defined(_M_IX86)

        __declspec(naked) LinearAxisEntry* buildLinearAxisWeights()
        {
            __asm {
                mov eax, [esp+4]
                push eax
                push edi
                push ebx
                call buildLinearAxisWeightsBody
                add esp, 0Ch
                ret
            }
        }

        LinearAxisEntry* callBuildLinearAxisWeights(DWORD destinationSize, DWORD sourceSize, bool wrapBorder)
        {
            LinearAxisEntry* result = nullptr;
            const int wrap = wrapBorder ? 1 : 0;
            __asm {
                mov ebx, destinationSize
                mov edi, sourceSize
                push wrap
                call buildLinearAxisWeights
                add esp, 4
                mov result, eax
            }
            return result;
        }
#else
        LinearAxisEntry* buildLinearAxisWeights(DWORD destinationSize, DWORD sourceSize, bool wrapBorder)
        {
            return buildLinearAxisWeightsBody(destinationSize, sourceSize, wrapBorder ? 1 : 0);
        }

        LinearAxisEntry* callBuildLinearAxisWeights(DWORD destinationSize, DWORD sourceSize, bool wrapBorder)
        {
            return buildLinearAxisWeights(destinationSize, sourceSize, wrapBorder);
        }
#endif

        DWORD triangleReadDword(const BYTE* address)
        {
            DWORD value = 0;
            std::memcpy(&value, address, sizeof(value));
            return value;
        }

        float triangleReadFloat(const BYTE* address)
        {
            float value = 0.0f;
            std::memcpy(&value, address, sizeof(value));
            return value;
        }

        void triangleWriteDword(BYTE* address, DWORD value)
        {
            std::memcpy(address, &value, sizeof(value));
        }

        void triangleWriteFloat(BYTE* address, float value)
        {
            std::memcpy(address, &value, sizeof(value));
        }

        void destroyTriangleAccumulationChain(TriangleAccumulationNode* node)
        {
            ::operator delete(node->pixels);
            TriangleAccumulationNode* next = node->next;
            if (next)
            {
                destroyTriangleAccumulationChain(next);
                ::operator delete(next);
            }
        }

        BYTE* buildTriangleAxisWeights(DWORD sourceSize, DWORD destinationSize, bool wrapBorder)
        {
            if (sourceSize == 0u || destinationSize == 0u)
                return nullptr;

            const float destinationSizeFloat = static_cast<float>(destinationSize);
            const float sourceSizeFloat = static_cast<float>(sourceSize);
            const float ratio = destinationSizeFloat / sourceSizeFloat;
            const float halfReciprocal = 0.5f / ratio;

            std::size_t allocationSize = 16u;
            for (DWORD source = 0; source < sourceSize; ++source)
            {
                const double start = (static_cast<double>(source) - 0.5) * static_cast<double>(ratio);
                const std::int64_t estimatedPairs = static_cast<std::int64_t>(
                    (start + static_cast<double>(ratio)) - start +
                    static_cast<double>(wrapBorder ? 1 : 0) + 1.0);
                allocationSize += static_cast<std::size_t>(16 * estimatedPairs + 12);
            }

            BYTE* blob = static_cast<BYTE*>(::operator new(allocationSize, std::nothrow));
            if (!blob)
                return nullptr;

            DWORD currentDestination = 0u;
            float accumulatedWeight = 0.0f;
            std::size_t offset = 4u;
            for (DWORD source = 0; source < sourceSize; ++source)
            {
                const std::size_t listStart = offset;
                offset += 4u;
                for (DWORD half = 0; half < 2u; ++half)
                {
                    const double sourceCoordinate =
                        static_cast<double>(source) + static_cast<double>(half) - 0.5;

                    double floorInput = sourceCoordinate * static_cast<double>(ratio);
                    float intervalStartFloat = static_cast<float>(floorInput);
                    float intervalEndFloat = static_cast<float>(
                        floorInput + static_cast<double>(ratio));
                    if (!wrapBorder)
                    {
                        if (floorInput < 0.0)
                        {
                            floorInput = 0.0;
                            intervalStartFloat = 0.0f;
                        }
                        if (intervalEndFloat > destinationSizeFloat)
                            intervalEndFloat = destinationSizeFloat;
                    }
                    const double intervalStart = static_cast<double>(intervalStartFloat);
                    const double intervalEnd = static_cast<double>(intervalEndFloat);
                    int destination = static_cast<int>(std::floor(floorInput));
                    double destinationStart = static_cast<double>(destination);
                    while (destinationStart < intervalEnd)
                    {
                        int mappedDestination = destination - static_cast<int>(destinationSize);
                        if (destination >= 0)
                        {
                            if (destination < static_cast<int>(destinationSize))
                                mappedDestination = destination;
                        }
                        else
                        {
                            mappedDestination = destination + static_cast<int>(destinationSize);
                        }

                        if (static_cast<DWORD>(mappedDestination) != currentDestination)
                        {
                            if (accumulatedWeight > 0.0000099999997f)
                            {
                                triangleWriteDword(blob + offset, currentDestination);
                                triangleWriteFloat(blob + offset + 4u, accumulatedWeight);
                                offset += 8u;
                            }
                            currentDestination = static_cast<DWORD>(mappedDestination);
                            accumulatedWeight = 0.0f;
                        }

                        double destinationEnd = destinationStart + 1.0;
                        double clippedStart = destinationStart;
                        double clippedEnd = destinationEnd;
                        if (clippedStart < intervalStart)
                            clippedStart = intervalStart;
                        if (clippedEnd > intervalEnd)
                            clippedEnd = intervalEnd;

                        double factor = 0.0;
                        if (wrapBorder)
                        {
                            factor = (clippedEnd + clippedStart) * halfReciprocal - sourceCoordinate;
                        }
                        else if (sourceCoordinate < 0.0)
                        {
                            factor = 1.0;
                        }
                        else if (sourceCoordinate + 1.0 < static_cast<double>(sourceSizeFloat))
                        {
                            factor = (clippedEnd + clippedStart) * halfReciprocal - sourceCoordinate;
                        }
                        if (half != 0u)
                            factor = 1.0 - factor;

                        accumulatedWeight = static_cast<float>(
                            static_cast<double>(accumulatedWeight) +
                            (clippedEnd - clippedStart) * factor);
                        ++destination;
                        destinationStart = static_cast<double>(destination);
                    }
                }

                if (accumulatedWeight > 0.0000099999997f)
                {
                    triangleWriteDword(blob + offset, currentDestination);
                    triangleWriteFloat(blob + offset + 4u, accumulatedWeight);
                    offset += 8u;
                }
                accumulatedWeight = 0.0f;
                triangleWriteDword(blob + listStart, static_cast<DWORD>(offset - listStart));
            }
            triangleWriteDword(blob, static_cast<DWORD>(offset));
            return blob;
        }

        struct RetailSurfaceRecord32
        {
            DWORD value[19]{};
        };


        struct FormatObject
        {
            alignas(4) BYTE retail[kBlockObjectSize]{};
            DWORD retailObjectSize = 0;
            FormatInfo info{};
            MemoryView view{};
            DWORD colorKey = 0;
            void* packedScratch = nullptr;
            std::array<void*, 4> blockScratch{};
        };


        DWORD pointerToRetailDword(const void* value)
        {
            return static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(value) & 0xFFFFFFFFu);
        }

        void retailWriteDword(FormatObject& object, std::size_t offset, DWORD value)
        {
            if (offset + sizeof(value) <= object.retailObjectSize)
                std::memcpy(object.retail + offset, &value, sizeof(value));
        }

        DWORD retailReadDword(const FormatObject& object, std::size_t offset)
        {
            DWORD value = 0;
            if (offset + sizeof(value) <= object.retailObjectSize)
                std::memcpy(&value, object.retail + offset, sizeof(value));
            return value;
        }

        void retailWriteFloat(FormatObject& object, std::size_t offset, float value)
        {
            if (offset + sizeof(value) <= object.retailObjectSize)
                std::memcpy(object.retail + offset, &value, sizeof(value));
        }

        BYTE* retailPlainRowStart(FormatObject& object, DWORD slice, DWORD row) noexcept
        {
            const DWORD bytesPerPixel = retailReadDword(object, 0x1060);
            const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(object.view.bits)
                + static_cast<std::uintptr_t>(object.view.rect.left) * bytesPerPixel
                + static_cast<std::uintptr_t>(object.view.rect.top) *
                    static_cast<std::int32_t>(retailReadDword(object, 0x1048));
            return reinterpret_cast<BYTE*>(base
                + static_cast<std::uintptr_t>(slice) * retailReadDword(object, 0x104C)
                + static_cast<std::uintptr_t>(row) *
                    static_cast<std::int32_t>(retailReadDword(object, 0x1048)));
        }

        void AS1_SURFACE_THISCALL_BRIDGE destroyBaseFormatDescriptor(FormatObject& object) noexcept
        {
            retailWriteDword(object, 0x0000, kBaseDescriptorVtable);
        }

        Pixel unpackBgraToPixel(DWORD color) noexcept
        {
            constexpr float scale = 0.0039215689f;
#if defined(_MSC_VER) && defined(_M_IX86)

            const float factor = scale;
            DWORD packedColor = color;
            int component = 0;
            float outR = 0.0f;
            float outG = 0.0f;
            float outB = 0.0f;
            float outA = 0.0f;
            __asm
            {
                fld dword ptr [factor]
                mov ecx, dword ptr [packedColor]
                mov edx, ecx
                shr edx, 10h
                movzx edx, dl
                mov dword ptr [component], edx
                mov edx, ecx
                shr edx, 8
                fild dword ptr [component]
                movzx edx, dl
                fmul st, st(1)
                mov dword ptr [component], edx
                xor edx, edx
                mov dl, cl
                fstp dword ptr [outR]
                shr ecx, 18h
                fild dword ptr [component]
                fmul st, st(1)
                fstp dword ptr [outG]
                mov dword ptr [component], edx
                fild dword ptr [component]
                mov dword ptr [component], ecx
                fmul st, st(1)
                fstp dword ptr [outB]
                fild dword ptr [component]
                fmul st, st(1)
                fstp dword ptr [outA]
                fstp st
            }
            return {outR, outG, outB, outA};
#else
            return {
                static_cast<float>((color >> 16) & 0xFFu) * scale,
                static_cast<float>((color >> 8) & 0xFFu) * scale,
                static_cast<float>(color & 0xFFu) * scale,
                static_cast<float>((color >> 24) & 0xFFu) * scale,
            };
#endif
        }

        void applyColorKeyToPixelRow(FormatObject& object, Pixel* pixels) noexcept
        {
            const DWORD width = retailReadDword(object, 0x1050);
            const Pixel key{
                [&] { float v = 0.0f; std::memcpy(&v, object.retail + 0x1C, 4); return v; }(),
                [&] { float v = 0.0f; std::memcpy(&v, object.retail + 0x20, 4); return v; }(),
                [&] { float v = 0.0f; std::memcpy(&v, object.retail + 0x24, 4); return v; }(),
                [&] { float v = 0.0f; std::memcpy(&v, object.retail + 0x28, 4); return v; }(),
            };
            for (DWORD i = 0; i < width; ++i)
            {
                Pixel& p = pixels[i];
                if (p.r == key.r && p.g == key.g && p.b == key.b && p.a == key.a)
                    p = Pixel{};
            }
        }

        void retailApplyDescriptorColorKey(FormatObject& object, Pixel* pixels) noexcept
        {
            if (retailReadDword(object, 0x0010) != 0u)
                applyColorKeyToPixelRow(object, pixels);
        }

        void readRowV8U8(FormatObject& object, DWORD slice, DWORD row, Pixel* out) noexcept
        {
            BYTE* src = retailPlainRowStart(object, slice, row);
            const DWORD width = retailReadDword(object, 0x1050);
            constexpr float scale = 0.0078125f;
            for (DWORD i = 0; i < width; ++i, src += 2)
                out[i] = {static_cast<float>(static_cast<std::int8_t>(src[0])) * scale,
                          static_cast<float>(static_cast<std::int8_t>(src[1])) * scale, 0.0f, 1.0f};
        }

        void readRowL6V5U5(FormatObject& object, DWORD slice, DWORD row, Pixel* out) noexcept
        {
            BYTE* src = retailPlainRowStart(object, slice, row);
            const DWORD width = retailReadDword(object, 0x1050);
            for (DWORD i = 0; i < width; ++i, src += 2)
            {
                const WORD v = static_cast<WORD>(src[0] | (static_cast<WORD>(src[1]) << 8));
                const int u = static_cast<std::int8_t>(static_cast<BYTE>(src[0] << 3)) >> 3;
                const int vv = static_cast<std::int8_t>(static_cast<BYTE>((v >> 5) << 3)) >> 3;
                out[i] = {static_cast<float>(u) * 0.0625f,
                          static_cast<float>(vv) * 0.0625f,
                          0.0f,
                          static_cast<float>(v >> 10) * 0.015873017f};
            }
        }

        void readRowX8L8V8U8(FormatObject& object, DWORD slice, DWORD row, Pixel* out) noexcept
        {
            BYTE* src = retailPlainRowStart(object, slice, row);
            const DWORD width = retailReadDword(object, 0x1050);
            for (DWORD i = 0; i < width; ++i, src += 4)
                out[i] = {static_cast<float>(static_cast<std::int8_t>(src[0])) * 0.0078125f,
                          static_cast<float>(static_cast<std::int8_t>(src[1])) * 0.0078125f,
                          0.0f, static_cast<float>(src[2]) * 0.0039215689f};
        }

        void readRowQ8W8V8U8(FormatObject& object, DWORD slice, DWORD row, Pixel* out) noexcept
        {
            BYTE* src = retailPlainRowStart(object, slice, row);
            const DWORD width = retailReadDword(object, 0x1050);
            for (DWORD i = 0; i < width; ++i, src += 4)
                out[i] = {static_cast<float>(static_cast<std::int8_t>(src[0])) * 0.0078125f,
                          static_cast<float>(static_cast<std::int8_t>(src[1])) * 0.0078125f,
                          static_cast<float>(static_cast<std::int8_t>(src[2])) * 0.0078125f,
                          static_cast<float>(static_cast<std::int8_t>(src[3])) * 0.0078125f};
        }

        void readRowV16U16(FormatObject& object, DWORD slice, DWORD row, Pixel* out) noexcept
        {
            BYTE* src = retailPlainRowStart(object, slice, row);
            const DWORD width = retailReadDword(object, 0x1050);
            for (DWORD i = 0; i < width; ++i, src += 4)
            {
                const std::int16_t u = static_cast<std::int16_t>(src[0] | (static_cast<WORD>(src[1]) << 8));
                const std::int16_t v = static_cast<std::int16_t>(src[2] | (static_cast<WORD>(src[3]) << 8));
                out[i] = {static_cast<float>(u) * 0.000030517578f,
                          static_cast<float>(v) * 0.000030517578f, 0.0f, 1.0f};
            }
        }

        void readRowW11V11U10(FormatObject& object, DWORD slice, DWORD row, Pixel* out) noexcept
        {
            BYTE* src = retailPlainRowStart(object, slice, row);
            const DWORD width = retailReadDword(object, 0x1050);
            for (DWORD i = 0; i < width; ++i, src += 4)
            {
                DWORD v = 0; std::memcpy(&v, src, 4);
                const int u = static_cast<std::int16_t>(static_cast<WORD>(v << 6)) >> 6;
                const int vv = static_cast<std::int16_t>(static_cast<WORD>((v >> 10) << 5)) >> 5;
                const int w = static_cast<std::int16_t>(static_cast<WORD>((v >> 21) << 5)) >> 5;
                out[i] = {static_cast<float>(u) * 0.001953125f,
                          static_cast<float>(vv) * 0.0009765625f,
                          static_cast<float>(w) * 0.0009765625f, 1.0f};
            }
        }

        Pixel subtractPixel(const Pixel& left, const Pixel& right) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            Pixel result{};
            const Pixel* leftPtr = &left;
            const Pixel* rightPtr = &right;
            Pixel* resultPtr = &result;
            __asm {
                mov ecx, leftPtr
                mov eax, rightPtr
                fld dword ptr [ecx+0Ch]
                fsub dword ptr [eax+0Ch]
                fld dword ptr [ecx+8]
                fsub dword ptr [eax+8]
                fld dword ptr [ecx+4]
                fsub dword ptr [eax+4]
                fld dword ptr [ecx]
                fsub dword ptr [eax]
                mov eax, resultPtr
                fstp dword ptr [eax]
                fstp dword ptr [eax+4]
                fstp dword ptr [eax+8]
                fstp dword ptr [eax+0Ch]
            }
            return result;
#else
            return {left.r - right.r, left.g - right.g, left.b - right.b, left.a - right.a};
#endif
        }

        std::uint32_t g_savedDxtRoundingControl = 0u;

        std::uint32_t saveAndEnableDxtTruncatingRounding() noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            std::uint32_t saved = 0;
            std::uint32_t truncating = 0;
            __asm fnstcw word ptr [saved]
            truncating = saved | 0x0C00u;
            __asm fldcw word ptr [truncating]
            g_savedDxtRoundingControl = saved;
            return saved;
#else
            g_savedDxtRoundingControl = 0u;
            return g_savedDxtRoundingControl;
#endif
        }

        std::uint32_t restoreDxtRoundingControl() noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            std::uint32_t saved = g_savedDxtRoundingControl;
            __asm fldcw word ptr [saved]
#endif
            return g_savedDxtRoundingControl;
        }

        void readRowR8G8B8(FormatObject& object, DWORD slice, DWORD row, Pixel* out) noexcept
        {
            BYTE* src = retailPlainRowStart(object, slice, row);
            const DWORD width = retailReadDword(object, 0x1050);
            for (DWORD i = 0; i < width; ++i, src += 3)
                out[i] = {src[2] * 0.0039215689f, src[1] * 0.0039215689f,
                          src[0] * 0.0039215689f, 1.0f};
            retailApplyDescriptorColorKey(object, out);
        }

        void readRowA8R8G8B8(FormatObject& object, DWORD slice, DWORD row, Pixel* out) noexcept
        {
            BYTE* src = retailPlainRowStart(object, slice, row);
            const DWORD width = retailReadDword(object, 0x1050);
            for (DWORD i = 0; i < width; ++i, src += 4)
                out[i] = {src[2] * 0.0039215689f, src[1] * 0.0039215689f,
                          src[0] * 0.0039215689f, src[3] * 0.0039215689f};
            retailApplyDescriptorColorKey(object, out);
        }

        void readRowX8R8G8B8(FormatObject& object, DWORD slice, DWORD row, Pixel* out) noexcept
        {
            BYTE* src = retailPlainRowStart(object, slice, row);
            const DWORD width = retailReadDword(object, 0x1050);
            for (DWORD i = 0; i < width; ++i, src += 4)
                out[i] = {src[2] * 0.0039215689f, src[1] * 0.0039215689f,
                          src[0] * 0.0039215689f, 1.0f};
            retailApplyDescriptorColorKey(object, out);
        }

        void readRowR5G6B5(FormatObject& object, DWORD slice, DWORD row, Pixel* out) noexcept
        {
            BYTE* src = retailPlainRowStart(object, slice, row);
            const DWORD width = retailReadDword(object, 0x1050);
            for (DWORD i = 0; i < width; ++i, src += 2)
            {
                const WORD v = static_cast<WORD>(src[0] | (static_cast<WORD>(src[1]) << 8));
                out[i] = {static_cast<float>(v >> 11) * 0.032258064f,
                          static_cast<float>((v >> 5) & 0x3Fu) * 0.015873017f,
                          static_cast<float>(v & 0x1Fu) * 0.032258064f, 1.0f};
            }
            retailApplyDescriptorColorKey(object, out);
        }

        void readRowX1R5G5B5(FormatObject& object, DWORD slice, DWORD row, Pixel* out) noexcept
        {
            BYTE* src = retailPlainRowStart(object, slice, row);
            const DWORD width = retailReadDword(object, 0x1050);
            for (DWORD i = 0; i < width; ++i, src += 2)
            {
                const WORD v = static_cast<WORD>(src[0] | (static_cast<WORD>(src[1]) << 8));
                out[i] = {static_cast<float>((v >> 10) & 0x1Fu) * 0.032258064f,
                          static_cast<float>((v >> 5) & 0x1Fu) * 0.032258064f,
                          static_cast<float>(v & 0x1Fu) * 0.032258064f, 1.0f};
            }
            retailApplyDescriptorColorKey(object, out);
        }

        void readRowA1R5G5B5(FormatObject& object, DWORD slice, DWORD row, Pixel* out) noexcept
        {
            BYTE* src = retailPlainRowStart(object, slice, row);
            const DWORD width = retailReadDword(object, 0x1050);
            for (DWORD i = 0; i < width; ++i, src += 2)
            {
                const WORD v = static_cast<WORD>(src[0] | (static_cast<WORD>(src[1]) << 8));
                out[i] = {static_cast<float>((v >> 10) & 0x1Fu) * 0.032258064f,
                          static_cast<float>((v >> 5) & 0x1Fu) * 0.032258064f,
                          static_cast<float>(v & 0x1Fu) * 0.032258064f,
                          static_cast<float>(v >> 15)};
            }
            retailApplyDescriptorColorKey(object, out);
        }

        void readRowA4R4G4B4(FormatObject& object, DWORD slice, DWORD row, Pixel* out) noexcept
        {
            BYTE* src = retailPlainRowStart(object, slice, row);
            const DWORD width = retailReadDword(object, 0x1050);
            for (DWORD i = 0; i < width; ++i, src += 2)
            {
                const WORD v = static_cast<WORD>(src[0] | (static_cast<WORD>(src[1]) << 8));
                out[i] = {static_cast<float>((v >> 8) & 0xFu) * 0.06666667f,
                          static_cast<float>((v >> 4) & 0xFu) * 0.06666667f,
                          static_cast<float>(v & 0xFu) * 0.06666667f,
                          static_cast<float>(v >> 12) * 0.06666667f};
            }
            retailApplyDescriptorColorKey(object, out);
        }

        void readRowR3G3B2(FormatObject& object, DWORD slice, DWORD row, Pixel* out) noexcept
        {
            BYTE* src = retailPlainRowStart(object, slice, row);
            const DWORD width = retailReadDword(object, 0x1050);
            for (DWORD i = 0; i < width; ++i, ++src)
                out[i] = {static_cast<float>(src[0] >> 5) * 0.14285715f,
                          static_cast<float>((src[0] >> 2) & 7u) * 0.14285715f,
                          static_cast<float>(src[0] & 3u) * 0.33333334f, 1.0f};
            retailApplyDescriptorColorKey(object, out);
        }

        void readRowA8(FormatObject& object, DWORD slice, DWORD row, Pixel* out) noexcept
        {
            BYTE* src = retailPlainRowStart(object, slice, row);
            const DWORD width = retailReadDword(object, 0x1050);
            for (DWORD i = 0; i < width; ++i, ++src)
                out[i] = {1.0f, 1.0f, 1.0f, src[0] * 0.0039215689f};
            retailApplyDescriptorColorKey(object, out);
        }

        void readRowA8R3G3B2(FormatObject& object, DWORD slice, DWORD row, Pixel* out) noexcept
        {
            BYTE* src = retailPlainRowStart(object, slice, row);
            const DWORD width = retailReadDword(object, 0x1050);
            for (DWORD i = 0; i < width; ++i, src += 2)
                out[i] = {static_cast<float>((src[0] >> 5) & 7u) * 0.14285715f,
                          static_cast<float>((src[0] >> 2) & 7u) * 0.14285715f,
                          static_cast<float>(src[0] & 3u) * 0.33333334f,
                          src[1] * 0.0039215689f};
            retailApplyDescriptorColorKey(object, out);
        }

        void readRowX4R4G4B4(FormatObject& object, DWORD slice, DWORD row, Pixel* out) noexcept
        {
            BYTE* src = retailPlainRowStart(object, slice, row);
            const DWORD width = retailReadDword(object, 0x1050);
            for (DWORD i = 0; i < width; ++i, src += 2)
            {
                const WORD v = static_cast<WORD>(src[0] | (static_cast<WORD>(src[1]) << 8));
                out[i] = {static_cast<float>((v >> 8) & 0xFu) * 0.06666667f,
                          static_cast<float>((v >> 4) & 0xFu) * 0.06666667f,
                          static_cast<float>(v & 0xFu) * 0.06666667f, 1.0f};
            }
            retailApplyDescriptorColorKey(object, out);
        }

        void readRowA8P8(FormatObject& object, DWORD slice, DWORD row, Pixel* out) noexcept
        {
            BYTE* src = retailPlainRowStart(object, slice, row);
            const DWORD width = retailReadDword(object, 0x1050);
            for (DWORD i = 0; i < width; ++i, src += 2)
            {
                const std::size_t base = 0x30u + static_cast<std::size_t>(src[0]) * 16u;
                std::memcpy(&out[i], object.retail + base, sizeof(Pixel));
                out[i].a = src[1] * 0.0039215689f;
            }
            retailApplyDescriptorColorKey(object, out);
        }

        void readRowP8(FormatObject& object, DWORD slice, DWORD row, Pixel* out) noexcept
        {
            BYTE* src = retailPlainRowStart(object, slice, row);
            const DWORD width = retailReadDword(object, 0x1050);
            for (DWORD i = 0; i < width; ++i, ++src)
            {
                const std::size_t base = 0x30u + static_cast<std::size_t>(src[0]) * 16u;
                std::memcpy(&out[i], object.retail + base, sizeof(Pixel));
            }
            retailApplyDescriptorColorKey(object, out);
        }

        void readRowL8(FormatObject& object, DWORD slice, DWORD row, Pixel* out) noexcept
        {
            BYTE* src = retailPlainRowStart(object, slice, row);
            const DWORD width = retailReadDword(object, 0x1050);
            for (DWORD i = 0; i < width; ++i, ++src)
            {
                const float l = src[0] * 0.0039215689f;
                out[i] = {l, l, l, 1.0f};
            }
            retailApplyDescriptorColorKey(object, out);
        }

        void readRowA8L8(FormatObject& object, DWORD slice, DWORD row, Pixel* out) noexcept
        {
            BYTE* src = retailPlainRowStart(object, slice, row);
            const DWORD width = retailReadDword(object, 0x1050);
            for (DWORD i = 0; i < width; ++i, src += 2)
            {
                const float l = src[0] * 0.0039215689f;
                out[i] = {l, l, l, src[1] * 0.0039215689f};
            }
            retailApplyDescriptorColorKey(object, out);
        }

        void readRowA4L4(FormatObject& object, DWORD slice, DWORD row, Pixel* out) noexcept
        {
            BYTE* src = retailPlainRowStart(object, slice, row);
            const DWORD width = retailReadDword(object, 0x1050);
            for (DWORD i = 0; i < width; ++i, ++src)
            {
                const float l = static_cast<float>(src[0] & 0xFu) * 0.06666667f;
                out[i] = {l, l, l, static_cast<float>(src[0] >> 4) * 0.06666667f};
            }
            retailApplyDescriptorColorKey(object, out);
        }

        int retailFistpDword(float value) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            int result = 0;
            __asm {
                fld value
                fistp result
            }
            return result;
#else
            return static_cast<int>(value);
#endif
        }

        float retailSquaredDistance3(const Pixel& delta) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            const float r = delta.r;
            const float g = delta.g;
            const float b = delta.b;
            float result = 0.0f;
            __asm {
                fld b
                fmul st, st
                fld g
                fmul st, st
                faddp st(1), st
                fld r
                fmul st, st
                faddp st(1), st
                fstp result
            }
            return result;
#else
            return delta.r * delta.r + delta.g * delta.g + delta.b * delta.b;
#endif
        }

        float retailSquaredDistance4(const Pixel& delta) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            const float r = delta.r;
            const float g = delta.g;
            const float b = delta.b;
            const float a = delta.a;
            float result = 0.0f;
            __asm {
                fld a
                fmul st, st
                fld b
                fmul st, st
                faddp st(1), st
                fld g
                fmul st, st
                faddp st(1), st
                fld r
                fmul st, st
                faddp st(1), st
                fstp result
            }
            return result;
#else
            return delta.r * delta.r + delta.g * delta.g + delta.b * delta.b + delta.a * delta.a;
#endif
        }

        float retailDescriptorDither(const FormatObject& object, DWORD row, DWORD slice, DWORD x) noexcept
        {
            const float* table = retailReadDword(object, 0x002C) == kDitherMatrixRetailAddress
                ? kDitherMatrix : kRoundMatrix;
            // Retail write owners use ((slice & 3) + 8 * (row & 3)) as
            // the row/slice base, then add (x & 3) for each pixel.
            const std::size_t index = static_cast<std::size_t>(
                (slice & 3u) + 8u * (row & 3u) + (x & 3u));
            return table[index];
        }

        template <DWORD Format>
        std::uint32_t retailWritePlainOwner(FormatObject& object,
                                            DWORD row,
                                            DWORD slice,
                                            const Pixel* input) noexcept
        {
            BYTE* dst = retailPlainRowStart(object, slice, row);
            const DWORD width = retailReadDword(object, 0x1050);
            if constexpr (Format != kFmtP8)
                saveAndEnableDxtTruncatingRounding();
            for (DWORD x = 0; x < width; ++x)
            {
                const Pixel& p = input[x];
                float d = 0.0f;
                if constexpr (Format != kFmtP8)
                    d = retailDescriptorDither(object, row, slice, x);
                if constexpr (Format == kFmtR8G8B8)
                {
                    dst[0] = static_cast<BYTE>(retailFistpDword(p.b * 255.0f + d));
                    dst[1] = static_cast<BYTE>(retailFistpDword(p.g * 255.0f + d));
                    dst[2] = static_cast<BYTE>(retailFistpDword(p.r * 255.0f + d));
                    dst += 3;
                }
                else if constexpr (Format == kFmtA8R8G8B8)
                {
                    const DWORD b = static_cast<DWORD>(retailFistpDword(p.b * 255.0f + d));
                    const DWORD g = static_cast<DWORD>(retailFistpDword(p.g * 255.0f + d));
                    const DWORD r = static_cast<DWORD>(retailFistpDword(p.r * 255.0f + d));
                    const DWORD a = static_cast<DWORD>(retailFistpDword(p.a * 255.0f + d));
                    const DWORD v = b | (g << 8) | (r << 16) | (a << 24);
                    std::memcpy(dst, &v, 4); dst += 4;
                }
                else if constexpr (Format == kFmtX8R8G8B8)
                {
                    const DWORD b = static_cast<DWORD>(retailFistpDword(p.b * 255.0f + d));
                    const DWORD g = static_cast<DWORD>(retailFistpDword(p.g * 255.0f + d));
                    const DWORD r = static_cast<DWORD>(retailFistpDword(p.r * 255.0f + d));
                    const DWORD v = b | (g << 8) | (r << 16);
                    std::memcpy(dst, &v, 4); dst += 4;
                }
                else if constexpr (Format == kFmtR5G6B5)
                {
                    const DWORD b = static_cast<DWORD>(retailFistpDword(p.b * 31.0f + d));
                    const DWORD g = static_cast<DWORD>(retailFistpDword(p.g * 63.0f + d));
                    const DWORD r = static_cast<DWORD>(retailFistpDword(p.r * 31.0f + d));
                    const WORD v = static_cast<WORD>(b | (g << 5) | (r << 11));
                    std::memcpy(dst, &v, 2); dst += 2;
                }
                else if constexpr (Format == kFmtX1R5G5B5)
                {
                    const DWORD b = static_cast<DWORD>(retailFistpDword(p.b * 31.0f + d));
                    const DWORD g = static_cast<DWORD>(retailFistpDword(p.g * 31.0f + d));
                    const DWORD r = static_cast<DWORD>(retailFistpDword(p.r * 31.0f + d));
                    const WORD v = static_cast<WORD>(b | (g << 5) | (r << 10));
                    std::memcpy(dst, &v, 2); dst += 2;
                }
                else if constexpr (Format == kFmtA1R5G5B5)
                {
                    const DWORD b = static_cast<DWORD>(retailFistpDword(p.b * 31.0f + d));
                    const DWORD g = static_cast<DWORD>(retailFistpDword(p.g * 31.0f + d));
                    const DWORD r = static_cast<DWORD>(retailFistpDword(p.r * 31.0f + d));
                    const DWORD a = static_cast<DWORD>(retailFistpDword(p.a + d));
                    const WORD v = static_cast<WORD>(b | (g << 5) | (r << 10) | (a << 15));
                    std::memcpy(dst, &v, 2); dst += 2;
                }
                else if constexpr (Format == kFmtA4R4G4B4)
                {
                    const DWORD b = static_cast<DWORD>(retailFistpDword(p.b * 15.0f + d));
                    const DWORD g = static_cast<DWORD>(retailFistpDword(p.g * 15.0f + d));
                    const DWORD r = static_cast<DWORD>(retailFistpDword(p.r * 15.0f + d));
                    const DWORD a = static_cast<DWORD>(retailFistpDword(p.a * 15.0f + d));
                    const WORD v = static_cast<WORD>(b | (g << 4) | (r << 8) | (a << 12));
                    std::memcpy(dst, &v, 2); dst += 2;
                }
                else if constexpr (Format == kFmtR3G3B2)
                {
                    const DWORD b = static_cast<DWORD>(retailFistpDword(p.b * 3.0f + d));
                    const DWORD g = static_cast<DWORD>(retailFistpDword(p.g * 7.0f + d));
                    const DWORD r = static_cast<DWORD>(retailFistpDword(p.r * 7.0f + d));
                    *dst++ = static_cast<BYTE>(b | (g << 2) | (r << 5));
                }
                else if constexpr (Format == kFmtA8)
                {
                    *dst++ = static_cast<BYTE>(retailFistpDword(p.a * 255.0f + d));
                }
                else if constexpr (Format == kFmtA8R3G3B2)
                {
                    const DWORD b = static_cast<DWORD>(retailFistpDword(p.b * 3.0f + d));
                    const DWORD g = static_cast<DWORD>(retailFistpDword(p.g * 7.0f + d));
                    const DWORD r = static_cast<DWORD>(retailFistpDword(p.r * 7.0f + d));
                    const DWORD a = static_cast<DWORD>(retailFistpDword(p.a * 255.0f + d));
                    const WORD v = static_cast<WORD>(b | (g << 2) | (r << 5) | (a << 8));
                    std::memcpy(dst, &v, 2); dst += 2;
                }
                else if constexpr (Format == kFmtX4R4G4B4)
                {
                    const DWORD b = static_cast<DWORD>(retailFistpDword(p.b * 15.0f + d));
                    const DWORD g = static_cast<DWORD>(retailFistpDword(p.g * 15.0f + d));
                    const DWORD r = static_cast<DWORD>(retailFistpDword(p.r * 15.0f + d));
                    const WORD v = static_cast<WORD>(b | (g << 4) | (r << 8));
                    std::memcpy(dst, &v, 2); dst += 2;
                }
                else if constexpr (Format == kFmtA8P8)
                {
                    DWORD chosen = 0u;
                    for (DWORD i = 0; i < 256u; ++i)
                    {
                        Pixel palette{};
                        std::memcpy(&palette, object.retail + 0x30u + i * 16u, sizeof(Pixel));
                        const Pixel delta = subtractPixel(p, palette);
                        const float distance = retailSquaredDistance3(delta);
                        if (distance < std::numeric_limits<float>::max())
                            chosen = i;
                    }
                    const DWORD a = static_cast<DWORD>(retailFistpDword(p.a * 255.0f + d));
                    const WORD v = static_cast<WORD>(chosen | (a << 8));
                    std::memcpy(dst, &v, 2); dst += 2;
                }
                else if constexpr (Format == kFmtP8)
                {
                    BYTE chosen = 0;
                    float best = std::numeric_limits<float>::max();
                    for (DWORD i = 0; i < 256u; ++i)
                    {
                        Pixel palette{};
                        std::memcpy(&palette, object.retail + 0x30u + i * 16u, sizeof(Pixel));
                        const Pixel delta = subtractPixel(p, palette);
                        const float distance = retailSquaredDistance4(delta);
                        if (distance < best)
                        {
                            best = distance;
                            chosen = static_cast<BYTE>(i);
                        }
                    }
                    *dst++ = chosen;
                }
                else if constexpr (Format == kFmtL8)
                {
                    const float l = (p.r * 0.21250001f + p.b * 0.072099999f + p.g * 0.71539998f) * 255.0f + d;
                    *dst++ = static_cast<BYTE>(retailFistpDword(l));
                }
                else if constexpr (Format == kFmtA8L8)
                {
                    const DWORD l = static_cast<DWORD>(retailFistpDword(
                        (p.r * 0.21250001f + p.b * 0.072099999f + p.g * 0.71539998f) * 255.0f + d));
                    const DWORD a = static_cast<DWORD>(retailFistpDword(p.a * 255.0f + d));
                    const WORD v = static_cast<WORD>(l | (a << 8));
                    std::memcpy(dst, &v, 2); dst += 2;
                }
                else if constexpr (Format == kFmtA4L4)
                {
                    const DWORD l = static_cast<DWORD>(retailFistpDword(
                        (p.r * 0.21250001f + p.b * 0.072099999f + p.g * 0.71539998f) * 15.0f + d));
                    const DWORD a = static_cast<DWORD>(retailFistpDword(p.a * 15.0f + d));
                    *dst++ = static_cast<BYTE>(l | (a << 4));
                }
                else if constexpr (Format == kFmtV8U8)
                {
                    const DWORD u = static_cast<DWORD>(retailFistpDword(p.r * 128.0f + d));
                    const DWORD v = static_cast<DWORD>(retailFistpDword(p.g * 128.0f + d));
                    const WORD packed = static_cast<WORD>((u & 0xFFu) | ((v & 0xFFu) << 8));
                    std::memcpy(dst, &packed, 2); dst += 2;
                }
                else if constexpr (Format == kFmtL6V5U5)
                {
                    const DWORD u = static_cast<DWORD>(retailFistpDword(p.r * 16.0f + d));
                    const DWORD v = static_cast<DWORD>(retailFistpDword(p.g * 16.0f + d));
                    const DWORD l = static_cast<DWORD>(retailFistpDword(p.a * 63.0f + d));
                    const WORD packed = static_cast<WORD>((u & 0x1Fu) | ((v & 0x1Fu) << 5) | (l << 10));
                    std::memcpy(dst, &packed, 2); dst += 2;
                }
                else if constexpr (Format == kFmtX8L8V8U8)
                {
                    const DWORD u = static_cast<DWORD>(retailFistpDword(p.r * 128.0f + d));
                    const DWORD v = static_cast<DWORD>(retailFistpDword(p.g * 128.0f + d));
                    const DWORD l = static_cast<DWORD>(retailFistpDword(p.a * 255.0f + d));
                    const DWORD packed = (u & 0xFFu) | ((v & 0xFFu) << 8) | ((l & 0xFFu) << 16);
                    std::memcpy(dst, &packed, 4); dst += 4;
                }
                else if constexpr (Format == kFmtQ8W8V8U8)
                {
                    const DWORD u = static_cast<DWORD>(retailFistpDword(p.r * 128.0f + d));
                    const DWORD v = static_cast<DWORD>(retailFistpDword(p.g * 128.0f + d));
                    const DWORD w = static_cast<DWORD>(retailFistpDword(p.b * 128.0f + d));
                    const DWORD q = static_cast<DWORD>(retailFistpDword(p.a * 128.0f + d));
                    const DWORD packed = (u & 0xFFu) | ((v & 0xFFu) << 8) |
                                         ((w & 0xFFu) << 16) | (q << 24);
                    std::memcpy(dst, &packed, 4); dst += 4;
                }
                else if constexpr (Format == kFmtV16U16)
                {
                    const DWORD u = static_cast<DWORD>(retailFistpDword(p.r * 32768.0f + d));
                    const DWORD v = static_cast<DWORD>(retailFistpDword(p.g * 32768.0f + d));
                    const DWORD packed = (u & 0xFFFFu) | (v << 16);
                    std::memcpy(dst, &packed, 4); dst += 4;
                }
                else if constexpr (Format == kFmtW11V11U10)
                {
                    const DWORD u = static_cast<DWORD>(retailFistpDword(p.r * 512.0f + d));
                    const DWORD v = static_cast<DWORD>(retailFistpDword(p.g * 1024.0f + d));
                    const DWORD w = static_cast<DWORD>(retailFistpDword(p.b * 1024.0f + d));
                    const DWORD packed = (u & 0x3FFu) | ((v & 0x7FFu) << 10) |
                                         (((w & 0xFFFFFFFEu) << 11) << 10);
                    std::memcpy(dst, &packed, 4); dst += 4;
                }
            }
            if constexpr (Format == kFmtP8)
                return width;
            return restoreDxtRoundingControl();
        }

#define AS1_RETAIL_WRITE_OWNER(addr, fmt) \
        std::uint32_t addr(FormatObject& object, DWORD row, DWORD slice, const Pixel* input) noexcept \
        { return retailWritePlainOwner<fmt>(object, row, slice, input); }

        AS1_RETAIL_WRITE_OWNER(writeRowR8G8B8, kFmtR8G8B8)
        AS1_RETAIL_WRITE_OWNER(writeRowA8R8G8B8, kFmtA8R8G8B8)
        AS1_RETAIL_WRITE_OWNER(writeRowX8R8G8B8, kFmtX8R8G8B8)
        AS1_RETAIL_WRITE_OWNER(writeRowR5G6B5, kFmtR5G6B5)
        AS1_RETAIL_WRITE_OWNER(writeRowX1R5G5B5, kFmtX1R5G5B5)
        AS1_RETAIL_WRITE_OWNER(writeRowA1R5G5B5, kFmtA1R5G5B5)
        AS1_RETAIL_WRITE_OWNER(writeRowA4R4G4B4, kFmtA4R4G4B4)
        AS1_RETAIL_WRITE_OWNER(writeRowR3G3B2, kFmtR3G3B2)
        AS1_RETAIL_WRITE_OWNER(writeRowA8, kFmtA8)
        AS1_RETAIL_WRITE_OWNER(writeRowA8R3G3B2, kFmtA8R3G3B2)
        AS1_RETAIL_WRITE_OWNER(writeRowX4R4G4B4, kFmtX4R4G4B4)
        AS1_RETAIL_WRITE_OWNER(writeRowA8P8, kFmtA8P8)
        AS1_RETAIL_WRITE_OWNER(writeRowP8, kFmtP8)
        AS1_RETAIL_WRITE_OWNER(writeRowL8, kFmtL8)
        AS1_RETAIL_WRITE_OWNER(writeRowA8L8, kFmtA8L8)
        AS1_RETAIL_WRITE_OWNER(writeRowA4L4, kFmtA4L4)
        AS1_RETAIL_WRITE_OWNER(writeRowV8U8, kFmtV8U8)
        AS1_RETAIL_WRITE_OWNER(writeRowL6V5U5, kFmtL6V5U5)
        AS1_RETAIL_WRITE_OWNER(writeRowX8L8V8U8, kFmtX8L8V8U8)
        AS1_RETAIL_WRITE_OWNER(writeRowQ8W8V8U8, kFmtQ8W8V8U8)
        AS1_RETAIL_WRITE_OWNER(writeRowV16U16, kFmtV16U16)
        AS1_RETAIL_WRITE_OWNER(writeRowW11V11U10, kFmtW11V11U10)
#undef AS1_RETAIL_WRITE_OWNER

        RetailSurfaceRecord32 makeRetailSurfaceRecord(const MemoryView& view, DWORD colorKey)
        {
            RetailSurfaceRecord32 record{};
            record.value[0x00 / 4] = pointerToRetailDword(view.bits);
            record.value[0x04 / 4] = view.format;
            record.value[0x08 / 4] = static_cast<DWORD>(view.pitch);
            record.value[0x0C / 4] = 0u;
            record.value[0x18 / 4] = view.fullWidth ? view.fullWidth : static_cast<DWORD>(std::max(view.rect.right, 0));
            record.value[0x1C / 4] = view.fullHeight ? view.fullHeight : static_cast<DWORD>(std::max(view.rect.bottom, 0));
            record.value[0x24 / 4] = 1u;
            record.value[0x28 / 4] = static_cast<DWORD>(view.rect.left);
            record.value[0x2C / 4] = static_cast<DWORD>(view.rect.top);
            record.value[0x30 / 4] = static_cast<DWORD>(view.rect.right);
            record.value[0x34 / 4] = static_cast<DWORD>(view.rect.bottom);
            record.value[0x38 / 4] = 0u;
            record.value[0x3C / 4] = 1u;
            record.value[0x40 / 4] = view.dither ? kFilterDither : 0u;
            record.value[0x44 / 4] = colorKey;
            record.value[0x48 / 4] = pointerToRetailDword(view.palette);
            return record;
        }

        void initializePlainFormatDescriptor(FormatObject& object, const RetailSurfaceRecord32& record, DWORD bitsPerPixel, DWORD category)
        {

            retailWriteDword(object, 0x0000, kBaseDescriptorVtable);
            retailWriteDword(object, 0x0018, record.value[0x00 / 4]);
            retailWriteDword(object, 0x0004, record.value[0x04 / 4]);
            retailWriteDword(object, 0x1048, record.value[0x08 / 4]);
            retailWriteDword(object, 0x104C, record.value[0x0C / 4]);
            for (std::size_t i = 0; i < 6; ++i)
                retailWriteDword(object, 0x1030 + i * 4, record.value[0x28 / 4 + i]);

            constexpr float scale = 1.0f / 255.0f;
            const DWORD key = record.value[0x44 / 4];
            retailWriteFloat(object, 0x001C, static_cast<float>((key >> 16) & 0xFFu) * scale);
            retailWriteFloat(object, 0x0020, static_cast<float>((key >> 8) & 0xFFu) * scale);
            retailWriteFloat(object, 0x0024, static_cast<float>(key & 0xFFu) * scale);
            retailWriteFloat(object, 0x0028, static_cast<float>((key >> 24) & 0xFFu) * scale);
            retailWriteDword(object, 0x0010, key != 0u ? 1u : 0u);
            retailWriteDword(object, 0x002C, record.value[0x40 / 4] != 0u ? kDitherMatrixRetailAddress : kRoundMatrixRetailAddress);
            retailWriteDword(object, 0x1060, bitsPerPixel >> 3u);
            retailWriteDword(object, 0x000C, bitsPerPixel != 0u ? 1u : 0u);
            retailWriteDword(object, 0x0008, category);

            if (category == 2u)
            {
                retailWriteDword(object, 0x0008, 1u);
                retailWriteDword(object, 0x0014, 1u);
                const BYTE* palette = object.view.palette;
                for (std::size_t i = 0; i < 256; ++i)
                {
                    const BYTE* entry = palette ? palette + i * 4 : nullptr;
                    const std::size_t base = 0x0030 + i * 16;
                    retailWriteFloat(object, base + 0, entry ? static_cast<float>(entry[0]) * scale : 1.0f);
                    retailWriteFloat(object, base + 4, entry ? static_cast<float>(entry[1]) * scale : 1.0f);
                    retailWriteFloat(object, base + 8, entry ? static_cast<float>(entry[2]) * scale : 1.0f);
                    retailWriteFloat(object, base + 12, entry ? static_cast<float>(entry[3]) * scale : 1.0f);
                }
            }
            else
            {
                retailWriteDword(object, 0x0014, 0u);
            }

            const DWORD left = retailReadDword(object, 0x1030);
            const DWORD top = retailReadDword(object, 0x1034);
            const DWORD right = retailReadDword(object, 0x1038);
            const DWORD bottom = retailReadDword(object, 0x103C);
            const DWORD front = retailReadDword(object, 0x1040);
            const DWORD back = retailReadDword(object, 0x1044);
            const DWORD width = right - left;
            const DWORD height = bottom - top;
            const DWORD depth = back - front;
            const DWORD bytesPerPixel = retailReadDword(object, 0x1060);
            retailWriteDword(object, 0x1050, width);
            retailWriteDword(object, 0x1054, height);
            retailWriteDword(object, 0x1058, depth);
            retailWriteDword(object, 0x105C, bytesPerPixel * width);

            if (bitsPerPixel != 0u)
            {
                const std::uintptr_t adjusted = reinterpret_cast<std::uintptr_t>(object.view.bits)
                    + static_cast<std::uintptr_t>(bytesPerPixel) * left
                    + static_cast<std::uintptr_t>(static_cast<std::int32_t>(record.value[0x08 / 4])) * top
                    + static_cast<std::uintptr_t>(record.value[0x0C / 4]) * front;
                retailWriteDword(object, 0x0018, static_cast<DWORD>(adjusted & 0xFFFFFFFFu));
                retailWriteDword(object, 0x1030, 0u);
                retailWriteDword(object, 0x1034, 0u);
                retailWriteDword(object, 0x1040, 0u);
                retailWriteDword(object, 0x1038, width);
                retailWriteDword(object, 0x103C, height);
                retailWriteDword(object, 0x1044, depth);
            }
        }

        void initializePackedYuvDescriptor(FormatObject& object, const RetailSurfaceRecord32& record)
        {

            initializePlainFormatDescriptor(object, record, 0u, 1u);
            retailWriteDword(object, 0x0000, kPackedDescriptorVtable);
            const DWORD alignedLeft = retailReadDword(object, 0x1030) & 0xFFFFFFFEu;
            const DWORD alignedRight = (retailReadDword(object, 0x1038) + 1u) & 0xFFFFFFFEu;
            const DWORD span = alignedRight - alignedLeft;
            retailWriteDword(object, 0x1068, alignedLeft);
            retailWriteDword(object, 0x106C, 0u);
            retailWriteDword(object, 0x1070, alignedRight);
            retailWriteDword(object, 0x1074, 0u);
            retailWriteDword(object, 0x1078, 0u);
            retailWriteDword(object, 0x107C, 0u);
            retailWriteDword(object, 0x1080, span);
            retailWriteDword(object, 0x1084, 0u);
            retailWriteDword(object, 0x1088, 1u);

            object.packedScratch = ::operator new(static_cast<std::size_t>(span) * sizeof(Pixel), std::nothrow);
            retailWriteDword(object, 0x1064, pointerToRetailDword(object.packedScratch));
            if (!object.packedScratch)
                retailWriteDword(object, 0x1088, 0u);

            if (record.value[0x04 / 4] == kFmtUYVY)
            {
                retailWriteDword(object, 0x108C, 8u);
                retailWriteDword(object, 0x1090, 0u);
            }
            else
            {
                retailWriteDword(object, 0x108C, 0u);
                retailWriteDword(object, 0x1090, 8u);
            }
        }

        int retailPackedFistp(float value) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            int result = 0;
            __asm {
                fld value
                fistp result
            }
            return result;
#else
            return static_cast<int>(std::nearbyint(value));
#endif
        }

        float retailClampUnitFinite(float value) noexcept
        {
            // Packed YUV decode inputs are integer bytes and therefore finite;
            // this preserves the retail [0,1] clamp branches without creating
            // a different NaN policy for descriptor data that cannot be NaN.
            if (value < 0.0f)
                return 0.0f;
            if (value > 1.0f)
                return 1.0f;
            return value;
        }

        int flushPackedYuvRowCache(FormatObject& object) noexcept
        {

            if (retailReadDword(object, 0x1084) == 0u || retailReadDword(object, 0x1088) == 0u)
                return 0;

            const DWORD alignedLeft = retailReadDword(object, 0x1068);
            const DWORD alignedRight = retailReadDword(object, 0x1070);
            const DWORD cachedRow = retailReadDword(object, 0x106C);
            const DWORD cachedSlice = retailReadDword(object, 0x1078);
            BYTE* destination = object.view.bits
                + static_cast<std::size_t>(cachedSlice) * retailReadDword(object, 0x104C)
                + static_cast<std::size_t>(cachedRow) * static_cast<std::int32_t>(retailReadDword(object, 0x1048))
                + static_cast<std::size_t>(alignedLeft) * 2u;
            const Pixel* source = static_cast<const Pixel*>(object.packedScratch);

            const DWORD yShift = retailReadDword(object, 0x108C);
            const DWORD uvShift = retailReadDword(object, 0x1090);
            DWORD x = alignedLeft;
            while (x < alignedRight)
            {
                const Pixel& p0 = source[0];
                const Pixel& p1 = source[1];

                const float y0Raw = p0.r * 65.481003f + p0.g * 128.55299f + p0.b * 24.966f + 0.5f;
                const float y1Raw = p1.r * 65.481003f + p1.g * 128.55299f + p1.b * 24.966f + 0.5f;
                const float uRaw = p0.b * 112.0f - p0.g * 74.203003f - p0.r * 37.797001f + 0.5f;
                const float vRaw = p0.r * 112.0f - p0.g * 93.786003f - p0.b * 18.214001f + 0.5f;

                int y0 = retailPackedFistp(y0Raw) + 16;
                int y1 = retailPackedFistp(y1Raw) + 16;
                int u = retailPackedFistp(uRaw) + 128;
                int v = retailPackedFistp(vRaw) + 128;
                y0 = std::max(0, std::min(255, y0));
                y1 = std::max(0, std::min(255, y1));
                u = std::max(0, std::min(255, u));
                v = std::max(0, std::min(255, v));

                const WORD first = static_cast<WORD>((static_cast<DWORD>(y0) << yShift) |
                                                     (static_cast<DWORD>(u) << uvShift));
                const WORD second = static_cast<WORD>((static_cast<DWORD>(y1) << yShift) |
                                                      (static_cast<DWORD>(v) << uvShift));
                std::memcpy(destination + 0, &first, sizeof(first));
                std::memcpy(destination + 2, &second, sizeof(second));
                destination += 4;
                source += 2;
                x += 2;
            }
            retailWriteDword(object, 0x1084, 0u);
            return 0;
        }

        int preparePackedYuvRowCache(FormatObject& object, DWORD absoluteRow, DWORD absoluteSlice, int loadSource) noexcept
        {

            if (retailReadDword(object, 0x1088) == 0u)
                return static_cast<int>(kOutOfMemory);

            if (absoluteRow >= retailReadDword(object, 0x106C) &&
                absoluteRow < retailReadDword(object, 0x1074) &&
                absoluteSlice >= retailReadDword(object, 0x1078) &&
                absoluteSlice < retailReadDword(object, 0x107C))
            {
                return 0;
            }

            const int flush = flushPackedYuvRowCache(object);
            if (flush < 0)
                return flush;

            retailWriteDword(object, 0x106C, absoluteRow);
            retailWriteDword(object, 0x1074, absoluteRow + 1u);
            retailWriteDword(object, 0x1078, absoluteSlice);
            retailWriteDword(object, 0x107C, absoluteSlice + 1u);
            if (!loadSource)
                return 0;

            const DWORD alignedLeft = retailReadDword(object, 0x1068);
            const DWORD alignedRight = retailReadDword(object, 0x1070);
            const DWORD yShift = retailReadDword(object, 0x108C);
            const DWORD uvShift = retailReadDword(object, 0x1090);
            const BYTE* source = object.view.bits
                + static_cast<std::size_t>(absoluteRow) * static_cast<std::int32_t>(retailReadDword(object, 0x1048))
                + static_cast<std::size_t>(absoluteSlice) * retailReadDword(object, 0x104C)
                + static_cast<std::size_t>(alignedLeft) * 2u;
            Pixel* destination = static_cast<Pixel*>(object.packedScratch);

            for (DWORD x = alignedLeft; x < alignedRight; x += 2u, source += 4, destination += 2)
            {
                WORD first = 0;
                WORD second = 0;
                std::memcpy(&first, source + 0, sizeof(first));
                std::memcpy(&second, source + 2, sizeof(second));
                const int y0 = static_cast<int>((first >> yShift) & 0xFFu) - 16;
                const int u = static_cast<int>((first >> uvShift) & 0xFFu) - 128;
                const int y1 = static_cast<int>((second >> yShift) & 0xFFu) - 16;
                const int v = static_cast<int>((second >> uvShift) & 0xFFu) - 128;

                const float base0 = static_cast<float>(y0) * 0.0045662099f;
                const float base1 = static_cast<float>(y1) * 0.0045662099f;
                const float rv = static_cast<float>(v) * 0.0062589301f;
                const float gu = static_cast<float>(u) * 0.00153632f;
                const float gv = static_cast<float>(v) * 0.00318811f;
                const float bu = static_cast<float>(u) * 0.0079107098f;
                destination[0] = {
                    retailClampUnitFinite(base0 + rv),
                    retailClampUnitFinite(base0 - gu - gv),
                    retailClampUnitFinite(base0 + bu),
                    1.0f,
                };
                destination[1] = {
                    retailClampUnitFinite(base1 + rv),
                    retailClampUnitFinite(base1 - gu - gv),
                    retailClampUnitFinite(base1 + bu),
                    1.0f,
                };
            }
            return 0;
        }

        int writePackedYuvRow(FormatObject& object, DWORD row, DWORD slice, const Pixel* input) noexcept
        {
            const int status = preparePackedYuvRowCache(
                object,
                retailReadDword(object, 0x1034) + row,
                retailReadDword(object, 0x1040) + slice,
                retailReadDword(object, 0x1080) != retailReadDword(object, 0x1050));
            if (status < 0)
                return status;
            const DWORD width = retailReadDword(object, 0x1050);
            Pixel* destination = static_cast<Pixel*>(object.packedScratch)
                + (retailReadDword(object, 0x1030) - retailReadDword(object, 0x1068));
            std::memcpy(destination, input, static_cast<std::size_t>(width) * sizeof(Pixel));
            retailWriteDword(object, 0x1084, 1u);
            return static_cast<int>(width * sizeof(Pixel));
        }

        void readPackedYuvRow(FormatObject& object, DWORD row, DWORD slice, Pixel* output) noexcept
        {
            if (preparePackedYuvRowCache(object,
                           retailReadDword(object, 0x1034) + row,
                           retailReadDword(object, 0x1040) + slice,
                           1) < 0)
                return;
            const DWORD width = retailReadDword(object, 0x1050);
            const Pixel* source = static_cast<const Pixel*>(object.packedScratch)
                + (retailReadDword(object, 0x1030) - retailReadDword(object, 0x1068));
            std::memcpy(output, source, static_cast<std::size_t>(width) * sizeof(Pixel));
            if (retailReadDword(object, 0x0010) != 0u)
                applyColorKeyToPixelRow(object, output);
        }

        void initializeBlockFormatDescriptor(FormatObject& object, const RetailSurfaceRecord32& record)
        {

            initializePlainFormatDescriptor(object, record, 0u, 1u);
            retailWriteDword(object, 0x0000, kBlockDescriptorVtable);
            retailWriteDword(object, 0x1098, 0u);

            const auto decodeAxisMode = [&](DWORD value, std::size_t modeOffset)
            {
                if (value == 1u)
                {
                    retailWriteDword(object, modeOffset, 0u);
                    retailWriteDword(object, 0x1098, 1u);
                }
                else if (value == 2u)
                {
                    retailWriteDword(object, modeOffset, 1u);
                    retailWriteDword(object, 0x1098, 1u);
                }
                else
                {
                    retailWriteDword(object, modeOffset, 3u);
                }
            };
            decodeAxisMode(record.value[0x18 / 4], 0x109C);
            decodeAxisMode(record.value[0x1C / 4], 0x10A0);

            const DWORD alignedRight = (retailReadDword(object, 0x1038) + 3u) & 0xFFFFFFFCu;
            const DWORD alignedLeft = retailReadDword(object, 0x1030) & 0xFFFFFFFCu;
            const DWORD span = alignedRight - alignedLeft;
            retailWriteDword(object, 0x1074, alignedLeft);
            retailWriteDword(object, 0x1078, 0u);
            retailWriteDword(object, 0x107C, alignedRight);
            retailWriteDword(object, 0x1080, 0u);
            retailWriteDword(object, 0x1084, 0u);
            retailWriteDword(object, 0x1088, 0u);
            retailWriteDword(object, 0x108C, span);
            retailWriteDword(object, 0x1090, 0u);
            retailWriteDword(object, 0x1094, 1u);
            retailWriteDword(object, 0x10A4, 0u);

            for (std::size_t i = 0; i < object.blockScratch.size(); ++i)
            {
                object.blockScratch[i] = ::operator new(static_cast<std::size_t>(span) * sizeof(Pixel), std::nothrow);
                retailWriteDword(object, 0x1064 + i * 4, pointerToRetailDword(object.blockScratch[i]));
                if (!object.blockScratch[i])
                    retailWriteDword(object, 0x1094, 0u);
            }
        }

        struct ConverterContext
        {
            FormatObject* source = nullptr;
            FormatObject* destination = nullptr;
            DWORD filter = 0;
            SurfaceTransferState* state = nullptr;
        };

        ConverterContext& initializeConverterContext(ConverterContext& context) noexcept
        {

            context.destination = nullptr;
            context.source = nullptr;
            return context;
        }

        void releaseConverterContext(ConverterContext& context) noexcept;

        DWORD descriptorWidth(const FormatObject& object)
        {
            return retailReadDword(object, 0x1050);
        }

        DWORD descriptorHeight(const FormatObject& object)
        {
            return retailReadDword(object, 0x1054);
        }

        DWORD descriptorDepth(const FormatObject& object)
        {
            return retailReadDword(object, 0x1058);
        }

        DWORD descriptorCategory(const FormatObject& object)
        {
            return retailReadDword(object, 0x0008);
        }

        int dxtFistpPrepared(float value) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            int result = 0;
            __asm {
                fld value
                fistp result
            }
            return result;
#else
            return static_cast<int>(value);
#endif
        }

        float dxtPrepareByte(float component, float alpha, bool premultiply) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            const float scale = 255.0f;
            const float half = 0.5f;
            float result = 0.0f;
            if (premultiply)
            {
                __asm {
                    fld component
                    fmul alpha
                    fmul scale
                    fadd half
                    fstp result
                }
            }
            else
            {
                __asm {
                    fld component
                    fmul scale
                    fadd half
                    fstp result
                }
            }
            return result;
#else
            return (premultiply ? component * alpha : component) * 255.0f + 0.5f;
#endif
        }

        int flushDxtBlockCache(FormatObject& object) noexcept
        {

            if (retailReadDword(object, 0x1090) == 0u || retailReadDword(object, 0x1094) == 0u)
                return 0;

            const DWORD format = retailReadDword(object, 0x0004);
            const bool premultiply = format == kFmtDXT2 || format == kFmtDXT4;
            const DWORD alignedLeft = retailReadDword(object, 0x1074);
            const DWORD alignedRight = retailReadDword(object, 0x107C);
            for (DWORD x = alignedLeft; x < alignedRight; x += 4u)
            {
                saveAndEnableDxtTruncatingRounding();
                DWORD block[16]{};
                DWORD* write = block;
                for (DWORD cacheRow = 0; cacheRow < 4u; ++cacheRow)
                {
                    const Pixel* row = static_cast<const Pixel*>(object.blockScratch[cacheRow]);
                    row += x - alignedLeft;
                    for (DWORD column = 0; column < 4u; ++column)
                    {
                        const Pixel& pixel = row[column];
                        const int r = dxtFistpPrepared(dxtPrepareByte(pixel.r, pixel.a, premultiply));
                        const int g = dxtFistpPrepared(dxtPrepareByte(pixel.g, pixel.a, premultiply));
                        const int b = dxtFistpPrepared(dxtPrepareByte(pixel.b, pixel.a, premultiply));
                        const int a = dxtFistpPrepared(dxtPrepareByte(pixel.a, 1.0f, false));
                        DWORD packed = static_cast<DWORD>(a);
                        packed = (packed << 8u) | static_cast<DWORD>(r);
                        packed = (packed << 8u) | static_cast<DWORD>(g);
                        packed = (packed << 8u) | static_cast<DWORD>(b);
                        *write++ = packed;
                    }
                }
                restoreDxtRoundingControl();

                if (retailReadDword(object, 0x1098) != 0u)
                {
                    DWORD sourceBlock[16]{};
                    std::memcpy(sourceBlock, block, sizeof(sourceBlock));
                    const DWORD xMask = retailReadDword(object, 0x109C);
                    const DWORD yMask = retailReadDword(object, 0x10A0);
                    DWORD out = 0u;
                    for (DWORD row = 0; row < 4u; ++row)
                    {
                        const DWORD rowBase = (yMask & row) << 2u;
                        for (DWORD column = 0; column < 4u; ++column)
                            block[out++] = sourceBlock[rowBase + (xMask & column)];
                    }
                }

                BYTE* destination = object.view.bits
                    + static_cast<std::size_t>(retailReadDword(object, 0x1084)) * retailReadDword(object, 0x104C)
                    + static_cast<std::size_t>(retailReadDword(object, 0x1078) >> 2u) *
                        static_cast<std::int32_t>(retailReadDword(object, 0x1048));
                switch (format)
                {
                case kFmtDXT1:
                    destination += static_cast<std::size_t>(x >> 2u) * 8u;
                    encodeDxt1Block(block, destination);
                    break;
                case kFmtDXT2:
                case kFmtDXT3:
                    destination += static_cast<std::size_t>(x >> 2u) * 16u;
                    encodeDxt3Block(block, destination);
                    break;
                case kFmtDXT4:
                case kFmtDXT5:
                    destination += static_cast<std::size_t>(x >> 2u) * 16u;
                    encodeDxt5Block(block, destination);
                    break;
                default:
                    break;
                }
            }
            retailWriteDword(object, 0x1090, 0u);
            return 0;
        }

        int loadDxtBlockCache(FormatObject& object, DWORD absoluteRow, DWORD absoluteSlice, int loadSource) noexcept
        {

            if (retailReadDword(object, 0x1094) == 0u)
                return static_cast<int>(kOutOfMemory);

            if (absoluteRow >= retailReadDword(object, 0x1078) &&
                absoluteRow < retailReadDword(object, 0x1080) &&
                absoluteSlice >= retailReadDword(object, 0x1084) &&
                absoluteSlice < retailReadDword(object, 0x1088))
            {
                return 0;
            }

            const int flush = flushDxtBlockCache(object);
            if (flush < 0)
                return flush;

            const DWORD rowBase = absoluteRow & 0xFFFFFFFCu;
            retailWriteDword(object, 0x1078, rowBase);
            retailWriteDword(object, 0x1080, rowBase + 4u);
            retailWriteDword(object, 0x1084, absoluteSlice);
            retailWriteDword(object, 0x1088, absoluteSlice + 1u);
            if (retailReadDword(object, 0x10A4) <= rowBase)
            {
                retailWriteDword(object, 0x10A4, rowBase);
                if (!loadSource)
                    return 0;
            }

            const DWORD format = retailReadDword(object, 0x0004);
            const bool premultiplied = format == kFmtDXT2 || format == kFmtDXT4;
            const DWORD alignedLeft = retailReadDword(object, 0x1074);
            const DWORD alignedRight = retailReadDword(object, 0x107C);
            const DWORD span = retailReadDword(object, 0x108C);
            for (DWORD x = alignedLeft; x < alignedRight; x += 4u)
            {
                const int blockBytes = format == kFmtDXT1 ? 8 : 16;
                const BYTE* block = object.view.bits
                    + static_cast<std::size_t>(absoluteSlice) * retailReadDword(object, 0x104C)
                    + static_cast<std::size_t>(absoluteRow >> 2u) * static_cast<std::int32_t>(retailReadDword(object, 0x1048))
                    + static_cast<std::size_t>(x >> 2u) * blockBytes;
                DWORD decoded[16]{};
                switch (format)
                {
                case kFmtDXT1: decodeDxt1Block(block, decoded); break;
                case kFmtDXT2:
                case kFmtDXT3: decodeDxt3Block(block, decoded); break;
                case kFmtDXT4:
                case kFmtDXT5: decodeDxt5Block(block, decoded); break;
                default: return static_cast<int>(kFail);
                }

                for (DWORD cacheRow = 0; cacheRow < 4u; ++cacheRow)
                {
                    Pixel* destination = static_cast<Pixel*>(object.blockScratch[cacheRow]);
                    if (!destination)
                        return static_cast<int>(kOutOfMemory);
                    destination += x - alignedLeft;
                    for (DWORD column = 0; column < 4u; ++column)
                    {
                        const DWORD value = decoded[cacheRow * 4u + column];
                        constexpr float scale = 0.0039215689f;
                        Pixel pixel{
                            static_cast<float>((value >> 16) & 0xFFu) * scale,
                            static_cast<float>((value >> 8) & 0xFFu) * scale,
                            static_cast<float>(value & 0xFFu) * scale,
                            static_cast<float>((value >> 24) & 0xFFu) * scale,
                        };
                        if (premultiplied)
                        {
                            if (pixel.a == 0.0f)
                            {
                                pixel.r = pixel.g = pixel.b = 0.0f;
                            }
                            else if (pixel.a < 1.0f)
                            {
                                pixel.r = pixel.r >= pixel.a ? 1.0f : pixel.r / pixel.a;
                                pixel.g = pixel.g >= pixel.a ? 1.0f : pixel.g / pixel.a;
                                pixel.b = pixel.b >= pixel.a ? 1.0f : pixel.b / pixel.a;
                            }
                        }
                        destination[column] = pixel;
                    }
                }
                (void)span;
            }
            return 0;
        }

        int writeCompressedRow(FormatObject& object, DWORD row, DWORD slice, const Pixel* input) noexcept
        {
            const DWORD absoluteRow = retailReadDword(object, 0x1034) + row;
            const DWORD absoluteSlice = retailReadDword(object, 0x1040) + slice;
            const int status = loadDxtBlockCache(
                object,
                absoluteRow,
                absoluteSlice,
                retailReadDword(object, 0x108C) != retailReadDword(object, 0x1050));
            if (status < 0)
                return status;

            const DWORD widthBytes = retailReadDword(object, 0x1050) * sizeof(Pixel);
            const DWORD cacheRow = absoluteRow - retailReadDword(object, 0x1078);
            BYTE* destination = static_cast<BYTE*>(object.blockScratch[cacheRow])
                + static_cast<std::size_t>(retailReadDword(object, 0x1030) -
                                           retailReadDword(object, 0x1074)) * sizeof(Pixel);
            std::memcpy(destination, input, widthBytes);
            retailWriteDword(object, 0x1090, 1u);
            return static_cast<int>(widthBytes);
        }

        void readCompressedRow(FormatObject& object, DWORD row, DWORD slice, Pixel* output) noexcept
        {
            const DWORD absoluteRow = retailReadDword(object, 0x1034) + row;
            if (loadDxtBlockCache(object,
                           absoluteRow,
                           retailReadDword(object, 0x1040) + slice,
                           1) < 0)
                return;
            const DWORD width = retailReadDword(object, 0x1050);
            const DWORD cacheRow = absoluteRow - retailReadDword(object, 0x1078);
            const Pixel* source = static_cast<const Pixel*>(object.blockScratch[cacheRow])
                + (retailReadDword(object, 0x1030) - retailReadDword(object, 0x1074));
            std::memcpy(output, source, static_cast<std::size_t>(width) * sizeof(Pixel));
            if (retailReadDword(object, 0x0010) != 0u)
                applyColorKeyToPixelRow(object, output);
        }

        void descriptorReadRow(FormatObject& object, DWORD row, DWORD slice, Pixel* output, SurfaceTransferState* state)
        {
            if (state)
                ++state->descriptorReadRowCalls;
            if (!output || slice >= descriptorDepth(object))
                return;
            if (object.view.format == kFmtUYVY || object.view.format == kFmtYUY2)
            {
                readPackedYuvRow(object, row, slice, output);
                return;
            }
            if (object.info.compressed)
            {
                readCompressedRow(object, row, slice, output);
                return;
            }
            switch (object.view.format)
            {
            case kFmtR8G8B8: readRowR8G8B8(object, slice, row, output); break;
            case kFmtA8R8G8B8: readRowA8R8G8B8(object, slice, row, output); break;
            case kFmtX8R8G8B8: readRowX8R8G8B8(object, slice, row, output); break;
            case kFmtR5G6B5: readRowR5G6B5(object, slice, row, output); break;
            case kFmtX1R5G5B5: readRowX1R5G5B5(object, slice, row, output); break;
            case kFmtA1R5G5B5: readRowA1R5G5B5(object, slice, row, output); break;
            case kFmtA4R4G4B4: readRowA4R4G4B4(object, slice, row, output); break;
            case kFmtR3G3B2: readRowR3G3B2(object, slice, row, output); break;
            case kFmtA8: readRowA8(object, slice, row, output); break;
            case kFmtA8R3G3B2: readRowA8R3G3B2(object, slice, row, output); break;
            case kFmtX4R4G4B4: readRowX4R4G4B4(object, slice, row, output); break;
            case kFmtA8P8: readRowA8P8(object, slice, row, output); break;
            case kFmtP8: readRowP8(object, slice, row, output); break;
            case kFmtL8: readRowL8(object, slice, row, output); break;
            case kFmtA8L8: readRowA8L8(object, slice, row, output); break;
            case kFmtA4L4: readRowA4L4(object, slice, row, output); break;
            case kFmtV8U8: readRowV8U8(object, slice, row, output); break;
            case kFmtL6V5U5: readRowL6V5U5(object, slice, row, output); break;
            case kFmtX8L8V8U8: readRowX8L8V8U8(object, slice, row, output); break;
            case kFmtQ8W8V8U8: readRowQ8W8V8U8(object, slice, row, output); break;
            case kFmtV16U16: readRowV16U16(object, slice, row, output); break;
            case kFmtW11V11U10: readRowW11V11U10(object, slice, row, output); break;
            default:
            {
                const int width = static_cast<int>(descriptorWidth(object));
                for (int x = 0; x < width; ++x)
                    output[x] = readPixel(object.view, object.view.rect.left + x,
                                          object.view.rect.top + static_cast<int>(row), object.colorKey);
                break;
            }
            }
        }

        bool descriptorWriteRow(FormatObject& object, DWORD row, DWORD slice, const Pixel* input, SurfaceTransferState* state)
        {
            if (state)
                ++state->descriptorWriteRowCalls;
            if (!input || slice >= descriptorDepth(object))
                return false;
            if (object.view.format == kFmtUYVY || object.view.format == kFmtYUY2)
            {
                return writePackedYuvRow(object, row, slice, input) >= 0;
            }
            if (object.info.compressed)
                return writeCompressedRow(object, row, slice, input) >= 0;
            switch (object.view.format)
            {
            case kFmtR8G8B8: writeRowR8G8B8(object, row, slice, input); return true;
            case kFmtA8R8G8B8: writeRowA8R8G8B8(object, row, slice, input); return true;
            case kFmtX8R8G8B8: writeRowX8R8G8B8(object, row, slice, input); return true;
            case kFmtR5G6B5: writeRowR5G6B5(object, row, slice, input); return true;
            case kFmtX1R5G5B5: writeRowX1R5G5B5(object, row, slice, input); return true;
            case kFmtA1R5G5B5: writeRowA1R5G5B5(object, row, slice, input); return true;
            case kFmtA4R4G4B4: writeRowA4R4G4B4(object, row, slice, input); return true;
            case kFmtR3G3B2: writeRowR3G3B2(object, row, slice, input); return true;
            case kFmtA8: writeRowA8(object, row, slice, input); return true;
            case kFmtA8R3G3B2: writeRowA8R3G3B2(object, row, slice, input); return true;
            case kFmtX4R4G4B4: writeRowX4R4G4B4(object, row, slice, input); return true;
            case kFmtA8P8: writeRowA8P8(object, row, slice, input); return true;
            case kFmtP8: writeRowP8(object, row, slice, input); return true;
            case kFmtL8: writeRowL8(object, row, slice, input); return true;
            case kFmtA8L8: writeRowA8L8(object, row, slice, input); return true;
            case kFmtA4L4: writeRowA4L4(object, row, slice, input); return true;
            case kFmtV8U8: writeRowV8U8(object, row, slice, input); return true;
            case kFmtL6V5U5: writeRowL6V5U5(object, row, slice, input); return true;
            case kFmtX8L8V8U8: writeRowX8L8V8U8(object, row, slice, input); return true;
            case kFmtQ8W8V8U8: writeRowQ8W8V8U8(object, row, slice, input); return true;
            case kFmtV16U16: writeRowV16U16(object, row, slice, input); return true;
            case kFmtW11V11U10: writeRowW11V11U10(object, row, slice, input); return true;
            default:
                return writeRowPixels(object.view,
                                      object.view.rect.top + static_cast<int>(row),
                                      input,
                                      static_cast<int>(descriptorWidth(object)));
            }
        }

        constexpr DWORD kDestinationDescriptor = 1u;
        constexpr DWORD kSourceDescriptor = 2u;
        constexpr DWORD kConverterAddresses[10] = {
            0x00450A3Du,
            0x00450B6Fu,
            0x00450C11u,
            0x00450D86u,
            0x00450F1Cu,
            0x004512B1u,
            0x0045172Bu,
            0x00451A6Cu,
            0x0044F194u,
            0x0044F54Au,
        };

        FormatObject* createFormatDescriptor(const MemoryView& view,
                                 DWORD colorKey,
                                 SurfaceTransferState* state,
                                 bool destination)
        {

            const FormatInfo info = formatInfo(view.format);
            if (!info.supported)
            {
                if (state)
                {
                    if (destination)
                        state->unsupportedDestinationFormat = true;
                    else
                        state->unsupportedSourceFormat = true;
                }
                return nullptr;
            }

            FormatObject* object = new (std::nothrow) FormatObject{};
            if (!object)
                return nullptr;
            object->info = info;
            object->view = view;
            object->colorKey = colorKey;
            object->retailObjectSize = info.originalObjectSize;
            const RetailSurfaceRecord32 record = makeRetailSurfaceRecord(view, colorKey);

            switch (info.originalConstructor)
            {
            case kPlainConstructor:
                initializePlainFormatDescriptor(*object, record, static_cast<DWORD>(info.bitsPerPixel), static_cast<DWORD>(info.category));
                break;
            case kPackedYuvConstructor:
                initializePackedYuvDescriptor(*object, record);
                break;
            case kBlockConstructor:
                initializeBlockFormatDescriptor(*object, record);
                break;
            default:
                delete object;
                return nullptr;
            }

            retailWriteDword(*object, 0x0000, info.originalVtable);

            if (state)
            {
                if (destination)
                {
                    state->destinationDescriptorCreated = true;
                    state->destinationDescriptorCategory = retailReadDword(*object, 0x0008);
                    state->destinationDescriptorObjectSize = object->retailObjectSize;
                    state->destinationDescriptorVtable = retailReadDword(*object, 0x0000);
                    state->destinationDescriptorConstructor = info.originalConstructor;
                }
                else
                {
                    state->sourceDescriptorCreated = true;
                    state->sourceDescriptorCategory = retailReadDword(*object, 0x0008);
                    state->sourceDescriptorObjectSize = object->retailObjectSize;
                    state->sourceDescriptorVtable = retailReadDword(*object, 0x0000);
                    state->sourceDescriptorConstructor = info.originalConstructor;
                }
            }
            return object;
        }

        FormatObject* deleteBaseFormatDescriptor(FormatObject* object, BYTE flags) noexcept
        {

            destroyBaseFormatDescriptor(*object);
            if ((flags & 1u) != 0u)
                ::operator delete(object);
            return object;
        }

#if defined(_MSC_VER) && defined(_M_IX86)
        __declspec(naked) void AS1_SURFACE_THISCALL_BRIDGE destroyPlainFormatDescriptor(FormatObject& /*object*/) noexcept
        {

            __asm { jmp destroyBaseFormatDescriptor }
        }
#else
        void AS1_SURFACE_THISCALL_BRIDGE destroyPlainFormatDescriptor(FormatObject& object) noexcept
        {
            destroyBaseFormatDescriptor(object);
        }
#endif

        FormatObject* deletePlainFormatDescriptor(FormatObject* object, BYTE flags) noexcept
        {
            destroyPlainFormatDescriptor(*object);
            if ((flags & 1u) != 0u)
                ::operator delete(object);
            return object;
        }

        void AS1_SURFACE_THISCALL_BRIDGE destroyPackedYuvFormatDescriptor(FormatObject& object) noexcept
        {

            retailWriteDword(object, 0x0000, kPackedDescriptorVtable);
            flushPackedYuvRowCache(object);
            if (object.packedScratch)
                ::operator delete(object.packedScratch);
            destroyBaseFormatDescriptor(object);
        }

        FormatObject* deletePackedYuvFormatDescriptor(FormatObject* object, BYTE flags) noexcept
        {
            destroyPackedYuvFormatDescriptor(*object);
            if ((flags & 1u) != 0u)
                ::operator delete(object);
            return object;
        }

#if defined(_MSC_VER) && defined(_M_IX86)
        __declspec(naked) void AS1_SURFACE_THISCALL_BRIDGE destroyPackedYuvFormatDescriptorThunk(FormatObject& /*object*/) noexcept
        {

            __asm { jmp destroyPackedYuvFormatDescriptor }
        }
#else
        void AS1_SURFACE_THISCALL_BRIDGE destroyPackedYuvFormatDescriptorThunk(FormatObject& object) noexcept
        {
            destroyPackedYuvFormatDescriptor(object);
        }
#endif

        FormatObject* deletePackedYuvFormatDescriptorViaThunk(FormatObject* object, BYTE flags) noexcept
        {
            destroyPackedYuvFormatDescriptorThunk(*object);
            if ((flags & 1u) != 0u)
                ::operator delete(object);
            return object;
        }

        void AS1_SURFACE_THISCALL_BRIDGE destroyCompressedFormatDescriptor(FormatObject& object) noexcept
        {
            retailWriteDword(object, 0x0000, kBlockDescriptorVtable);
            (void)flushDxtBlockCache(object);
            for (std::size_t i = 0; i < 4; ++i)
            {
                if (object.blockScratch[i])
                    ::operator delete(object.blockScratch[i]);
            }
            destroyBaseFormatDescriptor(object);
        }

#if defined(_MSC_VER) && defined(_M_IX86)
        __declspec(naked) void AS1_SURFACE_THISCALL_BRIDGE destroyCompressedFormatDescriptorThunk(FormatObject& /*object*/) noexcept
        {

            __asm { jmp destroyCompressedFormatDescriptor }
        }
#else
        void AS1_SURFACE_THISCALL_BRIDGE destroyCompressedFormatDescriptorThunk(FormatObject& object) noexcept
        {
            destroyCompressedFormatDescriptor(object);
        }
#endif

        FormatObject* deleteCompressedFormatDescriptor(FormatObject* object, BYTE flags) noexcept
        {
            destroyCompressedFormatDescriptor(*object);
            if ((flags & 1u) != 0u)
                ::operator delete(object);
            return object;
        }

        FormatObject* deleteCompressedFormatDescriptorViaThunk(FormatObject* object, BYTE flags) noexcept
        {
            destroyCompressedFormatDescriptorThunk(*object);
            if ((flags & 1u) != 0u)
                ::operator delete(object);
            return object;
        }

        void releaseFormatObject(FormatObject*& object,
                                 DWORD descriptor,
                                 SurfaceTransferState* state)
        {
            if (!object)
                return;
            if (state)
            {
                ++state->descriptorReleaseCount;
                if (state->descriptorReleaseCount == 1)
                    state->firstReleasedDescriptor = descriptor;
                else if (state->descriptorReleaseCount == 2)
                    state->secondReleasedDescriptor = descriptor;
            }
            if (object->info.packedYuv)
                destroyPackedYuvFormatDescriptorThunk(*object);
            else if (object->info.compressed)
                destroyCompressedFormatDescriptorThunk(*object);
            else
                destroyPlainFormatDescriptor(*object);
            delete object;
            object = nullptr;
        }

        void releaseConverterContext(ConverterContext& context) noexcept
        {
            // Retail releaseConverterContext releases +0x04 destination first, then +0x00 source.
            releaseFormatObject(context.destination, kDestinationDescriptor, context.state);
            releaseFormatObject(context.source, kSourceDescriptor, context.state);
        }

        bool equalDimensions(const MemoryView& destination, const MemoryView& source)
        {
            return rectWidth(destination.rect) == rectWidth(source.rect) &&
                   rectHeight(destination.rect) == rectHeight(source.rect);
        }

        const BYTE* plainPixelPointer(const MemoryView& view, int x, int y)
        {
            const FormatInfo info = formatInfo(view.format);
            return view.bits + y * view.pitch + x * info.bytesPerPixel;
        }

        BYTE* plainPixelPointer(MemoryView& view, int x, int y)
        {
            const FormatInfo info = formatInfo(view.format);
            return view.bits + y * view.pitch + x * info.bytesPerPixel;
        }

        WORD readWordLE(const BYTE* value)
        {
            return static_cast<WORD>(value[0] | (static_cast<WORD>(value[1]) << 8u));
        }

        void writeWordLE(BYTE* destination, WORD value)
        {
            destination[0] = static_cast<BYTE>(value & 0xFFu);
            destination[1] = static_cast<BYTE>(value >> 8u);
        }

        unsigned averageFourUnsigned(unsigned a, unsigned b, unsigned c, unsigned d)
        {
            return (a + b + c + d + 2u) >> 2u;
        }

        DWORD boxDownsampleA8R8G8B8(ConverterContext& context)
        {
            MemoryView& destination = context.destination->view;
            const MemoryView& source = context.source->view;
            const int width = static_cast<int>(descriptorWidth(*context.destination));
            const int height = static_cast<int>(descriptorHeight(*context.destination));
            for (int y = 0; y < height; ++y)
            {
                const BYTE* source0 = plainPixelPointer(source, source.rect.left, source.rect.top + y * 2);
                const BYTE* source1 = source0 + source.pitch;
                BYTE* output = plainPixelPointer(destination, destination.rect.left, destination.rect.top + y);
                for (int x = 0; x < width; ++x)
                {
                    const BYTE* p00 = source0 + x * 8;
                    const BYTE* p10 = p00 + 4;
                    const BYTE* p01 = source1 + x * 8;
                    const BYTE* p11 = p01 + 4;
                    for (int channel = 0; channel < 4; ++channel)
                        output[x * 4 + channel] = static_cast<BYTE>(averageFourUnsigned(p00[channel], p10[channel], p01[channel], p11[channel]));
                }
            }
            return 0u;
        }

        DWORD boxDownsampleX8R8G8B8(ConverterContext& context)
        {
            MemoryView& destination = context.destination->view;
            const MemoryView& source = context.source->view;
            const int width = static_cast<int>(descriptorWidth(*context.destination));
            const int height = static_cast<int>(descriptorHeight(*context.destination));
            for (int y = 0; y < height; ++y)
            {
                const BYTE* source0 = plainPixelPointer(source, source.rect.left, source.rect.top + y * 2);
                const BYTE* source1 = source0 + source.pitch;
                BYTE* output = plainPixelPointer(destination, destination.rect.left, destination.rect.top + y);
                for (int x = 0; x < width; ++x)
                {
                    const BYTE* p00 = source0 + x * 8;
                    const BYTE* p10 = p00 + 4;
                    const BYTE* p01 = source1 + x * 8;
                    const BYTE* p11 = p01 + 4;
                    for (int channel = 0; channel < 3; ++channel)
                        output[x * 4 + channel] = static_cast<BYTE>(averageFourUnsigned(p00[channel], p10[channel], p01[channel], p11[channel]));
                    output[x * 4 + 3] = 0u;
                }
            }
            return 0u;
        }

        DWORD boxDownsampleR5G6B5(ConverterContext& context)
        {
            MemoryView& destination = context.destination->view;
            const MemoryView& source = context.source->view;
            const int width = static_cast<int>(descriptorWidth(*context.destination));
            const int height = static_cast<int>(descriptorHeight(*context.destination));
            for (int y = 0; y < height; ++y)
            {
                const BYTE* source0 = plainPixelPointer(source, source.rect.left, source.rect.top + y * 2);
                const BYTE* source1 = source0 + source.pitch;
                BYTE* output = plainPixelPointer(destination, destination.rect.left, destination.rect.top + y);
                for (int x = 0; x < width; ++x)
                {
                    const WORD p00 = readWordLE(source0 + x * 4);
                    const WORD p10 = readWordLE(source0 + x * 4 + 2);
                    const WORD p01 = readWordLE(source1 + x * 4);
                    const WORD p11 = readWordLE(source1 + x * 4 + 2);
                    const WORD value = static_cast<WORD>(
                        (averageFourUnsigned(p00 & 0x1Fu, p10 & 0x1Fu, p01 & 0x1Fu, p11 & 0x1Fu)) |
                        (averageFourUnsigned((p00 >> 5u) & 0x3Fu, (p10 >> 5u) & 0x3Fu, (p01 >> 5u) & 0x3Fu, (p11 >> 5u) & 0x3Fu) << 5u) |
                        (averageFourUnsigned((p00 >> 11u) & 0x1Fu, (p10 >> 11u) & 0x1Fu, (p01 >> 11u) & 0x1Fu, (p11 >> 11u) & 0x1Fu) << 11u));
                    writeWordLE(output + x * 2, value);
                }
            }
            return 0u;
        }

        DWORD boxDownsampleX1R5G5B5(ConverterContext& context)
        {
            MemoryView& destination = context.destination->view;
            const MemoryView& source = context.source->view;
            const int width = static_cast<int>(descriptorWidth(*context.destination));
            const int height = static_cast<int>(descriptorHeight(*context.destination));
            for (int y = 0; y < height; ++y)
            {
                const BYTE* source0 = plainPixelPointer(source, source.rect.left, source.rect.top + y * 2);
                const BYTE* source1 = source0 + source.pitch;
                BYTE* output = plainPixelPointer(destination, destination.rect.left, destination.rect.top + y);
                for (int x = 0; x < width; ++x)
                {
                    const WORD p00 = readWordLE(source0 + x * 4);
                    const WORD p10 = readWordLE(source0 + x * 4 + 2);
                    const WORD p01 = readWordLE(source1 + x * 4);
                    const WORD p11 = readWordLE(source1 + x * 4 + 2);
                    const WORD value = static_cast<WORD>(
                        averageFourUnsigned(p00 & 0x1Fu, p10 & 0x1Fu, p01 & 0x1Fu, p11 & 0x1Fu) |
                        (averageFourUnsigned((p00 >> 5u) & 0x1Fu, (p10 >> 5u) & 0x1Fu, (p01 >> 5u) & 0x1Fu, (p11 >> 5u) & 0x1Fu) << 5u) |
                        (averageFourUnsigned((p00 >> 10u) & 0x1Fu, (p10 >> 10u) & 0x1Fu, (p01 >> 10u) & 0x1Fu, (p11 >> 10u) & 0x1Fu) << 10u));
                    writeWordLE(output + x * 2, value);
                }
            }
            return 0u;
        }

        DWORD boxDownsampleA1R5G5B5(ConverterContext& context)
        {
            MemoryView& destination = context.destination->view;
            const MemoryView& source = context.source->view;
            const int width = static_cast<int>(descriptorWidth(*context.destination));
            const int height = static_cast<int>(descriptorHeight(*context.destination));
            for (int y = 0; y < height; ++y)
            {
                const BYTE* source0 = plainPixelPointer(source, source.rect.left, source.rect.top + y * 2);
                const BYTE* source1 = source0 + source.pitch;
                BYTE* output = plainPixelPointer(destination, destination.rect.left, destination.rect.top + y);
                for (int x = 0; x < width; ++x)
                {
                    const WORD p00 = readWordLE(source0 + x * 4);
                    const WORD p10 = readWordLE(source0 + x * 4 + 2);
                    const WORD p01 = readWordLE(source1 + x * 4);
                    const WORD p11 = readWordLE(source1 + x * 4 + 2);
                    const WORD value = static_cast<WORD>(
                        averageFourUnsigned(p00 & 0x1Fu, p10 & 0x1Fu, p01 & 0x1Fu, p11 & 0x1Fu) |
                        (averageFourUnsigned((p00 >> 5u) & 0x1Fu, (p10 >> 5u) & 0x1Fu, (p01 >> 5u) & 0x1Fu, (p11 >> 5u) & 0x1Fu) << 5u) |
                        (averageFourUnsigned((p00 >> 10u) & 0x1Fu, (p10 >> 10u) & 0x1Fu, (p01 >> 10u) & 0x1Fu, (p11 >> 10u) & 0x1Fu) << 10u) |
                        (averageFourUnsigned((p00 >> 15u) & 1u, (p10 >> 15u) & 1u, (p01 >> 15u) & 1u, (p11 >> 15u) & 1u) << 15u));
                    writeWordLE(output + x * 2, value);
                }
            }
            return 0u;
        }

        DWORD boxDownsampleA4R4G4B4(ConverterContext& context)
        {
            MemoryView& destination = context.destination->view;
            const MemoryView& source = context.source->view;
            const int width = static_cast<int>(descriptorWidth(*context.destination));
            const int height = static_cast<int>(descriptorHeight(*context.destination));
            for (int y = 0; y < height; ++y)
            {
                const BYTE* source0 = plainPixelPointer(source, source.rect.left, source.rect.top + y * 2);
                const BYTE* source1 = source0 + source.pitch;
                BYTE* output = plainPixelPointer(destination, destination.rect.left, destination.rect.top + y);
                for (int x = 0; x < width; ++x)
                {
                    const WORD p00 = readWordLE(source0 + x * 4);
                    const WORD p10 = readWordLE(source0 + x * 4 + 2);
                    const WORD p01 = readWordLE(source1 + x * 4);
                    const WORD p11 = readWordLE(source1 + x * 4 + 2);
                    WORD value = 0u;
                    for (unsigned shift = 0; shift < 16u; shift += 4u)
                        value = static_cast<WORD>(value | (averageFourUnsigned((p00 >> shift) & 0xFu, (p10 >> shift) & 0xFu, (p01 >> shift) & 0xFu, (p11 >> shift) & 0xFu) << shift));
                    writeWordLE(output + x * 2, value);
                }
            }
            return 0u;
        }

        DWORD boxDownsampleR3G3B2(ConverterContext& context)
        {
            MemoryView& destination = context.destination->view;
            const MemoryView& source = context.source->view;
            const int width = static_cast<int>(descriptorWidth(*context.destination));
            const int height = static_cast<int>(descriptorHeight(*context.destination));
            for (int y = 0; y < height; ++y)
            {
                const BYTE* source0 = plainPixelPointer(source, source.rect.left, source.rect.top + y * 2);
                const BYTE* source1 = source0 + source.pitch;
                BYTE* output = plainPixelPointer(destination, destination.rect.left, destination.rect.top + y);
                for (int x = 0; x < width; ++x)
                {
                    const BYTE p00 = source0[x * 2];
                    const BYTE p10 = source0[x * 2 + 1];
                    const BYTE p01 = source1[x * 2];
                    const BYTE p11 = source1[x * 2 + 1];
                    output[x] = static_cast<BYTE>(
                        averageFourUnsigned(p00 & 0x3u, p10 & 0x3u, p01 & 0x3u, p11 & 0x3u) |
                        (averageFourUnsigned((p00 >> 2u) & 0x7u, (p10 >> 2u) & 0x7u, (p01 >> 2u) & 0x7u, (p11 >> 2u) & 0x7u) << 2u) |
                        (averageFourUnsigned((p00 >> 5u) & 0x7u, (p10 >> 5u) & 0x7u, (p01 >> 5u) & 0x7u, (p11 >> 5u) & 0x7u) << 5u));
                }
            }
            return 0u;
        }

        DWORD boxDownsampleL8(ConverterContext& context)
        {
            MemoryView& destination = context.destination->view;
            const MemoryView& source = context.source->view;
            const int width = static_cast<int>(descriptorWidth(*context.destination));
            const int height = static_cast<int>(descriptorHeight(*context.destination));
            for (int y = 0; y < height; ++y)
            {
                const BYTE* source0 = plainPixelPointer(source, source.rect.left, source.rect.top + y * 2);
                const BYTE* source1 = source0 + source.pitch;
                BYTE* output = plainPixelPointer(destination, destination.rect.left, destination.rect.top + y);
                for (int x = 0; x < width; ++x)
                    output[x] = static_cast<BYTE>(averageFourUnsigned(source0[x * 2], source0[x * 2 + 1], source1[x * 2], source1[x * 2 + 1]));
            }
            return 0u;
        }

        DWORD boxDownsampleA8R3G3B2(ConverterContext& context)
        {
            MemoryView& destination = context.destination->view;
            const MemoryView& source = context.source->view;
            const int width = static_cast<int>(descriptorWidth(*context.destination));
            const int height = static_cast<int>(descriptorHeight(*context.destination));
            for (int y = 0; y < height; ++y)
            {
                const BYTE* source0 = plainPixelPointer(source, source.rect.left, source.rect.top + y * 2);
                const BYTE* source1 = source0 + source.pitch;
                BYTE* output = plainPixelPointer(destination, destination.rect.left, destination.rect.top + y);
                for (int x = 0; x < width; ++x)
                {
                    const WORD p00 = readWordLE(source0 + x * 4);
                    const WORD p10 = readWordLE(source0 + x * 4 + 2);
                    const WORD p01 = readWordLE(source1 + x * 4);
                    const WORD p11 = readWordLE(source1 + x * 4 + 2);
                    const BYTE low = static_cast<BYTE>(
                        averageFourUnsigned(p00 & 0x3u, p10 & 0x3u, p01 & 0x3u, p11 & 0x3u) |
                        (averageFourUnsigned((p00 >> 2u) & 0x7u, (p10 >> 2u) & 0x7u, (p01 >> 2u) & 0x7u, (p11 >> 2u) & 0x7u) << 2u) |
                        (averageFourUnsigned((p00 >> 5u) & 0x7u, (p10 >> 5u) & 0x7u, (p01 >> 5u) & 0x7u, (p11 >> 5u) & 0x7u) << 5u));
                    const BYTE alpha = static_cast<BYTE>(averageFourUnsigned(p00 >> 8u, p10 >> 8u, p01 >> 8u, p11 >> 8u));
                    writeWordLE(output + x * 2, static_cast<WORD>(low | (static_cast<WORD>(alpha) << 8u)));
                }
            }
            return 0u;
        }

        DWORD boxDownsampleX4R4G4B4(ConverterContext& context)
        {
            MemoryView& destination = context.destination->view;
            const MemoryView& source = context.source->view;
            const int width = static_cast<int>(descriptorWidth(*context.destination));
            const int height = static_cast<int>(descriptorHeight(*context.destination));
            for (int y = 0; y < height; ++y)
            {
                const BYTE* source0 = plainPixelPointer(source, source.rect.left, source.rect.top + y * 2);
                const BYTE* source1 = source0 + source.pitch;
                BYTE* output = plainPixelPointer(destination, destination.rect.left, destination.rect.top + y);
                for (int x = 0; x < width; ++x)
                {
                    const WORD p00 = readWordLE(source0 + x * 4);
                    const WORD p10 = readWordLE(source0 + x * 4 + 2);
                    const WORD p01 = readWordLE(source1 + x * 4);
                    const WORD p11 = readWordLE(source1 + x * 4 + 2);
                    WORD value = 0u;
                    for (unsigned shift = 0; shift < 12u; shift += 4u)
                        value = static_cast<WORD>(value | (averageFourUnsigned((p00 >> shift) & 0xFu, (p10 >> shift) & 0xFu, (p01 >> shift) & 0xFu, (p11 >> shift) & 0xFu) << shift));
                    writeWordLE(output + x * 2, value);
                }
            }
            return 0u;
        }

        DWORD boxDownsampleUnsupportedA8P8()
        {
            return kNotImplemented;
        }

        DWORD boxDownsampleA8L8(ConverterContext& context)
        {
            MemoryView& destination = context.destination->view;
            const MemoryView& source = context.source->view;
            const int width = static_cast<int>(descriptorWidth(*context.destination));
            const int height = static_cast<int>(descriptorHeight(*context.destination));
            for (int y = 0; y < height; ++y)
            {
                const BYTE* source0 = plainPixelPointer(source, source.rect.left, source.rect.top + y * 2);
                const BYTE* source1 = source0 + source.pitch;
                BYTE* output = plainPixelPointer(destination, destination.rect.left, destination.rect.top + y);
                for (int x = 0; x < width; ++x)
                {
                    const WORD p00 = readWordLE(source0 + x * 4);
                    const WORD p10 = readWordLE(source0 + x * 4 + 2);
                    const WORD p01 = readWordLE(source1 + x * 4);
                    const WORD p11 = readWordLE(source1 + x * 4 + 2);
                    const BYTE luminance = static_cast<BYTE>(averageFourUnsigned(p00 & 0xFFu, p10 & 0xFFu, p01 & 0xFFu, p11 & 0xFFu));
                    const BYTE alpha = static_cast<BYTE>(averageFourUnsigned(p00 >> 8u, p10 >> 8u, p01 >> 8u, p11 >> 8u));
                    writeWordLE(output + x * 2, static_cast<WORD>(luminance | (static_cast<WORD>(alpha) << 8u)));
                }
            }
            return 0u;
        }

        DWORD boxDownsampleA4L4(ConverterContext& context)
        {
            MemoryView& destination = context.destination->view;
            const MemoryView& source = context.source->view;
            const int width = static_cast<int>(descriptorWidth(*context.destination));
            const int height = static_cast<int>(descriptorHeight(*context.destination));
            for (int y = 0; y < height; ++y)
            {
                const BYTE* source0 = plainPixelPointer(source, source.rect.left, source.rect.top + y * 2);
                const BYTE* source1 = source0 + source.pitch;
                BYTE* output = plainPixelPointer(destination, destination.rect.left, destination.rect.top + y);
                for (int x = 0; x < width; ++x)
                {
                    const BYTE p00 = source0[x * 2];
                    const BYTE p10 = source0[x * 2 + 1];
                    const BYTE p01 = source1[x * 2];
                    const BYTE p11 = source1[x * 2 + 1];
                    output[x] = static_cast<BYTE>(
                        averageFourUnsigned(p00 & 0xFu, p10 & 0xFu, p01 & 0xFu, p11 & 0xFu) |
                        (averageFourUnsigned((p00 >> 4u) & 0xFu, (p10 >> 4u) & 0xFu, (p01 >> 4u) & 0xFu, (p11 >> 4u) & 0xFu) << 4u));
                }
            }
            return 0u;
        }

        DWORD copyCompressedBlocksSameFormat(ConverterContext& context)
        {

            FormatObject& source = *context.source;
            FormatObject& destination = *context.destination;

            const unsigned sourceAlignment =
                static_cast<unsigned>(source.view.rect.left | source.view.rect.top |
                                      source.view.rect.right | source.view.rect.bottom);
            const unsigned destinationAlignment =
                static_cast<unsigned>(destination.view.rect.left | destination.view.rect.top |
                                      destination.view.rect.right | destination.view.rect.bottom);
            if (((sourceAlignment | destinationAlignment) & 3u) != 0u)
                return kFail;

            int blockBytes = 0;
            switch (destination.view.format)
            {
            case kFmtDXT1: blockBytes = 8; break;
            case kFmtDXT2:
            case kFmtDXT3:
            case kFmtDXT4:
            case kFmtDXT5: blockBytes = 16; break;
            default: return kFail;
            }

            const DWORD width = descriptorWidth(destination);
            const DWORD height = descriptorHeight(destination);
            const DWORD depth = descriptorDepth(destination);
            const std::size_t bytesPerRow =
                static_cast<std::size_t>((width >> 2u) * static_cast<DWORD>(blockBytes));

            const BYTE* const sourceBase =
                source.view.bits +
                (source.view.rect.top >> 2) * source.view.pitch +
                (source.view.rect.left >> 2) * blockBytes;
            BYTE* destinationSlice =
                destination.view.bits +
                (destination.view.rect.top >> 2) * destination.view.pitch +
                (destination.view.rect.left >> 2) * blockBytes;

            const std::uint32_t sourceSlicePitch = retailReadDword(source, 0x104C);
            const std::uint32_t destinationSlicePitch = retailReadDword(destination, 0x104C);
            int copiedRows = 0;
            for (DWORD slice = 0; slice < depth; ++slice)
            {
                // Retail oddity is intentional: sourceBase does NOT advance per
                // depth slice. Destination advances by sourceSlicePitch +
                // destinationSlicePitch after each slice.
                const BYTE* sourceRow = sourceBase;
                BYTE* destinationRow = destinationSlice;
                for (DWORD row = 0; row < height; row += 4u)
                {
                    std::memcpy(destinationRow, sourceRow, bytesPerRow);
                    sourceRow += source.view.pitch;
                    destinationRow += destination.view.pitch;
                    ++copiedRows;
                }
                destinationSlice += static_cast<std::size_t>(sourceSlicePitch) +
                                    static_cast<std::size_t>(destinationSlicePitch);
            }

            if (context.state)
            {
                context.state->directCopyRoute = true;
                context.state->directCompressedCopyRoute = true;
                context.state->copiedRows = copiedRows;
                context.state->copiedPixels = static_cast<int>(width * height * depth);
            }
            return 0u;
        }


        DWORD copySurfaceDirect(ConverterContext& context)
        {

            FormatObject& source = *context.source;
            FormatObject& destination = *context.destination;
            if (retailReadDword(destination, 0x0004) != retailReadDword(source, 0x0004) ||
                retailReadDword(source, 0x0010) != 0u ||
                descriptorWidth(destination) != descriptorWidth(source) ||
                descriptorHeight(destination) != descriptorHeight(source) ||
                descriptorDepth(destination) != descriptorDepth(source))
                return kFail;

            if (retailReadDword(destination, 0x000C) == 0u)
                return copyCompressedBlocksSameFormat(context);

            if (retailReadDword(destination, 0x0014) != 0u &&
                std::memcmp(destination.retail + 0x0030,
                            source.retail + 0x0030,
                            0x100u * sizeof(DWORD)) != 0)
                return kFail;

            const DWORD depth = descriptorDepth(destination);
            const DWORD height = descriptorHeight(destination);
            const std::size_t rowBytes = retailReadDword(destination, 0x105C);
            const std::size_t sourceSlicePitch = retailReadDword(source, 0x104C);
            const std::size_t destinationSlicePitch = retailReadDword(destination, 0x104C);
            const FormatInfo sourceInfo = formatInfo(source.view.format);
            const FormatInfo destinationInfo = formatInfo(destination.view.format);
            const BYTE* sourceBase = source.view.bits +
                static_cast<std::ptrdiff_t>(source.view.rect.top) * source.view.pitch +
                static_cast<std::ptrdiff_t>(source.view.rect.left) * sourceInfo.bytesPerPixel;
            BYTE* destinationBase = destination.view.bits +
                static_cast<std::ptrdiff_t>(destination.view.rect.top) * destination.view.pitch +
                static_cast<std::ptrdiff_t>(destination.view.rect.left) * destinationInfo.bytesPerPixel;

            int copiedRows = 0;
            for (DWORD slice = 0; slice < depth; ++slice)
            {
                const BYTE* sourceRow = sourceBase + sourceSlicePitch * slice;
                BYTE* destinationRow = destinationBase + destinationSlicePitch * slice;
                for (DWORD row = 0; row < height; ++row)
                {
                    std::memcpy(destinationRow, sourceRow, rowBytes);
                    sourceRow += source.view.pitch;
                    destinationRow += destination.view.pitch;
                    ++copiedRows;
                }
            }
            if (context.state)
            {
                context.state->directCopyRoute = true;
                context.state->copiedRows = copiedRows;
                context.state->copiedPixels = static_cast<int>(
                    descriptorWidth(destination) * height * depth);
            }
            return 0u;
        }


        DWORD convertSameSize(ConverterContext& context)
        {
            FormatObject& source = *context.source;
            FormatObject& destination = *context.destination;
            if (descriptorWidth(destination) != descriptorWidth(source) ||
                descriptorHeight(destination) != descriptorHeight(source) ||
                descriptorDepth(destination) != descriptorDepth(source))
                return kFail;

            const DWORD width = descriptorWidth(destination);
            std::unique_ptr<Pixel[]> row(new (std::nothrow) Pixel[width]);
            if (!row)
                return kFail;

            for (DWORD slice = 0; slice < descriptorDepth(destination); ++slice)
            {
                for (DWORD y = 0; y < descriptorHeight(destination); ++y)
                {
                    descriptorReadRow(source, y, slice, row.get(), context.state);
                    if (!descriptorWriteRow(destination, y, slice, row.get(), context.state))
                        return kFail;
                }
            }
            if (context.state)
            {
                context.state->sameSizeConverterRoute = true;
                context.state->copiedRows = static_cast<int>(descriptorHeight(destination) * descriptorDepth(destination));
                context.state->copiedPixels = static_cast<int>(descriptorWidth(destination) * descriptorHeight(destination) * descriptorDepth(destination));
            }
            return 0u;
        }

        DWORD convertFilterNone(ConverterContext& context)
        {
            if ((context.filter & 0xFFu) != kFilterNone)
                return kFail;

            FormatObject& source = *context.source;
            FormatObject& destination = *context.destination;
            const DWORD maximumWidth = std::max(descriptorWidth(source), descriptorWidth(destination));
            const DWORD overlapHeight = std::min(descriptorHeight(source), descriptorHeight(destination));
            const DWORD overlapDepth = std::min(descriptorDepth(source), descriptorDepth(destination));

            std::unique_ptr<Pixel[]> transfer(new (std::nothrow) Pixel[maximumWidth]);
            if (!transfer)
                return kFail;
            std::unique_ptr<Pixel[]> zeroRow(new (std::nothrow) Pixel[descriptorWidth(destination)]);
            if (!zeroRow)
                return kFail;
            std::memset(transfer.get(), 0, static_cast<std::size_t>(maximumWidth) * sizeof(Pixel));
            std::memset(zeroRow.get(), 0, static_cast<std::size_t>(descriptorWidth(destination)) * sizeof(Pixel));

            for (DWORD slice = 0; slice < overlapDepth; ++slice)
            {
                for (DWORD y = 0; y < overlapHeight; ++y)
                {
                    descriptorReadRow(source, y, slice, transfer.get(), context.state);
                    if (!descriptorWriteRow(destination, y, slice, transfer.get(), context.state))
                        return kFail;
                }
                for (DWORD y = overlapHeight; y < descriptorHeight(destination); ++y)
                {
                    if (!descriptorWriteRow(destination, y, slice, zeroRow.get(), context.state))
                        return kFail;
                    if (context.state)
                        ++context.state->zeroFillRows;
                }
            }
            for (DWORD slice = overlapDepth; slice < descriptorDepth(destination); ++slice)
            {
                for (DWORD y = 0; y < descriptorHeight(destination); ++y)
                {
                    if (!descriptorWriteRow(destination, y, slice, zeroRow.get(), context.state))
                        return kFail;
                    if (context.state)
                        ++context.state->zeroFillRows;
                }
            }
            if (context.state)
            {
                context.state->copiedRows = static_cast<int>(descriptorHeight(destination) * descriptorDepth(destination));
                context.state->copiedPixels = static_cast<int>(descriptorWidth(destination) * descriptorHeight(destination) * descriptorDepth(destination));
            }
            return 0u;
        }

        DWORD convertFilterPoint(ConverterContext& context)
        {
            if ((context.filter & 0xFFu) != kFilterPoint)
                return kFail;

            FormatObject& source = *context.source;
            FormatObject& destination = *context.destination;
            std::unique_ptr<Pixel[]> sourceRow(new (std::nothrow) Pixel[descriptorWidth(source)]);
            if (!sourceRow)
                return kOutOfMemory;
            std::unique_ptr<Pixel[]> destinationRow(new (std::nothrow) Pixel[descriptorWidth(destination)]);
            if (!destinationRow)
                return kOutOfMemory;

            const DWORD xStep = (descriptorWidth(source) << 16u) / descriptorWidth(destination);
            const DWORD yStep = (descriptorHeight(source) << 16u) / descriptorHeight(destination);
            const DWORD zStep = (descriptorDepth(source) << 16u) / descriptorDepth(destination);
            DWORD sourceZ = 0u;

            for (DWORD destinationZ = 0; destinationZ < descriptorDepth(destination); ++destinationZ)
            {
                DWORD previousSourceY = 0xFFFFFFFFu;
                DWORD sourceY = 0u;
                for (DWORD destinationY = 0; destinationY < descriptorHeight(destination); ++destinationY)
                {
                    if (((sourceY ^ previousSourceY) & 0xFFFF0000u) != 0u)
                    {
                        descriptorReadRow(source, sourceY >> 16u, sourceZ >> 16u, sourceRow.get(), context.state);
                        previousSourceY = sourceY;
                        if (context.state)
                            ++context.state->pointSourceRowReloads;
                    }
                    DWORD sourceX = 0u;
                    for (DWORD destinationX = 0; destinationX < descriptorWidth(destination); ++destinationX)
                    {
                        destinationRow[destinationX] = sourceRow[sourceX >> 16u];
                        sourceX += xStep;
                    }
                    if (!descriptorWriteRow(destination, destinationY, destinationZ, destinationRow.get(), context.state))
                        return kFail;
                    sourceY += yStep;
                }
                sourceZ += zStep;
            }
            if (context.state)
            {
                context.state->copiedRows = static_cast<int>(descriptorHeight(destination) * descriptorDepth(destination));
                context.state->copiedPixels = static_cast<int>(descriptorWidth(destination) * descriptorHeight(destination) * descriptorDepth(destination));
            }
            return 0u;
        }

        DWORD convertFilterBox2D(ConverterContext& context)
        {
            if ((context.filter & 0xFFu) != kFilterBox ||
                descriptorCategory(*context.destination) != 1u ||
                descriptorCategory(*context.source) != 1u)
                return kFail;

            FormatObject& source = *context.source;
            FormatObject& destination = *context.destination;
            const DWORD sourceWidth = descriptorWidth(source);
            const DWORD sourceHeight = descriptorHeight(source);
            const DWORD destinationWidth = descriptorWidth(destination);
            const DWORD destinationHeight = descriptorHeight(destination);
            if ((destinationWidth != (sourceWidth >> 1u) && (destinationWidth != 1u || sourceWidth != 1u)) ||
                (destinationHeight != (sourceHeight >> 1u) && (destinationHeight != 1u || sourceHeight != 1u)) ||
                descriptorDepth(destination) != 1u ||
                descriptorDepth(source) != 1u)
                return kFail;

            DWORD optimizedResult = kFail;
            DWORD optimizedAddress = 0u;
            if ((context.filter & kFilterDither) == 0u &&
                destination.view.format == source.view.format &&
                sourceWidth >= 2u &&
                sourceHeight >= 2u)
            {
                switch (source.view.format)
                {
                case kFmtR8G8B8: optimizedAddress = 0x00450779u; optimizedResult = boxDownsampleUnsupportedA8P8(); break;
                case kFmtA8R8G8B8: optimizedAddress = 0x0044FBA2u; optimizedResult = boxDownsampleA8R8G8B8(context); break;
                case kFmtX8R8G8B8: optimizedAddress = 0x0044FD00u; optimizedResult = boxDownsampleX8R8G8B8(context); break;
                case kFmtR5G6B5: optimizedAddress = 0x0044FE1Cu; optimizedResult = boxDownsampleR5G6B5(context); break;
                case kFmtX1R5G5B5: optimizedAddress = 0x0044FF6Cu; optimizedResult = boxDownsampleX1R5G5B5(context); break;
                case kFmtA1R5G5B5: optimizedAddress = 0x00450090u; optimizedResult = boxDownsampleA1R5G5B5(context); break;
                case kFmtA4R4G4B4: optimizedAddress = 0x004501DFu; optimizedResult = boxDownsampleA4R4G4B4(context); break;
                case kFmtR3G3B2: optimizedAddress = 0x00450332u; optimizedResult = boxDownsampleR3G3B2(context); break;
                case kFmtA8:
                case kFmtL8: optimizedAddress = 0x00450471u; optimizedResult = boxDownsampleL8(context); break;
                case kFmtA8R3G3B2: optimizedAddress = 0x0045050Cu; optimizedResult = boxDownsampleA8R3G3B2(context); break;
                case kFmtX4R4G4B4: optimizedAddress = 0x00450658u; optimizedResult = boxDownsampleX4R4G4B4(context); break;
                case kFmtA8P8:
                case kFmtP8: optimizedAddress = 0x00450779u; optimizedResult = boxDownsampleUnsupportedA8P8(); break;
                case kFmtA8L8: optimizedAddress = 0x0045077Fu; optimizedResult = boxDownsampleA8L8(context); break;
                case kFmtA4L4: optimizedAddress = 0x004508CBu; optimizedResult = boxDownsampleA4L4(context); break;
                default: break;
                }
                if (context.state)
                {
                    context.state->boxOptimizedAttempted = optimizedAddress != 0u;
                    context.state->boxOptimizedHelperAddress = optimizedAddress;
                }
                if (static_cast<std::int32_t>(optimizedResult) >= 0)
                {
                    if (context.state)
                    {
                        context.state->boxOptimizedRoute = true;
                        context.state->copiedRows = static_cast<int>(destinationHeight);
                        context.state->copiedPixels = static_cast<int>(destinationWidth * destinationHeight);
                    }
                    return 0u;
                }
            }

            std::unique_ptr<Pixel[]> destinationRow(new (std::nothrow) Pixel[destinationWidth]);
            if (!destinationRow)
                return kOutOfMemory;

            const DWORD sourceRowCount = sourceHeight == 1u ? 1u : 2u;
            std::unique_ptr<Pixel[]> sourceRows(new (std::nothrow) Pixel[sourceWidth * sourceRowCount]);
            if (!sourceRows)
                return kOutOfMemory;

            Pixel* sourceRow0 = sourceRows.get();
            Pixel* sourceRow1 = sourceHeight == 1u ? sourceRows.get() : sourceRows.get() + sourceWidth;
            Pixel* sourceRow0Next = sourceWidth == 1u ? sourceRow0 : sourceRow0 + 1;
            Pixel* sourceRow1Next = sourceWidth == 1u ? sourceRow1 : sourceRow1 + 1;

            for (DWORD y = 0; y < destinationHeight; ++y)
            {
                descriptorReadRow(source, y * 2u, 0u, sourceRow0, context.state);
                if (sourceRow1 != sourceRow0)
                    descriptorReadRow(source, y * 2u + 1u, 0u, sourceRow1, context.state);
                for (DWORD x = 0; x < destinationWidth; ++x)
                {
                    const DWORD sourceX = x * 2u;
                    const Pixel& p00 = sourceRow0[sourceX];
                    const Pixel& p10 = sourceRow0Next[sourceX];
                    const Pixel& p01 = sourceRow1[sourceX];
                    const Pixel& p11 = sourceRow1Next[sourceX];
                    destinationRow[x] = {
                        (p00.r + p10.r + p01.r + p11.r) * 0.25f,
                        (p00.g + p10.g + p01.g + p11.g) * 0.25f,
                        (p00.b + p10.b + p01.b + p11.b) * 0.25f,
                        (p00.a + p10.a + p01.a + p11.a) * 0.25f,
                    };
                }
                if (!descriptorWriteRow(destination, y, 0u, destinationRow.get(), context.state))
                    return kFail;
            }
            if (context.state)
            {
                context.state->boxGenericRoute = true;
                context.state->copiedRows = static_cast<int>(destinationHeight);
                context.state->copiedPixels = static_cast<int>(destinationWidth * destinationHeight);
            }
            return 0u;
        }

        DWORD convertFilterBox3D(ConverterContext& context)
        {

            FormatObject& source = *context.source;
            FormatObject& destination = *context.destination;
            if ((context.filter & 0xFFu) != kFilterBox ||
                descriptorCategory(destination) != 1u ||
                descriptorCategory(source) != 1u)
                return kFail;

            const DWORD sourceWidth = descriptorWidth(source);
            const DWORD sourceHeight = descriptorHeight(source);
            const DWORD sourceDepth = descriptorDepth(source);
            const DWORD destinationWidth = descriptorWidth(destination);
            const DWORD destinationHeight = descriptorHeight(destination);
            const DWORD destinationDepth = descriptorDepth(destination);
            if ((destinationWidth != (sourceWidth >> 1u) &&
                 (destinationWidth != 1u || sourceWidth != 1u)) ||
                (destinationHeight != (sourceHeight >> 1u) &&
                 (destinationHeight != 1u || sourceHeight != 1u)) ||
                destinationDepth != (sourceDepth >> 1u))
                return kFail;

            Pixel* destinationRow = static_cast<Pixel*>(::operator new(
                static_cast<std::size_t>(destinationWidth) * sizeof(Pixel),
                std::nothrow));
            if (!destinationRow)
                return kOutOfMemory;

            const DWORD yRowsPerSlice = sourceHeight == 1u ? 1u : 2u;
            Pixel* sourceRows = static_cast<Pixel*>(::operator new(
                static_cast<std::size_t>(sourceWidth) * yRowsPerSlice * 2u * sizeof(Pixel),
                std::nothrow));
            if (!sourceRows)
            {
                ::operator delete(destinationRow);
                return kOutOfMemory;
            }

            Pixel* z0y0 = sourceRows;
            Pixel* z0y1 = sourceHeight == 1u ? z0y0 : z0y0 + sourceWidth;
            Pixel* z1y0 = sourceRows + sourceWidth * yRowsPerSlice;
            Pixel* z1y1 = sourceHeight == 1u ? z1y0 : z1y0 + sourceWidth;

            for (DWORD destinationZ = 0u; destinationZ < destinationDepth; ++destinationZ)
            {
                const DWORD sourceZ = destinationZ * 2u;
                for (DWORD destinationY = 0u; destinationY < destinationHeight; ++destinationY)
                {
                    const DWORD sourceY = destinationY * 2u;
                    descriptorReadRow(source, sourceY, sourceZ, z0y0, context.state);
                    if (z0y1 != z0y0)
                        descriptorReadRow(source, sourceY + 1u, sourceZ, z0y1, context.state);
                    descriptorReadRow(source, sourceY, sourceZ + 1u, z1y0, context.state);
                    if (z1y1 != z1y0)
                        descriptorReadRow(source, sourceY + 1u, sourceZ + 1u, z1y1, context.state);

                    for (DWORD destinationX = 0u; destinationX < destinationWidth; ++destinationX)
                    {
                        const DWORD sourceX = destinationX * 2u;
                        const DWORD sourceX1 = sourceWidth == 1u ? sourceX : sourceX + 1u;
                        const Pixel& p000 = z0y0[sourceX];
                        const Pixel& p100 = z0y0[sourceX1];
                        const Pixel& p010 = z0y1[sourceX];
                        const Pixel& p110 = z0y1[sourceX1];
                        const Pixel& p001 = z1y0[sourceX];
                        const Pixel& p101 = z1y0[sourceX1];
                        const Pixel& p011 = z1y1[sourceX];
                        const Pixel& p111 = z1y1[sourceX1];
                        destinationRow[destinationX] = {
                            (p000.r + p100.r + p010.r + p110.r + p001.r + p101.r + p011.r + p111.r) * 0.125f,
                            (p000.g + p100.g + p010.g + p110.g + p001.g + p101.g + p011.g + p111.g) * 0.125f,
                            (p000.b + p100.b + p010.b + p110.b + p001.b + p101.b + p011.b + p111.b) * 0.125f,
                            (p000.a + p100.a + p010.a + p110.a + p001.a + p101.a + p011.a + p111.a) * 0.125f,
                        };
                    }
                    if (!descriptorWriteRow(destination, destinationY, destinationZ, destinationRow, context.state))
                    {
                        ::operator delete(sourceRows);
                        ::operator delete(destinationRow);
                        return kFail;
                    }
                }
            }

            ::operator delete(sourceRows);
            ::operator delete(destinationRow);
            if (context.state)
            {
                context.state->copiedRows = static_cast<int>(destinationHeight * destinationDepth);
                context.state->copiedPixels = static_cast<int>(destinationWidth * destinationHeight * destinationDepth);
            }
            return 0u;
        }

        DWORD convertFilterLinear2D(ConverterContext& context)
        {
            FormatObject& source = *context.source;
            FormatObject& destination = *context.destination;
            if (descriptorCategory(destination) != 1u ||
                descriptorCategory(source) != 1u ||
                (context.filter & 0xFFu) != kFilterLinear)
                return kFail;

            const DWORD destinationWidth = descriptorWidth(destination);
            const DWORD destinationHeight = descriptorHeight(destination);
            const DWORD sourceWidth = descriptorWidth(source);
            const bool wrapX = ((~(context.filter >> 16u)) & 1u) != 0u;
            const bool wrapY = ((~(context.filter >> 17u)) & 1u) != 0u;

            LinearAxisEntry* xWeights = callBuildLinearAxisWeights(destinationWidth, sourceWidth, wrapX);
            LinearAxisEntry* yWeights = callBuildLinearAxisWeights(
                destinationHeight,
                descriptorHeight(source),
                wrapY);
            Pixel* destinationRow = static_cast<Pixel*>(::operator new(
                static_cast<std::size_t>(destinationWidth) * sizeof(Pixel),
                std::nothrow));
            Pixel* sourceRows = static_cast<Pixel*>(::operator new(
                static_cast<std::size_t>(sourceWidth) * 2u * sizeof(Pixel),
                std::nothrow));

            DWORD result = kOutOfMemory;
            if (xWeights && yWeights && destinationRow && sourceRows)
            {
                Pixel* firstRow = sourceRows;
                Pixel* secondRow = sourceRows + sourceWidth;
                std::int32_t firstRowIndex = -1;
                std::int32_t secondRowIndex = -1;

                for (DWORD destinationY = 0; destinationY < destinationHeight; ++destinationY)
                {
                    const LinearAxisEntry& y = yWeights[destinationY];
                    if (static_cast<std::int32_t>(y.firstIndex) != firstRowIndex)
                    {
                        if (static_cast<std::int32_t>(y.firstIndex) == secondRowIndex)
                        {
                            std::swap(firstRow, secondRow);
                            firstRowIndex = secondRowIndex;
                            secondRowIndex = -1;
                        }
                        else
                        {
                            firstRowIndex = static_cast<std::int32_t>(y.firstIndex);
                            descriptorReadRow(source, y.firstIndex, 0u, firstRow, context.state);
                        }
                    }
                    if (static_cast<std::int32_t>(y.secondIndex) != secondRowIndex)
                    {
                        secondRowIndex = static_cast<std::int32_t>(y.secondIndex);
                        descriptorReadRow(source, y.secondIndex, 0u, secondRow, context.state);
                    }

                    for (DWORD destinationX = 0; destinationX < destinationWidth; ++destinationX)
                    {
                        const LinearAxisEntry& x = xWeights[destinationX];
                        const Pixel& second0 = secondRow[x.firstIndex];
                        const Pixel& second1 = secondRow[x.secondIndex];
                        const Pixel& first0 = firstRow[x.firstIndex];
                        const Pixel& first1 = firstRow[x.secondIndex];

                        const float secondR = x.firstWeight * second0.r + x.secondWeight * second1.r;
                        const float secondG = x.firstWeight * second0.g + x.secondWeight * second1.g;
                        const float secondB = x.firstWeight * second0.b + x.secondWeight * second1.b;
                        const float secondA = x.firstWeight * second0.a + x.secondWeight * second1.a;
                        const float firstR = x.firstWeight * first0.r + x.secondWeight * first1.r;
                        const float firstG = x.firstWeight * first0.g + x.secondWeight * first1.g;
                        const float firstB = x.firstWeight * first0.b + x.secondWeight * first1.b;
                        const float firstA = x.firstWeight * first0.a + x.secondWeight * first1.a;

                        destinationRow[destinationX] = {
                            firstR * y.firstWeight + secondR * y.secondWeight,
                            firstG * y.firstWeight + secondG * y.secondWeight,
                            firstB * y.firstWeight + secondB * y.secondWeight,
                            firstA * y.firstWeight + secondA * y.secondWeight,
                        };
                    }

                    (void)descriptorWriteRow(destination, destinationY, 0u, destinationRow, context.state);
                    result = 0u;
                }
                if (destinationHeight == 0u)
                    result = 0u;
                if (result == 0u && context.state)
                {
                    context.state->copiedRows = static_cast<int>(destinationHeight);
                    context.state->copiedPixels = static_cast<int>(destinationWidth * destinationHeight);
                }
            }

            // Original cleanup order: X table, Y table, destination row, two-row cache.
            ::operator delete(xWeights);
            ::operator delete(yWeights);
            ::operator delete(destinationRow);
            ::operator delete(sourceRows);
            return result;
        }

        DWORD convertFilterLinear3D(ConverterContext& context)
        {

            FormatObject& source = *context.source;
            FormatObject& destination = *context.destination;
            if (descriptorCategory(destination) != 1u ||
                descriptorCategory(source) != 1u ||
                (context.filter & 0xFFu) != kFilterLinear)
                return kFail;

            const DWORD destinationWidth = descriptorWidth(destination);
            const DWORD destinationHeight = descriptorHeight(destination);
            const DWORD destinationDepth = descriptorDepth(destination);
            const DWORD sourceWidth = descriptorWidth(source);
            const bool wrapX = ((~(context.filter >> 16u)) & 1u) != 0u;
            const bool wrapY = ((~(context.filter >> 17u)) & 1u) != 0u;
            const bool wrapZ = ((~(context.filter >> 18u)) & 1u) != 0u;

            LinearAxisEntry* xWeights = callBuildLinearAxisWeights(destinationWidth, sourceWidth, wrapX);
            LinearAxisEntry* yWeights = callBuildLinearAxisWeights(destinationHeight, descriptorHeight(source), wrapY);
            LinearAxisEntry* zWeights = callBuildLinearAxisWeights(destinationDepth, descriptorDepth(source), wrapZ);
            Pixel* destinationRow = static_cast<Pixel*>(::operator new(
                static_cast<std::size_t>(destinationWidth) * sizeof(Pixel),
                std::nothrow));
            Pixel* sourceRows = static_cast<Pixel*>(::operator new(
                static_cast<std::size_t>(sourceWidth) * 4u * sizeof(Pixel),
                std::nothrow));

            DWORD result = kOutOfMemory;
            if (xWeights && yWeights && zWeights && destinationRow && sourceRows)
            {
                Pixel* firstZ0 = sourceRows;
                Pixel* secondZ0 = sourceRows + sourceWidth;
                Pixel* firstZ1 = sourceRows + sourceWidth * 2u;
                Pixel* secondZ1 = sourceRows + sourceWidth * 3u;

                for (DWORD destinationZ = 0u; destinationZ < destinationDepth; ++destinationZ)
                {
                    const LinearAxisEntry& z = zWeights[destinationZ];
                    std::int32_t firstRowIndex = -1;
                    std::int32_t secondRowIndex = -1;

                    for (DWORD destinationY = 0u; destinationY < destinationHeight; ++destinationY)
                    {
                        const LinearAxisEntry& y = yWeights[destinationY];
                        if (static_cast<std::int32_t>(y.firstIndex) != firstRowIndex)
                        {
                            if (static_cast<std::int32_t>(y.firstIndex) == secondRowIndex)
                            {
                                std::swap(firstZ0, secondZ0);
                                std::swap(firstZ1, secondZ1);
                                firstRowIndex = secondRowIndex;
                                secondRowIndex = -1;
                            }
                            else
                            {
                                firstRowIndex = static_cast<std::int32_t>(y.firstIndex);
                                descriptorReadRow(source, y.firstIndex, z.firstIndex, firstZ0, context.state);
                                descriptorReadRow(source, y.firstIndex, z.secondIndex, firstZ1, context.state);
                            }
                        }
                        if (static_cast<std::int32_t>(y.secondIndex) != secondRowIndex)
                        {
                            secondRowIndex = static_cast<std::int32_t>(y.secondIndex);
                            descriptorReadRow(source, y.secondIndex, z.firstIndex, secondZ0, context.state);
                            descriptorReadRow(source, y.secondIndex, z.secondIndex, secondZ1, context.state);
                        }

                        for (DWORD destinationX = 0u; destinationX < destinationWidth; ++destinationX)
                        {
                            const LinearAxisEntry& x = xWeights[destinationX];
                            auto interpX = [&](const Pixel* row) noexcept -> Pixel {
                                const Pixel& p0 = row[x.firstIndex];
                                const Pixel& p1 = row[x.secondIndex];
                                return {
                                    x.firstWeight * p0.r + x.secondWeight * p1.r,
                                    x.firstWeight * p0.g + x.secondWeight * p1.g,
                                    x.firstWeight * p0.b + x.secondWeight * p1.b,
                                    x.firstWeight * p0.a + x.secondWeight * p1.a,
                                };
                            };
                            const Pixel z0y0 = interpX(firstZ0);
                            const Pixel z0y1 = interpX(secondZ0);
                            const Pixel z1y0 = interpX(firstZ1);
                            const Pixel z1y1 = interpX(secondZ1);
                            const Pixel plane0 = {
                                z0y0.r * y.firstWeight + z0y1.r * y.secondWeight,
                                z0y0.g * y.firstWeight + z0y1.g * y.secondWeight,
                                z0y0.b * y.firstWeight + z0y1.b * y.secondWeight,
                                z0y0.a * y.firstWeight + z0y1.a * y.secondWeight,
                            };
                            const Pixel plane1 = {
                                z1y0.r * y.firstWeight + z1y1.r * y.secondWeight,
                                z1y0.g * y.firstWeight + z1y1.g * y.secondWeight,
                                z1y0.b * y.firstWeight + z1y1.b * y.secondWeight,
                                z1y0.a * y.firstWeight + z1y1.a * y.secondWeight,
                            };
                            destinationRow[destinationX] = {
                                plane0.r * z.firstWeight + plane1.r * z.secondWeight,
                                plane0.g * z.firstWeight + plane1.g * z.secondWeight,
                                plane0.b * z.firstWeight + plane1.b * z.secondWeight,
                                plane0.a * z.firstWeight + plane1.a * z.secondWeight,
                            };
                        }
                        // Retail vtable +8 writer is void here as well.
                        (void)descriptorWriteRow(
                            destination, destinationY, destinationZ, destinationRow, context.state);
                    }
                }
                result = 0u;
                if (context.state)
                {
                    context.state->copiedRows = static_cast<int>(destinationHeight * destinationDepth);
                    context.state->copiedPixels = static_cast<int>(destinationWidth * destinationHeight * destinationDepth);
                }
            }

            ::operator delete(xWeights);
            ::operator delete(yWeights);
            ::operator delete(zWeights);
            ::operator delete(destinationRow);
            ::operator delete(sourceRows);
            return result;
        }

        DWORD convertFilterTriangle2D(ConverterContext& context)
        {
            FormatObject& source = *context.source;
            FormatObject& destination = *context.destination;
            if (descriptorCategory(destination) != 1u ||
                descriptorCategory(source) != 1u ||
                descriptorDepth(destination) != 1u ||
                descriptorDepth(source) != 1u)
            {
                return kFail;
            }

            BYTE* xWeights = buildTriangleAxisWeights(
                descriptorWidth(source),
                descriptorWidth(destination),
                ((~(context.filter >> 16u)) & 1u) != 0u);
            BYTE* yWeights = buildTriangleAxisWeights(
                descriptorHeight(source),
                descriptorHeight(destination),
                ((~(context.filter >> 17u)) & 1u) != 0u);

            TriangleAccumulationNode** pendingRows = nullptr;
            TriangleAccumulationNode* freeRows = nullptr;
            Pixel* sourceRow = nullptr;
            DWORD result = kFail;

            if (xWeights && yWeights)
            {
                const DWORD destinationHeight = descriptorHeight(destination);
                const DWORD destinationWidth = descriptorWidth(destination);
                const DWORD sourceWidth = descriptorWidth(source);
                pendingRows = static_cast<TriangleAccumulationNode**>(::operator new(
                    static_cast<std::size_t>(destinationHeight) * sizeof(TriangleAccumulationNode*),
                    std::nothrow));
                if (pendingRows)
                {
                    std::memset(
                        pendingRows,
                        0,
                        static_cast<std::size_t>(destinationHeight) * sizeof(TriangleAccumulationNode*));
                    sourceRow = static_cast<Pixel*>(::operator new(
                        static_cast<std::size_t>(sourceWidth) * sizeof(Pixel),
                        std::nothrow));
                }

                if (pendingRows && sourceRow)
                {
                    DWORD activeRows = 0u;
                    DWORD sourceY = 0u;
                    const BYTE* const xEnd = xWeights + triangleReadDword(xWeights);
                    const BYTE* const yEnd = yWeights + triangleReadDword(yWeights);
                    const BYTE* yList = yWeights + 4u;
                    bool allocationFailed = false;
                    while (yList < yEnd && !allocationFailed)
                    {
                        const BYTE* const yListEnd = yList + triangleReadDword(yList);
                        const BYTE* yPair = yList + 4u;
                        while (yPair < yListEnd)
                        {
                            const DWORD destinationY = triangleReadDword(yPair);
                            if (!pendingRows[destinationY])
                            {
                                TriangleAccumulationNode* row = freeRows;
                                if (row)
                                {
                                    freeRows = row->next;
                                }
                                else
                                {
                                    row = static_cast<TriangleAccumulationNode*>(::operator new(
                                        sizeof(TriangleAccumulationNode),
                                        std::nothrow));
                                    if (!row)
                                    {
                                        allocationFailed = true;
                                        break;
                                    }
                                    row->pixels = static_cast<Pixel*>(::operator new(
                                        static_cast<std::size_t>(destinationWidth) * sizeof(Pixel),
                                        std::nothrow));
                                    row->completion = 0.0f;
                                    row->next = nullptr;
                                    if (!row->pixels)
                                    {
                                        destroyTriangleAccumulationChain(row);
                                        ::operator delete(row);
                                        allocationFailed = true;
                                        break;
                                    }
                                }
                                std::memset(
                                    row->pixels,
                                    0,
                                    static_cast<std::size_t>(destinationWidth) * sizeof(Pixel));
                                row->completion = 0.0f;
                                row->next = nullptr;
                                pendingRows[destinationY] = row;
                                ++activeRows;
                            }
                            yPair += sizeof(TriangleWeightPair);
                        }
                        if (allocationFailed)
                            break;

                        descriptorReadRow(source, sourceY, 0u, sourceRow, context.state);
                        const BYTE* xList = xWeights + 4u;
                        DWORD sourceX = 0u;
                        while (xList < xEnd)
                        {
                            const BYTE* const xListEnd = xList + triangleReadDword(xList);
                            yPair = yList + 4u;
                            while (yPair < yListEnd)
                            {
                                const DWORD destinationY = triangleReadDword(yPair);
                                const float yWeight = triangleReadFloat(yPair + 4u);
                                TriangleAccumulationNode* row = pendingRows[destinationY];
                                const BYTE* xPair = xList + 4u;
                                while (xPair < xListEnd)
                                {
                                    const DWORD destinationX = triangleReadDword(xPair);
                                    const float weight = triangleReadFloat(xPair + 4u) * yWeight;
                                    Pixel& output = row->pixels[destinationX];
                                    const Pixel& input = sourceRow[sourceX];
                                    output.r += weight * input.r;
                                    output.g += weight * input.g;
                                    output.b += weight * input.b;
                                    output.a += weight * input.a;
                                    xPair += sizeof(TriangleWeightPair);
                                }
                                yPair += sizeof(TriangleWeightPair);
                            }
                            ++sourceX;
                            xList = xListEnd;
                        }

                        yPair = yList + 4u;
                        while (yPair < yListEnd)
                        {
                            const DWORD destinationY = triangleReadDword(yPair);
                            TriangleAccumulationNode* row = pendingRows[destinationY];
                            row->completion += triangleReadFloat(yPair + 4u);
                            if (row->completion + 0.0000099999997f >= 1.0f)
                            {
                                (void)descriptorWriteRow(
                                    destination, destinationY, 0u, row->pixels, context.state);
                                pendingRows[destinationY] = nullptr;
                                --activeRows;
                                row->next = freeRows;
                                freeRows = row;
                            }
                            yPair += sizeof(TriangleWeightPair);
                        }

                        ++sourceY;
                        yList = yListEnd;
                    }

                    if (allocationFailed)
                    {
                        result = kOutOfMemory;
                    }
                    else
                    {
                        if (activeRows != 0u)
                        {
                            for (DWORD destinationY = 0u;
                                 destinationY < destinationHeight && activeRows != 0u;
                                 ++destinationY)
                            {
                                TriangleAccumulationNode* row = pendingRows[destinationY];
                                if (!row)
                                    continue;
                                (void)descriptorWriteRow(
                                    destination, destinationY, 0u, row->pixels, context.state);
                                destroyTriangleAccumulationChain(row);
                                ::operator delete(row);
                                pendingRows[destinationY] = nullptr;
                                --activeRows;
                            }
                        }
                        result = 0u;
                        if (result == 0u && context.state)
                        {
                            context.state->copiedRows = static_cast<int>(destinationHeight);
                            context.state->copiedPixels = static_cast<int>(
                                destinationWidth * destinationHeight);
                        }
                    }
                }
                else
                {
                    result = kOutOfMemory;
                }
            }

            ::operator delete(pendingRows);
            if (freeRows)
            {
                destroyTriangleAccumulationChain(freeRows);
                ::operator delete(freeRows);
            }
            ::operator delete(yWeights);
            ::operator delete(xWeights);
            ::operator delete(sourceRow);
            ::operator delete(nullptr);
            return result;
        }

        DWORD convertFilterTriangle3D(ConverterContext& context)
        {

            FormatObject& source = *context.source;
            FormatObject& destination = *context.destination;
            if (descriptorCategory(destination) != 1u || descriptorCategory(source) != 1u)
                return kFail;

            const DWORD sourceWidth = descriptorWidth(source);
            const DWORD sourceHeight = descriptorHeight(source);
            const DWORD sourceDepth = descriptorDepth(source);
            const DWORD destinationWidth = descriptorWidth(destination);
            const DWORD destinationHeight = descriptorHeight(destination);
            const DWORD destinationDepth = descriptorDepth(destination);

            BYTE* xWeights = buildTriangleAxisWeights(
                sourceWidth, destinationWidth, ((~(context.filter >> 16u)) & 1u) != 0u);
            BYTE* yWeights = buildTriangleAxisWeights(
                sourceHeight, destinationHeight, ((~(context.filter >> 17u)) & 1u) != 0u);
            BYTE* zWeights = buildTriangleAxisWeights(
                sourceDepth, destinationDepth, ((~(context.filter >> 18u)) & 1u) != 0u);

            TriangleAccumulationNode** pendingSlices = nullptr;
            TriangleAccumulationNode* freeSlices = nullptr;
            Pixel* sourceRow = nullptr;
            DWORD result = kFail;

            if (xWeights && yWeights && zWeights)
            {
                pendingSlices = static_cast<TriangleAccumulationNode**>(::operator new(
                    static_cast<std::size_t>(destinationDepth) * sizeof(TriangleAccumulationNode*),
                    std::nothrow));
                sourceRow = static_cast<Pixel*>(::operator new(
                    static_cast<std::size_t>(sourceWidth) * sizeof(Pixel),
                    std::nothrow));
                if (pendingSlices && sourceRow)
                {
                    std::memset(
                        pendingSlices,
                        0,
                        static_cast<std::size_t>(destinationDepth) * sizeof(TriangleAccumulationNode*));
                    DWORD activeSlices = 0u;
                    const BYTE* const xEnd = xWeights + triangleReadDword(xWeights);
                    const BYTE* const yEnd = yWeights + triangleReadDword(yWeights);
                    const BYTE* const zEnd = zWeights + triangleReadDword(zWeights);
                    const BYTE* zList = zWeights + 4u;
                    DWORD sourceZ = 0u;
                    bool allocationFailed = false;

                    while (zList < zEnd && !allocationFailed)
                    {
                        const BYTE* const zListEnd = zList + triangleReadDword(zList);
                        const BYTE* zPair = zList + 4u;
                        while (zPair < zListEnd)
                        {
                            const DWORD destinationZ = triangleReadDword(zPair);
                            if (!pendingSlices[destinationZ])
                            {
                                TriangleAccumulationNode* slice = freeSlices;
                                if (slice)
                                {
                                    freeSlices = slice->next;
                                }
                                else
                                {
                                    slice = static_cast<TriangleAccumulationNode*>(::operator new(
                                        sizeof(TriangleAccumulationNode),
                                        std::nothrow));
                                    if (!slice)
                                    {
                                        allocationFailed = true;
                                        break;
                                    }
                                    slice->pixels = static_cast<Pixel*>(::operator new(
                                        static_cast<std::size_t>(destinationWidth) *
                                            destinationHeight * sizeof(Pixel),
                                        std::nothrow));
                                    slice->completion = 0.0f;
                                    slice->next = nullptr;
                                    if (!slice->pixels)
                                    {
                                        destroyTriangleAccumulationChain(slice);
                                        ::operator delete(slice);
                                        allocationFailed = true;
                                        break;
                                    }
                                }
                                std::memset(
                                    slice->pixels,
                                    0,
                                    static_cast<std::size_t>(destinationWidth) *
                                        destinationHeight * sizeof(Pixel));
                                slice->completion = 0.0f;
                                slice->next = nullptr;
                                pendingSlices[destinationZ] = slice;
                                ++activeSlices;
                            }
                            zPair += sizeof(TriangleWeightPair);
                        }
                        if (allocationFailed)
                            break;

                        const BYTE* yList = yWeights + 4u;
                        DWORD sourceY = 0u;
                        while (yList < yEnd)
                        {
                            const BYTE* const yListEnd = yList + triangleReadDword(yList);
                            descriptorReadRow(source, sourceY, sourceZ, sourceRow, context.state);

                            const BYTE* xList = xWeights + 4u;
                            DWORD sourceX = 0u;
                            while (xList < xEnd)
                            {
                                const BYTE* const xListEnd = xList + triangleReadDword(xList);
                                zPair = zList + 4u;
                                while (zPair < zListEnd)
                                {
                                    const DWORD destinationZ = triangleReadDword(zPair);
                                    const float zWeight = triangleReadFloat(zPair + 4u);
                                    TriangleAccumulationNode* slice = pendingSlices[destinationZ];
                                    const BYTE* yPair = yList + 4u;
                                    while (yPair < yListEnd)
                                    {
                                        const DWORD destinationY = triangleReadDword(yPair);
                                        const float yzWeight = zWeight * triangleReadFloat(yPair + 4u);
                                        Pixel* const outputRow =
                                            slice->pixels + destinationY * destinationWidth;
                                        const BYTE* xPair = xList + 4u;
                                        while (xPair < xListEnd)
                                        {
                                            const DWORD destinationX = triangleReadDword(xPair);
                                            const float weight =
                                                yzWeight * triangleReadFloat(xPair + 4u);
                                            Pixel& output = outputRow[destinationX];
                                            const Pixel& input = sourceRow[sourceX];
                                            output.r += weight * input.r;
                                            output.g += weight * input.g;
                                            output.b += weight * input.b;
                                            output.a += weight * input.a;
                                            xPair += sizeof(TriangleWeightPair);
                                        }
                                        yPair += sizeof(TriangleWeightPair);
                                    }
                                    zPair += sizeof(TriangleWeightPair);
                                }
                                ++sourceX;
                                xList = xListEnd;
                            }

                            ++sourceY;
                            yList = yListEnd;
                        }

                        zPair = zList + 4u;
                        while (zPair < zListEnd)
                        {
                            const DWORD destinationZ = triangleReadDword(zPair);
                            TriangleAccumulationNode* slice = pendingSlices[destinationZ];
                            slice->completion += triangleReadFloat(zPair + 4u);
                            if (slice->completion + 0.0000099999997f >= 1.0f)
                            {
                                for (DWORD destinationY = 0u; destinationY < destinationHeight; ++destinationY)
                                {
                                (void)descriptorWriteRow(
                                    destination,
                                    destinationY,
                                    destinationZ,
                                    slice->pixels + destinationY * destinationWidth,
                                    context.state);
                                }
                                pendingSlices[destinationZ] = nullptr;
                                --activeSlices;
                                slice->next = freeSlices;
                                freeSlices = slice;
                            }
                            zPair += sizeof(TriangleWeightPair);
                        }

                        ++sourceZ;
                        zList = zListEnd;
                    }

                    if (allocationFailed)
                    {
                        result = kOutOfMemory;
                    }
                    else
                    {
                        if (activeSlices != 0u)
                        {
                            for (DWORD destinationZ = 0u;
                                 destinationZ < destinationDepth && activeSlices != 0u;
                                 ++destinationZ)
                            {
                                TriangleAccumulationNode* slice = pendingSlices[destinationZ];
                                if (!slice)
                                    continue;
                                for (DWORD destinationY = 0u; destinationY < destinationHeight; ++destinationY)
                                {
                                (void)descriptorWriteRow(
                                    destination,
                                    destinationY,
                                    destinationZ,
                                    slice->pixels + destinationY * destinationWidth,
                                    context.state);
                                }
                                destroyTriangleAccumulationChain(slice);
                                ::operator delete(slice);
                                pendingSlices[destinationZ] = nullptr;
                                --activeSlices;
                            }
                        }
                        result = 0u;
                        if (result == 0u && context.state)
                        {
                            context.state->copiedRows = static_cast<int>(destinationHeight * destinationDepth);
                            context.state->copiedPixels = static_cast<int>(
                                destinationWidth * destinationHeight * destinationDepth);
                        }
                    }
                }
                else
                {
                    result = kOutOfMemory;
                }
            }

            ::operator delete(pendingSlices);
            if (freeSlices)
            {
                destroyTriangleAccumulationChain(freeSlices);
                ::operator delete(freeSlices);
            }
            ::operator delete(zWeights);
            ::operator delete(yWeights);
            ::operator delete(xWeights);
            ::operator delete(sourceRow);
            ::operator delete(nullptr);
            return result;
        }

        using ConverterFunction = DWORD (*)(ConverterContext&);

        DWORD convertSurfaceViews(ConverterContext& context,
                         const MemoryView& destination,
                         const MemoryView& source,
                         DWORD filter,
                         DWORD colorKey)
        {

            initializeConverterContext(context);
            context.filter = filter;
            if (!validFilter(filter))
            {
                if (context.state)
                {
                    context.state->rejectedInvalidArgument = true;
                    context.state->filterType = filter & 0xFFFFu;
                }
                return kInvalidCall;
            }

            MemoryView destinationRecord = destination;
            destinationRecord.dither = (filter & kFilterDither) != 0u;
            context.destination = createFormatDescriptor(destinationRecord, 0u, context.state, true);
            DWORD result = kFail;
            if (context.destination)
                context.source = createFormatDescriptor(source, colorKey, context.state, false);

            if (context.destination && context.source)
            {
                if (descriptorCategory(*context.destination) == descriptorCategory(*context.source))
                {
                    constexpr ConverterFunction converters[10] = {
                        copySurfaceDirect, convertSameSize, convertFilterNone, convertFilterPoint, convertFilterBox2D,
                        convertFilterBox3D, convertFilterLinear2D, convertFilterLinear3D, convertFilterTriangle2D, convertFilterTriangle3D,
                    };
                    for (int index = 0; index < 10; ++index)
                    {
                        if (context.state)
                            context.state->converterAttemptMask |= (1u << index);
                        const DWORD converterResult = converters[index](context);
                        if (static_cast<std::int32_t>(converterResult) >= 0)
                        {
                            result = 0u;
                            if (context.state)
                            {
                                context.state->converterSelectedIndex = index;
                                context.state->converterSelectedAddress = kConverterAddresses[index];
                                context.state->filterRoute = index >= 2;
                                context.state->filterType = filter & 0xFFFFu;
                            }
                            break;
                        }
                    }
                }
                else if (context.state)
                {
                    context.state->descriptorCategoryMismatch = true;
                }
            }
            releaseConverterContext(context);
            return result;
        }


        DWORD convertMemory(const MemoryView& destination,
                            const MemoryView& source,
                            DWORD filter,
                            DWORD colorKey,
                            SurfaceTransferState* state)
        {
            const int destinationWidth = rectWidth(destination.rect);
            const int destinationHeight = rectHeight(destination.rect);
            const int sourceWidth = rectWidth(source.rect);
            const int sourceHeight = rectHeight(source.rect);
            if (destinationWidth <= 0 || destinationHeight <= 0 || sourceWidth <= 0 || sourceHeight <= 0)
                return kInvalidCall;
            if (state)
            {
                state->formatConversionRequired = destination.format != source.format;
                state->filterType = filter & 0xFFFFu;
            }
            ConverterContext context{};
            context.state = state;
            initializeConverterContext(context);
            return convertSurfaceViews(context, destination, source, filter, colorKey);
        }
    }

#ifdef _WIN32
    HRESULT __stdcall loadSurfaceFromMemoryRetail(IDirect3DSurface8* destinationSurface,
                                 const PALETTEENTRY* destinationPalette,
                                 const RECTI* destinationRect,
                                 const void* sourceMemory,
                                 D3DFORMAT sourceFormat,
                                 UINT sourcePitch,
                                 const PALETTEENTRY* sourcePalette,
                                 const RECTI* sourceRect,
                                 DWORD filterFlags,
                                 D3DCOLOR colorKey)
    {

        ConverterContext owner{};
        initializeConverterContext(owner);
        owner.state = nullptr;

        if (!destinationSurface || !sourceMemory || !sourceRect)
        {
            releaseConverterContext(owner);
            return static_cast<HRESULT>(kInvalidCall);
        }

        DWORD normalizedFilter = filterFlags;
        if (normalizedFilter == 0xFFFFFFFFu)
        {
            const DWORD* const formatEntry = findSurfaceFormatEntry(static_cast<DWORD>(sourceFormat));
            normalizedFilter = 0x00080002u + 2u * (formatEntry[1] != 3u ? 1u : 0u);
        }

        D3DSURFACE_DESC destinationDesc{};
        destinationSurface->GetDesc(&destinationDesc);

        RECTI lockedDestinationRect{};
        if (destinationRect)
            lockedDestinationRect = *destinationRect;
        else
            lockedDestinationRect = {0, 0, static_cast<int>(destinationDesc.Width), static_cast<int>(destinationDesc.Height)};

        if (!validRect(lockedDestinationRect) ||
            lockedDestinationRect.left < 0 || lockedDestinationRect.top < 0 ||
            static_cast<DWORD>(lockedDestinationRect.right) > destinationDesc.Width ||
            lockedDestinationRect.left > lockedDestinationRect.right ||
            static_cast<DWORD>(lockedDestinationRect.bottom) > destinationDesc.Height ||
            lockedDestinationRect.top > lockedDestinationRect.bottom)
        {
            releaseConverterContext(owner);
            return static_cast<HRESULT>(kInvalidCall);
        }

        RECTI normalizedDestinationRect = lockedDestinationRect;
        const bool wholeDestination = wholeSurfaceLockFormat(static_cast<DWORD>(destinationDesc.Format));
        D3DLOCKED_RECT destinationLock{};
        const RECT* destinationLockRect = wholeDestination ? nullptr : reinterpret_cast<const RECT*>(&lockedDestinationRect);
        const HRESULT destinationLockResult = destinationSurface->LockRect(&destinationLock, destinationLockRect, 0u);
        if (FAILED(destinationLockResult))
        {
            releaseConverterContext(owner);
            return destinationLockResult;
        }

        if (!wholeDestination)
        {
            normalizedDestinationRect.right -= normalizedDestinationRect.left;
            normalizedDestinationRect.bottom -= normalizedDestinationRect.top;
            normalizedDestinationRect.left = 0;
            normalizedDestinationRect.top = 0;
        }

        MemoryView destinationView{};
        destinationView.bits = static_cast<BYTE*>(destinationLock.pBits);
        destinationView.pitch = destinationLock.Pitch;
        destinationView.format = static_cast<DWORD>(destinationDesc.Format);
        destinationView.rect = normalizedDestinationRect;
        destinationView.fullWidth = destinationDesc.Width;
        destinationView.fullHeight = destinationDesc.Height;
        destinationView.palette = reinterpret_cast<const BYTE*>(destinationPalette);
        destinationView.dither = (normalizedFilter & kFilterDither) != 0u;

        MemoryView sourceView{};
        sourceView.bits = const_cast<BYTE*>(static_cast<const BYTE*>(sourceMemory));
        sourceView.pitch = static_cast<int>(sourcePitch);
        sourceView.format = static_cast<DWORD>(sourceFormat);
        sourceView.rect = *sourceRect;
        sourceView.fullWidth = static_cast<DWORD>(std::max(sourceRect->right, 0));
        sourceView.fullHeight = static_cast<DWORD>(std::max(sourceRect->bottom, 0));
        sourceView.palette = reinterpret_cast<const BYTE*>(sourcePalette);

        DWORD conversionResult = convertSurfaceViews(owner, destinationView, sourceView,
                                             normalizedFilter, colorKey);
        if (static_cast<std::int32_t>(conversionResult) >= 0)
            conversionResult = 0u;

        destinationSurface->UnlockRect();
        releaseConverterContext(owner);
        return static_cast<HRESULT>(conversionResult);
    }


    HRESULT __stdcall loadSurfaceFromSurfaceRetail(IDirect3DSurface8* destinationSurface,
                                 const PALETTEENTRY* destinationPalette,
                                 const RECTI* destinationRect,
                                 IDirect3DSurface8* sourceSurface,
                                 const PALETTEENTRY* sourcePalette,
                                 const RECTI* sourceRect,
                                 DWORD filterFlags,
                                 D3DCOLOR colorKey)
    {

        if (!destinationSurface || !sourceSurface)
            return static_cast<HRESULT>(kInvalidCall);

        D3DSURFACE_DESC sourceDesc{};
        sourceSurface->GetDesc(&sourceDesc);

        RECTI lockedSourceRect{};
        if (sourceRect)
            lockedSourceRect = *sourceRect;
        else
            lockedSourceRect = {0, 0, static_cast<int>(sourceDesc.Width), static_cast<int>(sourceDesc.Height)};
        if (!validRect(lockedSourceRect) ||
            lockedSourceRect.right > static_cast<int>(sourceDesc.Width) ||
            lockedSourceRect.bottom > static_cast<int>(sourceDesc.Height))
        {
            return static_cast<HRESULT>(kInvalidCall);
        }

        RECTI normalizedSourceRect = lockedSourceRect;
        const bool wholeSource = wholeSurfaceLockFormat(static_cast<DWORD>(sourceDesc.Format));
        D3DLOCKED_RECT sourceLock{};
        const RECT* sourceLockRect = wholeSource ? nullptr : reinterpret_cast<const RECT*>(&lockedSourceRect);
        const HRESULT sourceLockResult = sourceSurface->LockRect(&sourceLock, sourceLockRect, 0x10u);
        if (FAILED(sourceLockResult))
            return sourceLockResult;

        if (!wholeSource)
        {
            normalizedSourceRect.right -= normalizedSourceRect.left;
            normalizedSourceRect.bottom -= normalizedSourceRect.top;
            normalizedSourceRect.left = 0;
            normalizedSourceRect.top = 0;
        }

        HRESULT result = loadSurfaceFromMemoryRetail(
            destinationSurface,
            destinationPalette,
            destinationRect,
            sourceLock.pBits,
            sourceDesc.Format,
            static_cast<UINT>(sourceLock.Pitch),
            sourcePalette,
            &normalizedSourceRect,
            filterFlags,
            colorKey);
        if (result >= 0)
            result = 0;
        sourceSurface->UnlockRect();
        return result;
    }


    SurfaceTransferState CopySurfaceRegion(IDirect3DDevice8*,
                                           IDirect3DSurface8* destinationSurface,
                                           IDirect3DSurface8* sourceSurface,
                                           const RECTI& sourceRect,
                                           const RECTI& destinationRect,
                                           DWORD filterFlags)
    {
        SurfaceTransferState state{};
        state.recorded = true;
        state.sourceRect = sourceRect;
        state.destinationRect = destinationRect;
        state.filterFlags = filterFlags;
        state.result = static_cast<DWORD>(loadSurfaceFromSurfaceRetail(
            destinationSurface,
            nullptr,
            &destinationRect,
            sourceSurface,
            nullptr,
            &sourceRect,
            filterFlags,
            0u));
        return state;
    }
#else
    SurfaceTransferState CopySurfaceRegion(void*,
                                           void* destinationSurface,
                                           void* sourceSurface,
                                           const RECTI& sourceRect,
                                           const RECTI& destinationRect,
                                           DWORD filterFlags)
    {
        SurfaceTransferState state{};
        state.recorded = true;
        state.sourceRect = sourceRect;
        state.destinationRect = destinationRect;
        state.filterFlags = filterFlags;
        state.rejectedInvalidArgument = destinationSurface == nullptr || sourceSurface == nullptr;
        state.result = kInvalidCall;
        return state;
    }
#endif
}
