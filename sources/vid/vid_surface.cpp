#include "vid/vid_surface.h"
#include "core/resource.h"
#include "graph.h"
#include "sprite.h"
#ifdef _WIN32
#include "d3d8.h"
#endif
#include <array>
#include <new>

namespace as1
{
    namespace
    {
        WORD paletteDwordToR5G6B5(DWORD value)
        {

            return static_cast<WORD>(((value >> 8u) & 0xF800u) |
                                     ((value >> 5u) & 0x07E0u) |
                                     ((value >> 3u) & 0x001Fu));
        }

        unsigned surfaceFormatBitsPerPixel(DWORD format)
        {

            switch (format)
            {
            case 20u: return 24u;
            case 21u:
            case 22u: return 32u;
            case 23u:
            case 24u:
            case 25u:
            case 26u: return 16u;
            case 41u: return 8u;
            default: return 0u;
            }
        }


        template <class T>
        void deleteThroughRetailSlot00(T* owner) noexcept
        {
            if (!owner)
                return;
#if defined(_MSC_VER) && defined(_M_IX86)

            __asm
            {
                mov ecx, owner
                mov eax, dword ptr [ecx]
                push 1
                call dword ptr [eax]
            }
#else
            delete owner;
#endif
        }

        template <class T>
        void releaseSurfacePointerTable(T**& table, int frameCount) noexcept
        {
            if (!table)
                return;

            if (frameCount > 0)
            {
                for (int earlier = 0; earlier < frameCount; ++earlier)
                {
                    for (int later = earlier + 1; later < frameCount; ++later)
                    {
                        if (table[earlier] == table[later])
                            table[later] = nullptr;
                    }
                }

                for (int frame = 0; frame < frameCount; ++frame)
                {
                    deleteThroughRetailSlot00(table[frame]);
                }
            }

            ::operator delete(table);
            table = nullptr;
        }


    }

    VID_SURFACE::VID_SURFACE() = default;

    VID_SURFACE::VID_SURFACE(const VID_SURFACE& other)
        : VID()
    {

        nextMirror = other.nextMirrorVid();
        const_cast<VID_SURFACE&>(other).nextMirror = this;

        layer = other.layer;
        type = other.formatFlags();
        noCadr = static_cast<short>(other.totalFrames());
        frameSpeedDefault = other.defaultFrameSpeed();
        vidSizeX = other.vidWidth();
        vidSizeY = other.vidHeight();

        m_surfaceTextureOwners = other.m_surfaceTextureOwners;
        m_surfaceTexcoordOwners = other.m_surfaceTexcoordOwners;
    }

    VID_SURFACE* VID_SURFACE::CreateMirror()
    {

        return new (std::nothrow) VID_SURFACE(*this);
    }

    VID_SURFACE::~VID_SURFACE()
    {
        ReleaseSurfaceOwnerSlots();
    }

    VID_SURFACE* vidSurfaceScalarDeletingDestructor(VID_SURFACE* owner, unsigned char deletingFlags) noexcept
    {
        owner->~VID_SURFACE();
        if ((deletingFlags & 1u) != 0u)
            ::operator delete(owner);
        return owner;
    }

    void VID_SURFACE::ReleaseSurfaceOwnerSlots() const noexcept
    {

        if (!isMirrorChainOwner())
            return;

        const int frameCount = static_cast<int>(static_cast<short>(totalFrames()));
        releaseSurfacePointerTable(m_surfaceTextureOwners, frameCount);
        releaseSurfacePointerTable(m_surfaceTexcoordOwners, frameCount);
    }


    void VID_SURFACE::SetLayer()
    {

        layer = 5;
    }


