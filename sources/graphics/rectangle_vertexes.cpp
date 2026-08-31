#include "graphics/rectangle_vertexes.h"
#include <cstring>

namespace as1
{
    namespace
    {
        float floatFromRawDword(DWORD raw)
        {
            float out = 0.0f;
            static_assert(sizeof(out) == sizeof(raw), "float/DWORD size mismatch");
            std::memcpy(&out, &raw, sizeof(out));
            return out;
        }
    }

    RECTANGLE_VERTEXES makeRectangleVertexes(const RECTI& dst, const RECTI& src, int textureWidth, int textureHeight, DWORD diffuse)
    {
        RECTANGLE_VERTEXES out;
        const float invW = textureWidth > 0 ? 1.0f / static_cast<float>(textureWidth) : 0.0f;
        const float invH = textureHeight > 0 ? 1.0f / static_cast<float>(textureHeight) : 0.0f;

        const float left = static_cast<float>(dst.left);
        const float top = static_cast<float>(dst.top);
        const float right = static_cast<float>(dst.right);
        const float bottom = static_cast<float>(dst.bottom);
        const float u0 = (static_cast<float>(src.left) + 0.5f) * invW;
        const float v0 = (static_cast<float>(src.top) + 0.5f) * invH;
        const float u1 = (static_cast<float>(src.right) + 0.5f) * invW;
        const float v1 = (static_cast<float>(src.bottom) + 0.5f) * invH;

        out.v[0] = RECTANGLE_VERTEX{left,  top,    0.0f, 1.0f, diffuse, u0, v0};
        out.v[1] = RECTANGLE_VERTEX{right, top,    0.0f, 1.0f, diffuse, u1, v0};
        out.v[2] = RECTANGLE_VERTEX{right, bottom, 0.0f, 1.0f, diffuse, u1, v1};
        out.v[3] = RECTANGLE_VERTEX{left,  bottom, 0.0f, 1.0f, diffuse, u0, v1};
        return out;
    }

    RECTANGLE_VERTEXES_D3D makeFixedDepthRectangleVertexes(const RECTI& dst, const RECTI& src, int textureWidth, int textureHeight, DWORD color0, DWORD color1)
    {

        return makeDepthRectangleVertexes(dst, src, textureWidth, textureHeight, 0x3F7FFFFEu, 0x3F7FFFFEu, color0, color1);
    }

    RECTANGLE_VERTEXES_D3D makeDepthRectangleVertexes(const RECTI& dst, const RECTI& src, int textureWidth, int textureHeight, DWORD zTopRaw, DWORD zBottomRaw, DWORD color0, DWORD color1)
    {

        RECTANGLE_VERTEXES_D3D out;
        const float zTop = floatFromRawDword(zTopRaw);
        const float zBottom = floatFromRawDword(zBottomRaw);
        const DWORD diffuse = ~color0;
        const DWORD specular = color1;

        const float x0 = static_cast<float>(dst.left);
        const float y0 = static_cast<float>(dst.top);
        const float x1 = static_cast<float>(dst.right);
        const float y1 = static_cast<float>(dst.bottom);
        const float u0 = (static_cast<float>(src.left) + 0.5f) / static_cast<float>(textureWidth);
        const float v0 = (static_cast<float>(src.top) + 0.5f) / static_cast<float>(textureHeight);
        const float u1 = (static_cast<float>(src.right) + 0.5f) / static_cast<float>(textureWidth);
        const float v1 = (static_cast<float>(src.bottom) + 0.5f) / static_cast<float>(textureHeight);

        out.v[0] = RECTANGLE_VERTEX_D3D{x0, y0, zTop,    1.0f, diffuse, specular, u0, v0};
        out.v[1] = RECTANGLE_VERTEX_D3D{x1, y0, zTop,    1.0f, diffuse, specular, u1, v0};
        out.v[2] = RECTANGLE_VERTEX_D3D{x1, y1, zBottom, 1.0f, diffuse, specular, u1, v1};
        out.v[3] = RECTANGLE_VERTEX_D3D{x0, y1, zBottom, 1.0f, diffuse, specular, u0, v1};
        return out;
    }
}
