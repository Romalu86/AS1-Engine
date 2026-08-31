#include "vid/vid_software.h"
#include "graph.h"
#include "map.h"
#include "sprite.h"
#include "core/application.h"
#include "core/resource.h"
#include "core/log.h"
#include "script/logic_runtime.h"
#include "graphics/base_texture.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <array>
#include <cstring>
#include <memory>
#include <new>

namespace as1
{
    namespace
    {
        int retailDrawXFtolSoftware(float x, float cameraX, int halfWidth) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            __int64 converted = 0;
            unsigned short oldControl = 0;
            unsigned short truncateControl = 0;
            __asm
            {
                fld x
                fsub cameraX
                fisub halfWidth
                fstcw oldControl
                fwait
                mov ax, oldControl
                or ah, 0Ch
                mov truncateControl, ax
                fldcw truncateControl
                fistp qword ptr converted
                fldcw oldControl
            }
            return static_cast<int>(static_cast<unsigned int>(converted));
#else
            const long double value = static_cast<long double>(x) -
                                      static_cast<long double>(cameraX) -
                                      static_cast<long double>(halfWidth);
            if (!std::isfinite(value) ||
                value < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                value > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(value));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
#endif
        }

        int retailDrawYFtolSoftware(float y, float z, float cameraY, int halfHeight) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            __int64 converted = 0;
            unsigned short oldControl = 0;
            unsigned short truncateControl = 0;
            __asm
            {
                fld y
                fsub z
                fsub cameraY
                fisub halfHeight
                fstcw oldControl
                fwait
                mov ax, oldControl
                or ah, 0Ch
                mov truncateControl, ax
                fldcw truncateControl
                fistp qword ptr converted
                fldcw oldControl
            }
            return static_cast<int>(static_cast<unsigned int>(converted));
#else
            const long double value = static_cast<long double>(y) -
                                      static_cast<long double>(z) -
                                      static_cast<long double>(cameraY) -
                                      static_cast<long double>(halfHeight);
            if (!std::isfinite(value) ||
                value < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                value > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(value));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
#endif
        }
    }
