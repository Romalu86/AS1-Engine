#pragma once

#include "sprite.h"
#include "map.h"
#include "core/application.h"

namespace as1
{
    class BUILDED_TERRAIN : public SPRITE
    {
    public:
        using SPRITE::SPRITE;

        void drawBuiltTerrain()
        {
            
            const core::ApplicationVidTable& vids = core::GlobalApplicationVidTable();
            VID* ground = nullptr;
            if (vids.count() > 1024)
                ground = vids.slot(1024);
            if (!ground)
                ground = MAP::NullVid();
            if (ground == MAP::NullVid() || ground->directionCount() != 1)
                SPRITE::Draw();
        }

        void Draw() override { drawBuiltTerrain(); }
    };
#if UINTPTR_MAX == 0xFFFFFFFFu
    static_assert(sizeof(BUILDED_TERRAIN) == 0x70, "retail class 8 BUILDED_TERRAIN allocation must be 0x70 on x86");
#endif
}
