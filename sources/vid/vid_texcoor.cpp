#include "vid/vid_texcoor.h"
#include "sprite.h"
#include "vid/vid.h"
#include "graph.h"
#include "core/log.h"
#include "core/file_logger.h"
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstring>
#ifdef _WIN32
#include "d3d8.h"
#endif

namespace as1
{
    namespace
    {

        float directionSinValue(int index)
        {
            return SPRITE::rawDirectionSin(index);
        }

        float directionCosValue(int index)
        {
            return SPRITE::rawDirectionCos(index);
        }

        float directionSinAuxValue(int index)
        {
            return SPRITE::rawDirectionSinAux(index);
        }

        float directionCosAuxValue(int index)
        {
            return SPRITE::rawDirectionCosAux(index);
        }

        int retailIdivDirection(std::int32_t numerator, std::int32_t denominator) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            int quotient = 0;
            __asm
            {
                mov eax, numerator
                cdq
                idiv denominator
                mov quotient, eax
            }
            return quotient;
#else
            if (denominator == 0)
                std::abort();
            return numerator / denominator;
#endif
        }

#ifdef _WIN32
        IDirect3DDevice8* currentVidTexcoorDevice()
        {
            return static_cast<IDirect3DDevice8*>(GRAPH::CurrentDevice());
        }
#endif

