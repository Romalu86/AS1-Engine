#include "vid/vid_software_png.h"

namespace as1
{
    void VID_SOFTWARE_PNG::Draw(const SPRITE* sprite)
    {

        VID::Draw(sprite);
    }

    bool VID_SOFTWARE_PNG::transparencyCheck() const
    {
        return VID_SOFTWARE::transparencyCheck();
    }

    bool VID_SOFTWARE_PNG::isLoaded() const
    {
        return VID_SOFTWARE::isLoaded();
    }

    bool VID_SOFTWARE_PNG::unloadable() const
    {
        return VID_SOFTWARE::unloadable();
    }
}
