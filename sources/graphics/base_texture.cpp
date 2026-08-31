#include "graphics/base_texture.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "graphics/surface_transfer.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <new>
#ifdef _WIN32
#include "d3d8.h"
#include "graph.h"
#endif

namespace as1
{
    namespace
    {
        BaseTextureCaps g_baseTextureCaps;
        BaseTextureRuntimeGlobals g_baseTextureRuntimeGlobals;

        int nextPower2(int value)
        {
            int out = 1;
            while (out < value && out < 8192)
                out <<= 1;
            return out;
        }

#ifdef _WIN32
        IDirect3DDevice8* currentTextureDevice()
        {
            return static_cast<IDirect3DDevice8*>(GRAPH::CurrentDevice());
        }

#endif

        constexpr DWORD kSurfaceCopyInvalidCall = 0x8876086Cu;
    }

    BASE_TEXTURE::BASE_TEXTURE(int width, int height, DWORD format, DWORD flags)
    {

#ifdef _WIN32
        m_nativeHandle = nullptr;
        m_softwarePixels = nullptr;

        DWORD adjustedFlags = flags;
        if (g_baseTextureRuntimeGlobals.dynamicTextureFlag == 0)
            adjustedFlags &= 0xFFFFFFFEu;

        int normalizedWidth = width;
        int normalizedHeight = height;
        if (g_baseTextureRuntimeGlobals.squareOnlyMask != 0)
        {
            if (normalizedWidth > normalizedHeight)
                normalizedHeight = normalizedWidth;
            else if (normalizedWidth < normalizedHeight)
                normalizedWidth = normalizedHeight;
        }

        if (g_baseTextureRuntimeGlobals.powerOfTwoFlag != 0)
        {
            std::int32_t value = 1;
            while (normalizedWidth > value)
                value = static_cast<std::int32_t>(static_cast<std::uint32_t>(value) << 1u);
            normalizedWidth = value;

            value = 1;
            while (normalizedHeight > value)
                value = static_cast<std::int32_t>(static_cast<std::uint32_t>(value) << 1u);
            normalizedHeight = value;
        }

        if (normalizedWidth > g_baseTextureRuntimeGlobals.maxWidth || normalizedWidth <= 0)
        {
            LOG::ResourceError("TEXTURE", 4, "initial sizeX", normalizedWidth);
            return;
        }
        if (normalizedHeight > g_baseTextureRuntimeGlobals.maxHeight || normalizedHeight <= 0)
        {
            LOG::ResourceError("TEXTURE", 4, "initial sizeY", normalizedHeight);
            return;
        }

        DWORD finalFormat = format;
        HRESULT result = D3D_OK;
        if ((adjustedFlags & 0x2u) != 0 && finalFormat == 0x50u)
        {
            const DWORD allocationBytes = static_cast<DWORD>(normalizedWidth) *
                static_cast<DWORD>(normalizedHeight) * 2u;
            m_softwarePixels = ::operator new(static_cast<std::size_t>(allocationBytes));
        }
        else
        {
            DWORD usage = (adjustedFlags & 0x4u) != 0 ? 1u : 0u;
            const bool dynamicTexture = (adjustedFlags & 0x1u) != 0;
            if (dynamicTexture)
                usage |= 0x200u;

            IDirect3DDevice8* device = currentTextureDevice();
            for (;;)
            {
                result = device->CreateTexture(
                    static_cast<UINT>(normalizedWidth),
                    static_cast<UINT>(normalizedHeight),
                    1u,
                    usage,
                    static_cast<D3DFORMAT>(finalFormat),
                    static_cast<D3DPOOL>(dynamicTexture ? 0u : 1u),
                    &m_nativeHandle);
                if (result == D3D_OK)
                    break;

                if (finalFormat == 0x29u || finalFormat == 0x31545844u)
                    finalFormat = 0x17u;
                else if (finalFormat == 0x33545844u)
                    finalFormat = 0x1Au;
                else if (finalFormat == 0x17u)
                    finalFormat = 0x18u;
                else if (finalFormat == 0x18u)
                    finalFormat = 0x19u;
                else
                    break;
            }
        }

        m_width = normalizedWidth;
        m_height = normalizedHeight;
        m_format = finalFormat;
        m_flags = adjustedFlags;

        if (result != D3D_OK)
        {
            LOG::ResourceError("TEXTURE", 3, "", static_cast<int>(result));
            return;
        }

        if (m_nativeHandle)
        {
            const DWORD bytesPerPixel = finalFormat == 0x29u ? 1u : 2u;
            g_baseTextureRuntimeGlobals.nativeTextureBytes +=
                static_cast<DWORD>(normalizedWidth) *
                static_cast<DWORD>(normalizedHeight) * bytesPerPixel;
        }
#else
        create(width, height, format, flags);
#endif
    }