    void VID_SURFACE::Load(RESOURCE* globalRes)
    {

        const int frameCount = static_cast<int>(static_cast<std::int16_t>(totalFrames()));
        const std::uint32_t tableBytes32 = static_cast<std::uint32_t>(frameCount * 4);
        const std::size_t tableBytes = static_cast<std::size_t>(tableBytes32);
        m_surfaceTextureOwners = static_cast<BASE_TEXTURE**>(::operator new(tableBytes));
        m_surfaceTexcoordOwners = static_cast<VID_TEXCOOR**>(::operator new(tableBytes));

        // The original stack palette is not initialized when type bit 8 is clear.
        // It is filled only when PAL exists; a missing PAL is reported and the
        // function continues into DATA.
        std::array<DWORD, 256> paletteDwords;
        if ((formatFlags() & VID_TYPE_PALETTE) != 0)
        {
            if (globalRes->GoNext(RESOURCE::ResTypes::PALETTE) != 0)
                ReportResourceError(5, "PAL ", 0);
            else
                globalRes->read(paletteDwords.data(), static_cast<unsigned>(sizeof(paletteDwords)));
        }

        if (globalRes->GoNext(RESOURCE::ResTypes::DATA) != 0)
            ReportResourceError(5, "DATA", 0);

        if (frameCount <= 0)
            return;

        std::size_t tableByteOffset = 0;
        for (int frame = 0; frame < frameCount; ++frame, tableByteOffset += 4u)
        {
            DWORD marker;
            globalRes->read(&marker, sizeof(marker));

            if (marker == 4u)
            {
                DWORD repeatFrame;
                globalRes->read(&repeatFrame, sizeof(repeatFrame));
                BASE_TEXTURE* const repeated = m_surfaceTextureOwners[repeatFrame];
                if (repeated)
                    *reinterpret_cast<BASE_TEXTURE**>(reinterpret_cast<BYTE*>(m_surfaceTextureOwners) + tableByteOffset) = repeated;
                else
                    ReportResourceError(5, "Invalid repeat cadr", static_cast<int>(repeatFrame));
            }
            else
            {
                DWORD requestedFormat;
                WORD textureWidth;
                WORD textureHeight;
                globalRes->read(&requestedFormat, sizeof(requestedFormat));
                globalRes->read(&textureWidth, sizeof(textureWidth));
                globalRes->read(&textureHeight, sizeof(textureHeight));

                BASE_TEXTURE* texture = new (std::nothrow) BASE_TEXTURE(
                    static_cast<int>(textureWidth),
                    static_cast<int>(textureHeight),
                    requestedFormat,
                    0u);
                *reinterpret_cast<BASE_TEXTURE**>(reinterpret_cast<BYTE*>(m_surfaceTextureOwners) + tableByteOffset) = texture;

                if (texture->format() == 41u)
                    texture->createPaletteSlot(paletteDwords.data());

                int pitchBytes = static_cast<int>(marker);
                BYTE* row = reinterpret_cast<BYTE*>(texture->lock16(&pitchBytes, nullptr));
                for (unsigned y = 0; y < static_cast<unsigned>(textureHeight); ++y)
                {
                    if (texture->format() == requestedFormat)
                    {
                        const unsigned bits = surfaceFormatBitsPerPixel(requestedFormat);
                        const unsigned rowBytes = static_cast<unsigned>(textureWidth) * (bits >> 3u);
                        globalRes->read(row, rowBytes);
                    }
                    else
                    {
                        WORD* dst = reinterpret_cast<WORD*>(row);
                        for (unsigned x = 0; x < static_cast<unsigned>(textureWidth); ++x)
                        {
                            BYTE paletteIndex;
                            globalRes->read(&paletteIndex, sizeof(paletteIndex));
                            dst[x] = paletteDwordToR5G6B5(paletteDwords[paletteIndex]);
                        }
                    }
                    row += pitchBytes;
                }
                texture->unlock();

                DWORD vertexCount;
                DWORD indexCount;
                globalRes->read(&vertexCount, sizeof(vertexCount));
                globalRes->read(&indexCount, sizeof(indexCount));

                VID_TEXCOOR* texcoor = new (std::nothrow) VID_TEXCOOR(
                    static_cast<int>(vertexCount),
                    static_cast<int>(indexCount));
                *reinterpret_cast<VID_TEXCOOR**>(reinterpret_cast<BYTE*>(m_surfaceTexcoordOwners) + tableByteOffset) = texcoor;

                VID_TEXCOOR_VERTEX* dstVertex = texcoor->lockVertexBuffer();
                for (int vertex = 0; vertex < static_cast<int>(vertexCount); ++vertex)
                {
                    std::int16_t screenX;
                    std::int16_t screenY;
                    std::int16_t depthCode;
                    std::int16_t texU;
                    std::int16_t texV;
                    globalRes->read(&screenX, sizeof(screenX));
                    globalRes->read(&screenY, sizeof(screenY));
                    globalRes->read(&depthCode, sizeof(depthCode));
                    globalRes->read(&texU, sizeof(texU));
                    globalRes->read(&texV, sizeof(texV));

                    const float z = static_cast<float>(static_cast<int>(depthCode) - 1024) * 0.125f;
                    dstVertex[vertex].x = static_cast<float>(screenX) -
                        static_cast<float>(static_cast<std::int16_t>(vidWidth())) * 0.5f;
                    dstVertex[vertex].z = z;
                    dstVertex[vertex].y = (static_cast<float>(screenY) -
                        static_cast<float>(static_cast<std::int16_t>(vidHeight())) * 0.5f + z) * 1.5f;
                    dstVertex[vertex].u = (static_cast<float>(texU) + 0.5f) /
                        static_cast<float>(texture->width());
                    dstVertex[vertex].v = (static_cast<float>(texV) + 0.5f) /
                        static_cast<float>(texture->height());
                }
                texcoor->unlockVertexBuffer();

                WORD* dstIndex = texcoor->lockIndexBuffer();
                globalRes->read(dstIndex, static_cast<unsigned>(indexCount * sizeof(WORD)));
                texcoor->unlockIndexBuffer();
            }

            globalRes->GoNextSub(RESOURCE::ResTypes::DATA);
        }
    }

    void VID_SURFACE::Draw(const SPRITE* sprite)
    {

        if ((property & 0x00000400u) != 0u)
            return;

        const WORD flags = formatFlags();
        GRAPH* const graph = GRAPH::CurrentGraph();
        if ((flags & 0x0002u) != 0u)
        {
            if ((flags & 0x0001u) != 0u)
                graph->setAlphaBlendFactors(5u, 6u);
            else
                graph->setAlphaBlendFactors(9u, 2u);
        }
        else
        {
            graph->setRenderStateCached(0x1Bu, 0u);
        }

        const int frame = sprite->currentFrame();
        BASE_TEXTURE* const texture = m_surfaceTextureOwners[frame];
#ifdef _WIN32
        static_cast<IDirect3DDevice8*>(GRAPH::CurrentDevice())->SetTexture(
            0u,
            static_cast<IUnknown*>(static_cast<IDirect3DTexture8*>(texture->nativeHandle())));
#else
        (void)texture;
#endif
        m_surfaceTexcoordOwners[frame]->drawTexcoorMesh(*sprite);
    }

    bool VID_SURFACE::transparencyCheck() const
    {
        return true;
    }

    bool VID_SURFACE::isLoaded() const
    {

        return m_surfaceTextureOwners != nullptr && m_surfaceTexcoordOwners != nullptr;
    }
}
