#include "constant.h"
#include "core/resource.h"
#include "core/log.h"
#include <cstring>

namespace as1
{
    namespace
    {
        constexpr float CNST_MILLI_SCALE = 0.001f;
        constexpr float CNST_MICRO_SCALE = 0.000001f;

        BASE_CONSTANTS* g_dword_489230 = nullptr;

        float dwordToFloat(DWORD value)
        {
            float out = 0.0f;
            std::memcpy(&out, &value, sizeof(out));
            return out;
        }

        DWORD floatToDword(float value)
        {
            DWORD out = 0;
            std::memcpy(&out, &value, sizeof(out));
            return out;
        }

        void scaleFloatSlot(DWORD& value, float scale)
        {
            value = floatToDword(dwordToFloat(value) * scale);
        }
    }

    bool BASE_CONSTANTS::Load(RESOURCE* res)
    {
        // Retail loadBaseConstantsFromResource does not clear the 0x68-byte owner and does not
        // validate CNST SubSize/read return codes.  After GoBegin(CNST) succeeds
        // it issues exactly 26 consecutive virtual 4-byte reads into +0x00..+0x64.
        if (res->GoBegin(RESOURCE::ResTypes::CONSTANT))
        {
            LOG::Write("!!!ERROR!!! CNST Load Constant section not found");
            return false;
        }

        for (std::size_t index = 0; index < raw.size(); ++index)
            (void)res->read(&raw[index], 4u);

        scaleFloatSlot(raw[0], CNST_MILLI_SCALE);
        scaleFloatSlot(raw[1], CNST_MILLI_SCALE);
        scaleFloatSlot(raw[2], CNST_MICRO_SCALE);
        scaleFloatSlot(raw[3], CNST_MICRO_SCALE);
        scaleFloatSlot(raw[6], CNST_MILLI_SCALE);
        scaleFloatSlot(raw[7], CNST_MILLI_SCALE);
        scaleFloatSlot(raw[24], CNST_MILLI_SCALE);
        return true;
    }

    BASE_CONSTANTS* loadBaseConstantsFromResource(BASE_CONSTANTS* owner, RESOURCE* res)
    {

        (void)owner->Load(res);
        return owner;
    }

    BASE_CONSTANTS* GlobalBaseConstants() noexcept
    {
        return g_dword_489230;
    }

    void BindGlobalBaseConstants(BASE_CONSTANTS* constants) noexcept
    {
        g_dword_489230 = constants;
    }
}