    BASE_TEXTURE::~BASE_TEXTURE()
    {

#ifdef _WIN32
        if (m_nativeHandle)
        {
            m_nativeHandle->Release();
            const DWORD bytesPerPixel = m_format == 0x29u ? 1u : 2u;
            g_baseTextureRuntimeGlobals.nativeTextureBytes -=
                static_cast<DWORD>(m_width) *
                static_cast<DWORD>(m_height) * bytesPerPixel;
        }
#else
        release();
#endif
    }

    BASE_TEXTURE* baseTextureScalarDeletingDestructor(BASE_TEXTURE* owner, unsigned char deletingFlags) noexcept
    {
        // Physical retail owner combines the BASE_TEXTURE destructor body with
        // the scalar-deleting flag route and returns the original this pointer.
        owner->~BASE_TEXTURE();
        if ((deletingFlags & 1u) != 0u)
            ::operator delete(owner);
        return owner;
    }

    void BASE_TEXTURE::ConfigureCaps(const BaseTextureCaps& caps)
    {
        g_baseTextureCaps = caps;

        g_baseTextureRuntimeGlobals.maxWidth = g_baseTextureCaps.maxWidth;
        g_baseTextureRuntimeGlobals.maxHeight = g_baseTextureCaps.maxHeight;
        g_baseTextureRuntimeGlobals.squareOnlyMask = g_baseTextureCaps.requireSquare ? 0x20u : 0u;
        g_baseTextureRuntimeGlobals.dynamicTextureFlag = g_baseTextureCaps.dynamicTextures ? 1u : 0u;
        g_baseTextureRuntimeGlobals.powerOfTwoFlag = g_baseTextureCaps.requirePowerOfTwo ? 2u : 0u;
        g_baseTextureRuntimeGlobals.paletteSupport = g_baseTextureCaps.paletteTextures ? 1u : 0u;
        g_baseTextureRuntimeGlobals.compressedFormatMask = g_baseTextureCaps.compressedFormatMask;

        std::string text;
        if (g_baseTextureRuntimeGlobals.squareOnlyMask)
            text += "SQUARE ";
        text += g_baseTextureRuntimeGlobals.powerOfTwoFlag ? "POWER2 " : "NOTPOWER2 ";
        if (g_baseTextureRuntimeGlobals.dynamicTextureFlag)
            text += "DYNAMIC ";
        if (g_baseTextureRuntimeGlobals.paletteSupport)
            text += "PALETTE ";
        if (g_baseTextureRuntimeGlobals.compressedFormatMask & 1u)
            text += "DXT1\t";
        if (g_baseTextureRuntimeGlobals.compressedFormatMask & 4u)
            text += "DXT3\t";
        char maxSizeText[64] = {};
        std::snprintf(maxSizeText, sizeof(maxSizeText), "MAXSIZE=%i,%i",
                      g_baseTextureRuntimeGlobals.maxWidth,
                      g_baseTextureRuntimeGlobals.maxHeight);
        text += maxSizeText;
        LOG::Write("TextureCaps=%s", text.c_str());

        g_baseTextureRuntimeGlobals.powerOfTwoFlag = 1u;
    }

    const BaseTextureCaps& BASE_TEXTURE::Caps()
    {
        return g_baseTextureCaps;
    }

    const BaseTextureRuntimeGlobals& BASE_TEXTURE::RuntimeGlobals()
    {
        return g_baseTextureRuntimeGlobals;
    }

    DWORD BASE_TEXTURE::FallbackFormatAfterCreateFailure(DWORD format)
    {

        switch (format)
        {
        case 41:          // D3DFMT_P8
        case 0x31545844u: // D3DFMT_DXT1
            return 23;   // D3DFMT_R5G6B5
        case 0x33545844u: // D3DFMT_DXT3
            return 26;   // D3DFMT_A4R4G4B4
        case 23:
            return 24;   // D3DFMT_X1R5G5B5
        case 24:
            return 25;   // D3DFMT_A1R5G5B5
        default:
            return format;
        }
    }

    DWORD BASE_TEXTURE::NativeTextureBytesAfterCreate(int width, int height, DWORD format, DWORD currentBytes)
    {

        if (width <= 0 || height <= 0)
            return currentBytes;
        const DWORD bytesPerPixel = (format == 41u) ? 1u : 2u;
        return currentBytes + static_cast<DWORD>(width) * static_cast<DWORD>(height) * bytesPerPixel;
    }