        int normalizedCount(int value)
        {
            return value > 0 ? value : 0;
        }
    }

    VID_TEXCOOR::VID_TEXCOOR(int vertexCount, int indexCount)
    {

#ifdef _WIN32
        m_vertexCount = vertexCount;
        m_indexCount = indexCount;

        IDirect3DDevice8* device = currentVidTexcoorDevice();
        HRESULT result = device->CreateVertexBuffer(
            static_cast<UINT>(vertexCount) * 0x14u,
            8u,
            0x102u,
            static_cast<D3DPOOL>(1u),
            &m_nativeVertexBuffer);
        if (result != D3D_OK)
            LOG::ResourceError("MESH", 3, "VertexBuffer", static_cast<int>(result));

        result = device->CreateIndexBuffer(
            static_cast<UINT>(indexCount) * 2u,
            8u,
            static_cast<D3DFORMAT>(0x65u),
            static_cast<D3DPOOL>(1u),
            &m_nativeIndexBuffer);
        if (result != D3D_OK)
            LOG::ResourceError("MESH", 3, "IndexBuffer", static_cast<int>(result));
#else
        // Compatibility-only software storage. The original accepted signed counts
        // directly; this branch is not used for Win32 ABI/runtime acceptance.
        m_vertexCount = normalizedCount(vertexCount);
        m_indexCount = normalizedCount(indexCount);
        m_vertexBuffer.assign(static_cast<std::size_t>(m_vertexCount), VID_TEXCOOR_VERTEX{});
        m_indexBuffer.assign(static_cast<std::size_t>(m_indexCount), WORD{});
        m_bufferState.recorded = true;
        m_bufferState.vertexCount = m_vertexCount;
        m_bufferState.indexCount = m_indexCount;
        m_bufferState.vertexBufferBytes = static_cast<std::size_t>(m_vertexCount) * sizeof(VID_TEXCOOR_VERTEX);
        m_bufferState.indexBufferBytes = static_cast<std::size_t>(m_indexCount) * sizeof(WORD);
        m_bufferState.vertexBufferCreated = m_vertexCount > 0;
        m_bufferState.indexBufferCreated = m_indexCount > 0;
#endif
    }

    VID_TEXCOOR::~VID_TEXCOOR()
    {

#ifdef _WIN32
        if (m_nativeIndexBuffer)
            m_nativeIndexBuffer->Release();
        if (m_nativeVertexBuffer)
            m_nativeVertexBuffer->Release();

        IDirect3DDevice8* device = currentVidTexcoorDevice();
        device->SetIndices(nullptr, 0);
        device->SetStreamSource(0, nullptr, 0x14u);
#endif
    }

    VID_TEXCOOR* vidTexcoorScalarDeletingDestructor(VID_TEXCOOR* owner, unsigned char deletingFlags) noexcept
    {
        owner->~VID_TEXCOOR();
        if ((deletingFlags & 1u) != 0u)
            ::operator delete(owner);
        return owner;
    }

    VID_TEXCOOR_VERTEX* VID_TEXCOOR::lockVertexBuffer()
    {

#ifdef _WIN32

        BYTE* locked = reinterpret_cast<BYTE*>(this);
        m_nativeVertexBuffer->Lock(0, 0, &locked, 0x2000u);
        return reinterpret_cast<VID_TEXCOOR_VERTEX*>(locked);
#else
        m_lastVertexLockState = VidTexcoorLockState{};
        m_lastVertexLockState.recorded = true;
        m_lastVertexLockState.vertexBuffer = true;
        m_lastVertexLockState.byteCount = static_cast<std::size_t>(m_vertexCount) * sizeof(VID_TEXCOOR_VERTEX);
        m_vertexLocked = !m_vertexBuffer.empty();
        m_lastVertexLockState.lockActive = m_vertexLocked;
        m_lastVertexLockState.pointer = m_vertexLocked ? static_cast<void*>(m_vertexBuffer.data()) : nullptr;
        return m_vertexLocked ? m_vertexBuffer.data() : nullptr;
#endif
    }

    int VID_TEXCOOR::unlockVertexBuffer()
    {

#ifdef _WIN32
        return static_cast<int>(m_nativeVertexBuffer->Unlock());
#else
        m_vertexLocked = false;
        if (m_lastVertexLockState.recorded)
            m_lastVertexLockState.lockActive = false;
        return 0;
#endif
    }

    WORD* VID_TEXCOOR::lockIndexBuffer()
    {

#ifdef _WIN32

        BYTE* locked = reinterpret_cast<BYTE*>(this);
        m_nativeIndexBuffer->Lock(0, 0, &locked, 0x2000u);
        return reinterpret_cast<WORD*>(locked);
#else
        m_lastIndexLockState = VidTexcoorLockState{};
        m_lastIndexLockState.recorded = true;
        m_lastIndexLockState.indexBuffer = true;
        m_lastIndexLockState.byteCount = static_cast<std::size_t>(m_indexCount) * sizeof(WORD);
        m_indexLocked = !m_indexBuffer.empty();
        m_lastIndexLockState.lockActive = m_indexLocked;
        m_lastIndexLockState.pointer = m_indexLocked ? static_cast<void*>(m_indexBuffer.data()) : nullptr;
        return m_indexLocked ? m_indexBuffer.data() : nullptr;
#endif
    }

    int VID_TEXCOOR::unlockIndexBuffer()
    {

#ifdef _WIN32
        return static_cast<int>(m_nativeIndexBuffer->Unlock());
#else
        m_indexLocked = false;
        if (m_lastIndexLockState.recorded)
            m_lastIndexLockState.lockActive = false;
        return 0;
#endif
    }

    int VID_TEXCOOR::drawTexcoorMesh(const SPRITE& sprite) const
    {

        const VID* vid = sprite.Vid();
        const int noDir = vid->noDir;
        const int incDir = vid->directionQuantizationOffset();
        const int directionByte = sprite.Direction().Int() & 0xFF;
        const int dirSource = (directionByte + incDir) & 0xFF;
        const std::uint32_t product = static_cast<std::uint32_t>(dirSource) *
                                      static_cast<std::uint32_t>(noDir);
        const std::uint32_t dirIndexRaw = product >> 8u;
        const std::int32_t quantizedNumerator = static_cast<std::int32_t>(dirIndexRaw << 8u);
        const int quantizedDirectionByte = retailIdivDirection(quantizedNumerator, noDir) & 0xFF;
        const int residualDirectionByte = (directionByte - quantizedDirectionByte) & 0xFF;

        const float cosA = directionCosValue(residualDirectionByte);
        const float sinA = directionSinAuxValue(residualDirectionByte);
        const float negSinA = -directionSinValue(residualDirectionByte);
        const float cosB = directionCosAuxValue(residualDirectionByte);
        const float worldX = sprite.X();
        const float worldY = sprite.Y();
        const float worldZ = sprite.Z();

        const float worldMatrix[16] =
        {
            cosA,    sinA, 0.0f, 0.0f,
            negSinA, cosB, 0.0f, 0.0f,
            0.0f,    0.0f, 1.0f, 0.0f,
            worldX,  worldY, worldZ, 1.0f
        };

#ifdef _WIN32
        D3DMATRIX matrix;
        for (int i = 0; i < 16; ++i)
            matrix.m[i / 4][i % 4] = worldMatrix[i];

        HRESULT hr = currentVidTexcoorDevice()->SetTransform(static_cast<D3DTRANSFORMSTATETYPE>(0x100u), &matrix);
        if (FAILED(hr))
            LOG::ResourceError("%s", 8, "Transform world", static_cast<int>(hr), "MESH");

        hr = currentVidTexcoorDevice()->SetIndices(m_nativeIndexBuffer, 0);
        if (FAILED(hr))
            LOG::ResourceError("%s", 8, "Indices", static_cast<int>(hr), "MESH");

        hr = currentVidTexcoorDevice()->SetStreamSource(0, m_nativeVertexBuffer, 0x14u);
        if (FAILED(hr))
            LOG::ResourceError("%s", 8, "Vertex", static_cast<int>(hr), "MESH");

        hr = currentVidTexcoorDevice()->SetVertexShader(0x102u);
        if (FAILED(hr))
            LOG::ResourceError("%s", 8, "VertexShader", static_cast<int>(hr), "MESH");

        GRAPH::CurrentGraph()->setRenderStateCached(7u, 1u);
        GRAPH::CurrentGraph()->setRenderStateCached(0x17u, 5u);
        GRAPH::CurrentGraph()->setRenderStateCached(0x0Eu, 1u);
        GRAPH::CurrentGraph()->setRenderStateCached(0x16u, 1u);

        hr = currentVidTexcoorDevice()->DrawIndexedPrimitive(
            D3DPT_TRIANGLELIST,
            0,
            static_cast<UINT>(vertexCount()),
            0,
            static_cast<UINT>(indexCount() / 3));
        if (FAILED(hr))
        {

            return static_cast<int>(logFileLoggerResourceError(&GlobalFileLogger(), "%s", 10, "Draw", static_cast<int>(hr), "MESH"));
        }
        return static_cast<int>(hr);
#else
        m_drawState = VidTexcoorDrawState{};
        m_drawState.recorded = true;
        m_drawState.vertexCount = vertexCount();
        m_drawState.indexCount = indexCount();
        m_drawState.numVertices = vertexCount();
        m_drawState.primitiveCount = indexCount() / 3;
        m_drawState.noDir = noDir;
        m_drawState.incDir = incDir;
        m_drawState.directionByte = directionByte;
        m_drawState.quantizedDirectionByte = quantizedDirectionByte;
        m_drawState.residualDirectionByte = residualDirectionByte;
        m_drawState.worldX = worldX;
        m_drawState.worldY = worldY;
        m_drawState.worldZ = worldZ;
        for (int i = 0; i < 16; ++i)
            m_drawState.worldMatrix[i] = worldMatrix[i];
        return 0;
#endif
    }

    bool VID_TEXCOOR::fillSurfaceVertexIndexWords(const VID_TEXCOOR_SURFACE_VERTEX_WORDS* vertices,
                                                  int sourceVertexCount,
                                                  const WORD* indices,
                                                  int sourceIndexCount,
                                                  int vidSizeX,
                                                  int vidSizeY,
                                                  int textureWidth,
                                                  int textureHeight)
    {

        if (!vertices || sourceVertexCount < m_vertexCount || textureWidth <= 0 || textureHeight <= 0)
            return false;
        if (m_indexCount > 0 && (!indices || sourceIndexCount < m_indexCount))
            return false;

        auto s16 = [](WORD value) -> int
        {
            return static_cast<int>(static_cast<std::int16_t>(value));
        };

        VID_TEXCOOR_VERTEX* dstVertex = lockVertexBuffer();
        if (!dstVertex && m_vertexCount > 0)
            return false;

        for (int i = 0; i < m_vertexCount; ++i)
        {
            const VID_TEXCOOR_SURFACE_VERTEX_WORDS& src = vertices[i];
            const float z = (static_cast<float>(s16(src.depthCode) - 1024) * 0.125f);
            dstVertex[i].x = static_cast<float>(s16(src.screenX)) - static_cast<float>(vidSizeX) * 0.5f;
            dstVertex[i].z = z;
            dstVertex[i].y = (static_cast<float>(s16(src.screenY)) - static_cast<float>(vidSizeY) * 0.5f + z) * 1.5f;
            dstVertex[i].u = (static_cast<float>(s16(src.texU)) + 0.5f) / static_cast<float>(textureWidth);
            dstVertex[i].v = (static_cast<float>(s16(src.texV)) + 0.5f) / static_cast<float>(textureHeight);
        }
        unlockVertexBuffer();

        WORD* dstIndex = lockIndexBuffer();
        if (!dstIndex && m_indexCount > 0)
            return false;
        for (int i = 0; i < m_indexCount; ++i)
            dstIndex[i] = indices[i];
        unlockIndexBuffer();
        return true;
    }
}