#if defined(_MSC_VER) && defined(_M_IX86)
#define AS1_VID_SOFTWARE_STDCALL __stdcall
#else
#define AS1_VID_SOFTWARE_STDCALL
#endif

    int drawOpaquePaletteSpanWithDepth(BYTE* paletteIndexes, WORD* depth, DWORD* color, int count) noexcept
    {

        const std::int32_t result = static_cast<std::int32_t>(g_packedSoftwareDepth);
        int remaining = count;
        do
        {
            if (static_cast<std::int16_t>(static_cast<WORD>(g_packedSoftwareDepth)) >
                static_cast<std::int16_t>(*depth))
            {
                *depth = static_cast<WORD>(g_packedSoftwareDepth);
                *color = g_softwarePaletteLookup[*paletteIndexes];
            }
            ++color;
            ++depth;
            ++paletteIndexes;
            --remaining;
        } while (remaining > 0);
        return result;
    }

    int drawAlphaPaletteSpanWithDepth(const BYTE* paletteIndexes, WORD* depth, DWORD* color, int count) noexcept
    {

        if (count <= 0)
            return count;
        for (int i = 0; i < count; ++i)
        {
            if (static_cast<WORD>(g_packedSoftwareDepth) >= depth[i])
            {
                const DWORD source = g_softwarePaletteLookup[paletteIndexes[i]];
                const int alpha = static_cast<int>(source >> 24u);
                DWORD resultOwner = source;
                (void)blendBgraPixelByAlpha(&color[i], &resultOwner, static_cast<int>(source), alpha);
            }
        }
        return count;
    }

    DWORD* blendBgraPixelByAlpha(DWORD* destination, DWORD* resultOwner, int sourceColor, int sourceAlpha) noexcept
    {

        const std::uint32_t weightSource = static_cast<std::uint32_t>(sourceAlpha) + 1u;
        const std::uint32_t weightDestination = 0x100u - weightSource;
        const std::uint32_t dst = *destination;
        const std::uint32_t src = static_cast<std::uint32_t>(sourceColor);

        auto blend = [weightSource, weightDestination](std::uint32_t d, std::uint32_t s) noexcept
        {
            const std::uint32_t a = static_cast<std::uint32_t>(d * weightDestination);
            const std::uint32_t b = static_cast<std::uint32_t>(s * weightSource);
            return static_cast<std::uint32_t>(a + b) >> 8u;
        };

        std::uint32_t blue = blend(dst & 0xFFu, src & 0xFFu);
        std::uint32_t green = blend((dst >> 8u) & 0xFFu, (src >> 8u) & 0xFFu);
        std::uint32_t red = blend((dst >> 16u) & 0xFFu, (src >> 16u) & 0xFFu);

        auto clampByte = [](std::uint32_t value) noexcept -> std::uint32_t
        {
            const std::int32_t signedValue = static_cast<std::int32_t>(value);
            if (signedValue < 0)
                return 0u;
            if (signedValue > 255)
                return 255u;
            return value;
        };
        blue = clampByte(blue);
        green = clampByte(green);
        red = clampByte(red);

        const DWORD blended = 0xFF000000u | (red << 16u) | (green << 8u) | blue;
        *destination = blended;
        *resultOwner = blended;
        return resultOwner;
    }

    void AS1_VID_SOFTWARE_STDCALL applyGammaToPaletteEntries(DWORD* entries, const GammaRawPair* rawGamma)
    {

        if (!entries || (rawGamma->first == 0u && rawGamma->second == 0u))
            return;
        for (unsigned int index = 0; index < 256u; ++index)
            entries[index] = GammaRawBlend(*rawGamma, entries[index]);
    }

    VID_SOFTWARE::VID_SOFTWARE(const VID_SOFTWARE& other)
        : VID()
    {

        nextMirror = other.nextMirrorVid();
        const_cast<VID_SOFTWARE&>(other).nextMirror = this;
        layer = other.layer;
        type = other.formatFlags();
        noCadr = static_cast<short>(other.totalFrames());
        frameSpeedDefault = other.defaultFrameSpeed();
        vidSizeX = static_cast<short>(other.vidWidth());
        vidSizeY = static_cast<short>(other.vidHeight());
        m_frameOffsets = other.m_frameOffsets;
        m_frameStorage = other.m_frameStorage;
        m_frameStorageBytes = other.m_frameStorageBytes;
    }

    VID_SOFTWARE::~VID_SOFTWARE()
    {

        if (isMirrorChainOwner())
        {
            if (m_frameStorage)
                ::operator delete(m_frameStorage);
            m_frameStorage = nullptr;

            if (m_frameOffsets)
                ::operator delete(m_frameOffsets);
            m_frameOffsets = nullptr;

            g_vidAllocatedBytes -= static_cast<int>(m_frameStorageBytes);
            m_frameStorageBytes = 0;
        }
    }

    VID_SOFTWARE* vidSoftwareScalarDeletingDestructor(VID_SOFTWARE* owner, unsigned char deletingFlags) noexcept
    {
        owner->~VID_SOFTWARE();
        if ((deletingFlags & 1u) != 0u)
            ::operator delete(owner);
        return owner;
    }

    VID_SOFTWARE* VID_SOFTWARE::CreateMirror()
    {

        return new (std::nothrow) VID_SOFTWARE(*this);
    }

    namespace
    {
        constexpr size_t SOFTWARE_GAMMA_ENTRY_COUNT = 256u;
    }

    void VID_SOFTWARE::Load(RESOURCE* resource)
    {

        loadCompactSoftwareVidData(resource);
    }

    void VID_SOFTWARE::loadCompactSoftwareVidData(RESOURCE* resource)
    {

        std::unique_ptr<script::LogicAdaptiveCodec> codec;
        if ((formatFlags() & VID_TYPE_COMPRESS) != 0u)
        {
            codec.reset(new (std::nothrow) script::LogicAdaptiveCodec());
            if (codec)
                codec->setMode(1);
        }

        std::array<DWORD, 256> paletteWords{};
        if ((formatFlags() & VID_TYPE_PALETTE) != 0u)
        {
            if (resource->GoNext(RESOURCE::ResTypes::PALETTE))
            {
                ReportResourceError(5, "PAL ", 0);
            }
            else if ((formatFlags() & VID_TYPE_NEWVERSION) != 0u)
            {
                resource->read(paletteWords.data(), 1024u);
            }
            else
            {
                std::array<BYTE, 768> rgb{};
                resource->read(rgb.data(), static_cast<unsigned>(rgb.size()));
                for (std::size_t index = 0; index < 256u; ++index)
                {
                    const DWORD r = rgb[index * 3u + 0u];
                    const DWORD g = rgb[index * 3u + 1u];
                    const DWORD b = rgb[index * 3u + 2u];
                    paletteWords[index] = 0xFF000000u | (r << 16u) | (g << 8u) | b;
                }
            }

            if (gammaRaw.first != 0u || gammaRaw.second != 0u)
            {
                for (DWORD& color : paletteWords)
                    color = GammaRawBlend(gammaRaw, color);
            }
        }

        if (resource->GoNext(RESOURCE::ResTypes::DATA))
            ReportResourceError(5, "DATA", 0);

        m_frameStorageBytes = static_cast<DWORD>(resource->CurrentResourceSize());
        const DWORD paletteBlockBytes = (formatFlags() & VID_TYPE_PALETTE) != 0u
            ? static_cast<DWORD>(PaletteSize())
            : 0u;
        if ((formatFlags() & VID_TYPE_PALETTE) != 0u)
            m_frameStorageBytes += 2u * paletteBlockBytes;

        m_frameStorage = static_cast<BYTE*>(::operator new(m_frameStorageBytes, std::nothrow));
        if (!m_frameStorage)
        {
            ReportResourceError(2, "Cadr", static_cast<int>(m_frameStorageBytes));
            return;
        }

        m_frameOffsets = static_cast<DWORD*>(::operator new(4u * static_cast<std::size_t>(static_cast<WORD>(noCadr)), std::nothrow));
        if (!m_frameOffsets)
        {
            ReportResourceError(2, "CadrShift", static_cast<int>(static_cast<WORD>(noCadr)));
            return;
        }

        DWORD dataOffset = 0u;
        if ((formatFlags() & VID_TYPE_PALETTE) != 0u)
        {
            if (paletteBlockBytes == 1024u)
            {
                std::memcpy(m_frameStorage, paletteWords.data(), 1024u);
            }
            else
            {
                WORD* packed = reinterpret_cast<WORD*>(m_frameStorage);
                for (std::size_t index = 0; index < 256u; ++index)
                {
                    const DWORD color = paletteWords[index];
                    packed[index] = static_cast<WORD>(
                        ((color >> 3u) & 0x001Fu) |
                        (g_color16RedMask & (color >> (16u - g_color16RedShift))) |
                        (g_color16GreenMask & (color >> (8u - g_color16GreenShift))));
                }
            }

            std::memcpy(m_frameStorage + paletteBlockBytes, m_frameStorage, paletteBlockBytes);
            dataOffset = 2u * paletteBlockBytes;
        }

        for (int frame = 0; frame < static_cast<int>(static_cast<WORD>(noCadr)); ++frame)
        {
            DWORD decodedSize = 0u;
            resource->read(&decodedSize, 4u);
            const int missing = readResourcePayload(*resource,
                                           m_frameStorage + dataOffset,
                                           decodedSize,
                                           codec.get());
            if (missing != 0)
                ReportResourceError(5, "Can't decode soft", static_cast<int>(decodedSize) - missing);

            if (decodedSize == 2u)
            {
                const WORD alias = *reinterpret_cast<const WORD*>(m_frameStorage + dataOffset);
                m_frameOffsets[frame] = m_frameOffsets[alias];
            }
            else
            {
                const WORD typeFlags = formatFlags();
                const GRAPH* const graph = GRAPH::CurrentGraph();
                const BASE_TEXTURE* const hi = graph->hiBuffer();
                if ((typeFlags & VID_TYPE_PALETTE) == 0u && hi->format() == 24u &&
                    (typeFlags & VID_TYPE_ALPHA) == 0u)
                {
                    BYTE* row = m_frameStorage + dataOffset;
                    row += 6u * (*reinterpret_cast<WORD*>(row)) + 2u;
                    int line = *reinterpret_cast<WORD*>(row);
                    const int lineEnd = line + *reinterpret_cast<WORD*>(row + 2u);
                    row += 4u;
                    while (line < lineEnd)
                    {
                        while (*reinterpret_cast<WORD*>(row) != 0u)
                        {
                            int run = row[1];
                            row += 2u;
                            while (run > 0)
                            {
                                const DWORD value = *reinterpret_cast<WORD*>(row);
                                *reinterpret_cast<WORD*>(row) = static_cast<WORD>((value & 0x001Fu) | ((value >> 1u) & 0x7FE0u));
                                row += 2u;
                                --run;
                            }
                        }
                        row += 2u;
                        ++line;
                    }
                }

                m_frameOffsets[frame] = dataOffset;
                dataOffset += decodedSize;
            }
            resource->GoNextSub(RESOURCE::ResTypes::DATA);
        }

        g_vidAllocatedBytes += static_cast<int>(m_frameStorageBytes);
        SetLayer();

    }

    void VID_SOFTWARE::SetLayer()
    {

        if ((property & P_ALWAYSTOP) != 0)
            layer = 10;
        else if ((formatFlags() & VID_TYPE_ALPHA) != 0)
            layer = 4;
        else if (movementTactEnabled())
            layer = 3;
        else
            layer = 2;
    }

    int VID_SOFTWARE::SoftwareGammaPaletteBlockBytes() const
    {

        return 1024;
    }

    void VID_SOFTWARE::EnsureSoftwareGammaBaseBuffer()
    {
        // No compatibility mirror: retail owns only +0x41C/+0x420/+0x424.
    }

    void VID_SOFTWARE::CopySoftwareGammaBlock(size_t dstBlock, size_t srcBlock)
    {
        const size_t blockBytes = static_cast<size_t>(PaletteSize());
        if (!m_frameStorage || blockBytes == 0u)
            return;
        const size_t dst = dstBlock * blockBytes;
        const size_t src = srcBlock * blockBytes;
        if (dst + blockBytes > m_frameStorageBytes || src + blockBytes > m_frameStorageBytes)
            return;
        std::memmove(m_frameStorage + dst, m_frameStorage + src, blockBytes);
    }

    void VID_SOFTWARE::ExpandSoftwareGammaBufferIfNeeded()
    {

        if (!m_frameStorage || (formatFlags() & VID_TYPE_3D) != 0u)
        {
            EnsureSoftwareGammaBaseBuffer();
            return;
        }

        const size_t blockBytes = static_cast<size_t>(PaletteSize());
        const size_t oldSize = static_cast<size_t>(m_frameStorageBytes);
        if (blockBytes == 0u || oldSize < blockBytes)
            return;

        BYTE* const oldBacking = m_frameStorage;
        const size_t newSize = oldSize + 3u * blockBytes;


        BYTE* const newBacking = static_cast<BYTE*>(::operator new(newSize, std::nothrow));
        if (!newBacking)
        {
            ReportResourceError(2, "SetGamma", static_cast<int>(newSize));
            return;
        }

        // Retail copies old [block1..end] to new block4, then seeds blocks 0..3.
        std::memcpy(newBacking + 4u * blockBytes,
                    oldBacking + blockBytes,
                    oldSize - blockBytes);
        std::memcpy(newBacking, newBacking + 4u * blockBytes, blockBytes);
        std::memcpy(newBacking + blockBytes, newBacking + 4u * blockBytes, blockBytes);
        std::memcpy(newBacking + 2u * blockBytes, newBacking, 2u * blockBytes);

        m_frameStorage = newBacking;
        m_frameStorageBytes = static_cast<DWORD>(newSize);
        g_vidAllocatedBytes += static_cast<int>(3u * blockBytes);
        ::operator delete(oldBacking);

        if (m_frameOffsets)
        {
            const unsigned frames = static_cast<unsigned>(totalFrames());
            for (unsigned i = 0; i < frames; ++i)
                m_frameOffsets[i] += static_cast<DWORD>(3u * blockBytes);
        }

        type = static_cast<WORD>(type | 0x0400u);
        for (VID* mirror = nextMirrorVid(); mirror != this; mirror = mirror->nextMirrorVid())
        {
            auto* const softwareMirror = static_cast<VID_SOFTWARE*>(mirror);
            softwareMirror->type = static_cast<WORD>(softwareMirror->type | VID_TYPE_3D);
            softwareMirror->m_frameStorage = newBacking;
        }
        EnsureSoftwareGammaBaseBuffer();
    }

    bool VID_SOFTWARE::SoftwareGammaComposesGraphRaw() const
    {

        return spriteClass != 8u && (property & P_GAMMA) == 0;
    }

    GammaRawPair VID_SOFTWARE::ComposeSoftwareGammaApplyRaw(const GammaRawPair& rawGamma) const
    {
        if (!SoftwareGammaComposesGraphRaw())
            return rawGamma;
        const GammaRawPair& graphRaw = *GRAPH::CurrentRawGammaPair();

        return GammaRawSaturatingAdd(graphRaw, rawGamma);
    }

    void VID_SOFTWARE::ApplyGammaToSoftwareGammaBlock(size_t blockIndex, const GammaRawPair& rawGamma)
    {
        const size_t blockBytes = static_cast<size_t>(PaletteSize());
        const size_t offset = blockIndex * blockBytes;
        if (!m_frameStorage || blockBytes == 0u || offset + blockBytes > m_frameStorageBytes)
            return;
        if (rawGamma.first == 0u && rawGamma.second == 0u)
            return;

        ApplyGammaToPaletteRaw(m_frameStorage + offset, rawGamma);
    }

    void VID_SOFTWARE::ApplyGammaToPaletteRaw(void* palette, const GammaRawPair& rawGamma)
    {

        applyGammaToPaletteEntries(static_cast<DWORD*>(palette), &rawGamma);
    }

    void VID_SOFTWARE::SetGammaRawToSoftwareBuffer(const GammaRawPair& rawGamma, unsigned n_gamma, bool storeSlot)
    {

        if (!m_frameStorage || !hasPalette())
            return;

        if (n_gamma == 4u)
        {
            if ((formatFlags() & VID_TYPE_3D) != 0u)
            {
                // Retail dispatches the four stored raw slots back through vtable +0x1C.
                for (unsigned i = 0; i < 4u; ++i)
                    SetGammaRawToSoftwareBuffer(altGammaRaw[i], i, false);
                EnsureSoftwareGammaBaseBuffer();
                return;
            }

            CopySoftwareGammaBlock(0u, 1u);
            if (SoftwareGammaComposesGraphRaw())
                ApplyGammaToSoftwareGammaBlock(0u, rawGamma);
            EnsureSoftwareGammaBaseBuffer();
            return;
        }

        if (n_gamma >= 4u)
        {
            ReportResourceError(4, "n_gamma in VID_SOFTWARE::SetGamma", static_cast<int>(n_gamma));
            return;
        }

        if (storeSlot)
        {
            altGammaRaw[n_gamma] = rawGamma;
        }

        ExpandSoftwareGammaBufferIfNeeded();
        if ((formatFlags() & VID_TYPE_3D) == 0u || !m_frameStorage)
            return;

        CopySoftwareGammaBlock(n_gamma, 4u);
        ApplyGammaToSoftwareGammaBlock(n_gamma, ComposeSoftwareGammaApplyRaw(rawGamma));
        EnsureSoftwareGammaBaseBuffer();
    }

    void VID_SOFTWARE::SetGammaRaw(const GammaRawPair& rawGamma, unsigned n_gamma)
    {

        SetGammaRawToSoftwareBuffer(rawGamma, n_gamma, n_gamma < 4);
    }

    void VID_SOFTWARE::SetGamma(const Gamma& gamma, unsigned n_gamma)
    {
        // Readable convenience wrapper; retail dispatch is the raw +0x1C slot.
        VID::SetGamma(gamma, n_gamma);
    }

    void VID_SOFTWARE::Draw(const SPRITE* sprite)
    {

        const DWORD property = properties();
        if ((property & 0x00000400u) != 0u)
            return;

        GRAPH* const graph = GRAPH::CurrentGraph();
        const GraphViewportState& viewport = graph->viewportState();
        const int clipLeft = g_softwareClipLeft;
        const int clipTop = g_softwareClipTop;
        const int clipRight = g_softwareClipRight;
        const int clipBottom = g_softwareClipBottom;

        const int sizeX = static_cast<std::int16_t>(vidWidth());
        const int sizeY = static_cast<std::int16_t>(vidHeight());
        const core::ApplicationDrawDispatcherState& appDraw = core::GlobalApplicationDrawDispatcherState();
        const float cameraX = appDraw.cameraShiftX();
        const float cameraY = appDraw.cameraShiftY();
        const int drawLeft = retailDrawXFtolSoftware(sprite->X(), cameraX, sizeX / 2);
        int drawTop = retailDrawYFtolSoftware(sprite->Y(), sprite->Z(), cameraY, sizeY / 2);

        if (drawLeft + sizeX < clipLeft || drawLeft >= clipRight ||
            drawTop + sizeY < clipTop || drawTop >= clipBottom)
            return;

        int baseDepth = static_cast<int>(sprite->Z() * 8.0f);
        if ((property & P_ALWAYSTOP) != 0u && baseDepth < 0x3FFF)
            baseDepth += 0x3FFF;
        else if ((property & P_WAVE) != 0u)
        {
            const int waveDepth = static_cast<int>(
                SPRITE::rawDirectionSin(static_cast<int>((core::CurrentTimeMilliseconds() >> 3u) & 0xFFu)) *
                moveUpZ() * 8.0f);
            baseDepth += waveDepth;
            drawTop += waveDepth / -8;
        }

        const int frame = sprite->currentFrame();
        BYTE* const frameBase = frameStorage() + frameOffsets()[frame];
        const int contourCount = static_cast<std::int16_t>(*reinterpret_cast<const WORD*>(frameBase));
        BYTE* row = frameBase + 2 + 6 * contourCount;
        const int frameTop = static_cast<std::int16_t>(*reinterpret_cast<const WORD*>(row));
        const int rowCount = static_cast<std::int16_t>(*reinterpret_cast<const WORD*>(row + 2));
        row += 4;
        drawTop += frameTop;
        const int drawBottom = drawTop + rowCount;
        if (drawTop >= clipBottom || drawBottom < clipTop)
            return;

        WORD* const depthBase = graph->softwareDepthBuffer();
        const int depthPitch = graph->softwareDepthPitch();
        if (!graph->backBufferPixels())
            graph->lockBackBuffer();
        DWORD* const colorBase = static_cast<DWORD*>(graph->backBufferPixels());
        const int colorPitch = graph->backBufferPitchPixels();
        const WORD typeFlags = formatFlags();
        const bool texturePayload = (typeFlags & VID_TYPE_TEXTURE) != 0u;
        if (!texturePayload)
            return;
        const bool palettePayload = (typeFlags & VID_TYPE_PALETTE) != 0u;
        const bool alphaPalettePayload =
            (typeFlags & (VID_TYPE_ALPHA | VID_TYPE_TEXTURE | VID_TYPE_PALETTE)) ==
            (VID_TYPE_ALPHA | VID_TYPE_TEXTURE | VID_TYPE_PALETTE);
        const bool zPalettePayload = !alphaPalettePayload && palettePayload &&
                                     (typeFlags & VID_TYPE_ZBUFFER) != 0u;
        const bool directPayload = !palettePayload;

        const BYTE* drawPaletteBlock = nullptr;
        std::array<DWORD, 256> drawPaletteScratch{};
        {

            const size_t blockBytes = static_cast<size_t>(SoftwareGammaPaletteBlockBytes());
            GammaRawPair spriteOverride{};
            const bool hasSpriteOverride = sprite->spriteGammaOverride(spriteOverride);
            size_t blockIndex = 0u;
            if (!hasSpriteOverride)
            {
                blockIndex = (typeFlags & VID_TYPE_3D) != 0u
                    ? static_cast<size_t>((sprite->runtimeFlags() >> 10u) & 3u)
                    : 0u;
                drawPaletteBlock = frameStorage() + blockIndex * blockBytes;
            }
            else
            {

                blockIndex = (typeFlags & VID_TYPE_3D) != 0u ? 4u : 0u;
                std::memcpy(drawPaletteScratch.data(), frameStorage() + blockIndex * blockBytes, blockBytes);
                GammaRawPair drawPaletteApply = spriteOverride;
                if ((property & P_GAMMA) == 0u)
                    drawPaletteApply = GammaRawSaturatingAdd(drawPaletteApply, *GRAPH::CurrentRawGammaPair());
                ApplyGammaToPaletteRaw(drawPaletteScratch.data(), drawPaletteApply);
                drawPaletteBlock = reinterpret_cast<const BYTE*>(drawPaletteScratch.data());
            }
            g_softwarePaletteLookup = reinterpret_cast<const DWORD*>(drawPaletteBlock);
        }


        const int constantDepthInt = std::min(baseDepth + 0x400, 0x7FFF);
        const WORD constantDepth = static_cast<WORD>(constantDepthInt);
        const WORD baseDepthWord = static_cast<WORD>(baseDepth);
        WORD rowDepth = constantDepth;
        const int alphaDepthStep = alphaPalettePayload && this->sizeZ() > this->sizeY() ? -8 : 0;


        if (alphaPalettePayload)
        {
            g_packedSoftwareDepth = (g_packedSoftwareDepth & 0xFFFF0000u) | static_cast<DWORD>(constantDepth);
        }
        else if (zPalettePayload)
        {
            g_packedSoftwareDepth = static_cast<DWORD>(baseDepthWord) | (static_cast<DWORD>(baseDepthWord) << 16u);
            g_softwareDepthWordPrimary = baseDepthWord;
            g_softwareDepthWordSecondary = baseDepthWord;
        }
        else if (palettePayload)
        {
            g_packedSoftwareDepth = static_cast<DWORD>(constantDepth) | (static_cast<DWORD>(constantDepth) << 16u);
            g_softwareDepthWordPrimary = constantDepth;
            g_softwareDepthWordSecondary = constantDepth;
        }

        auto directWordToDword = [](WORD value) -> DWORD
        {

            return (static_cast<DWORD>(value & 0x001Fu) << 3u) |
                   ((static_cast<DWORD>(value) << (8u - g_color16GreenShift)) & 0x0000FF00u) |
                   ((static_cast<DWORD>(value) << (16u - g_color16RedShift)) & 0x00FF0000u);
        };

        for (int sourceRow = 0; sourceRow < rowCount; ++sourceRow)
        {
            const int dy = drawTop + sourceRow;
            // Retail clips the bottom before building its end pointer; DATA rows
            // below clipBottom are never parsed.  Top-clipped rows are still
            // walked to advance the compact stream.
            if (dy >= clipBottom)
                break;
            const bool visibleRow = dy >= clipTop;
            int sourceX = 0;
            for (;;)
            {
                const int skip = row[0];
                const int run = row[1];
                row += 2;
                if (skip == 0 && run == 0)
                    break;
                sourceX += skip;

                const WORD* zWords = nullptr;
                if (zPalettePayload)
                {
                    zWords = reinterpret_cast<const WORD*>(row);
                    row += static_cast<size_t>(run) * 2u;
                }

                const BYTE* paletteIndexes = nullptr;
                const WORD* colorWords = nullptr;
                if (palettePayload)
                {
                    paletteIndexes = row;
                    row += static_cast<size_t>(run);
                }
                else
                {
                    colorWords = reinterpret_cast<const WORD*>(row);
                    row += static_cast<size_t>(run) * 2u;
                }

                if (visibleRow)
                {
                    DWORD* const colorRow = colorBase + static_cast<size_t>(dy) * static_cast<size_t>(colorPitch);
                    WORD* const depthRow = depthBase + static_cast<size_t>(dy) * static_cast<size_t>(depthPitch);
                    for (int i = 0; i < run; ++i)
                    {
                        const int dx = drawLeft + sourceX + i;
                        if (dx < clipLeft || dx >= clipRight)
                            continue;

                        const WORD oldDepth = depthRow[dx];
                        WORD z = rowDepth;
                        if (zPalettePayload)
                            z = static_cast<WORD>(baseDepthWord + zWords[i]);

                        if (alphaPalettePayload)
                        {
                            // drawAlphaPaletteSpanWithDepth: unsigned JB, so equality is accepted.
                            if (z < oldDepth)
                                continue;
                        }
                        else if (zPalettePayload)
                        {
                            // Retail drawOpaquePaletteSpanWithDepth: (signed short)z > old WORD.
                            // Equality is rejected only for the per-pixel-Z palette path.
                            if (static_cast<std::int16_t>(z) <= static_cast<std::int16_t>(oldDepth))
                                continue;
                        }
                        else if (palettePayload)
                        {
                            // 4139E0/413A54 route through drawOpaquePaletteSpanWithDepth.  Its CMP
                            // is WORD + signed JLE, so the new constant depth must
                            // be strictly greater as int16; equality is rejected.
                            if (static_cast<std::int16_t>(constantDepth) <=
                                static_cast<std::int16_t>(oldDepth))
                                continue;
                        }
                        else if (directPayload)
                        {

                            if (constantDepthInt < static_cast<int>(oldDepth))
                                continue;
                        }

                        DWORD source = 0u;
                        if (palettePayload)
                        {
                            source = reinterpret_cast<const DWORD*>(drawPaletteBlock)[paletteIndexes[i]];
                        }
                        else
                        {
                            source = directWordToDword(colorWords[i]);
                        }

                        if (alphaPalettePayload)
                        {
                            const DWORD alpha = source >> 24u;
                            const DWORD sourceWeight = alpha + 1u;
                            const DWORD destWeight = 256u - sourceWeight;
                            const DWORD dest = colorRow[dx];
                            const DWORD b = (sourceWeight * (source & 0xFFu) + destWeight * (dest & 0xFFu)) >> 8u;
                            const DWORD g = (sourceWeight * ((source >> 8u) & 0xFFu) + destWeight * ((dest >> 8u) & 0xFFu)) >> 8u;
                            const DWORD r = (sourceWeight * ((source >> 16u) & 0xFFu) + destWeight * ((dest >> 16u) & 0xFFu)) >> 8u;
                            colorRow[dx] = 0xFF000000u | (r << 16u) | (g << 8u) | b;
                            continue;
                        }

                        depthRow[dx] = z;
                        colorRow[dx] = source;
                    }
                }
                sourceX += run;
            }

            // Retail changes g_packedSoftwareDepth only after a processed alpha row.
            // Rows skipped above the top clip do not consume the -8 slope.
            if (visibleRow && alphaDepthStep != 0)
            {
                rowDepth = static_cast<WORD>(rowDepth + alphaDepthStep);
                g_packedSoftwareDepth = (g_packedSoftwareDepth & 0xFFFF0000u) | static_cast<DWORD>(rowDepth);
            }
        }

    }

    int VID_SOFTWARE::DrawShadow(const SPRITE* sprite) const
    {

        if (!frameStorage())
            return 0;
        if (!frameOffsets())
            return 0;

        const DWORD property = properties();
        if ((property & 0x00000400u) != 0u)
            return static_cast<int>(property);

        GRAPH* const graph = GRAPH::CurrentGraph();
        const core::ApplicationDrawDispatcherState& appDraw = core::GlobalApplicationDrawDispatcherState();

        const int sizeX = static_cast<std::int16_t>(vidWidth());
        const int sizeY = static_cast<std::int16_t>(vidHeight());
        const float cameraX = appDraw.cameraShiftX();
        const float cameraY = appDraw.cameraShiftY();

        const float drawLeft = sprite->X() - cameraX - static_cast<float>(sizeX / 2);
        const float drawTop = sprite->Y() - sprite->Z() - cameraY - static_cast<float>(sizeY / 2);
        const GraphViewportState& viewport = graph->viewportState();

        const int leftI = static_cast<int>(drawLeft);
        if (leftI + sizeX + 200 < g_softwareClipLeft || leftI >= g_softwareClipRight)
            return leftI;

        const int topI = static_cast<int>(drawTop);
        if (topI + sizeY + 100 < g_softwareClipTop || topI >= g_softwareClipBottom)
            return topI;


        int earlyReturnValue = topI;
        float shadowZ = sprite->Z();
        const float recordBaseY = drawTop + sprite->Z();
        if ((property & P_WAVE) != 0u)
        {
            const int waveIndex = static_cast<int>((core::CurrentTimeMilliseconds() >> 3u) & 0xFFu);
            earlyReturnValue = waveIndex;
            shadowZ += SPRITE::rawDirectionSin(waveIndex) * moveUpZ();
        }

        const int frame = sprite->currentFrame();
        BYTE* frameData = frameStorage() + frameOffsets()[frame];
        const int contourCount = static_cast<int>(*reinterpret_cast<const std::int16_t*>(frameData));
        if (contourCount == 0)
            return earlyReturnValue;

        struct ShadowVertex
        {
            float x;
            float y;
            float z;
            float rhw;
            DWORD diffuse;
            DWORD specular;
        };
        static_assert(sizeof(ShadowVertex) == 24, "shadow vertex ABI mismatch");

        ShadowVertex vertices[513];
        const BYTE* record = frameData + 2;
        const DWORD firstPassColor = 0x00A4A4A4u;

        int generatedVertexCount = 0;
        if (contourCount * 2 > 0)
        {
            generatedVertexCount = contourCount * 2;
        }
        for (int i = 0; i < generatedVertexCount / 2; ++i, record += 6)
        {
            const float recordX = static_cast<float>(*reinterpret_cast<const std::int16_t*>(record + 0));
            const float recordY = static_cast<float>(*reinterpret_cast<const std::int16_t*>(record + 2));
            const float recordZ = static_cast<float>(*reinterpret_cast<const std::int16_t*>(record + 4));

            ShadowVertex& first = vertices[i * 2 + 0];
            ShadowVertex& second = vertices[i * 2 + 1];

            first.x = recordX + drawLeft;
            const float projectedY = recordY + recordBaseY;
            const float projectedZ = recordZ + shadowZ;
            first.y = projectedY - projectedZ;
            first.z = projectedZ * 0.00012207031f + 0.015625f;
            first.rhw = 1.0f;
            first.diffuse = firstPassColor;

            second.x = first.x + projectedZ * 0.34999999f;
            second.y = projectedY - projectedZ * 0.69999999f;
            second.z = 0.02f;
            second.rhw = 1.0f;
            second.diffuse = firstPassColor;
            // The retail stack owner does not initialize the FVF specular DWORD
            // for either generated vertex. Keep it untouched here as well.
        }

        const int vertexCount = contourCount * 2 + 2;
        std::memcpy(&vertices[generatedVertexCount + 0], &vertices[0], sizeof(ShadowVertex));
        std::memcpy(&vertices[generatedVertexCount + 1], &vertices[1], sizeof(ShadowVertex));

        graph->setRenderStateCached(29u, 0u);
#if defined(_WIN32)
        if (IDirect3DDevice8* const device = static_cast<IDirect3DDevice8*>(GRAPH::CurrentDevice()))
            device->SetTexture(0, nullptr);
#endif
        graph->setRenderStateCached(22u, 3u);
        graph->setAlphaBlendFactors(1u, 3u);

        for (int i = 0; i < vertexCount; ++i)
            vertices[i].diffuse = firstPassColor;
        graph->drawPrimitiveUp(5u, 0xC4u, vertices, 24u, vertexCount);

        graph->setRenderStateCached(22u, 2u);
        graph->setAlphaBlendFactors(9u, 2u);
        for (int i = 0; i < vertexCount; ++i)
            vertices[i].diffuse = 0x008F8F8Fu;
        graph->drawPrimitiveUp(5u, 0xC4u, vertices, 24u, vertexCount);

        return graph->setRenderStateCached(22u, 3u);
    }

    int VID_SOFTWARE::HaveShadow() const
    {

        if (!m_frameStorage)
            return 0;
        const DWORD firstShift = *m_frameOffsets;
        WORD value = 0;
        std::memcpy(&value, m_frameStorage + firstShift, sizeof(value));
        return static_cast<int>(value);
    }

    bool VID_SOFTWARE::transparencyCheck() const
    {
        return VID::transparencyCheck();
    }

    int VID_SOFTWARE::PaletteSize() const
    {

        return 1024;
    }

    bool VID_SOFTWARE::isLoaded() const
    {
        // Retail Load/loadCompactSoftwareVidData owns the compact frame-offset table at +0x41C
        // and backing DATA at +0x424.  Decoded FrameSurface state is inspection
        // only and must not define production load ownership.
        return m_frameOffsets != nullptr && m_frameStorage != nullptr &&
               static_cast<WORD>(totalFrames()) != 0u;
    }

    bool VID_SOFTWARE::unloadable() const
    {
        return isLoaded();
    }
}