    DWORD BASE_TEXTURE::NativeTextureBytesAfterRelease(int width, int height, DWORD format, DWORD currentBytes)
    {

        if (width <= 0 || height <= 0)
            return currentBytes;
        const DWORD bytesPerPixel = (format == 41u) ? 1u : 2u;
        const DWORD delta = static_cast<DWORD>(width) * static_cast<DWORD>(height) * bytesPerPixel;
        return currentBytes > delta ? currentBytes - delta : 0u;
    }

    BaseTextureCreateState BASE_TEXTURE::MakeCreateState(int width, int height, DWORD format, DWORD flags)
    {

        BaseTextureCreateState out;
        out.recorded = true;
        out.requestedWidth = width;
        out.requestedHeight = height;
        out.requestedFormat = format;
        out.requestedFlags = flags;

        DWORD adjustedFlags = flags;
        if (g_baseTextureRuntimeGlobals.dynamicTextureFlag == 0)
        {
            adjustedFlags &= 0xFFFFFFFEu;
            out.dynamicFlagClearedByCaps = (adjustedFlags != flags);
        }
        out.flagsAfterDynamicCapClear = adjustedFlags;

        int normalizedW = width;
        int normalizedH = height;
        if (g_baseTextureRuntimeGlobals.squareOnlyMask != 0)
        {
            const int maxSide = std::max(normalizedW, normalizedH);
            normalizedW = maxSide;
            normalizedH = maxSide;
            out.squareAppliedFromCaps = true;
        }
        if (g_baseTextureRuntimeGlobals.powerOfTwoFlag != 0)
        {
            normalizedW = nextPower2(normalizedW);
            normalizedH = nextPower2(normalizedH);
            out.power2AppliedFromCaps = true;
        }
        out.normalizedWidth = normalizedW;
        out.normalizedHeight = normalizedH;
        out.rejectedByWidth = (normalizedW > g_baseTextureRuntimeGlobals.maxWidth || normalizedW <= 0);
        out.rejectedByHeight = (normalizedH > g_baseTextureRuntimeGlobals.maxHeight || normalizedH <= 0);
        if (out.rejectedByWidth || out.rejectedByHeight)
        {
            out.failureReported = true;
            out.finalFormat = format;
            return out;
        }

        if ((adjustedFlags & 0x2u) != 0 && format == 0x50u)
        {
            out.softwareL8Path = true;
            out.softwareAllocationBytes = static_cast<DWORD>(normalizedW) * static_cast<DWORD>(normalizedH) * 2u;
            out.finalFormat = format;
            return out;
        }

        out.nativeCreatePath = true;
        out.usesRenderTargetUsage = (adjustedFlags & 0x4u) != 0;
        out.usesDynamicTexture = (adjustedFlags & 0x1u) != 0;
        DWORD usage = out.usesRenderTargetUsage ? 1u : 0u;
        if (out.usesDynamicTexture)
            usage |= 0x200u;
        out.nativeUsage = usage;
        out.nativePool = out.usesDynamicTexture ? 0u : 1u;

        DWORD currentFormat = format;
        for (int i = 0; i < 6; ++i)
        {
            out.attempts[out.attemptCount++] = BaseTextureCreateAttempt{currentFormat, usage, out.nativePool, out.usesDynamicTexture};
            const DWORD fallback = FallbackFormatAfterCreateFailure(currentFormat);
            if (fallback == currentFormat)
                break;
            currentFormat = fallback;
        }
        out.finalFormat = currentFormat;
        out.byteCounterIncrement = static_cast<DWORD>(normalizedW) * static_cast<DWORD>(normalizedH) * ((out.finalFormat == 41u) ? 1u : 2u);
        return out;
    }

#ifndef _WIN32
    bool BASE_TEXTURE::create(int width, int height, DWORD format, DWORD flags)
    {
        release();
        m_createState = MakeCreateState(width, height, format, flags);
        if (width <= 0 || height <= 0)
            return false;
        if (m_createState.rejectedByWidth || m_createState.rejectedByHeight)
            return false;

        m_width = m_createState.normalizedWidth;
        m_height = m_createState.normalizedHeight;
        m_format = m_createState.finalFormat ? m_createState.finalFormat : format;
        m_flags = m_createState.flagsAfterDynamicCapClear;

        if (m_createState.softwareL8Path || isSoftwareBacked())
        {
            m_pixels16.assign(static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height), 0);
            return true;
        }

#ifdef _WIN32
        if (m_createState.nativeCreatePath)
        {
            IDirect3DDevice8* device = currentTextureDevice();
            if (!device)
                return false;

            HRESULT lastResult = E_FAIL;
            for (int i = 0; i < m_createState.attemptCount; ++i)
            {
                const BaseTextureCreateAttempt& attempt = m_createState.attempts[i];
                IDirect3DTexture8* texture = nullptr;
                lastResult = device->CreateTexture(
                    static_cast<UINT>(m_width),
                    static_cast<UINT>(m_height),
                    1,
                    attempt.usageArg,
                    static_cast<D3DFORMAT>(attempt.format),
                    static_cast<D3DPOOL>(attempt.poolArg),
                    &texture);

                if (SUCCEEDED(lastResult) && texture)
                {
                    m_nativeHandle = texture;
                    m_format = attempt.format;
                    m_createState.finalFormat = attempt.format;
                    m_createState.nativeCreateSucceeded = true;
                    m_createState.nativeCreateResult = static_cast<DWORD>(lastResult);
                    g_baseTextureRuntimeGlobals.nativeTextureBytes = NativeTextureBytesAfterCreate(
                        m_width, m_height, m_format, g_baseTextureRuntimeGlobals.nativeTextureBytes);
                    m_createState.byteCounterIncrement = static_cast<DWORD>(m_width) *
                        static_cast<DWORD>(m_height) * ((m_format == 41u) ? 1u : 2u);
                    return true;
                }
            }

            m_createState.nativeCreateSucceeded = false;
            m_createState.nativeCreateResult = static_cast<DWORD>(lastResult);
            m_createState.failureReported = true;
            m_width = 0;
            m_height = 0;
            m_format = 0;
            m_flags = 0;
            return false;
        }
#endif

        return true;
    }


#endif

    std::intptr_t BASE_TEXTURE::uploadPaletteEntries(const void* paletteEntries)
    {

#ifdef _WIN32
        if (!m_nativeHandle)
            return logFileLoggerResourceError(g_fileLogger, "TEXTURE", 8, "palette for non initialized texture", 0);
        if (m_format != 0x29u)
            return logFileLoggerResourceError(g_fileLogger, "TEXTURE", 8, "palette for non palette texture", 0);

        const HRESULT result = currentTextureDevice()->SetPaletteEntries(
            static_cast<UINT>(g_baseTextureRuntimeGlobals.paletteSlotCounter),
            static_cast<const PALETTEENTRY*>(paletteEntries));
        if (result < 0)
            return logFileLoggerResourceError(g_fileLogger, "TEXTURE", 8, "palettes", static_cast<int>(result));

        m_paletteSlot = static_cast<int>(g_baseTextureRuntimeGlobals.paletteSlotCounter);
        ++g_baseTextureRuntimeGlobals.paletteSlotCounter;
        return static_cast<std::intptr_t>(g_baseTextureRuntimeGlobals.paletteSlotCounter);
#else
        m_paletteState = BaseTexturePaletteState{};
        m_paletteState.recorded = true;
        m_paletteState.nativeTexture = m_nativeHandle;
        m_paletteState.format = m_format;
        m_paletteState.paletteCounterBefore = g_baseTextureRuntimeGlobals.paletteSlotCounter;
        if (!m_nativeHandle)
        {
            m_paletteState.rejectedNotInitialized = true;
            return 0;
        }
        if (m_format != 41u)
        {
            m_paletteState.rejectedNonP8 = true;
            return 0;
        }
        m_paletteState.setPaletteEntriesRequired = true;
        m_paletteState.setPaletteEntriesSucceeded = true;
        m_paletteSlot = static_cast<int>(g_baseTextureRuntimeGlobals.paletteSlotCounter);
        m_paletteState.paletteSlotStored = m_paletteSlot;
        ++g_baseTextureRuntimeGlobals.paletteSlotCounter;
        m_paletteState.paletteCounterAfter = g_baseTextureRuntimeGlobals.paletteSlotCounter;
        return static_cast<std::intptr_t>(g_baseTextureRuntimeGlobals.paletteSlotCounter);
#endif
    }

    void BASE_TEXTURE::createPaletteSlot(const void* paletteEntries)
    {
        (void)uploadPaletteEntries(paletteEntries);
    }

#ifndef _WIN32
    void BASE_TEXTURE::release()
    {
        m_releaseState = BaseTextureReleaseState{};
        m_releaseState.recorded = true;
        m_releaseState.nativeTexture = m_nativeHandle;
        m_releaseState.format = m_format;
        m_releaseState.width = m_width;
        m_releaseState.height = m_height;
        m_releaseState.flags = m_flags;
        m_releaseState.nativeReleaseRequired = (m_nativeHandle != nullptr);
        m_releaseState.byteCounterBefore = g_baseTextureRuntimeGlobals.nativeTextureBytes;

        if (m_nativeHandle)
        {
#ifdef _WIN32
            static_cast<IDirect3DTexture8*>(m_nativeHandle)->Release();
#endif
            const DWORD after = NativeTextureBytesAfterRelease(m_width, m_height, m_format, g_baseTextureRuntimeGlobals.nativeTextureBytes);
            m_releaseState.byteCounterSubtract = g_baseTextureRuntimeGlobals.nativeTextureBytes - after;
            g_baseTextureRuntimeGlobals.nativeTextureBytes = after;
        }
        m_releaseState.byteCounterAfter = g_baseTextureRuntimeGlobals.nativeTextureBytes;

        m_nativeHandle = nullptr;
        m_paletteSlot = -1;
        m_pixels16.clear();
        m_width = 0;
        m_height = 0;
        m_format = 0;
        m_flags = 0;
    }


#endif

    std::uint16_t* BASE_TEXTURE::lock16(int* pitchBytes, const RECTI* rect)
    {

#ifdef _WIN32
        if ((m_flags & 0x2u) != 0)
        {

            *pitchBytes = static_cast<int>(static_cast<std::uint32_t>(m_width) << 1u);
            if (rect)
            {
                const std::uint32_t row = static_cast<std::uint32_t>(rect->top) *
                                          static_cast<std::uint32_t>(m_width);
                const int offsetPixels = static_cast<int>(row + static_cast<std::uint32_t>(rect->left));
                return reinterpret_cast<std::uint16_t*>(m_softwarePixels) + offsetPixels;
            }
            return reinterpret_cast<std::uint16_t*>(m_softwarePixels);
        }

        D3DLOCKED_RECT lockedRect;
        const HRESULT result = m_nativeHandle->LockRect(
            0u,
            &lockedRect,
            reinterpret_cast<const RECT*>(rect),
            0u);
        if (result != D3D_OK)
        {
            LOG::ResourceError("TEXTURE", 0, "", static_cast<int>(result));
            return nullptr;
        }
        *pitchBytes = lockedRect.Pitch;
        return static_cast<std::uint16_t*>(lockedRect.pBits);
#else
        m_lockState = BaseTextureLockState{};
        m_lockState.recorded = true;
        m_lockState.flags = m_flags;
        m_lockState.rectProvided = (rect != nullptr);
        if (rect)
            m_lockState.rect = *rect;

        if ((m_flags & 0x2u) == 0)
        {
            m_lockState.nativePath = true;
            if (pitchBytes)
                *pitchBytes = 0;
            return nullptr;
        }

        m_lockState.softwarePath = true;
        m_lockState.pitchBytesOut = m_width * static_cast<int>(sizeof(std::uint16_t));
        if (pitchBytes)
            *pitchBytes = m_lockState.pitchBytesOut;
        if (m_pixels16.empty())
            return nullptr;

        int offsetPixels = 0;
        if (rect)
            offsetPixels = rect->left + rect->top * m_width;
        m_lockState.softwareOffsetPixels = offsetPixels;
        m_lockState.resultNonNull = true;
        return m_pixels16.data() + offsetPixels;
#endif
    }

    const std::uint16_t* BASE_TEXTURE::lock16(int* pitchBytes, const RECTI* rect) const
    {
        return const_cast<BASE_TEXTURE*>(this)->lock16(pitchBytes, rect);
    }

    void BASE_TEXTURE::unlock()
    {

#ifdef _WIN32
        if (m_nativeHandle)
            m_nativeHandle->UnlockRect(0u);
#endif
    }

#ifdef _WIN32
    int BASE_TEXTURE::PrepareSurfaceCopy(IDirect3DSurface8* sourceSurface, const RECTI& sourceRect, const RECTI* destinationOrigin)
    {

        IDirect3DSurface8* destinationSurface;
        const HRESULT surfaceResult = m_nativeHandle->GetSurfaceLevel(0u, &destinationSurface);
        if (surfaceResult != D3D_OK)
        {
            LOG::ResourceError("TEXTURE", 9, "surface for copy", static_cast<int>(surfaceResult));
            return static_cast<int>(surfaceResult);
        }

        RECTI destinationRect;
        destinationRect.left = destinationOrigin->left;
        destinationRect.top = destinationOrigin->top;
        destinationRect.right = destinationOrigin->left + (sourceRect.right - sourceRect.left);
        destinationRect.bottom = destinationOrigin->top + (sourceRect.bottom - sourceRect.top);

        const SurfaceTransferState transfer = CopySurfaceRegion(
            currentTextureDevice(),
            destinationSurface,
            sourceSurface,
            sourceRect,
            destinationRect,
            0xFFFFFFFFu);
        const HRESULT copyResult = static_cast<HRESULT>(transfer.result);
        if (copyResult != D3D_OK)
            LOG::ResourceError("TEXTURE", 1, "from surface", static_cast<int>(copyResult));

        destinationSurface->Release();
        return static_cast<int>(copyResult);
    }

    void BASE_TEXTURE::DrawFixedDepthRectangle(const RECTI& destination, const RECTI& source, const DWORD* colors)
    {

        const RECTANGLE_VERTEXES_D3D vertexes = makeFixedDepthRectangleVertexes(
            destination,
            source,
            m_width,
            m_height,
            colors[0],
            colors[1]);

        IDirect3DDevice8* const device = currentTextureDevice();
        HRESULT result = device->SetTexture(0u, m_nativeHandle);
        if (result != D3D_OK)
            LOG::ResourceError("TEXTURE", 8, "", static_cast<int>(result));

        if (m_format == 0x29u)
        {
            result = device->SetCurrentTexturePalette(static_cast<UINT>(m_paletteSlot));
            if (result != D3D_OK)
                LOG::ResourceError("TEXTURE", 8, "palette", static_cast<int>(result));
        }

        GRAPH::CurrentGraph()->setRenderStateCached(0x1Du, colors[1] != 0u ? 1u : 0u);

        const DWORD filter =
            destination.width() == source.width() && destination.height() == source.height()
                ? 1u
                : 2u;
        device->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(0x10u), filter);
        device->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(0x11u), filter);
        device->SetVertexShader(0x1C4u);
        result = device->DrawPrimitiveUP(
            D3DPT_TRIANGLEFAN,
            2u,
            vertexes.v,
            0x20u);
        if (result != D3D_OK)
            LOG::ResourceError("TEXTURE", 10, "DrawPrimitiveUP", static_cast<int>(result));
    }

    void BASE_TEXTURE::DrawDepthRectangle(
        DWORD zTop,
        DWORD zBottom,
        const RECTI& destination,
        const RECTI& source,
        const DWORD* colors)
    {

        const RECTANGLE_VERTEXES_D3D vertexes = makeDepthRectangleVertexes(
            destination,
            source,
            m_width,
            m_height,
            zTop,
            zBottom,
            colors[0],
            colors[1]);

        IDirect3DDevice8* const device = currentTextureDevice();
        HRESULT result = device->SetTexture(0u, m_nativeHandle);
        if (result != D3D_OK)
            LOG::ResourceError("TEXTURE", 8, "", static_cast<int>(result));

        if (m_format == 0x29u)
        {
            result = device->SetCurrentTexturePalette(static_cast<UINT>(m_paletteSlot));
            if (result != D3D_OK)
                LOG::ResourceError("TEXTURE", 8, "palette", static_cast<int>(result));
        }

        GRAPH::CurrentGraph()->setRenderStateCached(0x1Du, colors[1] != 0u ? 1u : 0u);
        device->SetVertexShader(0x1C4u);

        const DWORD filter =
            destination.width() == source.width() && destination.height() == source.height()
                ? 1u
                : 2u;
        device->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(0x10u), filter);
        device->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(0x11u), filter);
        result = device->DrawPrimitiveUP(
            D3DPT_TRIANGLEFAN,
            2u,
            vertexes.v,
            0x20u);
        if (result != D3D_OK)
            LOG::ResourceError("TEXTURE", 10, "DrawPrimitiveUP", static_cast<int>(result));
    }
#else
    const BaseTextureSurfaceCopyState& BASE_TEXTURE::PrepareSurfaceCopy(void* sourceSurface, const RECTI& sourceRect, const RECTI* destinationOrigin)
    {

        m_surfaceCopyState = BaseTextureSurfaceCopyState{};
        m_surfaceCopyState.recorded = true;
        m_surfaceCopyState.nativeTexture = m_nativeHandle;
        m_surfaceCopyState.sourceSurface = sourceSurface;
        m_surfaceCopyState.getSurfaceLevelRequired = (m_nativeHandle != nullptr);
        m_surfaceCopyState.sourceRect = sourceRect;
        m_surfaceCopyState.sourceRectProvided = true;
        if (destinationOrigin)
            m_surfaceCopyState.destinationOrigin = *destinationOrigin;

        const int baseLeft = destinationOrigin ? destinationOrigin->left : 0;
        const int baseTop = destinationOrigin ? destinationOrigin->top : 0;
        m_surfaceCopyState.shiftedDestinationRect.left = baseLeft;
        m_surfaceCopyState.shiftedDestinationRect.top = baseTop;
        m_surfaceCopyState.shiftedDestinationRect.right = baseLeft + (sourceRect.right - sourceRect.left);
        m_surfaceCopyState.shiftedDestinationRect.bottom = baseTop + (sourceRect.bottom - sourceRect.top);

        if (!m_nativeHandle || !sourceSurface)
        {
            m_surfaceCopyState.surfaceCopyResult = kSurfaceCopyInvalidCall;
            return m_surfaceCopyState;
        }

#ifdef _WIN32
        IDirect3DSurface8* destinationSurface = nullptr;
        const HRESULT surfaceResult = static_cast<IDirect3DTexture8*>(m_nativeHandle)->GetSurfaceLevel(0, &destinationSurface);
        m_surfaceCopyState.getSurfaceLevelResult = static_cast<DWORD>(surfaceResult);
        m_surfaceCopyState.destinationSurface = destinationSurface;
        if (FAILED(surfaceResult) || !destinationSurface)
        {
            m_surfaceCopyState.surfaceCopyResult = static_cast<DWORD>(surfaceResult);
            LOG::ResourceError("%s", 9, "surface for copy", static_cast<int>(surfaceResult), "TEXTURE");
            return m_surfaceCopyState;
        }
        m_surfaceCopyState.getSurfaceLevelSucceeded = true;

        m_surfaceCopyState.transfer = CopySurfaceRegion(
            nullptr,
            destinationSurface,
            static_cast<IDirect3DSurface8*>(sourceSurface),
            sourceRect,
            m_surfaceCopyState.shiftedDestinationRect);
        const HRESULT copyResult = static_cast<HRESULT>(m_surfaceCopyState.transfer.result);
        m_surfaceCopyState.surfaceCopySubmitted = true;
        m_surfaceCopyState.surfaceCopySucceeded = (m_surfaceCopyState.transfer.result == 0);
        m_surfaceCopyState.surfaceCopyResult = m_surfaceCopyState.transfer.result;
        destinationSurface->Release();
        m_surfaceCopyState.destinationSurfaceReleased = true;
        if (FAILED(copyResult))
            LOG::ResourceError("%s", 1, "from surface", static_cast<int>(copyResult), "TEXTURE");
#else
        m_surfaceCopyState.surfaceCopyResult = kSurfaceCopyInvalidCall;
#endif
        return m_surfaceCopyState;
    }

    const BaseTextureSurfaceCopyState& BASE_TEXTURE::PrepareSurfaceCopy(const RECTI& destination, const RECTI& source, const RECTI* sourceBase)
    {
        (void)destination;
        return PrepareSurfaceCopy(nullptr, source, sourceBase);
    }

    const BaseTextureFixedDepthRectangleDrawState& BASE_TEXTURE::DrawFixedDepthRectangle(const RECTI& destination, const RECTI& source, DWORD color0, DWORD color1)
    {

        m_fixedDepthRectangleDrawState = BaseTextureFixedDepthRectangleDrawState{};
        m_fixedDepthRectangleDrawState.recorded = true;
        m_fixedDepthRectangleDrawState.nativeTexture = m_nativeHandle;
        m_fixedDepthRectangleDrawState.format = m_format;
        m_fixedDepthRectangleDrawState.width = m_width;
        m_fixedDepthRectangleDrawState.height = m_height;
        m_fixedDepthRectangleDrawState.paletteSlot = m_paletteSlot;
        m_fixedDepthRectangleDrawState.destination = destination;
        m_fixedDepthRectangleDrawState.source = source;
        m_fixedDepthRectangleDrawState.color0 = color0;
        m_fixedDepthRectangleDrawState.color1 = color1;
        m_fixedDepthRectangleDrawState.diffuseAfterNot = ~color0;
        m_fixedDepthRectangleDrawState.specular = color1;
        m_fixedDepthRectangleDrawState.requiresSetTexture = (m_nativeHandle != nullptr);
        m_fixedDepthRectangleDrawState.requiresSetCurrentTexturePalette = (m_format == 41u);
        m_fixedDepthRectangleDrawState.alphaGateFromSecondColorDword = (color1 != 0);
        m_fixedDepthRectangleDrawState.alphaBlendState = color1 != 0 ? 1u : 0u;
        const bool sameWidth = (destination.right - destination.left) == (source.right - source.left);
        const bool sameHeight = (destination.bottom - destination.top) == (source.bottom - source.top);
        m_fixedDepthRectangleDrawState.sourceAndDestinationSizeMatch = sameWidth && sameHeight;
        const DWORD filterValue = m_fixedDepthRectangleDrawState.sourceAndDestinationSizeMatch ? 1u : 2u;
        m_fixedDepthRectangleDrawState.minFilterState = filterValue;
        m_fixedDepthRectangleDrawState.magFilterState = filterValue;
        m_fixedDepthRectangleDrawState.vertices = makeFixedDepthRectangleVertexes(destination, source, m_width, m_height, color0, color1);

#ifdef _WIN32
        IDirect3DDevice8* device = currentTextureDevice();
        if (device && m_nativeHandle)
        {
            device->SetTexture(0, static_cast<IUnknown*>(static_cast<IDirect3DTexture8*>(m_nativeHandle)));
            if (m_format == 41u && m_paletteSlot >= 0)
                device->SetCurrentTexturePalette(static_cast<UINT>(m_paletteSlot));
            device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x1Du), color1 != 0 ? 1u : 0u);
            device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(0x10u), filterValue);
            device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(0x11u), filterValue);
            device->SetVertexShader(0x1C4u);
            const HRESULT result = device->DrawPrimitiveUP(
                D3DPT_TRIANGLEFAN,
                2,
                m_fixedDepthRectangleDrawState.vertices.v,
                sizeof(RECTANGLE_VERTEX_D3D));
            m_fixedDepthRectangleDrawState.drawSubmitted = true;
            m_fixedDepthRectangleDrawState.drawResult = static_cast<DWORD>(result);
        }
#endif
        return m_fixedDepthRectangleDrawState;
    }

    const BaseTextureDepthRectangleDrawState& BASE_TEXTURE::DrawDepthRectangle(DWORD zTop, DWORD zBottom, const RECTI& destination, const RECTI& source, DWORD color0, DWORD color1)
    {

        m_depthRectangleDrawState = BaseTextureDepthRectangleDrawState{};
        m_depthRectangleDrawState.recorded = true;
        m_depthRectangleDrawState.nativeTexture = m_nativeHandle;
        m_depthRectangleDrawState.format = m_format;
        m_depthRectangleDrawState.width = m_width;
        m_depthRectangleDrawState.height = m_height;
        m_depthRectangleDrawState.paletteSlot = m_paletteSlot;
        m_depthRectangleDrawState.zTop = zTop;
        m_depthRectangleDrawState.zBottom = zBottom;
        m_depthRectangleDrawState.destination = destination;
        m_depthRectangleDrawState.source = source;
        m_depthRectangleDrawState.color0 = color0;
        m_depthRectangleDrawState.color1 = color1;
        m_depthRectangleDrawState.diffuseAfterNot = ~color0;
        m_depthRectangleDrawState.specular = color1;
        m_depthRectangleDrawState.requiresSetTexture = (m_nativeHandle != nullptr);
        m_depthRectangleDrawState.requiresSetCurrentTexturePalette = (m_format == 41u);
        m_depthRectangleDrawState.alphaGateFromSecondColorDword = (color1 != 0);
        m_depthRectangleDrawState.alphaBlendState = color1 != 0 ? 1u : 0u;
        const bool sameWidth = (destination.right - destination.left) == (source.right - source.left);
        const bool sameHeight = (destination.bottom - destination.top) == (source.bottom - source.top);
        m_depthRectangleDrawState.sourceAndDestinationSizeMatch = sameWidth && sameHeight;
        const DWORD filterValue = m_depthRectangleDrawState.sourceAndDestinationSizeMatch ? 1u : 2u;
        m_depthRectangleDrawState.minFilterState = filterValue;
        m_depthRectangleDrawState.magFilterState = filterValue;
        m_depthRectangleDrawState.vertexShaderFvf = 0x1C4u;
        m_depthRectangleDrawState.primitiveType = 6u;
        m_depthRectangleDrawState.primitiveCount = 2u;
        m_depthRectangleDrawState.vertexStride = 0x20u;
        m_depthRectangleDrawState.vertices = makeDepthRectangleVertexes(destination, source, m_width, m_height, zTop, zBottom, color0, color1);

#ifdef _WIN32
        IDirect3DDevice8* device = currentTextureDevice();
        if (device && m_nativeHandle)
        {
            device->SetTexture(0, static_cast<IUnknown*>(static_cast<IDirect3DTexture8*>(m_nativeHandle)));
            if (m_format == 41u && m_paletteSlot >= 0)
                device->SetCurrentTexturePalette(static_cast<UINT>(m_paletteSlot));
            device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x1Du), color1 != 0 ? 1u : 0u);
            device->SetVertexShader(0x1C4u);
            device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(0x10u), filterValue);
            device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(0x11u), filterValue);
            const HRESULT result = device->DrawPrimitiveUP(
                D3DPT_TRIANGLEFAN,
                2,
                m_depthRectangleDrawState.vertices.v,
                sizeof(RECTANGLE_VERTEX_D3D));
            m_depthRectangleDrawState.drawSubmitted = true;
            m_depthRectangleDrawState.drawResult = static_cast<DWORD>(result);
        }
#endif
        return m_depthRectangleDrawState;
    }

#endif

}
