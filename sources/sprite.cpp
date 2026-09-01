#include "sprite.h"
#include "engine.h"
#include "sprite_act_const.h"
#include "vid/vid.h"
#include "map.h"
#ifdef _WIN32
#include "win/application_win.h"
#endif
#include "graph.h"
#include "graphics/gamma.h"
#include "graphics/base_texture.h"
#include "sprite_collector_hash.h"
#include "rail.h"
#include "mouse.h"
#include "core/application.h"
#include "constant.h"
#include "base_sprite_list.h"
#include "core/as_string.h"
#include "core/base_stream.h"
#include "core/resource.h"
#include "core/configuration.h"
#include "core/profile_p.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "core/retail_stack_abi.h"
#include "core/weak_controller.h"
#include "sound/engine.h"
#include <array>
#include <cmath>
#include <cctype>
#include <algorithm>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <new>
#include <memory>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <limits>

#if defined(_MSC_VER) && defined(_M_IX86)
#define AS1_SPRITE_STDCALL __stdcall
#elif defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
#define AS1_SPRITE_STDCALL __attribute__((stdcall))
#else
#define AS1_SPRITE_STDCALL
#endif

namespace as1
{
    SpriteCommandRecord* copyCommandRecord(SpriteCommandRecord* destination, const SpriteCommandRecord* source) noexcept
    {
        destination->opcode = source->opcode;
        destination->argument1 = source->argument1;
        destination->argument2 = source->argument2;
        destination->argument3 = source->argument3;
        return destination;
    }

    namespace
    {

        const char kEmptyString[] = "";
        const char kCommandRecordDelimiter[] = ";";
        const char kCommandWordPrefixMarker[] = { '\x01', '\0' };
        constexpr unsigned short kX87TruncateRoundingBits = static_cast<unsigned short>(3u << 10);
        constexpr unsigned short kX87RoundingBitsClearMask = static_cast<unsigned short>(~kX87TruncateRoundingBits);

        // Retail-internal action selectors that are not exported by Maps/EXPORT.LGC.
        enum class InternalActionCode : std::uint32_t
        {
            SetAnimationAndDirection = 201u,
            GetAnimation = 202u,
            ChangeCoordinateXY = 203u,
            ChangeCoordinateZ = 204u,
        };

        int g_pathSearchScore0 = 0;
        int g_pathSearchScore1 = 0;

        constexpr char kTrainCollapseBeginLog[] =
        {
            static_cast<char>(0xF1), static_cast<char>(0xF5), static_cast<char>(0xEB),
            static_cast<char>(0xE0), static_cast<char>(0xEF), static_cast<char>(0xFB),
            static_cast<char>(0xE2), static_cast<char>(0xE0), static_cast<char>(0xED),
            static_cast<char>(0xE8), static_cast<char>(0xE5), ' ',
            static_cast<char>(0xE2), static_cast<char>(0xE0), static_cast<char>(0xE3),
            static_cast<char>(0xEE), static_cast<char>(0xED), static_cast<char>(0xEE),
            static_cast<char>(0xE2), ' ', 'z', 'm', '-', 'e', 'r', 'r', 'o', 'r', ' ',
            '-', ' ', 'k', 'a', 'w', 'a', 'b', 'a', 'n', 'g', 'a', ' ', '-', ' ',
            'b', 'e', 'g', 'i', 'n', '\0'
        };

        constexpr char kMissingTailDot2ResourceError[] =
        {
            static_cast<char>(240), static_cast<char>(229), static_cast<char>(235), static_cast<char>(252),
            static_cast<char>(241), static_cast<char>(251), ' ', static_cast<char>(237), static_cast<char>(229),
            static_cast<char>(239), static_cast<char>(240), static_cast<char>(224), static_cast<char>(226),
            static_cast<char>(232), static_cast<char>(235), static_cast<char>(252), static_cast<char>(237),
            static_cast<char>(251), static_cast<char>(229), ' ', static_cast<char>(237), static_cast<char>(229),
            static_cast<char>(242), ' ', 't', 'a', 'i', 'l', '.', 'D', 'o', 't', '2', '(', ')', 0
        };
        constexpr char kMissingLinkResourceError[] =
        {
            static_cast<char>(240), static_cast<char>(229), static_cast<char>(235), static_cast<char>(252),
            static_cast<char>(241), static_cast<char>(251), ' ', static_cast<char>(237), static_cast<char>(229),
            static_cast<char>(239), static_cast<char>(240), static_cast<char>(224), static_cast<char>(226),
            static_cast<char>(232), static_cast<char>(235), static_cast<char>(252), static_cast<char>(237),
            static_cast<char>(251), static_cast<char>(229), ' ', 'l', 'i', 'n', 'k', '>', 'n', 'o', 'l', 'i', 'n', 'k', 0
        };

        int g_collisionPushRecursionDepth = 0;

        std::uintptr_t g_currentImageFrameVtable = 0u;

        void captureCurrentImageFrameVtable(const FRAME* frame) noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            g_currentImageFrameVtable = *reinterpret_cast<const std::uintptr_t*>(frame);
#else
            (void)frame;
#endif
        }

        void publishCurrentImageFrameVtable(FRAME* frame) noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            if (g_currentImageFrameVtable != 0u)
                *reinterpret_cast<std::uintptr_t*>(frame) = g_currentImageFrameVtable;
#else
            (void)frame;
#endif
        }

        constexpr std::uint32_t kDirectionSinTableBits[256] =
        {
            0x00000000u, 0x3CC90AB0u, 0x3D48FB2Fu, 0x3D96A905u, 0x3DC8BD36u, 0x3DFAB273u, 0x3E164083u, 0x3E2F10A2u,
            0x3E47C5C2u, 0x3E605C13u, 0x3E78CFCCu, 0x3E888E93u, 0x3E94A031u, 0x3EA09AE5u, 0x3EAC7CD4u, 0x3EB8442Au,
            0x3EC3EF15u, 0x3ECF7BCAu, 0x3EDAE880u, 0x3EE63375u, 0x3EF15AEAu, 0x3EFC5D27u, 0x3F039C3Du, 0x3F08F59Bu,
            0x3F0E39DAu, 0x3F13682Au, 0x3F187FC0u, 0x3F1D7FD1u, 0x3F226799u, 0x3F273656u, 0x3F2BEB4Au, 0x3F3085BBu,
            0x3F3504F3u, 0x3F396842u, 0x3F3DAEF9u, 0x3F41D870u, 0x3F45E403u, 0x3F49D112u, 0x3F4D9F02u, 0x3F514D3Du,
            0x3F54DB31u, 0x3F584853u, 0x3F5B941Au, 0x3F5EBE05u, 0x3F61C598u, 0x3F64AA59u, 0x3F676BD8u, 0x3F6A09A7u,
            0x3F6C835Eu, 0x3F6ED89Eu, 0x3F710908u, 0x3F731447u, 0x3F74FA0Bu, 0x3F76BA07u, 0x3F7853F8u, 0x3F79C79Du,
            0x3F7B14BEu, 0x3F7C3B28u, 0x3F7D3AACu, 0x3F7E1324u, 0x3F7EC46Du, 0x3F7F4E6Du, 0x3F7FB10Fu, 0x3F7FEC43u,
            0x3F800000u, 0x3F7FEC43u, 0x3F7FB10Fu, 0x3F7F4E6Du, 0x3F7EC46Du, 0x3F7E1324u, 0x3F7D3AACu, 0x3F7C3B28u,
            0x3F7B14BEu, 0x3F79C79Du, 0x3F7853F8u, 0x3F76BA07u, 0x3F74FA0Bu, 0x3F731447u, 0x3F710908u, 0x3F6ED89Eu,
            0x3F6C835Eu, 0x3F6A09A7u, 0x3F676BD8u, 0x3F64AA59u, 0x3F61C598u, 0x3F5EBE05u, 0x3F5B941Au, 0x3F584853u,
            0x3F54DB31u, 0x3F514D3Du, 0x3F4D9F02u, 0x3F49D112u, 0x3F45E403u, 0x3F41D870u, 0x3F3DAEF9u, 0x3F396842u,
            0x3F3504F3u, 0x3F3085BBu, 0x3F2BEB4Au, 0x3F273656u, 0x3F226799u, 0x3F1D7FD1u, 0x3F187FC0u, 0x3F13682Au,
            0x3F0E39DAu, 0x3F08F59Bu, 0x3F039C3Du, 0x3EFC5D27u, 0x3EF15AEAu, 0x3EE63375u, 0x3EDAE880u, 0x3ECF7BCAu,
            0x3EC3EF15u, 0x3EB8442Au, 0x3EAC7CD4u, 0x3EA09AE5u, 0x3E94A031u, 0x3E888E93u, 0x3E78CFCCu, 0x3E605C13u,
            0x3E47C5C2u, 0x3E2F10A2u, 0x3E164083u, 0x3DFAB273u, 0x3DC8BD36u, 0x3D96A905u, 0x3D48FB2Fu, 0x3CC90AB0u,
            0x00000000u, 0xBCC90AAFu, 0xBD48FB2Fu, 0xBD96A905u, 0xBDC8BD36u, 0xBDFAB273u, 0xBE164083u, 0xBE2F10A2u,
            0xBE47C5C2u, 0xBE605C13u, 0xBE78CFCCu, 0xBE888E93u, 0xBE94A031u, 0xBEA09AE5u, 0xBEAC7CD4u, 0xBEB8442Au,
            0xBEC3EF15u, 0xBECF7BCAu, 0xBEDAE880u, 0xBEE63375u, 0xBEF15AEAu, 0xBEFC5D27u, 0xBF039C3Du, 0xBF08F59Bu,
            0xBF0E39DAu, 0xBF13682Au, 0xBF187FC0u, 0xBF1D7FD1u, 0xBF226799u, 0xBF273656u, 0xBF2BEB4Au, 0xBF3085BBu,
            0xBF3504F3u, 0xBF396842u, 0xBF3DAEF9u, 0xBF41D870u, 0xBF45E403u, 0xBF49D112u, 0xBF4D9F02u, 0xBF514D3Du,
            0xBF54DB31u, 0xBF584853u, 0xBF5B941Au, 0xBF5EBE05u, 0xBF61C598u, 0xBF64AA59u, 0xBF676BD8u, 0xBF6A09A7u,
            0xBF6C835Eu, 0xBF6ED89Eu, 0xBF710908u, 0xBF731447u, 0xBF74FA0Bu, 0xBF76BA07u, 0xBF7853F8u, 0xBF79C79Du,
            0xBF7B14BEu, 0xBF7C3B28u, 0xBF7D3AACu, 0xBF7E1324u, 0xBF7EC46Du, 0xBF7F4E6Du, 0xBF7FB10Fu, 0xBF7FEC43u,
            0xBF800000u, 0xBF7FEC43u, 0xBF7FB10Fu, 0xBF7F4E6Du, 0xBF7EC46Du, 0xBF7E1324u, 0xBF7D3AACu, 0xBF7C3B28u,
            0xBF7B14BEu, 0xBF79C79Du, 0xBF7853F8u, 0xBF76BA07u, 0xBF74FA0Bu, 0xBF731447u, 0xBF710908u, 0xBF6ED89Eu,
            0xBF6C835Eu, 0xBF6A09A7u, 0xBF676BD8u, 0xBF64AA59u, 0xBF61C598u, 0xBF5EBE05u, 0xBF5B941Au, 0xBF584853u,
            0xBF54DB31u, 0xBF514D3Du, 0xBF4D9F02u, 0xBF49D112u, 0xBF45E403u, 0xBF41D870u, 0xBF3DAEF9u, 0xBF396842u,
            0xBF3504F3u, 0xBF3085BBu, 0xBF2BEB4Au, 0xBF273656u, 0xBF226799u, 0xBF1D7FD1u, 0xBF187FC0u, 0xBF13682Au,
            0xBF0E39DAu, 0xBF08F59Bu, 0xBF039C3Du, 0xBEFC5D27u, 0xBEF15AEAu, 0xBEE63375u, 0xBEDAE880u, 0xBECF7BCAu,
            0xBEC3EF15u, 0xBEB8442Au, 0xBEAC7CD4u, 0xBEA09AE5u, 0xBE94A031u, 0xBE888E93u, 0xBE78CFCCu, 0xBE605C13u,
            0xBE47C5C2u, 0xBE2F10A2u, 0xBE164083u, 0xBDFAB273u, 0xBDC8BD36u, 0xBD96A905u, 0xBD48FB30u, 0xBCC90AB0u,
        };

        constexpr std::uint32_t kDirectionCosTableBits[256] =
        {
            0x3F800000u, 0x3F7FEC43u, 0x3F7FB10Fu, 0x3F7F4E6Du, 0x3F7EC46Du, 0x3F7E1324u, 0x3F7D3AACu, 0x3F7C3B28u,
            0x3F7B14BEu, 0x3F79C79Du, 0x3F7853F8u, 0x3F76BA07u, 0x3F74FA0Bu, 0x3F731447u, 0x3F710908u, 0x3F6ED89Eu,
            0x3F6C835Eu, 0x3F6A09A7u, 0x3F676BD8u, 0x3F64AA59u, 0x3F61C598u, 0x3F5EBE05u, 0x3F5B941Au, 0x3F584853u,
            0x3F54DB31u, 0x3F514D3Du, 0x3F4D9F02u, 0x3F49D112u, 0x3F45E403u, 0x3F41D870u, 0x3F3DAEF9u, 0x3F396842u,
            0x3F3504F3u, 0x3F3085BBu, 0x3F2BEB4Au, 0x3F273656u, 0x3F226799u, 0x3F1D7FD1u, 0x3F187FC0u, 0x3F13682Au,
            0x3F0E39DAu, 0x3F08F59Bu, 0x3F039C3Du, 0x3EFC5D27u, 0x3EF15AEAu, 0x3EE63375u, 0x3EDAE880u, 0x3ECF7BCAu,
            0x3EC3EF15u, 0x3EB8442Au, 0x3EAC7CD4u, 0x3EA09AE5u, 0x3E94A031u, 0x3E888E93u, 0x3E78CFCCu, 0x3E605C13u,
            0x3E47C5C2u, 0x3E2F10A2u, 0x3E164083u, 0x3DFAB273u, 0x3DC8BD36u, 0x3D96A905u, 0x3D48FB2Fu, 0x3CC90AB0u,
            0x00000000u, 0xBCC90AAFu, 0xBD48FB2Fu, 0xBD96A905u, 0xBDC8BD36u, 0xBDFAB273u, 0xBE164083u, 0xBE2F10A2u,
            0xBE47C5C2u, 0xBE605C13u, 0xBE78CFCCu, 0xBE888E93u, 0xBE94A031u, 0xBEA09AE5u, 0xBEAC7CD4u, 0xBEB8442Au,
            0xBEC3EF15u, 0xBECF7BCAu, 0xBEDAE880u, 0xBEE63375u, 0xBEF15AEAu, 0xBEFC5D27u, 0xBF039C3Du, 0xBF08F59Bu,
            0xBF0E39DAu, 0xBF13682Au, 0xBF187FC0u, 0xBF1D7FD1u, 0xBF226799u, 0xBF273656u, 0xBF2BEB4Au, 0xBF3085BBu,
            0xBF3504F3u, 0xBF396842u, 0xBF3DAEF9u, 0xBF41D870u, 0xBF45E403u, 0xBF49D112u, 0xBF4D9F02u, 0xBF514D3Du,
            0xBF54DB31u, 0xBF584853u, 0xBF5B941Au, 0xBF5EBE05u, 0xBF61C598u, 0xBF64AA59u, 0xBF676BD8u, 0xBF6A09A7u,
            0xBF6C835Eu, 0xBF6ED89Eu, 0xBF710908u, 0xBF731447u, 0xBF74FA0Bu, 0xBF76BA07u, 0xBF7853F8u, 0xBF79C79Du,
            0xBF7B14BEu, 0xBF7C3B28u, 0xBF7D3AACu, 0xBF7E1324u, 0xBF7EC46Du, 0xBF7F4E6Du, 0xBF7FB10Fu, 0xBF7FEC43u,
            0xBF800000u, 0xBF7FEC43u, 0xBF7FB10Fu, 0xBF7F4E6Du, 0xBF7EC46Du, 0xBF7E1324u, 0xBF7D3AACu, 0xBF7C3B28u,
            0xBF7B14BEu, 0xBF79C79Du, 0xBF7853F8u, 0xBF76BA07u, 0xBF74FA0Bu, 0xBF731447u, 0xBF710908u, 0xBF6ED89Eu,
            0xBF6C835Eu, 0xBF6A09A7u, 0xBF676BD8u, 0xBF64AA59u, 0xBF61C598u, 0xBF5EBE05u, 0xBF5B941Au, 0xBF584853u,
            0xBF54DB31u, 0xBF514D3Du, 0xBF4D9F02u, 0xBF49D112u, 0xBF45E403u, 0xBF41D870u, 0xBF3DAEF9u, 0xBF396842u,
            0xBF3504F3u, 0xBF3085BBu, 0xBF2BEB4Au, 0xBF273656u, 0xBF226799u, 0xBF1D7FD1u, 0xBF187FC0u, 0xBF13682Au,
            0xBF0E39DAu, 0xBF08F59Bu, 0xBF039C3Du, 0xBEFC5D27u, 0xBEF15AEAu, 0xBEE63375u, 0xBEDAE880u, 0xBECF7BCAu,
            0xBEC3EF15u, 0xBEB8442Au, 0xBEAC7CD4u, 0xBEA09AE5u, 0xBE94A031u, 0xBE888E93u, 0xBE78CFCCu, 0xBE605C13u,
            0xBE47C5C2u, 0xBE2F10A2u, 0xBE164083u, 0xBDFAB273u, 0xBDC8BD36u, 0xBD96A905u, 0xBD48FB2Fu, 0xBCC90AB0u,
            0x00000000u, 0x3CC90AAFu, 0x3D48FB2Fu, 0x3D96A905u, 0x3DC8BD36u, 0x3DFAB273u, 0x3E164083u, 0x3E2F10A2u,
            0x3E47C5C2u, 0x3E605C13u, 0x3E78CFCCu, 0x3E888E93u, 0x3E94A031u, 0x3EA09AE5u, 0x3EAC7CD4u, 0x3EB8442Au,
            0x3EC3EF15u, 0x3ECF7BCAu, 0x3EDAE880u, 0x3EE63375u, 0x3EF15AEAu, 0x3EFC5D27u, 0x3F039C3Du, 0x3F08F59Bu,
            0x3F0E39DAu, 0x3F13682Au, 0x3F187FC0u, 0x3F1D7FD1u, 0x3F226799u, 0x3F273656u, 0x3F2BEB4Au, 0x3F3085BBu,
            0x3F3504F3u, 0x3F396842u, 0x3F3DAEF9u, 0x3F41D870u, 0x3F45E403u, 0x3F49D112u, 0x3F4D9F02u, 0x3F514D3Du,
            0x3F54DB31u, 0x3F584853u, 0x3F5B941Au, 0x3F5EBE05u, 0x3F61C598u, 0x3F64AA59u, 0x3F676BD8u, 0x3F6A09A7u,
            0x3F6C835Eu, 0x3F6ED89Eu, 0x3F710908u, 0x3F731447u, 0x3F74FA0Bu, 0x3F76BA07u, 0x3F7853F8u, 0x3F79C79Du,
            0x3F7B14BEu, 0x3F7C3B28u, 0x3F7D3AACu, 0x3F7E1324u, 0x3F7EC46Du, 0x3F7F4E6Du, 0x3F7FB10Fu, 0x3F7FEC43u,
        };

        constexpr std::uint32_t kDirectionSinAuxTableBits[256] =
        {
            0x00000000u, 0x3CC90AB0u, 0x3D48FB2Fu, 0x3D96A905u, 0x3DC8BD36u, 0x3DFAB273u, 0x3E164083u, 0x3E2F10A2u,
            0x3E47C5C2u, 0x3E605C13u, 0x3E78CFCCu, 0x3E888E93u, 0x3E94A031u, 0x3EA09AE5u, 0x3EAC7CD4u, 0x3EB8442Au,
            0x3EC3EF15u, 0x3ECF7BCAu, 0x3EDAE880u, 0x3EE63375u, 0x3EF15AEAu, 0x3EFC5D27u, 0x3F039C3Du, 0x3F08F59Bu,
            0x3F0E39DAu, 0x3F13682Au, 0x3F187FC0u, 0x3F1D7FD1u, 0x3F226799u, 0x3F273656u, 0x3F2BEB4Au, 0x3F3085BBu,
            0x3F3504F3u, 0x3F396842u, 0x3F3DAEF9u, 0x3F41D870u, 0x3F45E403u, 0x3F49D112u, 0x3F4D9F02u, 0x3F514D3Du,
            0x3F54DB31u, 0x3F584853u, 0x3F5B941Au, 0x3F5EBE05u, 0x3F61C598u, 0x3F64AA59u, 0x3F676BD8u, 0x3F6A09A7u,
            0x3F6C835Eu, 0x3F6ED89Eu, 0x3F710908u, 0x3F731447u, 0x3F74FA0Bu, 0x3F76BA07u, 0x3F7853F8u, 0x3F79C79Du,
            0x3F7B14BEu, 0x3F7C3B28u, 0x3F7D3AACu, 0x3F7E1324u, 0x3F7EC46Du, 0x3F7F4E6Du, 0x3F7FB10Fu, 0x3F7FEC43u,
            0x3F800000u, 0x3F7FEC43u, 0x3F7FB10Fu, 0x3F7F4E6Du, 0x3F7EC46Du, 0x3F7E1324u, 0x3F7D3AACu, 0x3F7C3B28u,
            0x3F7B14BEu, 0x3F79C79Du, 0x3F7853F8u, 0x3F76BA07u, 0x3F74FA0Bu, 0x3F731447u, 0x3F710908u, 0x3F6ED89Eu,
            0x3F6C835Eu, 0x3F6A09A7u, 0x3F676BD8u, 0x3F64AA59u, 0x3F61C598u, 0x3F5EBE05u, 0x3F5B941Au, 0x3F584853u,
            0x3F54DB31u, 0x3F514D3Du, 0x3F4D9F02u, 0x3F49D112u, 0x3F45E403u, 0x3F41D870u, 0x3F3DAEF9u, 0x3F396842u,
            0x3F3504F3u, 0x3F3085BBu, 0x3F2BEB4Au, 0x3F273656u, 0x3F226799u, 0x3F1D7FD1u, 0x3F187FC0u, 0x3F13682Au,
            0x3F0E39DAu, 0x3F08F59Bu, 0x3F039C3Du, 0x3EFC5D27u, 0x3EF15AEAu, 0x3EE63375u, 0x3EDAE880u, 0x3ECF7BCAu,
            0x3EC3EF15u, 0x3EB8442Au, 0x3EAC7CD4u, 0x3EA09AE5u, 0x3E94A031u, 0x3E888E93u, 0x3E78CFCCu, 0x3E605C13u,
            0x3E47C5C2u, 0x3E2F10A2u, 0x3E164083u, 0x3DFAB273u, 0x3DC8BD36u, 0x3D96A905u, 0x3D48FB2Fu, 0x3CC90AB0u,
            0x00000000u, 0xBCC90AAFu, 0xBD48FB2Fu, 0xBD96A905u, 0xBDC8BD36u, 0xBDFAB273u, 0xBE164083u, 0xBE2F10A2u,
            0xBE47C5C2u, 0xBE605C13u, 0xBE78CFCCu, 0xBE888E93u, 0xBE94A031u, 0xBEA09AE5u, 0xBEAC7CD4u, 0xBEB8442Au,
            0xBEC3EF15u, 0xBECF7BCAu, 0xBEDAE880u, 0xBEE63375u, 0xBEF15AEAu, 0xBEFC5D27u, 0xBF039C3Du, 0xBF08F59Bu,
            0xBF0E39DAu, 0xBF13682Au, 0xBF187FC0u, 0xBF1D7FD1u, 0xBF226799u, 0xBF273656u, 0xBF2BEB4Au, 0xBF3085BBu,
            0xBF3504F3u, 0xBF396842u, 0xBF3DAEF9u, 0xBF41D870u, 0xBF45E403u, 0xBF49D112u, 0xBF4D9F02u, 0xBF514D3Du,
            0xBF54DB31u, 0xBF584853u, 0xBF5B941Au, 0xBF5EBE05u, 0xBF61C598u, 0xBF64AA59u, 0xBF676BD8u, 0xBF6A09A7u,
            0xBF6C835Eu, 0xBF6ED89Eu, 0xBF710908u, 0xBF731447u, 0xBF74FA0Bu, 0xBF76BA07u, 0xBF7853F8u, 0xBF79C79Du,
            0xBF7B14BEu, 0xBF7C3B28u, 0xBF7D3AACu, 0xBF7E1324u, 0xBF7EC46Du, 0xBF7F4E6Du, 0xBF7FB10Fu, 0xBF7FEC43u,
            0xBF800000u, 0xBF7FEC43u, 0xBF7FB10Fu, 0xBF7F4E6Du, 0xBF7EC46Du, 0xBF7E1324u, 0xBF7D3AACu, 0xBF7C3B28u,
            0xBF7B14BEu, 0xBF79C79Du, 0xBF7853F8u, 0xBF76BA07u, 0xBF74FA0Bu, 0xBF731447u, 0xBF710908u, 0xBF6ED89Eu,
            0xBF6C835Eu, 0xBF6A09A7u, 0xBF676BD8u, 0xBF64AA59u, 0xBF61C598u, 0xBF5EBE05u, 0xBF5B941Au, 0xBF584853u,
            0xBF54DB31u, 0xBF514D3Du, 0xBF4D9F02u, 0xBF49D112u, 0xBF45E403u, 0xBF41D870u, 0xBF3DAEF9u, 0xBF396842u,
            0xBF3504F3u, 0xBF3085BBu, 0xBF2BEB4Au, 0xBF273656u, 0xBF226799u, 0xBF1D7FD1u, 0xBF187FC0u, 0xBF13682Au,
            0xBF0E39DAu, 0xBF08F59Bu, 0xBF039C3Du, 0xBEFC5D27u, 0xBEF15AEAu, 0xBEE63375u, 0xBEDAE880u, 0xBECF7BCAu,
            0xBEC3EF15u, 0xBEB8442Au, 0xBEAC7CD4u, 0xBEA09AE5u, 0xBE94A031u, 0xBE888E93u, 0xBE78CFCCu, 0xBE605C13u,
            0xBE47C5C2u, 0xBE2F10A2u, 0xBE164083u, 0xBDFAB273u, 0xBDC8BD36u, 0xBD96A905u, 0xBD48FB30u, 0xBCC90AB0u,
        };

        constexpr std::uint32_t kDirectionCosAuxTableBits[256] =
        {
            0x3F800000u, 0x3F7FEC43u, 0x3F7FB10Fu, 0x3F7F4E6Du, 0x3F7EC46Du, 0x3F7E1324u, 0x3F7D3AACu, 0x3F7C3B28u,
            0x3F7B14BEu, 0x3F79C79Du, 0x3F7853F8u, 0x3F76BA07u, 0x3F74FA0Bu, 0x3F731447u, 0x3F710908u, 0x3F6ED89Eu,
            0x3F6C835Eu, 0x3F6A09A7u, 0x3F676BD8u, 0x3F64AA59u, 0x3F61C598u, 0x3F5EBE05u, 0x3F5B941Au, 0x3F584853u,
            0x3F54DB31u, 0x3F514D3Du, 0x3F4D9F02u, 0x3F49D112u, 0x3F45E403u, 0x3F41D870u, 0x3F3DAEF9u, 0x3F396842u,
            0x3F3504F3u, 0x3F3085BBu, 0x3F2BEB4Au, 0x3F273656u, 0x3F226799u, 0x3F1D7FD1u, 0x3F187FC0u, 0x3F13682Au,
            0x3F0E39DAu, 0x3F08F59Bu, 0x3F039C3Du, 0x3EFC5D27u, 0x3EF15AEAu, 0x3EE63375u, 0x3EDAE880u, 0x3ECF7BCAu,
            0x3EC3EF15u, 0x3EB8442Au, 0x3EAC7CD4u, 0x3EA09AE5u, 0x3E94A031u, 0x3E888E93u, 0x3E78CFCCu, 0x3E605C13u,
            0x3E47C5C2u, 0x3E2F10A2u, 0x3E164083u, 0x3DFAB273u, 0x3DC8BD36u, 0x3D96A905u, 0x3D48FB2Fu, 0x3CC90AB0u,
            0x00000000u, 0xBCC90AAFu, 0xBD48FB2Fu, 0xBD96A905u, 0xBDC8BD36u, 0xBDFAB273u, 0xBE164083u, 0xBE2F10A2u,
            0xBE47C5C2u, 0xBE605C13u, 0xBE78CFCCu, 0xBE888E93u, 0xBE94A031u, 0xBEA09AE5u, 0xBEAC7CD4u, 0xBEB8442Au,
            0xBEC3EF15u, 0xBECF7BCAu, 0xBEDAE880u, 0xBEE63375u, 0xBEF15AEAu, 0xBEFC5D27u, 0xBF039C3Du, 0xBF08F59Bu,
            0xBF0E39DAu, 0xBF13682Au, 0xBF187FC0u, 0xBF1D7FD1u, 0xBF226799u, 0xBF273656u, 0xBF2BEB4Au, 0xBF3085BBu,
            0xBF3504F3u, 0xBF396842u, 0xBF3DAEF9u, 0xBF41D870u, 0xBF45E403u, 0xBF49D112u, 0xBF4D9F02u, 0xBF514D3Du,
            0xBF54DB31u, 0xBF584853u, 0xBF5B941Au, 0xBF5EBE05u, 0xBF61C598u, 0xBF64AA59u, 0xBF676BD8u, 0xBF6A09A7u,
            0xBF6C835Eu, 0xBF6ED89Eu, 0xBF710908u, 0xBF731447u, 0xBF74FA0Bu, 0xBF76BA07u, 0xBF7853F8u, 0xBF79C79Du,
            0xBF7B14BEu, 0xBF7C3B28u, 0xBF7D3AACu, 0xBF7E1324u, 0xBF7EC46Du, 0xBF7F4E6Du, 0xBF7FB10Fu, 0xBF7FEC43u,
            0xBF800000u, 0xBF7FEC43u, 0xBF7FB10Fu, 0xBF7F4E6Du, 0xBF7EC46Du, 0xBF7E1324u, 0xBF7D3AACu, 0xBF7C3B28u,
            0xBF7B14BEu, 0xBF79C79Du, 0xBF7853F8u, 0xBF76BA07u, 0xBF74FA0Bu, 0xBF731447u, 0xBF710908u, 0xBF6ED89Eu,
            0xBF6C835Eu, 0xBF6A09A7u, 0xBF676BD8u, 0xBF64AA59u, 0xBF61C598u, 0xBF5EBE05u, 0xBF5B941Au, 0xBF584853u,
            0xBF54DB31u, 0xBF514D3Du, 0xBF4D9F02u, 0xBF49D112u, 0xBF45E403u, 0xBF41D870u, 0xBF3DAEF9u, 0xBF396842u,
            0xBF3504F3u, 0xBF3085BBu, 0xBF2BEB4Au, 0xBF273656u, 0xBF226799u, 0xBF1D7FD1u, 0xBF187FC0u, 0xBF13682Au,
            0xBF0E39DAu, 0xBF08F59Bu, 0xBF039C3Du, 0xBEFC5D27u, 0xBEF15AEAu, 0xBEE63375u, 0xBEDAE880u, 0xBECF7BCAu,
            0xBEC3EF15u, 0xBEB8442Au, 0xBEAC7CD4u, 0xBEA09AE5u, 0xBE94A031u, 0xBE888E93u, 0xBE78CFCCu, 0xBE605C13u,
            0xBE47C5C2u, 0xBE2F10A2u, 0xBE164083u, 0xBDFAB273u, 0xBDC8BD36u, 0xBD96A905u, 0xBD48FB2Fu, 0xBCC90AB0u,
            0x00000000u, 0x3CC90AAFu, 0x3D48FB2Fu, 0x3D96A905u, 0x3DC8BD36u, 0x3DFAB273u, 0x3E164083u, 0x3E2F10A2u,
            0x3E47C5C2u, 0x3E605C13u, 0x3E78CFCCu, 0x3E888E93u, 0x3E94A031u, 0x3EA09AE5u, 0x3EAC7CD4u, 0x3EB8442Au,
            0x3EC3EF15u, 0x3ECF7BCAu, 0x3EDAE880u, 0x3EE63375u, 0x3EF15AEAu, 0x3EFC5D27u, 0x3F039C3Du, 0x3F08F59Bu,
            0x3F0E39DAu, 0x3F13682Au, 0x3F187FC0u, 0x3F1D7FD1u, 0x3F226799u, 0x3F273656u, 0x3F2BEB4Au, 0x3F3085BBu,
            0x3F3504F3u, 0x3F396842u, 0x3F3DAEF9u, 0x3F41D870u, 0x3F45E403u, 0x3F49D112u, 0x3F4D9F02u, 0x3F514D3Du,
            0x3F54DB31u, 0x3F584853u, 0x3F5B941Au, 0x3F5EBE05u, 0x3F61C598u, 0x3F64AA59u, 0x3F676BD8u, 0x3F6A09A7u,
            0x3F6C835Eu, 0x3F6ED89Eu, 0x3F710908u, 0x3F731447u, 0x3F74FA0Bu, 0x3F76BA07u, 0x3F7853F8u, 0x3F79C79Du,
            0x3F7B14BEu, 0x3F7C3B28u, 0x3F7D3AACu, 0x3F7E1324u, 0x3F7EC46Du, 0x3F7F4E6Du, 0x3F7FB10Fu, 0x3F7FEC43u,
        };

        float spriteFloatFromBits(std::uint32_t bits)
        {
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        constexpr std::array<std::uint32_t, 1024> makeRetailDirectionTrigWindow()
        {
            std::array<std::uint32_t, 1024> out{};
            for (std::size_t i = 0; i < 256; ++i)
            {
                out[i] = kDirectionSinTableBits[i];
                out[256 + i] = kDirectionCosTableBits[i];
                out[512 + i] = kDirectionSinAuxTableBits[i];
                out[768 + i] = kDirectionCosAuxTableBits[i];
            }
            return out;
        }

        std::array<std::uint32_t, 1024> g_retailDirectionTrigWindow =
            makeRetailDirectionTrigWindow();

        float directionSin(int index)
        {
            return spriteFloatFromBits(g_retailDirectionTrigWindow[static_cast<std::size_t>(index & 0xFF)]);
        }

        float directionSinUnchecked(std::uint32_t index)
        {
            return spriteFloatFromBits(g_retailDirectionTrigWindow[index]);
        }

        float directionCos(int index)
        {
            return spriteFloatFromBits(g_retailDirectionTrigWindow[256u + static_cast<std::size_t>(index & 0xFF)]);
        }

        float directionCosUnchecked(std::uint32_t index)
        {
            return spriteFloatFromBits(g_retailDirectionTrigWindow[256u + index]);
        }

        float directionSinAux(int index)
        {
            return spriteFloatFromBits(g_retailDirectionTrigWindow[512u + static_cast<std::size_t>(index & 0xFF)]);
        }

        float directionCosAux(int index)
        {
            return spriteFloatFromBits(g_retailDirectionTrigWindow[768u + static_cast<std::size_t>(index & 0xFF)]);
        }

        std::int32_t spriteImul32Low(std::int32_t a, std::int32_t b) noexcept
        {
            return static_cast<std::int32_t>(
                static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(static_cast<std::uint32_t>(a)) *
                    static_cast<std::uint32_t>(b)));
        }

        std::int32_t spriteAdd32Wrap(std::int32_t a, std::int32_t b) noexcept
        {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
        }

        std::int32_t spriteSub32Wrap(std::int32_t a, std::int32_t b) noexcept
        {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
        }

        int spriteFtolLow32(long double value) noexcept;

        float spriteFildMulF32(std::int32_t value, float scale) noexcept
        {

#if defined(_MSC_VER) && defined(_M_IX86)
            std::int32_t integerValue = value;
            float scaleValue = scale;
            float result = 0.0f;
            __asm
            {
                fild dword ptr [integerValue]
                fmul dword ptr [scaleValue]
                fstp dword ptr [result]
            }
            return result;
#else
            return static_cast<float>(
                static_cast<long double>(value) * static_cast<long double>(scale));
#endif
        }

        float spriteFildToF32(std::int32_t value) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            std::int32_t integerValue = value;
            float result = 0.0f;
            __asm
            {
                fild dword ptr [integerValue]
                fstp dword ptr [result]
            }
            return result;
#else
            return static_cast<float>(value);
#endif
        }

        float spriteFildAddF32(std::int32_t value, float addend) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            std::int32_t integerValue = value;
            float addendValue = addend;
            float result = 0.0f;
            __asm
            {
                fild dword ptr [integerValue]
                fadd dword ptr [addendValue]
                fstp dword ptr [result]
            }
            return result;
#else
            return static_cast<float>(
                static_cast<long double>(value) + static_cast<long double>(addend));
#endif
        }

        float spriteFildSubF32(std::int32_t value, float subtrahend) noexcept
        {

#if defined(_MSC_VER) && defined(_M_IX86)
            std::int32_t integerValue = value;
            float subtrahendValue = subtrahend;
            float result = 0.0f;
            __asm
            {
                fild dword ptr [integerValue]
                fsub dword ptr [subtrahendValue]
                fstp dword ptr [result]
            }
            return result;
#else
            return static_cast<float>(
                static_cast<long double>(value) - static_cast<long double>(subtrahend));
#endif
        }

        float addThenSubtractRounded(float base, float addend, float subtractend) noexcept
        {

#if defined(_MSC_VER) && defined(_M_IX86)
            float a = base;
            float b = addend;
            float c = subtractend;
            float result = 0.0f;
            __asm
            {
                fld dword ptr [a]
                fadd dword ptr [b]
                fsub dword ptr [c]
                fstp dword ptr [result]
            }
            return result;
#else
            return static_cast<float>(
                static_cast<long double>(base) +
                static_cast<long double>(addend) -
                static_cast<long double>(subtractend));
#endif
        }

        float spriteWeightedQuarterF32(float primary, float secondary) noexcept
        {

#if defined(_MSC_VER) && defined(_M_IX86)
            float a = primary;
            float b = secondary;
            const float three = 3.0f;
            const float quarter = 0.25f;
            float result = 0.0f;
            __asm
            {
                fld dword ptr [a]
                fmul dword ptr [three]
                fadd dword ptr [b]
                fmul dword ptr [quarter]
                fstp dword ptr [result]
            }
            return result;
#else
            return static_cast<float>(
                (static_cast<long double>(primary) * 3.0L +
                 static_cast<long double>(secondary)) * 0.25L);
#endif
        }

        int spriteAddF32StoreAndFtolLow32(float value, float addend,
                                          float& storedValue) noexcept
        {

#if defined(_MSC_VER) && defined(_M_IX86)
            float source = value;
            float offsetValue = addend;
            float roundedValue = 0.0f;
            unsigned short oldControl = 0;
            unsigned short truncControl = 0;
            std::int64_t wide = 0;
            __asm
            {
                fld dword ptr [source]
                fadd dword ptr [offsetValue]
                fst dword ptr [roundedValue]
                fstcw oldControl
            }
            truncControl = static_cast<unsigned short>((oldControl & kX87RoundingBitsClearMask) | kX87TruncateRoundingBits);
            __asm
            {
                fldcw truncControl
                fistp qword ptr [wide]
                fldcw oldControl
            }
            storedValue = roundedValue;
            return static_cast<int>(static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(wide)));
#else
            const long double extended =
                static_cast<long double>(value) + static_cast<long double>(addend);
            storedValue = static_cast<float>(extended);
            return spriteFtolLow32(extended);
#endif
        }

        bool spriteFcompC3(float lhs, float rhs) noexcept
        {

#if defined(_MSC_VER) && defined(_M_IX86)
            unsigned short status = 0;
            __asm
            {
                fld dword ptr [lhs]
                fcomp dword ptr [rhs]
                fnstsw ax
                mov word ptr [status], ax
            }
            return (status & 0x4000u) != 0u;
#else
            return lhs == rhs || std::isnan(lhs) || std::isnan(rhs);
#endif
        }

        int spriteFtolLow32(long double value) noexcept
        {

            if (!std::isfinite(value) ||
                value < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                value > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
                return 0;
            return static_cast<int>(static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(static_cast<std::int64_t>(std::trunc(value)))));
        }

        int spriteFsubFtolLow32(float lhs, float rhs) noexcept
        {

#if defined(_MSC_VER) && defined(_M_IX86)
            std::int64_t converted = 0;
            unsigned short oldControl = 0;
            unsigned short truncControl = 0;
            __asm
            {
                fld dword ptr [lhs]
                fsub dword ptr [rhs]
                fstcw word ptr [oldControl]
                fwait
                mov ax, word ptr [oldControl]
                and ax, 0F3FFh
                or ax, 0C00h
                mov word ptr [truncControl], ax
                fldcw word ptr [truncControl]
                fistp qword ptr [converted]
                fldcw word ptr [oldControl]
            }
            return static_cast<int>(static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(converted)));
#else
            return spriteFtolLow32(static_cast<long double>(lhs) -
                                   static_cast<long double>(rhs));
#endif
        }

        int spriteFsubStoreF32FtolLow32(float lhs, float rhs) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            float rounded = 0.0f;
            __asm
            {
                fld dword ptr [lhs]
                fsub dword ptr [rhs]
                fstp dword ptr [rounded]
            }
            return spriteFsubFtolLow32(rounded, 0.0f);
#else
            const float rounded = static_cast<float>(
                static_cast<long double>(lhs) - static_cast<long double>(rhs));
            return spriteFtolLow32(static_cast<long double>(rounded));
#endif
        }

        int spriteFildMulAddFtolLow32(std::uint32_t integerBits,
                                      float multiplier,
                                      float addend) noexcept
        {

#if defined(_MSC_VER) && defined(_M_IX86)
            const std::int32_t signedValue = static_cast<std::int32_t>(integerBits);
            std::int64_t converted = 0;
            unsigned short oldControl = 0;
            unsigned short truncControl = 0;
            __asm
            {
                fild dword ptr [signedValue]
                fmul dword ptr [multiplier]
                fadd dword ptr [addend]
                fstcw word ptr [oldControl]
                fwait
                mov ax, word ptr [oldControl]
                and ax, 0F3FFh
                or ax, 0C00h
                mov word ptr [truncControl], ax
                fldcw word ptr [truncControl]
                fistp qword ptr [converted]
                fldcw word ptr [oldControl]
            }
            return static_cast<int>(static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(converted)));
#else
            const std::int32_t signedValue = static_cast<std::int32_t>(integerBits);
            return spriteFtolLow32(static_cast<long double>(signedValue) *
                                   static_cast<long double>(multiplier) +
                                   static_cast<long double>(addend));
#endif
        }

        int spriteFmulFtolLow32(float value, float multiplier) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            std::int64_t converted = 0;
            unsigned short oldControl = 0;
            unsigned short truncControl = 0;
            __asm
            {
                fld dword ptr [value]
                fmul dword ptr [multiplier]
                fstcw word ptr [oldControl]
                fwait
                mov ax, word ptr [oldControl]
                and ax, 0F3FFh
                or ax, 0C00h
                mov word ptr [truncControl], ax
                fldcw word ptr [truncControl]
                fistp qword ptr [converted]
                fldcw word ptr [oldControl]
            }
            return static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(converted)));
#else
            return spriteFtolLow32(static_cast<long double>(value) *
                                   static_cast<long double>(multiplier));
#endif
        }

        int spriteFdivMulFtolLow32(float numerator, float denominator, float multiplier) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            std::int64_t converted = 0;
            unsigned short oldControl = 0;
            unsigned short truncControl = 0;
            __asm
            {
                fld dword ptr [numerator]
                fdiv dword ptr [denominator]
                fmul dword ptr [multiplier]
                fstcw word ptr [oldControl]
                fwait
                mov ax, word ptr [oldControl]
                and ax, 0F3FFh
                or ax, 0C00h
                mov word ptr [truncControl], ax
                fldcw word ptr [truncControl]
                fistp qword ptr [converted]
                fldcw word ptr [oldControl]
            }
            return static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(converted)));
#else
            return spriteFtolLow32(static_cast<long double>(numerator) /
                                   static_cast<long double>(denominator) *
                                   static_cast<long double>(multiplier));
#endif
        }

        float spriteFildMulStoreFloat(int value, float multiplier) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            float resultValue = 0.0f;
            __asm
            {
                fild dword ptr [value]
                fmul dword ptr [multiplier]
                fstp dword ptr [resultValue]
            }
            return resultValue;
#else
            return static_cast<float>(static_cast<long double>(value) *
                                      static_cast<long double>(multiplier));
#endif
        }

        int pathScaledProgressQuotient(int progress, int delta, int duration) noexcept
        {

#if defined(_MSC_VER) && defined(_M_IX86)
            int result = 0;
            __asm
            {
                mov eax, progress
                imul eax, delta
                shl eax, 8
                cdq
                idiv duration
                mov result, eax
            }
            return result;
#else
            std::uint32_t product =
                static_cast<std::uint32_t>(progress) * static_cast<std::uint32_t>(delta);
            product <<= 8;
            return static_cast<std::int32_t>(product) / duration;
#endif
        }

        float pathInterpolateCoordinate(int quotient, int base) noexcept
        {

#if defined(_MSC_VER) && defined(_M_IX86)
            const float scale = 256.0f;
            float resultValue = 0.0f;
            __asm
            {
                fild dword ptr [quotient]
                fild dword ptr [base]
                fmul dword ptr [scale]
                faddp st(1), st
                fstp dword ptr [resultValue]
            }
            return resultValue;
#else
            return static_cast<float>(
                static_cast<long double>(quotient) +
                static_cast<long double>(base) * 256.0L);
#endif
        }

        float pathAverageCoordinate(float lhs, float rhs) noexcept
        {

#if defined(_MSC_VER) && defined(_M_IX86)
            const float scale = 0.001953125f;
            float resultValue = 0.0f;
            __asm
            {
                fld dword ptr [lhs]
                fadd dword ptr [rhs]
                fmul dword ptr [scale]
                fstp dword ptr [resultValue]
            }
            return resultValue;
#else
            return static_cast<float>(
                (static_cast<long double>(lhs) + static_cast<long double>(rhs)) *
                0.001953125L);
#endif
        }

        int pathDirectionDeltaXToInt(float lhs, float rhs) noexcept
        {

#if defined(_MSC_VER) && defined(_M_IX86)
            const float bias = 128.0f;
            const float multiplier = 0.00390625f;
            std::int64_t converted = 0;
            unsigned short oldControl = 0;
            unsigned short truncControl = 0;
            __asm
            {
                fld dword ptr [lhs]
                fsub dword ptr [rhs]
                fadd dword ptr [bias]
                fmul dword ptr [multiplier]
                fstcw word ptr [oldControl]
                fwait
                mov ax, word ptr [oldControl]
                and ax, 0F3FFh
                or ax, 0C00h
                mov word ptr [truncControl], ax
                fldcw word ptr [truncControl]
                fistp qword ptr [converted]
                fldcw word ptr [oldControl]
            }
            return static_cast<int>(static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(converted)));
#else
            return spriteFtolLow32(
                (static_cast<long double>(lhs) - static_cast<long double>(rhs) + 128.0L) *
                0.00390625L);
#endif
        }

        int pathDirectionDeltaYToInt(float lhs, float rhs) noexcept
        {

#if defined(_MSC_VER) && defined(_M_IX86)
            const float bias = 128.0f;
            const float multiplier1 = 3.0f;
            const float multiplier2 = 0.001953125f;
            std::int64_t converted = 0;
            unsigned short oldControl = 0;
            unsigned short truncControl = 0;
            __asm
            {
                fld dword ptr [lhs]
                fsub dword ptr [rhs]
                fadd dword ptr [bias]
                fmul dword ptr [multiplier1]
                fmul dword ptr [multiplier2]
                fstcw word ptr [oldControl]
                fwait
                mov ax, word ptr [oldControl]
                and ax, 0F3FFh
                or ax, 0C00h
                mov word ptr [truncControl], ax
                fldcw word ptr [truncControl]
                fistp qword ptr [converted]
                fldcw word ptr [oldControl]
            }
            return static_cast<int>(static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(converted)));
#else
            return spriteFtolLow32(
                (static_cast<long double>(lhs) - static_cast<long double>(rhs) + 128.0L) *
                3.0L * 0.001953125L);
#endif
        }

        int animationDelayFromSpeed(float speed) noexcept
        {

            const std::uint32_t scaled = static_cast<std::uint32_t>(
                spriteFmulFtolLow32(speed, 1000.0f));
            const std::uint32_t sign = 0u - (scaled >> 31);
            const std::uint32_t magnitude = (scaled ^ sign) - sign;
            const std::uint32_t plusTen = magnitude + 10u;
            const std::uint32_t sign2 = 0u - (plusTen >> 31);
            const std::uint32_t adjusted = plusTen - sign2;
            const std::uint32_t half = (adjusted >> 1) | (adjusted & 0x80000000u);
            return static_cast<std::int32_t>(0u - half);
        }

        bool spriteFildIntLessEqualOrUnordered(std::int32_t lhs, float rhs) noexcept
        {

#if defined(_MSC_VER) && defined(_M_IX86)
            unsigned short status = 0;
            __asm
            {
                fild dword ptr [lhs]
                fcomp dword ptr [rhs]
                fnstsw ax
                mov word ptr [status], ax
            }
            return (status & 0x4100u) != 0u;
#else
            return std::isnan(rhs) ||
                   static_cast<long double>(lhs) <= static_cast<long double>(rhs);
#endif
        }

        void computeCollisionKinematics(float thisSpeedRaw, float targetSpeedRaw,
                                              float thisWeight, float targetWeight, int mode,
                                              float& sharedSpeedOut, float& relativeSpeedOut) noexcept
        {
            // resolveEngineChainCollision keeps the weighted-speed division live in x87 extended
            // precision for the lower clamp comparison, stores a binary32 copy,
            // and computes relative speed from the two fabs values that remain
            // on the x87 stack. Preserve both observable rounding boundaries.
#if defined(_MSC_VER) && defined(_M_IX86)
            const float lower = 0.001f;
            const float upper = 0.01f;
            unsigned short lowerStatus = 0;
            float* const sharedSpeedPtr = &sharedSpeedOut;
            float* const relativeSpeedPtr = &relativeSpeedOut;
            __asm
            {
                fld dword ptr [thisSpeedRaw]
                fabs
                fld dword ptr [targetSpeedRaw]
                fabs
                fld dword ptr [targetWeight]
                fmul st, st(1)
                fld dword ptr [thisWeight]
                fmul st, st(3)
                faddp st(1), st
                fld dword ptr [targetWeight]
                fadd dword ptr [thisWeight]
                fdivp st(1), st
                mov eax, dword ptr [sharedSpeedPtr]
                fst dword ptr [eax]
                fcomp dword ptr [lower]
                fnstsw ax
                mov word ptr [lowerStatus], ax
                fstp st(0)
                fstp st(0)
            }
            // TEST AH,41h skips the upper clamp for <= lower and unordered.
            if ((lowerStatus & 0x4100u) == 0u &&
                (sharedSpeedOut < upper || std::isnan(sharedSpeedOut)))
                sharedSpeedOut = upper;

            if (mode == 2 || mode == 3)
            {
                __asm
                {
                    fld dword ptr [thisSpeedRaw]
                    fabs
                    fld dword ptr [targetSpeedRaw]
                    fabs
                    fxch st(1)
                    fsub st, st(1)
                    fabs
                    mov eax, dword ptr [relativeSpeedPtr]
                    fstp dword ptr [eax]
                    fstp st(0)
                }
            }
            else
            {
                __asm
                {
                    fld dword ptr [thisSpeedRaw]
                    fabs
                    fld dword ptr [targetSpeedRaw]
                    fabs
                    fadd st, st(1)
                    mov eax, dword ptr [relativeSpeedPtr]
                    fstp dword ptr [eax]
                    fstp st(0)
                }
            }
#else
            const long double thisSpeed = std::fabs(static_cast<long double>(thisSpeedRaw));
            const long double targetSpeed = std::fabs(static_cast<long double>(targetSpeedRaw));
            const long double numerator =
                static_cast<long double>(targetWeight) * targetSpeed +
                static_cast<long double>(thisWeight) * thisSpeed;
            const long double denominator =
                static_cast<long double>(targetWeight) + static_cast<long double>(thisWeight);
            const long double sharedExtended = numerator / denominator;
            sharedSpeedOut = static_cast<float>(sharedExtended);
            if (!std::isnan(sharedExtended) && sharedExtended > 0.001L &&
                (sharedSpeedOut < 0.01f || std::isnan(sharedSpeedOut)))
                sharedSpeedOut = 0.01f;
            const long double relative = (mode == 2 || mode == 3)
                ? std::fabs(thisSpeed - targetSpeed)
                : targetSpeed + thisSpeed;
            relativeSpeedOut = static_cast<float>(relative);
#endif
        }

        bool projectVerticalMotion(int direction, float speed, float zSpeed,
                                            int& projectedX, int& projectedY) noexcept
        {
            const float sinValue = directionSin(direction);
            const float cosValue = directionCos(direction);
#if defined(_MSC_VER) && defined(_M_IX86)
            const float zero = 0.0f;
            const float plusMillion = 1000000.0f;
            const float minusMillion = -1000000.0f;
            unsigned short status = 0;
            __asm
            {
                fld zSpeed
                fcomp zero
                fnstsw ax
                mov status, ax
            }
            // Retail tests C3 only: +0, -0 and unordered/NaN all skip P_VERTDIR.
            if ((status & 0x4000u) != 0)
                return false;

            float roundedX = 0.0f;
            std::int64_t yWide = 0;
            std::int64_t xWide = 0;
            unsigned short oldControl = 0;
            unsigned short truncControl = 0;
            __asm
            {
                // Keep Y live in x87 extended precision exactly as retail.
                fld cosValue
                fmul speed
                fadd zSpeed
                fmul minusMillion

                fld sinValue
                fmul speed
                fmul plusMillion
                fstp roundedX

                fstcw oldControl
            }
            truncControl = static_cast<unsigned short>((oldControl & kX87RoundingBitsClearMask) | kX87TruncateRoundingBits);
            __asm
            {
                fldcw truncControl
                fistp qword ptr [yWide]
                fld roundedX
                fistp qword ptr [xWide]
                fldcw oldControl
            }
            projectedY = static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(yWide)));
            projectedX = static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(xWide)));
            return true;
#else
            if (zSpeed == 0.0f || std::isnan(zSpeed))
                return false;
            const long double yExtended =
                (static_cast<long double>(cosValue) * static_cast<long double>(speed) +
                 static_cast<long double>(zSpeed)) * -1000000.0L;
            const long double xExtended =
                static_cast<long double>(sinValue) * static_cast<long double>(speed) * 1000000.0L;
            const float roundedX = static_cast<float>(xExtended);
            projectedY = spriteFtolLow32(yExtended);
            projectedX = spriteFtolLow32(static_cast<long double>(roundedX));
            return true;
#endif
        }

        double trainEndpointMetric(float x, float y, float nodeX, float nodeY) noexcept
        {
            // Every input is binary32 and the only scale is exactly 0.5, so
            // double retains all finite x87 precision needed by this metric.
            const double dx = std::fabs(static_cast<double>(x) - static_cast<double>(nodeX));
            const double dy = std::fabs(static_cast<double>(y) - static_cast<double>(nodeY));

            if (dx <= dy || std::isnan(dx) || std::isnan(dy))
                return dx * 0.5 + dy;
            return dx + dy * 0.5;
        }

        bool preferFirstTrainEndpoint(float x, float y,
                                    float prevX, float prevY,
                                    float nextX, float nextY) noexcept
        {
            const double firstMetric = trainEndpointMetric(x, y, prevX, prevY);
            const double lastMetric = trainEndpointMetric(x, y, nextX, nextY);

            return firstMetric < lastMetric || std::isnan(firstMetric) || std::isnan(lastMetric);
        }

        bool x87IsZeroOrUnordered(float value) noexcept
        {

            return value == 0.0f || std::isnan(value);
        }

        bool x87EqualOrUnordered(float value, float reference) noexcept
        {

            return value == reference || std::isnan(value) || std::isnan(reference);
        }

        bool x87LessOrUnordered(float lhs, float rhs) noexcept
        {

            return lhs < rhs || std::isnan(lhs) || std::isnan(rhs);
        }

        bool x87LessEqualOrUnordered(float lhs, float rhs) noexcept
        {

            return lhs <= rhs || std::isnan(lhs) || std::isnan(rhs);
        }

        bool x87OrderedLess(float lhs, float rhs) noexcept
        {
            return lhs < rhs && !std::isnan(lhs) && !std::isnan(rhs);
        }

        bool x87OrderedGreater(float lhs, float rhs) noexcept
        {
            return lhs > rhs && !std::isnan(lhs) && !std::isnan(rhs);
        }

        bool x87AbsDiffGreaterOrdered(float lhs, float rhs, float limit) noexcept
        {

#if defined(_MSC_VER) && defined(_M_IX86)
            unsigned short status = 0;
            __asm
            {
                fld dword ptr [lhs]
                fsub dword ptr [rhs]
                fabs
                fcomp dword ptr [limit]
                fnstsw ax
                mov word ptr [status], ax
            }
            return (status & 0x4100u) == 0u;
#else
            const long double diff = std::fabs(
                static_cast<long double>(lhs) - static_cast<long double>(rhs));
            const long double bound = static_cast<long double>(limit);
            return !std::isnan(diff) && !std::isnan(bound) && diff > bound;
#endif
        }

        bool metricWithinFromRoundedDeltas(float deltaX, float deltaY, float radius) noexcept
        {
            // Initial linked-child route in evaluateEngineTargetRangeState stores X/Y deltas to
            // binary32 stack locals, calls approximatePlanarDistance, then compares the live
            // x87 metric with (radius-10) using TEST AH,41h. Recreate that
            // exact numeric route without a premature metric spill.
#if defined(_MSC_VER) && defined(_M_IX86)
            static const float half = 0.5f;
            static const float ten = 10.0f;
            unsigned short status = 0;
            __asm
            {
                fld dword ptr [deltaX]
                fabs
                fld dword ptr [deltaY]
                fabs
                fld st(1)
                fcomp st(1)
                fnstsw ax
                test ah, 41h
                jnz metric_second_axis_larger
                fmul dword ptr [half]
                faddp st(1), st
                jmp metric_ready
metric_second_axis_larger:
                fxch st(1)
                fmul dword ptr [half]
                fadd st, st(1)
                fxch st(1)
                fstp st
metric_ready:
                fld dword ptr [radius]
                fsub dword ptr [ten]
                fxch st(1)
                fcompp
                fnstsw ax
                mov word ptr [status], ax
            }
            return (status & 0x4100u) != 0u;
#else
            const long double ax = std::fabs(static_cast<long double>(deltaX));
            const long double ay = std::fabs(static_cast<long double>(deltaY));
            const long double metric =
                (ax <= ay || std::isnan(ax) || std::isnan(ay))
                    ? ax * 0.5L + ay
                    : ax + ay * 0.5L;
            const long double limit = static_cast<long double>(radius) - 10.0L;
            return metric <= limit || std::isnan(metric) || std::isnan(limit);
#endif
        }

        bool metricWithinPositions(float ownerX, float ownerY,
                                             float targetX, float targetY,
                                             float radius) noexcept
        {

#if defined(_MSC_VER) && defined(_M_IX86)
            static const float half = 0.5f;
            static const float ten = 10.0f;
            unsigned short status = 0;
            __asm
            {
                fld dword ptr [ownerX]
                fsub dword ptr [targetX]
                fabs
                fld dword ptr [ownerY]
                fsub dword ptr [targetY]
                fabs
                fld st(1)
                fcomp st(1)
                fnstsw ax
                test ah, 41h
                jnz position_second_axis_larger
                fmul dword ptr [half]
                faddp st(1), st
                jmp position_metric_ready
position_second_axis_larger:
                fxch st(1)
                fmul dword ptr [half]
                fadd st, st(1)
                fxch st(1)
                fstp st
position_metric_ready:
                fld dword ptr [radius]
                fsub dword ptr [ten]
                fxch st(1)
                fcompp
                fnstsw ax
                mov word ptr [status], ax
            }
            return (status & 0x4100u) != 0u;
#else
            const long double ax = std::fabs(
                static_cast<long double>(ownerX) - static_cast<long double>(targetX));
            const long double ay = std::fabs(
                static_cast<long double>(ownerY) - static_cast<long double>(targetY));
            const long double metric =
                (ax <= ay || std::isnan(ax) || std::isnan(ay))
                    ? ax * 0.5L + ay
                    : ax + ay * 0.5L;
            const long double limit = static_cast<long double>(radius) - 10.0L;
            return metric <= limit || std::isnan(metric) || std::isnan(limit);
#endif
        }

        bool spriteBitsEqual(float value, std::uint32_t bits) noexcept
        {
            std::uint32_t raw = 0;
            std::memcpy(&raw, &value, sizeof(raw));
            return raw == bits;
        }

        bool advanceAccelerationStep(std::int32_t deltaMs, float factor,
                                    float maxSpeed, float& speed) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            unsigned short status = 0;
            float* const speedPtr = &speed;
            __asm
            {
                fild dword ptr [deltaMs]
                fmul dword ptr [factor]
                mov eax, dword ptr [speedPtr]
                fadd dword ptr [eax]
                fst dword ptr [eax]
                fld dword ptr [maxSpeed]
                fxch st(1)
                fcompp
                fnstsw ax
                mov word ptr [status], ax
            }
            // C0 set means newSpeed < max OR unordered: retail keeps stored speed.
            return (status & 0x0100u) == 0u;
#else
            const long double extended =
                static_cast<long double>(deltaMs) * static_cast<long double>(factor) +
                static_cast<long double>(speed);
            speed = static_cast<float>(extended);
            return !(extended < static_cast<long double>(maxSpeed) ||
                     std::isnan(static_cast<double>(extended)) || std::isnan(maxSpeed));
#endif
        }

        bool advanceDecelerationStep(std::int32_t deltaMs, float factor, float& speed) noexcept
        {
            // Retail stores m32 but tests the still-live x87 result against zero.
            // As above, explicitly dereference the MSVC reference carrier.
#if defined(_MSC_VER) && defined(_M_IX86)
            const float zero = 0.0f;
            unsigned short status = 0;
            float* const speedPtr = &speed;
            __asm
            {
                fild dword ptr [deltaMs]
                fmul dword ptr [factor]
                mov eax, dword ptr [speedPtr]
                fsubr dword ptr [eax]
                fst dword ptr [eax]
                fcomp dword ptr [zero]
                fnstsw ax
                mov word ptr [status], ax
            }
            return (status & 0x0100u) != 0u; // less-than OR unordered
#else
            const long double extended =
                static_cast<long double>(speed) -
                static_cast<long double>(deltaMs) * static_cast<long double>(factor);
            speed = static_cast<float>(extended);
            return extended < 0.0L || std::isnan(static_cast<double>(extended));
#endif
        }

        void advancePlanarPosition(std::int32_t deltaMs, float speed,
                                float sinValue, float cosValue,
                                float& x, float& y) noexcept
        {
            // One live x87 step feeds both X and Y in retail.  x/y are C++
            // references here, so their physical x86 carriers must be loaded
            // before using the referenced m32 operands.
#if defined(_MSC_VER) && defined(_M_IX86)
            float* const xPtr = &x;
            float* const yPtr = &y;
            __asm
            {
                fild dword ptr [deltaMs]
                fmul dword ptr [speed]
                fld st(0)
                fmul dword ptr [sinValue]
                mov eax, dword ptr [xPtr]
                fadd dword ptr [eax]
                fstp dword ptr [eax]
                fmul dword ptr [cosValue]
                mov edx, dword ptr [yPtr]
                fsubr dword ptr [edx]
                fstp dword ptr [edx]
            }
#else
            const long double step =
                static_cast<long double>(deltaMs) * static_cast<long double>(speed);
            x = static_cast<float>(static_cast<long double>(x) +
                                   step * static_cast<long double>(sinValue));
            y = static_cast<float>(static_cast<long double>(y) -
                                   step * static_cast<long double>(cosValue));
#endif
        }

        void applyGravityStep(std::int32_t deltaMs, float gravity, float& zSpeed) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            float* const zSpeedPtr = &zSpeed;
            __asm
            {
                fild dword ptr [deltaMs]
                fmul dword ptr [gravity]
                mov eax, dword ptr [zSpeedPtr]
                fsubr dword ptr [eax]
                fstp dword ptr [eax]
            }
#else
            zSpeed = static_cast<float>(static_cast<long double>(zSpeed) -
                static_cast<long double>(deltaMs) * static_cast<long double>(gravity));
#endif
        }

        void advanceVerticalPosition(std::int32_t deltaMs, float zSpeed, float& z) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            float* const zPtr = &z;
            __asm
            {
                fild dword ptr [deltaMs]
                fmul dword ptr [zSpeed]
                mov eax, dword ptr [zPtr]
                fadd dword ptr [eax]
                fstp dword ptr [eax]
            }
#else
            z = static_cast<float>(static_cast<long double>(z) +
                static_cast<long double>(deltaMs) * static_cast<long double>(zSpeed));
#endif
        }

        float applicationWorldFloatAt(std::size_t offset) noexcept
        {
#if defined(_WIN32)
            const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(
                core::ApplicationPhysicalOwner());
            return *reinterpret_cast<const float*>(base + offset);
#else
            return offset == 0x28u
                ? core::ApplicationMapWidth()
                : core::ApplicationMapHeight();
#endif
        }

        int computeRegionTileCount(float width, float height,
                                             float childSizeX, float childSizeY) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            float w = width;
            float h = height;
            float sx = childSizeX;
            float sy = childSizeY;
            unsigned short oldControl = 0;
            unsigned short truncControl = 0;
            std::int64_t wide = 0;
            __asm
            {
                fld dword ptr [h]
                fmul dword ptr [w]
                fdiv dword ptr [sx]
                fdiv dword ptr [sy]
                fstcw oldControl
            }
            truncControl = static_cast<unsigned short>((oldControl & kX87RoundingBitsClearMask) | kX87TruncateRoundingBits);
            __asm
            {
                fldcw truncControl
                fistp qword ptr [wide]
                fldcw oldControl
            }
            return static_cast<int>(static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(wide)));
#else
            const long double value =
                static_cast<long double>(width) * static_cast<long double>(height) /
                static_cast<long double>(childSizeX) / static_cast<long double>(childSizeY);
            return spriteFtolLow32(value);
#endif
        }

        std::uint32_t computeChildAnimationCadence(int direction,
                                               float speed,
                                               float zSpeed,
                                               float childMaxZ,
                                               bool subtractGraphMotion,
                                               int graphDirection,
                                               float graphSpeed,
                                               float childSizeX,
                                               float childSizeY) noexcept
        {
            const std::uint32_t rawDirection = static_cast<std::uint32_t>(direction);
            const std::uint32_t rawGraphDirection = static_cast<std::uint32_t>(graphDirection);
            const float dirSin = spriteFloatFromBits(
                g_retailDirectionTrigWindow[512u + rawDirection]);
            const float dirCos = spriteFloatFromBits(
                g_retailDirectionTrigWindow[768u + rawDirection]);
            const float graphSin = spriteFloatFromBits(
                g_retailDirectionTrigWindow[512u + rawGraphDirection]);
            const float graphCos = spriteFloatFromBits(
                g_retailDirectionTrigWindow[768u + rawGraphDirection]);
#if defined(_MSC_VER) && defined(_M_IX86)
            float localSpeed = speed;
            float localZSpeed = zSpeed;
            float localChildMaxZ = childMaxZ;
            float localGraphSpeed = graphSpeed;
            float localChildSizeX = childSizeX;
            float localChildSizeY = childSizeY;
            float projectedY = 0.0f;
            float xTime = 0.0f;
            const float zero = 0.0f;
            const float fallback = 30000.0f;
            int subtractMotion = subtractGraphMotion ? 1 : 0;
            unsigned short oldControl = 0;
            unsigned short truncControl = 0;
            std::int64_t wide = 0;
            __asm
            {
                // ST0 = projected X (kept extended after projectedY store).
                fld dword ptr [dirSin]
                fmul dword ptr [localSpeed]
                fld dword ptr [localZSpeed]
                fsub dword ptr [localChildMaxZ]
                fld dword ptr [dirCos]
                fmul dword ptr [localSpeed]
                faddp st(1), st
                fstp dword ptr [projectedY]

                cmp dword ptr [subtractMotion], 0
                je cadence43A220_after_graph
                fld dword ptr [localGraphSpeed]
                fmul dword ptr [graphSin]
                fsubp st(1), st
                fld dword ptr [localGraphSpeed]
                fmul dword ptr [graphCos]
                fsubr dword ptr [projectedY]
                fstp dword ptr [projectedY]

            cadence43A220_after_graph:
                // X-time: equal/unordered to zero takes the 30000.0 path.
                fcom dword ptr [zero]
                fnstsw ax
                test ah, 40h
                jnz cadence43A220_x_zero
                fcom dword ptr [zero]
                fnstsw ax
                test ah, 1
                jz cadence43A220_x_abs_ready
                fchs
            cadence43A220_x_abs_ready:
                fld dword ptr [localChildSizeX]
                fdiv st, st(1)
                fstp dword ptr [xTime]
                fstp st
                jmp cadence43A220_y
            cadence43A220_x_zero:
                fstp st
                fld dword ptr [fallback]
                fstp dword ptr [xTime]

            cadence43A220_y:
                fld dword ptr [projectedY]
                fcomp dword ptr [zero]
                fnstsw ax
                test ah, 40h
                jnz cadence43A220_y_zero
                fld dword ptr [projectedY]
                fcomp dword ptr [zero]
                fnstsw ax
                fld dword ptr [projectedY]
                test ah, 1
                jz cadence43A220_y_abs_ready
                fchs
            cadence43A220_y_abs_ready:
                fdivr dword ptr [localChildSizeY]
                jmp cadence43A220_select
            cadence43A220_y_zero:
                fld dword ptr [fallback]

            cadence43A220_select:
                // ST0=yTime. Compare rounded xTime to live yTime. C0 selects
                // xTime for less-than or unordered exactly like TEST AH,1.
                fld dword ptr [xTime]
                fcomp st(1)
                fnstsw ax
                test ah, 1
                jz cadence43A220_convert
                fstp st
                fld dword ptr [xTime]

            cadence43A220_convert:
                fstcw oldControl
                mov ax, oldControl
                and ax, 0F3FFh
                or ax, 0C00h
                mov truncControl, ax
                fldcw truncControl
                fistp qword ptr [wide]
                fldcw oldControl
            }
            return static_cast<std::uint32_t>(static_cast<std::uint64_t>(wide));
#else
            long double projectedX =
                static_cast<long double>(dirSin) * static_cast<long double>(speed);
            float projectedY = static_cast<float>(
                static_cast<long double>(zSpeed) - static_cast<long double>(childMaxZ) +
                static_cast<long double>(dirCos) * static_cast<long double>(speed));
            if (subtractGraphMotion)
            {
                projectedX -= static_cast<long double>(graphSpeed) * static_cast<long double>(graphSin);
                projectedY = static_cast<float>(
                    static_cast<long double>(projectedY) -
                    static_cast<long double>(graphSpeed) * static_cast<long double>(graphCos));
            }
            float xTime = 30000.0f;
            if (projectedX != 0.0L && !std::isnan(projectedX))
                xTime = static_cast<float>(static_cast<long double>(childSizeX) / std::fabs(projectedX));

            long double yTime = 30000.0L;
            if (projectedY != 0.0f && !std::isnan(projectedY))
                yTime = static_cast<long double>(childSizeY) /
                    std::fabs(static_cast<long double>(projectedY));
            const long double selected =
                (static_cast<long double>(xTime) < yTime ||
                 std::isnan(static_cast<long double>(xTime)) || std::isnan(yTime))
                    ? static_cast<long double>(xTime)
                    : yTime;
            return static_cast<std::uint32_t>(spriteFtolLow32(selected));
#endif
        }

        int computeDirectionToTarget(float targetX, float targetY,
                                          float sourceX, float sourceY) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            float tx = targetX;
            float ty = targetY;
            float sx = sourceX;
            float sy = sourceY;
            float roundedX = 0.0f;
            unsigned short oldControl = 0;
            unsigned short truncControl = 0;
            std::int64_t yWide = 0;
            std::int64_t xWide = 0;
            __asm
            {
                fld dword ptr [ty]
                fsub dword ptr [sy]
                fld dword ptr [tx]
                fsub dword ptr [sx]
                fstp dword ptr [roundedX]
                fstcw oldControl
            }
            truncControl = static_cast<unsigned short>((oldControl & kX87RoundingBitsClearMask) | kX87TruncateRoundingBits);
            __asm
            {
                fldcw truncControl
                fistp qword ptr [yWide]
                fld dword ptr [roundedX]
                fistp qword ptr [xWide]
                fldcw oldControl
            }
            const int x = static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(xWide)));
            const int y = static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(yWide)));
            int projectedLength = 0;
            return AngleFromXY(x, y, &projectedLength);
#else
            const long double yExt =
                static_cast<long double>(targetY) - static_cast<long double>(sourceY);
            const float xRounded = static_cast<float>(
                static_cast<long double>(targetX) - static_cast<long double>(sourceX));
            int projectedLength = 0;
            return AngleFromXY(spriteFtolLow32(static_cast<long double>(xRounded)),
                               spriteFtolLow32(yExt), &projectedLength);
#endif
        }

        float targetDistanceMetric(float targetX, float targetY,
                                 float sourceX, float sourceY) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            float tx = targetX;
            float ty = targetY;
            float sx = sourceX;
            float sy = sourceY;
            const float half = 0.5f;
            float result = 0.0f;
            __asm
            {
                fld dword ptr [tx]
                fsub dword ptr [sx]
                fabs
                fld dword ptr [ty]
                fsub dword ptr [sy]
                fabs
                fld st(1)
                fcomp st(1)
                fnstsw ax
                test ah, 41h
                jz metric43F410_ordered_greater
                fxch st(1)
            metric43F410_ordered_greater:
                fmul dword ptr [half]
                fadd st, st(1)
                fstp dword ptr [result]
                fstp st
            }
            return result;
#else
            const long double dx = std::fabs(
                static_cast<long double>(targetX) - static_cast<long double>(sourceX));
            const long double dy = std::fabs(
                static_cast<long double>(targetY) - static_cast<long double>(sourceY));
            const long double metric =
                (dx <= dy || std::isnan(dx) || std::isnan(dy))
                    ? dx * 0.5L + dy
                    : dx + dy * 0.5L;
            return static_cast<float>(metric);
#endif
        }

        std::int32_t spriteNeg32Wrap(std::int32_t value) noexcept
        {
            return static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(value));
        }

        std::int32_t spriteAbs32Wrap(std::int32_t value) noexcept
        {
            const std::int32_t sign = value < 0 ? -1 : 0;
            return static_cast<std::int32_t>(
                (static_cast<std::uint32_t>(value) ^ static_cast<std::uint32_t>(sign)) -
                static_cast<std::uint32_t>(sign));
        }

        bool x87SumGreaterThanAbsDiffOrdered(float boundA, float boundB,
                                             float lhs, float rhs) noexcept
        {
            // Retail area-damage AABB gates keep both the absolute coordinate
            // delta and bound sum live in x87 until FCOMPP; TEST AH,41h rejects
            // <= and unordered. Do not round either intermediate to binary32.
#if defined(_MSC_VER) && defined(_M_IX86)
            float a = boundA;
            float b = boundB;
            float x = lhs;
            float y = rhs;
            unsigned short status = 0;
            __asm
            {
                fld dword ptr [x]
                fsub dword ptr [y]
                fabs
                fld dword ptr [a]
                fadd dword ptr [b]
                fcompp
                fnstsw ax
                mov status, ax
            }
            return (status & 0x4100u) == 0u;
#else
            const long double diff = std::fabs(
                static_cast<long double>(lhs) - static_cast<long double>(rhs));
            const long double bound =
                static_cast<long double>(boundA) + static_cast<long double>(boundB);
            return !std::isnan(diff) && !std::isnan(bound) && bound > diff;
#endif
        }

        bool x87SumLessOrUnordered(float lhsA, float lhsB, float rhs) noexcept
        {

#if defined(_MSC_VER) && defined(_M_IX86)
            float a = lhsA;
            float b = lhsB;
            float r = rhs;
            unsigned short status = 0;
            __asm
            {
                fld dword ptr [a]
                fadd dword ptr [b]
                fcomp dword ptr [r]
                fnstsw ax
                mov status, ax
                fstp st
            }
            return (status & 0x0100u) != 0u;
#else
            const long double sum =
                static_cast<long double>(lhsA) + static_cast<long double>(lhsB);
            const long double right = static_cast<long double>(rhs);
            return sum < right || std::isnan(sum) || std::isnan(right);
#endif
        }

        bool shouldSuppressFlagmanCommand(std::int32_t x, std::int32_t y,
                                                std::int32_t range,
                                                float controlledX,
                                                float controlledY) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            std::int32_t ix = x;
            std::int32_t iy = y;
            std::int32_t irange = range;
            float px = controlledX;
            float py = controlledY;
            const float half = 0.5f;
            unsigned short status = 0;
            __asm
            {
                fild dword ptr [ix]
                fsub dword ptr [px]
                fabs
                fild dword ptr [iy]
                fsub dword ptr [py]
                fabs
                fld st(1)
                fcomp st(1)
                fnstsw ax
                mov status, ax
            }
            if ((status & 0x4100u) != 0u)
            {
                __asm
                {
                    fxch st(1)
                    fmul dword ptr [half]
                    fadd st, st(1)
                    fxch st(1)
                    fstp st
                }
            }
            else
            {
                __asm
                {
                    fmul dword ptr [half]
                    faddp st(1), st
                }
            }
            __asm
            {
                fild dword ptr [irange]
                fxch st(1)
                fcompp
                fnstsw ax
                mov status, ax
            }
            return (status & 0x0100u) != 0u;
#else
            const long double dx = std::fabs(
                static_cast<long double>(x) - static_cast<long double>(controlledX));
            const long double dy = std::fabs(
                static_cast<long double>(y) - static_cast<long double>(controlledY));
            const long double metric =
                (dx <= dy || std::isnan(dx) || std::isnan(dy))
                    ? dx * 0.5L + dy
                    : dx + dy * 0.5L;
            const long double threshold = static_cast<long double>(range);
            return metric < threshold || std::isnan(metric) || std::isnan(threshold);
#endif
        }

        bool computeFalloffDamage(float sourceX, float sourceY,
                                       float candidateX, float candidateY,
                                       float deathRange, std::int32_t damageRaw,
                                       int& damageOut) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            float sx = sourceX;
            float sy = sourceY;
            float candidateXValue = candidateX;
            float candidateYValue = candidateY;
            float range = deathRange;
            const float half = 0.5f;
            std::int32_t rawDamage = damageRaw;
            unsigned short status = 0;
            unsigned short oldControl = 0;
            unsigned short truncControl = 0;
            std::int64_t wide = 0;
            __asm
            {
                fld dword ptr [candidateXValue]
                fsub dword ptr [sx]
                fabs
                fld dword ptr [candidateYValue]
                fsub dword ptr [sy]
                fabs
                fld st(1)
                fcomp st(1)
                fnstsw ax
                mov status, ax
            }
            if ((status & 0x4100u) != 0u)
            {
                __asm
                {
                    fxch st(1)
                    fmul dword ptr [half]
                    fadd st, st(1)
                    fxch st(1)
                    fstp st
                }
            }
            else
            {
                __asm
                {
                    fmul dword ptr [half]
                    faddp st(1), st
                }
            }
            __asm
            {
                fcom dword ptr [range]
                fnstsw ax
                mov status, ax
            }
            if ((status & 0x4100u) == 0u)
            {
                __asm { fstp st }
                return false;
            }

            __asm
            {
                fild dword ptr [rawDamage]
                fld st
                fmul st, st(2)
                fdiv dword ptr [range]
                fsubr st, st(1)
                fstcw oldControl
            }
            truncControl = static_cast<unsigned short>((oldControl & kX87RoundingBitsClearMask) | kX87TruncateRoundingBits);
            __asm
            {
                fldcw truncControl
                fistp qword ptr [wide]
                fldcw oldControl
                fstp st
                fstp st
            }
            damageOut = static_cast<int>(static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(wide)));
            return true;
#else
            const long double dx = std::fabs(
                static_cast<long double>(candidateX) - static_cast<long double>(sourceX));
            const long double dy = std::fabs(
                static_cast<long double>(candidateY) - static_cast<long double>(sourceY));
            const long double metric =
                (dx <= dy || std::isnan(dx) || std::isnan(dy))
                    ? dx * 0.5L + dy
                    : dx + dy * 0.5L;
            const long double range = static_cast<long double>(deathRange);
            if (!(metric <= range || std::isnan(metric) || std::isnan(range)))
                return false;
            const long double scaledDamage =
                static_cast<long double>(damageRaw) -
                static_cast<long double>(damageRaw) * metric / range;
            damageOut = spriteFtolLow32(scaledDamage);
            return true;
#endif
        }

        int quantizeDirectionForVid(int direction, int directionBase, int directionCount) noexcept
        {
            if (directionCount == 0)
                return direction & 0xFF;
#if defined(_MSC_VER) && defined(_M_IX86)
            int result = 0;
            __asm
            {
                mov eax, directionBase
                add eax, direction
                and eax, 0FFh
                imul eax, directionCount
                shr eax, 8
                shl eax, 8
                cdq
                idiv directionCount
                mov result, eax
            }
            return result;
#else
            const std::uint32_t angle = static_cast<std::uint32_t>(directionBase + direction) & 0xFFu;
            const std::uint32_t lowProduct = static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(angle) * static_cast<std::uint32_t>(directionCount));
            const std::uint32_t numeratorBits = (lowProduct >> 8u) << 8u;
            const std::int32_t numerator = static_cast<std::int32_t>(numeratorBits);
            return numerator / directionCount;
#endif
        }

        void appendU32LE(std::vector<BYTE>& out, std::uint32_t v)
        {
            out.push_back(static_cast<BYTE>(v & 0xFF));
            out.push_back(static_cast<BYTE>((v >> 8) & 0xFF));
            out.push_back(static_cast<BYTE>((v >> 16) & 0xFF));
            out.push_back(static_cast<BYTE>((v >> 24) & 0xFF));
        }

        std::vector<BYTE> wordsToBytes(const std::vector<std::uint32_t>& words)
        {
            std::vector<BYTE> out;
            out.reserve(words.size() * 4);
            for (std::uint32_t v : words)
                appendU32LE(out, v);
            return out;
        }

        constexpr DWORD kSub44B310ResumeFlag80 = 0x80u;
        constexpr DWORD kSub44B310DirectionFlag01 = 0x01u;

    }

    void SPRITE::initializeRetailStartupTrigTables() noexcept
    {
        const float kScale4096 = 4096.0f;
        const float kScaleRadians = 0.00017262212f;
        for (std::size_t i = 0; i < 256u; ++i)
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            float* const base = reinterpret_cast<float*>(g_retailDirectionTrigWindow.data());
            const float* const sourceSin = base + i;
            float* const targetSin = base + 512u + i;
            const float* const sourceCosWindow = base + 256u + i;
            float* const targetCosWindow = base + 768u + i;
            __asm
            {
                mov eax, sourceSin
                fld dword ptr [eax]
                fmul dword ptr [kScale4096]
                fmul dword ptr [kScaleRadians]
                mov eax, targetSin
                fstp dword ptr [eax]

                mov eax, sourceCosWindow
                fld dword ptr [eax]
                fmul dword ptr [kScale4096]
                fmul dword ptr [kScaleRadians]
                mov eax, targetCosWindow
                fstp dword ptr [eax]
            }
#else
            const float sourceSin = spriteFloatFromBits(g_retailDirectionTrigWindow[i]);
            const float sourceCosWindow = spriteFloatFromBits(g_retailDirectionTrigWindow[256u + i]);
            const float derivedSin = static_cast<float>(
                static_cast<long double>(sourceSin) *
                static_cast<long double>(kScale4096) *
                static_cast<long double>(kScaleRadians));
            const float derivedCosWindow = static_cast<float>(
                static_cast<long double>(sourceCosWindow) *
                static_cast<long double>(kScale4096) *
                static_cast<long double>(kScaleRadians));
            std::memcpy(&g_retailDirectionTrigWindow[512u + i], &derivedSin, sizeof(derivedSin));
            std::memcpy(&g_retailDirectionTrigWindow[768u + i], &derivedCosWindow, sizeof(derivedCosWindow));
#endif
        }
    }

    float SPRITE::rawDirectionSin(int index) noexcept
    {
        return directionSin(index);
    }

    float SPRITE::rawDirectionSinUnchecked(DWORD index) noexcept
    {
        return directionSinUnchecked(index);
    }

    float SPRITE::rawDirectionCos(int index) noexcept
    {
        return directionCos(index);
    }

    float SPRITE::rawDirectionCosUnchecked(DWORD index) noexcept
    {
        return directionCosUnchecked(index);
    }

    float SPRITE::rawDirectionSinAux(int index) noexcept
    {
        return directionSinAux(index);
    }

    float SPRITE::rawDirectionCosAux(int index) noexcept
    {
        return directionCosAux(index);
    }

    void* destroyCommandRecordListOwner(void* rawListOwner, unsigned char deleteSelfFlag) noexcept
    {
        auto* const raw = reinterpret_cast<BYTE*>(rawListOwner);
        const std::uint32_t vtable = currentCommandRecordListVtable();
        std::memcpy(raw + 0x00, &vtable, sizeof(vtable));

        std::uint32_t dataToken = 0u;
        std::memcpy(&dataToken, raw + 0x0C, sizeof(dataToken));
#if UINTPTR_MAX == 0xFFFFFFFFu
        if (dataToken != 0u)
            ::operator delete(reinterpret_cast<void*>(static_cast<std::uintptr_t>(dataToken)));
#endif
        const std::uint32_t zero = 0u;
        std::memcpy(raw + 0x0C, &zero, sizeof(zero));
        std::memcpy(raw + 0x04, &zero, sizeof(zero));

        if ((deleteSelfFlag & 1u) != 0u)
            ::operator delete(rawListOwner);
        return rawListOwner;
    }

    namespace
    {
        struct CommandRecordListVtableOwner
        {
            virtual void* deletingDestructor(unsigned char flags) noexcept
            {
                return destroyCommandRecordListOwner(this, flags);
            }
        };
    }

    std::uint32_t currentCommandRecordListVtable() noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        static CommandRecordListVtableOwner owner;
        return static_cast<std::uint32_t>(*reinterpret_cast<const std::uintptr_t*>(&owner));
#else
        return 0u;
#endif
    }

    void* destroyCommandWordListOwner(void* rawListOwner, unsigned char deleteSelfFlag) noexcept
    {
        auto* const raw = reinterpret_cast<BYTE*>(rawListOwner);
        const std::uint32_t vtable = currentCommandWordListVtable();
        std::memcpy(raw + 0x00, &vtable, sizeof(vtable));

        std::uint32_t dataToken = 0;
        std::memcpy(&dataToken, raw + 0x0C, sizeof(dataToken));
#if UINTPTR_MAX == 0xFFFFFFFFu
        if (dataToken != 0u)
            ::operator delete(reinterpret_cast<void*>(static_cast<std::uintptr_t>(dataToken)));
#endif
        const std::uint32_t zero = 0u;
        std::memcpy(raw + 0x0C, &zero, sizeof(zero));
        std::memcpy(raw + 0x04, &zero, sizeof(zero));

        if ((deleteSelfFlag & 1u) != 0u)
            ::operator delete(rawListOwner);
        return rawListOwner;
    }

    namespace
    {
        struct CommandWordListVtableOwner
        {
            virtual void* deletingDestructor(unsigned char flags) noexcept
            {
                return destroyCommandWordListOwner(this, flags);
            }
        };
    }

    std::uint32_t currentCommandWordListVtable() noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        static CommandWordListVtableOwner owner;
        return static_cast<std::uint32_t>(*reinterpret_cast<const std::uintptr_t*>(&owner));
#else
        return 0u;
#endif
    }

    namespace
    {
        using CommandWordListOwner = SpriteCommandStack::CommandWordList;

        std::unordered_map<const SpriteCommandStack*, CommandWordListOwner>& commandWordListSidecars()
        {
            static std::unordered_map<const SpriteCommandStack*, CommandWordListOwner> owners;
            return owners;
        }

        std::unordered_set<const SpriteCommandStack*>& physicalCommandWordListOwners()
        {
            static std::unordered_set<const SpriteCommandStack*> owners;
            return owners;
        }

        void eraseCommandWordListSidecar(const SpriteCommandStack* owner) noexcept
        {
            commandWordListSidecars().erase(owner);
        }
    }

    void SpriteCommandStack::markCommandWordsPhysicalOwner(bool enabled) noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        if (enabled)
            physicalCommandWordListOwners().insert(this);
        else
            physicalCommandWordListOwners().erase(this);
#else
        (void)enabled;
#endif
    }

    SpriteCommandStack::CommandWordList& SpriteCommandStack::commandWords() noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        if (physicalCommandWordListOwners().find(this) != physicalCommandWordListOwners().end())
        {
            // m_commandStack is physically SPRITE+0x58.  Retail UNIT embeds
            // count/capacity/pointer at +0x94/+0x98/+0x9C, i.e. +0x3C from
            // this 0x10-byte command-record owner.
            return *reinterpret_cast<CommandWordList*>(
                reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::CommandWordsFromRecordStack);
        }
#endif
        return commandWordListSidecars()[this];
    }

    const SpriteCommandStack::CommandWordList& SpriteCommandStack::commandWords() const noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        if (physicalCommandWordListOwners().find(this) != physicalCommandWordListOwners().end())
        {
            return *reinterpret_cast<const CommandWordList*>(
                reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::CommandWordsFromRecordStack);
        }
#endif
        return commandWordListSidecars()[this];
    }

    SpriteCommandStack::SpriteCommandStack()
    {
        m_commandRecords.vtableTag = currentCommandRecordListVtable();
    }

    SpriteCommandStack::SpriteCommandStack(const SpriteCommandStack& other)
    {
        copyCommandRecordsFrom(other);
        copyCommandWordsFrom(other);
    }

    SpriteCommandStack& SpriteCommandStack::operator=(const SpriteCommandStack& other)
    {
        if (this == &other)
            return *this;
        copyCommandRecordsFrom(other);
        copyCommandWordsFrom(other);
        return *this;
    }

    int PathSearchScore0() noexcept { return g_pathSearchScore0; }
    int PathSearchScore1() noexcept { return g_pathSearchScore1; }

    SpriteCommandStack::SpriteCommandStack(SpriteCommandStack&& other) noexcept
        : m_commandRecords(other.m_commandRecords)
    {
        commandWords() = other.commandWords();
        other.m_commandRecords.count = 0;
        other.m_commandRecords.capacity = 0;
        other.m_commandRecords.records = nullptr;
        other.commandWords().count = 0;
        other.commandWords().capacity = 0;
        other.commandWords().words = nullptr;
        eraseCommandWordListSidecar(&other);
    }

    SpriteCommandStack& SpriteCommandStack::operator=(SpriteCommandStack&& other) noexcept
    {
        if (this == &other)
            return *this;
        releaseCommandRecords();
        releaseCommandWords();
        m_commandRecords = other.m_commandRecords;
        commandWords() = other.commandWords();
        other.m_commandRecords.count = 0;
        other.m_commandRecords.capacity = 0;
        other.m_commandRecords.records = nullptr;
        other.commandWords().count = 0;
        other.commandWords().capacity = 0;
        other.commandWords().words = nullptr;
        eraseCommandWordListSidecar(&other);
        return *this;
    }

    SpriteCommandStack::~SpriteCommandStack()
    {
        releaseCommandRecords();
        releaseCommandWords();
        markCommandWordsPhysicalOwner(false);
        eraseCommandWordListSidecar(this);
    }

    void SpriteCommandStack::releaseCommandRecords()
    {
        if (m_commandRecords.records)
            ::operator delete(m_commandRecords.records);
        m_commandRecords.records = nullptr;
        m_commandRecords.vtableTag = currentCommandRecordListVtable();
        m_commandRecords.count = 0;
        m_commandRecords.capacity = 0;
    }

    void SpriteCommandStack::releaseCommandRecordsRetailTail()
    {
        m_commandRecords.vtableTag = currentCommandRecordListVtable();
        if (m_commandRecords.records)
            ::operator delete(m_commandRecords.records);
        m_commandRecords.records = nullptr;
        m_commandRecords.count = 0;
    }

    void SpriteCommandStack::clearTargetReferences(SPRITE* target)
    {
        if (!target || !m_commandRecords.records)
            return;

        const std::uint32_t targetBits = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(target) & 0xFFFFFFFFu);
        const std::uint32_t count = m_commandRecords.count;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            CommandRecordStorage& raw = m_commandRecords.records[i];
            if (raw.words[1] != targetBits)
                continue;

            const std::uint32_t opcode = raw.words[0];
            if (opcode != 32u && opcode != 34u && opcode != 74u &&
                opcode != 150u && opcode != 151u && opcode != 152u)
                continue;

            raw.words[0] = 255u;
            raw.words[1] = 0u;
        }
    }

    void SpriteCommandStack::copyCommandRecordsFrom(const SpriteCommandStack& other)
    {
        releaseCommandRecords();
        m_commandRecords.vtableTag = other.m_commandRecords.vtableTag;
        if (other.m_commandRecords.count == 0)
            return;
        ensureCommandRecordCapacity(other.m_commandRecords.count);
        std::memcpy(m_commandRecords.records, other.m_commandRecords.records, other.m_commandRecords.count * sizeof(CommandRecordStorage));
        m_commandRecords.count = other.m_commandRecords.count;
    }

    void SpriteCommandStack::ensureCommandRecordCapacity(std::uint32_t requiredCapacity)
    {
        // Retail ensureCommandRecordCapacityRetail uses a signed JLE for the capacity gate even
        // though the fields are physically DWORDs.  Preserve the raw 32-bit
        // comparison rather than silently switching malformed/high-bit values
        // to unsigned ordering.
        if (static_cast<std::int32_t>(requiredCapacity) <=
            static_cast<std::int32_t>(m_commandRecords.capacity))
            return;

        CommandRecordStorage* const oldArray = m_commandRecords.records;
        const std::uint32_t oldCapacity = m_commandRecords.capacity;
        const std::uint32_t allocationBytes = requiredCapacity << 4;
        CommandRecordStorage* const newArray = static_cast<CommandRecordStorage*>(
            ::operator new(static_cast<std::size_t>(allocationBytes), std::nothrow));
        m_commandRecords.records = newArray;
        if (!m_commandRecords.records)
            fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", static_cast<int>(requiredCapacity));
        if (oldArray && static_cast<std::int32_t>(oldCapacity) > 0)
        {
            // ensureCommandRecordCapacityRetail copies one 16-byte element per signed-positive old
            // capacity.  Normal capacities are tiny; the explicit loop also
            // keeps the owner semantics independent of host size_t width.
            for (std::uint32_t i = 0; i < oldCapacity; ++i)
                m_commandRecords.records[i] = oldArray[i];
        }
        if (oldArray)
            ::operator delete(oldArray);
        m_commandRecords.capacity = requiredCapacity;
    }

    void SpriteCommandStack::writeCommandRecord(std::size_t index, const SpriteCommandRecord& command)
    {
        CommandRecordStorage& raw = m_commandRecords.records[index];
        raw.words[0] = command.opcode;
        raw.words[1] = command.argument1;
        raw.words[2] = command.argument2;
        raw.words[3] = command.argument3;
    }

    void SpriteCommandStack::releaseCommandWords()
    {
        if (commandWords().words)
            ::operator delete(commandWords().words);
        commandWords().words = nullptr;
        commandWords().count = 0;
        commandWords().capacity = 0;
    }

    void SpriteCommandStack::initializeCommandWords()
    {
        CommandWordList& owner = commandWords();
        owner.count = 0;
        owner.capacity = 0;
        owner.words = nullptr;
    }

    void SpriteCommandStack::releaseCommandWordsRetailTail()
    {
        if (commandWords().words)
            ::operator delete(commandWords().words);
        commandWords().words = nullptr;
        commandWords().count = 0;
    }

    void SpriteCommandStack::copyCommandWordsFrom(const SpriteCommandStack& other)
    {
        releaseCommandWords();
        if (other.commandWords().count == 0)
            return;
        ensureCommandWordCapacity(other.commandWords().count);
        std::memcpy(commandWords().words, other.commandWords().words, other.commandWords().count * sizeof(std::int16_t));
        commandWords().count = other.commandWords().count;
    }

    void SpriteCommandStack::ensureCommandWordCapacity(std::uint32_t requiredCapacity)
    {
        if (static_cast<std::int32_t>(requiredCapacity) <=
            static_cast<std::int32_t>(commandWords().capacity))
            return;

        std::int16_t* const oldArray = commandWords().words;
        const std::uint32_t oldCapacity = commandWords().capacity;
        std::int16_t* const newArray = static_cast<std::int16_t*>(::operator new(requiredCapacity * sizeof(std::int16_t), std::nothrow));
        commandWords().words = newArray;
        if (!commandWords().words)
            fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", static_cast<int>(requiredCapacity));
        if (oldArray && static_cast<std::int32_t>(oldCapacity) > 0)
            std::memcpy(commandWords().words, oldArray, oldCapacity * sizeof(std::int16_t));
        if (oldArray)
            ::operator delete(oldArray);
        commandWords().capacity = requiredCapacity;
    }

    void SpriteCommandStack::appendCommandWord(std::int16_t word)
    {
        if (commandWords().count >= commandWords().capacity)
        {
            const std::uint32_t oldCapacity = commandWords().capacity;
            const std::uint32_t grownCapacity = oldCapacity * 2u + 4u;
            if (grownCapacity > oldCapacity)
                ensureCommandWordCapacity(grownCapacity);
        }
        commandWords().words[commandWords().count] = word;
        ++commandWords().count;
    }

    std::uint32_t SpriteCommandStack::commandWordCount() const noexcept
    {
        return commandWords().count;
    }

    const std::int16_t* SpriteCommandStack::commandWordData() const noexcept
    {
        return commandWords().words;
    }

    int SpriteCommandStack::findLastCommandWord(std::uint16_t word) const noexcept
    {
        std::uint32_t eax = commandWords().count;
        const std::int16_t* ecx = commandWords().words;
        if (eax == 0)
            return -1;

        ecx += eax;
        while (eax != 0)
        {
            --ecx;
            --eax;
            if (static_cast<std::uint16_t>(*ecx) == word)
                return static_cast<int>(eax);
        }
        return -1;
    }

    int SpriteCommandStack::findLastCommandWordPointer(const std::int16_t* word) const noexcept
    {
        return findLastCommandWord(static_cast<std::uint16_t>(*word));
    }

    int SpriteCommandStack::removeCommandWordAt(int index) noexcept
    {
        if (index < 0)
            return 1;
        const std::uint32_t rawIndex = static_cast<std::uint32_t>(index);
        std::uint32_t count = commandWords().count;
        if (index >= static_cast<std::int32_t>(count))
            return 1;
        --count;
        commandWords().count = count;
        commandWords().words[rawIndex] = commandWords().words[count];
        return 0;
    }

    void SpriteCommandStack::ensureCommandWordCapacityRetail(std::uint32_t requiredCapacity)
    {
        ensureCommandWordCapacity(requiredCapacity);
    }

    void SpriteCommandStack::appendCommandWordRetail(std::int16_t word)
    {
        // Retail WORD-list append owners use signed CMP/JL for count versus
        // capacity.  Preserve that even for restored/corrupted high-bit state;
        // unsigned comparison would spuriously enter the grow route.
        auto& list = commandWords();
        if (static_cast<std::int32_t>(list.count) >= static_cast<std::int32_t>(list.capacity))
        {
            const std::uint32_t oldCapacity = list.capacity;
            ensureCommandWordCapacityRetail(oldCapacity * 2u + 4u);
        }
        list.words[list.count] = word;
        ++list.count;
    }

    void SpriteCommandStack::clear()
    {
        // Action/clearCommandStackAndReleaseTargets raw-list clear body: zero +0x5C/+0x60, free
        // +0x64 and clear the pointer. Retail does NOT rewrite the embedded
        // owner vtable DWORD at +0x58 on these routes.
        CommandRecordStorage* const oldArray = m_commandRecords.records;
        m_commandRecords.capacity = 0;
        m_commandRecords.count = 0;
        if (oldArray)
            ::operator delete(oldArray);
        m_commandRecords.records = nullptr;
    }

    void SpriteCommandStack::appendCommandRecord(const SpriteCommandRecord& command)
    {
        if (static_cast<std::int32_t>(m_commandRecords.count) >=
            static_cast<std::int32_t>(m_commandRecords.capacity))
        {
            const std::uint32_t oldCapacity = m_commandRecords.capacity;
            const std::uint32_t grownCapacity = oldCapacity * 2u + 4u;
            if (static_cast<std::int32_t>(grownCapacity) > static_cast<std::int32_t>(oldCapacity))
                ensureCommandRecordCapacity(grownCapacity);
        }
        const std::uint32_t oldCount = m_commandRecords.count;
        writeCommandRecord(oldCount, command);
        ++m_commandRecords.count;
    }

    void SpriteCommandStack::prependCommandRecord(const SpriteCommandRecord& command)
    {
        if (static_cast<std::int32_t>(m_commandRecords.count) >=
            static_cast<std::int32_t>(m_commandRecords.capacity))
        {
            const std::uint32_t oldCapacity = m_commandRecords.capacity;
            const std::uint32_t grownCapacity = oldCapacity * 2u + 4u;
            if (static_cast<std::int32_t>(grownCapacity) > static_cast<std::int32_t>(oldCapacity))
                ensureCommandRecordCapacity(grownCapacity);
        }
        const std::uint32_t oldCount = m_commandRecords.count;
        m_commandRecords.count = oldCount + 1u;
        if (oldCount != 0)
        {
            for (std::size_t i = oldCount; i > 0; --i)
                m_commandRecords.records[i] = m_commandRecords.records[i - 1u];
        }
        writeCommandRecord(0, command);
    }

    void SpriteCommandStack::insertCommandRecord(size_t index, const SpriteCommandRecord& command)
    {
        if (static_cast<std::int32_t>(m_commandRecords.count) >=
            static_cast<std::int32_t>(m_commandRecords.capacity))
        {
            const std::uint32_t oldCapacity = m_commandRecords.capacity;
            const std::uint32_t grownCapacity = oldCapacity * 2u + 4u;
            if (static_cast<std::int32_t>(grownCapacity) > static_cast<std::int32_t>(oldCapacity))
                ensureCommandRecordCapacity(grownCapacity);
        }
        const std::uint32_t oldCount = m_commandRecords.count;
        m_commandRecords.count = oldCount + 1u;
        const std::uint32_t rawIndex = static_cast<std::uint32_t>(index);
        if (static_cast<std::int32_t>(oldCount) > static_cast<std::int32_t>(rawIndex))
        {
            for (std::uint32_t i = oldCount; i > rawIndex; --i)
                m_commandRecords.records[i] = m_commandRecords.records[i - 1u];
        }
        writeCommandRecord(rawIndex, command);

    }

    void SpriteCommandStack::ensureCommandRecordCapacityRetail(std::uint32_t requiredCapacity)
    {
        ensureCommandRecordCapacity(requiredCapacity);
    }

    void SpriteCommandStack::setCommandRecordCount(std::uint32_t count)
    {

        m_commandRecords.count = count;
    }

    void SpriteCommandStack::serializeCommandRecordsText(STRING& out) const
    {
        STRING serializedText;
        const int recordCount = static_cast<int>(m_commandRecords.count);
        for (int i = 0; i < recordCount; ++i)
        {
            const CommandRecordStorage& raw = m_commandRecords.records[static_cast<std::size_t>(i)];
            const unsigned char encoded = static_cast<unsigned char>((raw.words[0] + RetailSpriteLayout::CommandWordsFromRecordStack) & 0xFFu);
            STRING recordText;
            constructFormattedString(recordText, "%c%i,%i,%i;",
                static_cast<char>(encoded),
                static_cast<int>(raw.words[1]),
                static_cast<int>(raw.words[2]),
                static_cast<int>(raw.words[3]));
            appendStringOwner(serializedText, recordText);
            recordText.ReleaseOwnedStorage();
        }
        out.AssignAllocatedCopyWithoutRelease(serializedText.c_str());
        serializedText.ReleaseOwnedStorage();
    }

    std::string SpriteCommandStack::serializeCommandRecordsText() const
    {
        STRING out;
        serializeCommandRecordsText(out);
        return out.str();
    }

    void SpriteCommandStack::parseCommandRecordsText(const STRING& text)
    {
        STRING remainingText(text);
        std::uint32_t encodedOpcodeWord = 0;
#if defined(_MSC_VER) && defined(_M_IX86)
        int parsedArgument1;
        int parsedArgument2;
        int parsedArgument3;
#else
        int parsedArgument1 = 0;
        int parsedArgument2 = 0;
        int parsedArgument3 = 0;
#endif
        while (std::strcmp(remainingText.c_str(), kEmptyString) != 0)
        {
            char* const encodedOpcodeByte = reinterpret_cast<char*>(&encodedOpcodeWord);
            std::sscanf(remainingText.c_str(), "%c%i,%i,%i", encodedOpcodeByte, &parsedArgument1, &parsedArgument2, &parsedArgument3);

            const int argument1Value = static_cast<int>(core::retailReadStackDword(&parsedArgument1));
            const int argument2Value = static_cast<int>(core::retailReadStackDword(&parsedArgument2));
            const int argument3Value = static_cast<int>(core::retailReadStackDword(&parsedArgument3));
            SpriteCommandRecord rec = SPRITE::buildCommandRecord(
                (encodedOpcodeWord & 0xFFu) - 0x3Cu,
                argument1Value, argument2Value, argument3Value);
            appendCommandRecord(rec);

            STRING remainingTail;
            constructRightOfFirstMarker(remainingText, remainingTail, kCommandRecordDelimiter);
            assignStringFromString(remainingText, remainingTail);
            remainingTail.ReleaseOwnedStorage();
        }
        remainingText.ReleaseOwnedStorage();
    }

    void SpriteCommandStack::queueCommandBeforeStopSentinel(std::uint32_t opcode, int argument1, int argument2, int argument3)
    {
        std::uint32_t esi = m_commandRecords.count;
        if (esi != 0 && m_commandRecords.records)
        {
            while (esi != 0)
            {
                const std::uint32_t idx = esi - 1u;
                const CommandRecordStorage& raw = m_commandRecords.records[idx];
                --esi;
                if (raw.words[0] == static_cast<std::uint32_t>(ActionCode::ACT_STOP_STACK) && raw.words[1] == 0u && raw.words[2] == 0u && raw.words[3] == 0u)
                {
                    SpriteCommandRecord rec = SPRITE::buildCommandRecord(opcode, argument1, argument2, argument3);
                    insertCommandRecord(esi + 1u, rec);
                    return;
                }
            }
        }

        SpriteCommandRecord rec = SPRITE::buildCommandRecord(opcode, argument1, argument2, argument3);
        prependCommandRecord(rec);
    }

    void SpriteCommandStack::saveCommandRecordsToStream(BaseStream* stream)
    {
        if (m_commandRecords.count == 1u
            && m_commandRecords.records[0].words[0] == static_cast<std::uint32_t>(ActionCode::ACT_STOP_STACK))
        {
            clear();
            }

        const std::uint32_t count = m_commandRecords.count;
        stream->write(&count, 4u);
        const std::uint32_t bytes = count << 4;
        // Retail performs the second vtable write even when count==0.
        // Preserve that call boundary instead of optimizing away a zero-byte write.
        stream->write(m_commandRecords.records, bytes);
    }

    void SpriteCommandStack::restoreCommandRecordsFromStream(BaseStream* stream, const SPRITE* ownerSprite)
    {
        std::uint32_t count = 0;
        stream->read(&count, 4u);
        m_commandRecords.count = count;
        if (static_cast<std::int32_t>(count) > static_cast<std::int32_t>(m_commandRecords.capacity))
            ensureCommandRecordCapacity(count);

        const std::uint32_t bytes = count << 4;
        // Retail calls the stream read slot unconditionally, including the
        // zero-byte case. Valid raw-list lifetime guarantees slot64 when count>0.
        stream->read(m_commandRecords.records, bytes);

        const std::uint32_t rawLastIndex = m_commandRecords.count - 1u;
        std::int32_t index = static_cast<std::int32_t>(rawLastIndex);
        while (index >= 0)
        {
            CommandRecordStorage& raw = m_commandRecords.records[index];
            const std::uint32_t opcode = raw.words[0];
            if (opcode == static_cast<std::uint32_t>(ActionCode::ACT_ATTACK) || opcode == static_cast<std::uint32_t>(ActionCode::ACT_MOVE_TO) || opcode == 0x4Au
                || opcode == 0x96u || opcode == 0x97u || opcode == 0x98u)
            {
                raw.words[1] = ownerSprite->rawResolveOldSpriteHandleLow32(static_cast<int>(raw.words[1]));
            }
            else if (index != 0 && opcode == static_cast<std::uint32_t>(ActionCode::ACT_STOP_STACK) && m_commandRecords.records[index - 1].words[0] == static_cast<std::uint32_t>(ActionCode::ACT_STOP_STACK))
            {
                const std::uint32_t oldCount = m_commandRecords.count;
                if (index < static_cast<std::int32_t>(oldCount))
                {
                    const std::uint32_t newCount = oldCount - 1u;
                    m_commandRecords.count = newCount;
                    m_commandRecords.records[index] = m_commandRecords.records[newCount];
                }
            }
            --index;
        }
    }

    bool SpriteCommandStack::restoreOldMapCommandRecordsFromStream(BaseStream* stream, int mapVersion, const SPRITE* ownerSprite, int* armyBucket)
    {
        if (armyBucket)
            *armyBucket = 0;

        std::int16_t signedCount = 0;
        stream->read(&signedCount, 2u);
        const std::int32_t count = static_cast<std::int32_t>(signedCount);
        m_commandRecords.count = static_cast<std::uint32_t>(count);

        // Retail always calls ensureCommandRecordCapacityRetail with the sign-extended WORD.  Its
        // capacity comparison is signed (JLE), so a negative malformed count
        // does not grow the list; normal MAP data uses non-negative counts.
        if (count > static_cast<std::int32_t>(m_commandRecords.capacity))
            ensureCommandRecordCapacity(static_cast<std::uint32_t>(count));

        // Important retail quirk: case 0xC8 performs ONE contiguous read of
        // count*12 bytes into [SPRITE+0x64]. It does not scatter each legacy
        // 12-byte record into a 16-byte destination stride. The normalization
        // loop below nevertheless walks the buffer with a 0x10 stride.
        const std::uint32_t legacyBytes = static_cast<std::uint32_t>(count) * 12u;
        stream->read(m_commandRecords.records, legacyBytes);

        if (mapVersion < 7)
            clear();

        {
            std::int32_t i = 0;
            const std::int32_t normalizedCount =
                static_cast<std::int32_t>(m_commandRecords.count);
            while (i < normalizedCount)
            {
                CommandRecordStorage& raw =
                    m_commandRecords.records[static_cast<std::uint32_t>(i)];
                raw.words[0] &= 0xFFu;
                raw.words[3] = 0u;
                if (raw.words[0] == 0x28u)
                    raw.words[0] = static_cast<std::uint32_t>(ActionCode::ACT_MOVE);
                else if (raw.words[0] == 0x27u)
                    raw.words[0] = static_cast<std::uint32_t>(ActionCode::ACT_ATTACK);
                else if (raw.words[0] == 0x2Fu)
                    raw.words[0] = static_cast<std::uint32_t>(ActionCode::ACT_STOP_STACK);
                else
                {
                    const int nvid = ownerSprite->Vid() ? ownerSprite->Vid()->nVid : -1;
                    LOG::ResourceError("SPRITE %i", 14, "actionStack.act restore", static_cast<int>(raw.words[0]), nvid);
                }

                if (raw.words[0] == static_cast<std::uint32_t>(ActionCode::ACT_ATTACK) || raw.words[0] == static_cast<std::uint32_t>(ActionCode::ACT_MOVE_TO) || raw.words[0] == 0x4Au
                    || raw.words[0] == 0x96u || raw.words[0] == 0x97u || raw.words[0] == 0x98u)
                {
                    raw.words[1] = ownerSprite->rawResolveOldSpriteHandleLow32(static_cast<int>(raw.words[1]));
                }
                ++i;
            }
        }

        bool hasArmyBucket = false;
        if (mapVersion >= 7)
        {
            std::uint32_t rawArmy = 0u;
            stream->read(&rawArmy, 1u);
            if (armyBucket)
                *armyBucket = static_cast<int>(rawArmy & 0xFFu);
            hasArmyBucket = true;
        }
        return hasArmyBucket;
    }

    void SpriteCommandStack::serializeCommandWordsText(STRING& out) const
    {
        static const char kCommandWordFormat[] = { '\x01', '%', 'i', '\0' };
        static const char kCommandSectionDelimiter[] = { '\x02', '\0' };

        STRING serializedText;
        const int wordCount = static_cast<int>(commandWords().count);
        for (int i = 0; i < wordCount; ++i)
        {
            const std::int16_t word = commandWords().words[static_cast<std::size_t>(i)];
            STRING wordText;
            constructFormattedString(wordText, kCommandWordFormat, static_cast<int>(word));
            appendStringOwner(serializedText, wordText);
            wordText.ReleaseOwnedStorage();
        }

        if (wordCount != 0)
            appendCStringToString(serializedText, kCommandSectionDelimiter);

        out.AssignAllocatedCopyWithoutRelease(serializedText.c_str());
        serializedText.ReleaseOwnedStorage();
    }

    std::string SpriteCommandStack::serializeCommandWordsText() const
    {
        STRING out;
        serializeCommandWordsText(out);
        return out.str();
    }

    void SpriteCommandStack::parseCommandWordsText(STRING remainingText)
    {
        constructRightOfFirstMarker(remainingText, remainingText, kCommandWordPrefixMarker);

        const char* parserPointer = remainingText.c_str();
#if defined(_MSC_VER) && defined(_M_IX86)
        int parsedWord;
#else
        int parsedWord = 0;
#endif
        while (std::strcmp(parserPointer, kEmptyString) != 0)
        {
            std::sscanf(parserPointer, "%i", &parsedWord);
            const int parsedWordValue = static_cast<int>(core::retailReadStackDword(&parsedWord));

            appendCommandWord(static_cast<std::int16_t>(parsedWordValue));

            STRING remainingTail;
            constructRightOfFirstMarker(remainingText, remainingTail, kCommandWordPrefixMarker);
            assignStringFromString(remainingText, remainingTail);
            remainingTail.ReleaseOwnedStorage();

            parserPointer = remainingText.c_str();
        }

        remainingText.ReleaseOwnedStorage();
    }

    void SPRITE::serializeCommandWordsText(STRING& out) const
    {
        m_commandStack.serializeCommandWordsText(out);
    }

    std::string SPRITE::serializeCommandWordsText() const
    {
        STRING out;
        serializeCommandWordsText(out);
        return out.str();
    }

    void SPRITE::parseCommandWordsText(STRING text)
    {
        m_commandStack.parseCommandWordsText(text);
    }

    namespace
    {
        std::unordered_map<const SPRITE*, SpriteHostState>& spriteHostStates()
        {
            static std::unordered_map<const SPRITE*, SpriteHostState> states;
            return states;
        }
    }

    SpriteHostState& SPRITE::hostState() noexcept
    {
        return spriteHostStates()[this];
    }

    const SpriteHostState& SPRITE::hostState() const noexcept
    {
        return spriteHostStates()[this];
    }

    void SPRITE::releaseHostState() noexcept
    {
        spriteHostStates().erase(this);
    }

    SPRITE::SPRITE(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir)
        : SPRITE(owner, vid, xyz, dir, nullptr)
    {
    }

    SPRITE::SPRITE(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent)
        : m_vid(vid), m_xyz(xyz), m_direction(dir)
    {
        hostState().owner = owner;
        // Retail B_SPRITE/class 9 and the temporary Action targets from
        // dispatchActionOpcode cases 0x21/0x25 allocate exactly 0x70 bytes and call
        // initializeBaseSprite directly.  Derived constructor families append their
        // own retail tails after this base constructor; the SPRITE language
        // constructor must therefore not run initializeExtendedSpriteState/initializeCommandSpriteState.
        initializeBaseSprite(vid, xyz.x, xyz.y, xyz.z, dir.Int() & 0xFF, parent);
    }

    SPRITE* SPRITE::initializeBaseSprite(VID* vid, float x, float y, float z, int direction, SPRITE* parent) noexcept
    {
        m_vid = vid;
        m_childChain = nullptr;
        m_childBacklink = nullptr;
        m_goalSprite = nullptr;
        m_bestTargetSprite = nullptr;
        m_actionTimer = 0;
        hostState().actionAuxCommandMask = {0, 0};
        m_actionAuxState = nullptr;
        m_commandStack.releaseCommandRecordsRetailTail();

        int dirByte = direction & 0xFF;
        VECTOR next{x, y, z};

        if ((vid->properties() & P_NOISE) != 0)
        {
            next.x += static_cast<float>(8 - (std::rand() % 17));
            next.y += static_cast<float>(8 - (std::rand() % 17));
        }
        if ((vid->properties() & P_ZEROZ) != 0)
        {
            const float groundZ = hostState().owner->GetGroundZ(vid, VECTOR2{next.x, next.y}, ANGLE(dirByte));
            next.z = parent ? (z - parent->Z() + groundZ) : groundZ;
        }

        m_xyz = next;

        m_direction = ANGLE(0);

        int bucket = 0;
        if (parent)
            bucket = parent->armyIndex();
        else
        {
            const VID* bucketOwner = vid;
            if (vid->weaponCount() == 0)
            {
                if (VID* linkVid = vid->linkedVid())
                    bucketOwner = linkVid;
            }
            bucket = bucketOwner ? (bucketOwner->weaponIntAt(0x30) & 3) : 0;
        }

        DWORD nextFlags = (m_runtimeFlags & 0xFFFF0000u) | (static_cast<DWORD>(bucket & 3) << 10);
        if ((vid->properties() & P_INVISIBLEFORENEMY) != 0)
        {
            const std::uint32_t appBucket = core::ActivePlayerIndex();
            if (static_cast<std::uint32_t>(bucket & 3) != appBucket)
                nextFlags |= 0x00008000u;
        }
        m_runtimeFlags = nextFlags;

        const std::uint32_t now = core::CurrentTimeMilliseconds();
        int randomDelay = 0;
        if ((vid->properties() & P_ONEPHASE) == 0)
        {
            const int speedDefault = static_cast<int>(vid->defaultFrameSpeed());
            randomDelay = std::rand() % (speedDefault + 1);
        }
        m_applicationBucketTime = now - static_cast<std::uint32_t>(randomDelay);
        m_animationLastTick = now;
        m_listReferenceCount = 0;
        m_speed = 0.0f;
        m_zSpeed = 0.0f;
        m_currentFrame = 0;
        m_currentFrameBegin = 0;
        m_currentFrameEnd = 0;

        if (vid->actionAuxStateRequired() != 0)
        {
            // Retail tests the operator-new result before calling initializeActionAuxState
            // and writes null to +0x68 on allocation failure. This constructor
            // is noexcept, so use the non-throwing allocation form to preserve
            // that tested-null branch instead of terminating on bad_alloc.
            m_actionAuxState = static_cast<ActionAuxState*>(
                ::operator new(sizeof(ActionAuxState), std::nothrow));
            if (m_actionAuxState)
                initializeActionAuxState(this);
        }

        initializeAnimationRouteFromVid();
        m_animationFrameTime = vid->animationFrameDuration(armyIndex());
        hostState().runtimeInitializedFromVid = true;

        ChangeDirection(dirByte);

        if (m_currentAnimation == 0 && m_currentFrameEnd > m_currentFrame)
        {
            const int span = m_currentFrameEnd - m_currentFrameBegin;
            if (span > 0 && (!vid || (vid->properties() & P_ONEPHASE) == 0))
                m_currentFrame += std::rand() % (span + 1);
        }

        if (m_vid != MAP::NullVid())
        {
            ++m_listReferenceCount;
            addToDrawBucketsRecursive();
        }

        ensureLinkedVidChild();
        GlobalSpriteHashMap()->addSprite(this);

        m_vid->setLastSpriteCountChangeTimestamp(core::RealTimeMilliseconds());
        m_vid->incrementSpriteCountForArmy(armyIndex());

        if (m_currentAnimation != 14 && (core::ApplicationFlags() & application_flags::MapLoading) == 0)
        {
            const int child238Gate = m_vid->birthChildVid() ? 1 : 0;
            if (child238Gate)
            {
                const int savedAnimation = m_currentAnimation;
                m_currentAnimation = 14;
                spawnAnimationChild();
                m_currentAnimation = savedAnimation;
            }

            const int constructorSfx = m_vid->constructorSfxId();
            if (constructorSfx != 0)
            {
                playSfxAtWorldPosition(constructorSfx);
            }
        }

        if ((core::ApplicationFlags() & 0x1u) == 0)
        {
            if ((m_vid->properties() & P_BUILDSIZETOGRIDZ) != 0)
            {
                int yOffset = 0;
                if (x87LessOrUnordered(0.0f, m_vid->sizeXYZ.y))
                {
                    do
                    {
                        int xOffset = 0;
                        if (x87LessOrUnordered(0.0f, m_vid->sizeXYZ.x))
                        {
                            do
                            {
                                hostState().owner->setTerrainHeightAtWorldPosition(
                                    m_xyz.x + static_cast<float>(xOffset),
                                    m_xyz.y + static_cast<float>(yOffset),
                                    m_xyz.z + m_vid->sizeXYZ.z);
                                xOffset += 8;
                            }
                            while (x87LessOrUnordered(static_cast<float>(xOffset), m_vid->sizeXYZ.x));
                        }
                        hostState().owner->setTerrainHeightAtWorldPosition(
                            m_xyz.x + m_vid->sizeXYZ.x,
                            m_xyz.y + static_cast<float>(yOffset),
                            m_xyz.z + m_vid->sizeXYZ.z);
                        yOffset += 8;
                    }
                    while (x87LessOrUnordered(static_cast<float>(yOffset), m_vid->sizeXYZ.y));
                }

                int xOffset = 0;
                if (x87LessOrUnordered(0.0f, m_vid->sizeXYZ.x))
                {
                    do
                    {
                        hostState().owner->setTerrainHeightAtWorldPosition(
                            m_xyz.x + static_cast<float>(xOffset),
                            m_xyz.y + m_vid->sizeXYZ.y,
                            m_xyz.z + m_vid->sizeXYZ.z);
                        xOffset += 8;
                    }
                    while (x87LessOrUnordered(static_cast<float>(xOffset), m_vid->sizeXYZ.x));
                }
                hostState().owner->setTerrainHeightAtWorldPosition(
                    m_xyz.x + m_vid->sizeXYZ.x,
                    m_xyz.y + m_vid->sizeXYZ.y,
                    m_xyz.z + m_vid->sizeXYZ.z);
            }

            if ((m_vid->properties() & P_BUILDVIDZTOGRIDZ) != 0)
                m_vid->SetGridZ(this);
        }

        return this;
    }

    int SPRITE::ammoFixedPoint() const noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::AmmoFixedPoint);
#else
        return hostState().ammoFixedPointValue;
#endif
    }

    int SPRITE::turnTimer() const noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::TurnTimer);
#else
        return hostState().turnTimer;
#endif
    }

    void SPRITE::setTurnTimer(int value) noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::TurnTimer) = value;
#else
        hostState().turnTimer = value;
#endif
    }

    int SPRITE::behaviorFlags() const noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::BehaviorFlags);
#else
        return hostState().behaviorFlags;
#endif
    }

    void SPRITE::setBehaviorFlags(int value) noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::BehaviorFlags) = value;
#else
        hostState().behaviorFlags = value;
#endif
    }

    void SPRITE::setCommandWordListVtable(std::uint32_t value) noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        *reinterpret_cast<std::uint32_t*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::CommandWordListVtable) = value;
#else
        hostState().commandWordListVtable = value;
#endif
    }

    void SPRITE::setAmmoFixedPoint(int value) noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::AmmoFixedPoint) = value;
#else
        hostState().ammoFixedPointValue = value;
#endif
    }

    SPRITE* SPRITE::initializeExtendedSpriteState(VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent) noexcept
    {
        initializeBaseSprite(vid, xyz.x, xyz.y, xyz.z, dir.Int() & 0xFF, parent);
#if UINTPTR_MAX == 0xFFFFFFFFu
        // The rebuilt x86 image has its own vtable VA.  Preserve the observed
        // slot identity without copying the retail executable's absolute VA.
        hostState().extendedSpriteVtableSnapshot = static_cast<std::uint32_t>(*reinterpret_cast<const std::uintptr_t*>(this));
#else
        hostState().extendedSpriteVtableSnapshot = 0u;
#endif
        setSharedPrimaryState(-1);
        setSharedSecondaryState(0);
        return this;
    }

    SPRITE* SPRITE::initializeCommandSpriteState(VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent) noexcept
    {
        initializeExtendedSpriteState(vid, xyz, dir, parent);

        setCommandWordListVtable(currentCommandWordListVtable());
        m_commandStack.markCommandWordsPhysicalOwner(true);
        m_commandStack.initializeCommandWords();

#if UINTPTR_MAX == 0xFFFFFFFFu
        *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::ExtendedStateBase) = 0;
        *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::LegacyCommandState1) = 0;
        *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::LegacyCommandState2) = -1;
#else
        hostState().legacyCommandState0 = 0;
        hostState().legacyCommandState1 = 0;
        hostState().legacyCommandState2 = -1;
#endif
        setTurnTimer(0);

        VID* const baseVid = Vid();
        const VID* valueOwner = baseVid;
        if (m_childChain)
        {
            VID* const childVid = m_childChain->Vid();
            VID* const linkVid = baseVid->linkedVid();
            if (childVid == linkVid &&
                childVid->hasWeaponChildDescriptor() != 0u &&
                childVid->weaponCount() != 0u)
            {
                valueOwner = linkVid;
            }
        }

        setBehaviorFlags(valueOwner->weaponIntAt(0x34));

        const VID* counterOwner = baseVid;
        if (VID* const linkVid = baseVid->linkedVid())
        {
            if (linkVid->hasWeaponChildDescriptor() != 0u &&
                linkVid->weaponCount() != 0u)
            {
                counterOwner = linkVid;
            }
        }
        setAmmoFixedPoint(counterOwner->weaponRecordAmmoCapacity() << 6);

        return this;
    }

    TERRAIN::TERRAIN(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent)
        : SPRITE(owner, vid, xyz, dir, parent),
          m_sharedPrimaryState(-1),
          m_sharedSecondaryState(0)
    {

        setSharedPrimaryState(m_sharedPrimaryState);
        setSharedSecondaryState(m_sharedSecondaryState);
    }

    int TERRAIN::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        const int argument1 = static_cast<int>(argument1Carrier);
        const int argument2 = argument2Carrier;
        const int argument3 = argument3Carrier;

        switch (opcode)
        {
        case 0x34:
            ChangeCoor(X() + static_cast<float>(argument1),
                       Y() + static_cast<float>(argument2),
                       Z() + static_cast<float>(argument3));
            ChangeAnimation(15);
            return 0;
        case 0x32:
        case 0x33:
            ChangeAnimation(0);
            return 0;
        case 0x56:
            repairLinkedChildState(1);
            return 0;
        default:
            return dispatchActionOpcode(static_cast<std::uint32_t>(opcode), argument1, argument2, argument3);
        }
    }

    LINKER::LINKER(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent)
        : SPRITE(owner, vid, xyz, dir, parent)
    {
        const int directionByte = dir.Int() & 0xFF;
        if (parent)
        {
            parent->appendChildChain(this);
            setLinkerState(
                xyz.x - parent->X(),
                xyz.y - parent->Y(),
                xyz.z - parent->Z(),
                directionByte,
                parent);
            return;
        }

        setLinkerState(0.0f, 0.0f, 0.0f, directionByte, nullptr);
    }

    LINKER::~LINKER()
    {
        detachFromChildChain();
    }

    REGION::REGION(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent)
        : SPRITE(owner, vid, xyz, dir, parent),
          m_fogRampPhase(0),
          m_lastFogRampPhase(0),
          m_fogRamp(nullptr),
          m_regionFlags(0),
          m_fogEnd(0),
          m_fogStart(0),
          m_fogColor(0xFF000000u),
          m_reservedRegionState8C(0),
          m_regionWidth(0.0f),
          m_regionHeight(0.0f),
          m_savedRegionVid(MAP::NullVid())
    {
        for (int i = 0; i < 6; ++i)
        {
            m_sourceVidMap[i] = nullptr;
            m_targetVidMap[i] = nullptr;
        }
    }

    REGION::~REGION()
    {
        // Retail destroyRegionState: Application vtable +8 first, then release
        // REGION+0x78, followed by base destroyBaseSpriteState through SPRITE::~SPRITE.
#ifdef _WIN32
        win::applicationWinInstance()->transferFrom(this);
#else
        if (MAP* const owner = mapOwner())
            owner->releaseSpriteReferencesHost(this);
#endif
        if (m_fogRamp)
        {
            ::operator delete(m_fogRamp);
            m_fogRamp = nullptr;
        }
    }

    REGION* REGION::regionScalarDeletingDestructor(unsigned char flags) noexcept
    {

        REGION* const self = this;
        destroyRegionState();
        if ((flags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    void REGION::destroyRegionState() noexcept
    {
#ifdef _WIN32
        win::applicationWinInstance()->transferFrom(this);
#else
        if (MAP* const owner = mapOwner())
            owner->releaseSpriteReferencesHost(this);
#endif
        if (m_fogRamp)
        {
            ::operator delete(m_fogRamp);
            m_fogRamp = nullptr;
        }
        destroyBaseSpriteState();
    }

    VID* resolveRegionMappedVid(VID* sourceVid, float x, float y, float z) noexcept
    {
        const core::ApplicationDrawPassBucket& bucket =
            core::GlobalApplicationDrawDispatcherState().drawPassBucket(7);

        for (int cursor = bucket.count() - 1; cursor >= 0; --cursor)
        {
            SPRITE* const candidate = bucket.spriteAt(cursor);
            if (!candidate)
                continue;

            VID* const candidateVid = candidate->Vid();
            if (candidateVid->spriteClassId() != B_REGION)
                continue;

            const double zGate = static_cast<double>(candidate->Z()) + 25.0;
            if (zGate <= static_cast<double>(z) || std::isnan(zGate) || std::isnan(z))
                continue;

            REGION* const region = static_cast<REGION*>(candidate);
            if ((region->regionFlags() & REGION::FullViewportFlag) == 0u)
            {
                // Retail keeps these edges live in x87.  Binary32 inputs and an
                // exact 0.5 scale fit exactly in binary64, avoiding the previous
                // extra m32 rounding of the intermediate rectangle edges.
                const double cx = static_cast<double>(candidate->X());
                const double cy = static_cast<double>(candidate->Y());
                const double halfWidth = static_cast<double>(region->regionWidth()) * 0.5;
                const double halfHeight = static_cast<double>(region->regionHeight()) * 0.5;
                const double px = static_cast<double>(x);
                const double py = static_cast<double>(y);
                const bool insideX =
                    (cx - halfWidth <= px || std::isnan(cx - halfWidth) || std::isnan(px)) &&
                    (px <= cx + halfWidth || std::isnan(px) || std::isnan(cx + halfWidth));
                const bool insideY =
                    (cy - halfHeight <= py || std::isnan(cy - halfHeight) || std::isnan(py)) &&
                    (py <= cy + halfHeight || std::isnan(py) || std::isnan(cy + halfHeight));
                if (!insideX || !insideY)
                    continue;
            }

            for (int index = 0; index < 6; ++index)
            {
                if (region->sourceMappedVid(index) == sourceVid)
                    return region->targetMappedVid(index);
            }
        }
        return sourceVid;
    }

    void REGION::Draw()
    {
        drawRegionTilesAndFog();
    }

    double REGION::regionScreenLeft() const noexcept
    {
        return static_cast<double>(X()) - static_cast<double>(m_regionWidth) * 0.5 -
               static_cast<double>(core::GlobalApplicationDrawDispatcherState().cameraShiftX());
    }

    double REGION::regionScreenTop() const noexcept
    {
        return static_cast<double>(Y()) - static_cast<double>(Z()) -
               static_cast<double>(m_regionHeight) * 0.5 -
               static_cast<double>(core::GlobalApplicationDrawDispatcherState().cameraShiftY());
    }

    double REGION::regionScreenRight() const noexcept
    {
        return static_cast<double>(m_regionWidth) * 0.5 + static_cast<double>(X()) -
               static_cast<double>(core::GlobalApplicationDrawDispatcherState().cameraShiftX());
    }

    double REGION::regionScreenBottom() const noexcept
    {
        return static_cast<double>(Y()) - static_cast<double>(Z()) +
               static_cast<double>(m_regionHeight) * 0.5 -
               static_cast<double>(core::GlobalApplicationDrawDispatcherState().cameraShiftY());
    }

    void REGION::DrawDebugOverlay()
    {
        drawRegionDebugBounds();
    }

    void REGION::drawRegionDebugBounds()
    {
        GRAPH* const graph = GRAPH::CurrentGraph();
        const DWORD white = GammaRawCreateOpaque(255, 255, 255);
        graph->DrawRect(static_cast<float>(regionScreenLeft() - 1.0),
                        static_cast<float>(regionScreenTop() - 1.0),
                        static_cast<float>(regionScreenRight() + 1.0),
                        static_cast<float>(regionScreenBottom() + 1.0),
                        white);
    }

    void REGION::drawRegionTilesAndFog()
    {
        const int savedFrame = currentFrame();
        const float savedX = X();
        const float savedY = Y();
        VID* const regionVid = Vid();
        GRAPH* const graph = GRAPH::CurrentGraph();

        if (regionVid != MAP::NullVid())
        {
            if ((m_regionFlags & FullViewportFlag) == 0u)
            {
                graph->rawSetSoftwareClipBounds(
                    spriteFtolLow32(static_cast<long double>(regionScreenLeft())),
                    spriteFtolLow32(static_cast<long double>(regionScreenTop())),
                    spriteFtolLow32(static_cast<long double>(regionScreenRight())),
                    spriteFtolLow32(static_cast<long double>(regionScreenBottom())));
            }

            const float halfHeight = static_cast<float>(
                static_cast<long double>(m_regionHeight) * 0.5L);
            float tileY = static_cast<float>(static_cast<long double>(savedY) - halfHeight);
            const float tileYEnd = static_cast<float>(static_cast<long double>(savedY) + halfHeight);
            int tileIndex = 0;
            while (x87OrderedLess(tileY, tileYEnd))
            {
                const float halfWidth = static_cast<float>(
                    static_cast<long double>(m_regionWidth) * 0.5L);
                float tileX = static_cast<float>(static_cast<long double>(savedX) - halfWidth);
                const float tileXEnd = static_cast<float>(static_cast<long double>(savedX) + halfWidth);
                while (x87OrderedLess(tileX, tileXEnd))
                {
                    if ((regionVid->properties() & P_ONEPHASE) == 0u)
                    {
                        const int noCadr = static_cast<int>(regionVid->totalFrames());
                        setCurrentFrameDirect((savedFrame + 2 * tileIndex++) % noCadr);
                    }
                    setXPosition(tileX + static_cast<float>(static_cast<std::int16_t>(regionVid->vidWidth()) / 2));
                    setYPosition(tileY + static_cast<float>(static_cast<std::int16_t>(regionVid->vidHeight()) / 2));
                    regionVid->Draw(this);
                    tileX += static_cast<float>(static_cast<std::int16_t>(regionVid->vidWidth()));
                }
                tileY += static_cast<float>(static_cast<std::int16_t>(regionVid->vidHeight()));
            }

            if ((m_regionFlags & FullViewportFlag) == 0u)
            {
                const GraphViewportState& liveViewport = graph->viewportState();
                graph->rawSetSoftwareClipBounds(
                    spriteFtolLow32(static_cast<long double>(liveViewport.left)),
                    spriteFtolLow32(static_cast<long double>(liveViewport.top)),
                    spriteFtolLow32(static_cast<long double>(liveViewport.right)),
                    spriteFtolLow32(static_cast<long double>(liveViewport.bottom)));
            }
        }

        setXPosition(savedX);
        setYPosition(savedY);
        setCurrentFrameDirect(savedFrame);

        if (m_fogStart >= m_fogEnd)
        {
            m_fogRampPhase = 0;
            return;
        }

        if ((m_regionFlags & FogAnimatedFlag) != 0u)
        {
            const int phase = static_cast<int>(core::CurrentTimeMilliseconds() & 7u);
            if (static_cast<unsigned>(phase) < static_cast<unsigned>(m_lastFogRampPhase))
            {
                const int limit = 8 * m_fogEnd;
                if (m_fogRampPhase < limit)
                {
                    m_fogRampPhase += 2;
                    m_lastFogRampPhase = phase;
                }
                else if (m_fogRampPhase > limit)
                {
                    m_fogRampPhase = 0;
                    m_lastFogRampPhase = phase;
                }
            }
            m_lastFogRampPhase = phase;
        }
        else
        {
            m_fogRampPhase = 8 * m_fogEnd;
        }

        const DWORD color = m_fogColor;
        const WORD* const ramp = static_cast<const WORD*>(m_fogRamp);
        const int blend = static_cast<int>(m_regionFlags & FogBlendFlag);
        if ((m_regionFlags & FullViewportFlag) != 0u)
        {
            graph->drawFogBufferOverlay(static_cast<float>(graph->getViewportLeft()),
                              static_cast<float>(graph->getViewportTop()),
                              static_cast<float>(graph->getViewportRight()),
                              static_cast<float>(graph->getViewportBottom()),
                              m_fogStart, m_fogEnd, color, ramp,
                              m_fogRampPhase, blend);
        }
        else
        {
            graph->drawFogBufferOverlay(static_cast<float>(regionScreenLeft()),
                              static_cast<float>(regionScreenTop()),
                              static_cast<float>(regionScreenRight()),
                              static_cast<float>(regionScreenBottom()),
                              m_fogStart, m_fogEnd, color, ramp,
                              m_fogRampPhase, blend);
        }
    }

    int REGION::rebuildRegionFogRamp(int start, int end, int color)
    {
        m_fogEnd = end;
        m_fogStart = start;
        m_fogColor = static_cast<std::uint32_t>(color);
        if (m_fogRamp)
            ::operator delete(m_fogRamp);

        int eaxCarrier = start;
        if (start >= end)
            return eaxCarrier;

        const std::uint32_t difference =
            static_cast<std::uint32_t>(end) - static_cast<std::uint32_t>(start);
        const std::uint32_t allocationSize = difference * 16u + 2u;
        m_fogRamp = ::operator new(static_cast<std::size_t>(allocationSize), std::nothrow);
        if (!m_fogRamp)
        {
            fatalLogError(g_fileLogger, "Enough memory\tfor DrawFog",
                       static_cast<int>(difference * 8u + 1u));
        }

        const std::int32_t count = static_cast<std::int32_t>(difference * 8u);
        if (count < 0)
            return eaxCarrier;

        WORD* const ramp = static_cast<WORD*>(m_fogRamp);
        std::int32_t index = count;
        std::int32_t numerator = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(count) * 255u);
        do
        {
            const std::int32_t signedDifference = static_cast<std::int32_t>(difference);
            std::int32_t quotient = numerator / signedDifference;

            if (quotient < 0)
                quotient += 7;
            const int intensity = quotient >> 3;
            eaxCarrier = intensity;
            if (GRAPH::CurrentGraph()->lightBuffer()->format() != 41u)
            {
                const WORD palette = GRAPH::CurrentGraph()->intensityPaletteEntry(
                    static_cast<std::size_t>(intensity));
                eaxCarrier = (eaxCarrier & ~0xFFFF) | static_cast<int>(palette);
            }
            ramp[index] = static_cast<WORD>(eaxCarrier);
            --index;
            numerator = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(numerator) - 255u);
        }
        while (index >= 0);
        return eaxCarrier;
    }

    int REGION::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        RESOURCE* const resource = reinterpret_cast<RESOURCE*>(static_cast<std::uintptr_t>(argument1Carrier));
        if (opcode == 80)
        {
            (void)SPRITE::Action(opcode, argument1Carrier, argument2Carrier, argument3Carrier);
            resource->write(&m_regionFlags, 4u);
            resource->write(&m_fogEnd, 4u);
            resource->write(&m_fogStart, 4u);
            resource->write(&m_fogColor, 4u);
            resource->write(&m_regionWidth, 4u);
            resource->write(&m_regionHeight, 4u);
            resource->write(&m_persistedRegionState, 4u);
            int nvid = m_savedRegionVid ? m_savedRegionVid->nvid() : -1;
            resource->write(&nvid, 4u);
            for (int i = 0; i < 6; ++i)
            {
                nvid = m_sourceVidMap[i] ? m_sourceVidMap[i]->nvid() : -1;
                resource->write(&nvid, 4u);
                nvid = m_targetVidMap[i] ? m_targetVidMap[i]->nvid() : -1;
                resource->write(&nvid, 4u);
            }
            return 0;
        }
        if (opcode != SpriteActConst::ACT_RESTORE && opcode != SpriteActConst::ACT_RESTORE_OLD_MAP)
            return SPRITE::Action(opcode, argument1Carrier, argument2Carrier, argument3Carrier);

        const int routedVersion = argument2Carrier;
        (void)SPRITE::Action(opcode, argument1Carrier, argument2Carrier, argument3Carrier);
        resource->read(&m_regionFlags, 4u);
        int slot80 = 0, slot84 = 0, color88 = 0;
        resource->read(&slot80, 4u);
        resource->read(&slot84, 4u);
        resource->read(&color88, 4u);
        rebuildRegionFogRamp(slot84, slot80, color88);
        if (routedVersion <= 9)
        {
            BYTE legacyByte = 0;
            resource->read(&legacyByte, 1u);
            m_regionWidth = static_cast<float>(color88);
            int legacyHeight = 0;
            resource->read(&legacyHeight, 4u);
            m_regionHeight = static_cast<float>(legacyHeight);
        }
        else
        {
            resource->read(&m_regionWidth, 4u);
            resource->read(&m_regionHeight, 4u);
        }
        resource->read(&m_persistedRegionState, 4u);
        core::ApplicationVidTable& vidTable = core::GlobalApplicationVidTable();
        const auto resolveVid = [&vidTable](int nvid) -> VID*
        {
            if (nvid < 0 || nvid >= vidTable.count())
                return nullptr;
            return vidTable.slot(nvid);
        };
        int nvid = -1;
        resource->read(&nvid, 4u);
        m_savedRegionVid = resolveVid(nvid);
        if (!m_savedRegionVid)
            m_savedRegionVid = MAP::NullVid();
        for (int i = 0; i < 6; ++i)
        {
            resource->read(&nvid, 4u);
            m_sourceVidMap[i] = resolveVid(nvid);
            resource->read(&nvid, 4u);
            m_targetVidMap[i] = resolveVid(nvid);
            if (m_sourceVidMap[i])
                m_sourceVidMap[i]->setProperties(
                    m_sourceVidMap[i]->properties() | P_NOTCHANGELINKERCOOR);
        }
        return 0;
    }

    FRAME::FRAME(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent)
        : SPRITE(owner,
                 vid,
                 VECTOR{xyz.x + core::GlobalApplicationDrawDispatcherState().cameraShiftX(),
                        xyz.y + core::GlobalApplicationDrawDispatcherState().cameraShiftY(),
                        xyz.z},
                 dir,
                 parent)
    {
        captureCurrentImageFrameVtable(this);
        applicationFrameSpriteList().append(this);
    }

    FRAME::~FRAME()
    {
        // Language destructor path represents destroyFrameState's derived prefix;
        // SPRITE::~SPRITE supplies the final destroyBaseSpriteState exactly once.
        as1::core::GlobalApplicationFrameRuntimeState().clearCurrentFrameSpriteIfMatches(this);
        applicationFrameSpriteList().removeSortedError(this);
#ifdef _WIN32
        win::applicationWinInstance()->transferFrom(this);
#else
        if (MAP* const owner = mapOwner())
            owner->releaseSpriteReferencesHost(this);
#endif
    }

    FRAME* FRAME::frameScalarDeletingDestructor(unsigned char flags) noexcept
    {
        FRAME* const self = this;
        destroyFrameState();
        if ((flags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    void FRAME::destroyFrameState() noexcept
    {
        publishCurrentImageFrameVtable(this);
        as1::core::GlobalApplicationFrameRuntimeState().clearCurrentFrameSpriteIfMatches(this);
        applicationFrameSpriteList().removeSortedError(this);
#ifdef _WIN32
        win::applicationWinInstance()->transferFrom(this);
#else
        if (MAP* const owner = mapOwner())
            owner->releaseSpriteReferencesHost(this);
#endif
        destroyBaseSpriteState();
    }

    STEXT::STEXT(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent)
        : FRAME(owner, vid, xyz, dir, parent)
    {
        initializeTextState(
            STRING::SharedEmptyText(),
            STRING::SharedEmptyText(),
            0,
            0);
    }

    STEXT::~STEXT()
    {
        releaseOwnedText(m_textClass);
        releaseOwnedText(m_text);
    }

    STEXT* STEXT::textScalarDeletingDestructor(unsigned char flags) noexcept
    {
        STEXT* const self = this;
        releaseOwnedText(m_textClass);
        releaseOwnedText(m_text);
        destroyFrameState();
        if ((flags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    void STEXT::initializeTextState(char* text70, char* class74, int length78, int flags7C) noexcept
    {
        m_text = text70 ? text70 : STRING::SharedEmptyText();
        m_textClass = class74 ? class74 : STRING::SharedEmptyText();
        m_textLength = length78;
        m_textFlags = flags7C;
    }

    char* STEXT::cloneOwnedText(const char* text)
    {
        if (!text || *text == '\0')
            return STRING::SharedEmptyText();
        const std::size_t len = std::strlen(text);
        char* owner = static_cast<char*>(::operator new(len + 1));
        std::memcpy(owner, text, len + 1);
        return owner;
    }

    void STEXT::releaseOwnedText(char*& owner) noexcept
    {
        if (owner && owner != STRING::SharedEmptyText())
            ::operator delete(owner);
        owner = STRING::SharedEmptyText();
    }

    void STEXT::assignText(const char* text)
    {
        releaseOwnedText(m_text);
        m_text = cloneOwnedText(text);
        m_textLength = static_cast<int>(std::strlen(m_text));
    }

    void STEXT::assignTextClass(const char* text)
    {
        releaseOwnedText(m_textClass);
        m_textClass = cloneOwnedText(text);
    }

    int FRAME::dispatchFrameActionOpcode(int opcode, std::intptr_t actionArgument1, std::intptr_t actionArgument2, std::intptr_t actionArgument3)
    {
        if (opcode >= 0 && opcode <= 5)
            return 0;

        if (opcode == 0x82)
        {
            const int animation = currentAnimation();
            if (animation >= 15)
                return 0;
            if (animation == 4)
            {
                ChangeAnimation(2);
                setRuntimeFlags(runtimeFlags() | 0x00000200u);
            }
            else if (animation == 5)
            {
                ChangeAnimation(3);
                setRuntimeFlags(runtimeFlags() | 0x00000200u);
            }
            if (currentAnimation() == 14)
                ChangeAnimation(0);
            return 0;
        }

        return dispatchActionOpcode(static_cast<std::uint32_t>(opcode),
                          static_cast<int>(actionArgument1),
                          static_cast<int>(actionArgument2),
                          static_cast<int>(actionArgument3));
    }

    int FRAME::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        return dispatchFrameActionOpcode(opcode, argument1Carrier,
                          static_cast<std::intptr_t>(argument2Carrier),
                          static_cast<std::intptr_t>(argument3Carrier));
    }

    int STEXT::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        return dispatchTextActionOpcode(opcode, argument1Carrier,
                          static_cast<std::intptr_t>(argument2Carrier),
                          static_cast<std::intptr_t>(argument3Carrier));
    }

    void STEXT::Draw()
    {
        const char* const initialText = m_text;
        if (std::strcmp(initialText, kEmptyString) == 0)
            return;

        VID* fontVid = Vid();
        if (fontVid && fontVid != MAP::NullVid() && fontVid->directionCount() > 0x7E)
        {
            const int savedFrame = currentFrame();
            const float savedX = X();
            const float savedY = Y();

            if ((m_textFlags & 0x70) == 0x60)
            {
                STRING expanded = expandTextScriptExpression(STRING(m_textClass));
                assignText(expanded.c_str());
            }

            const int flags = m_textFlags;
            const float glyphWidth = fontVid->sizeX();
            const int textLength = m_textLength;

            if ((flags & 1) != 0)
            {
                setXPosition(X() - static_cast<float>(textLength - 1) * glyphWidth * 0.5f);
            }
            else if ((flags & 2) != 0)
            {
                setXPosition(X() - (static_cast<float>(textLength - 1) * glyphWidth + fontVid->halfSizeX()));
            }
            else
            {
                setXPosition(X() + fontVid->halfSizeX());
            }

            if ((flags & 8) == 0)
            {
                if ((flags & 4) != 0)
                    setYPosition(Y() - fontVid->halfSizeY());
                else
                    setYPosition(Y() + fontVid->halfSizeY());
            }

            float lineStartX = X() - fontVid->sizeX();
            VID* currentFont = fontVid;
            const core::ApplicationVidTable& vidTable = core::GlobalApplicationVidTable();
            auto resolveFont = [&](int nvid) -> VID*
            {
                if (nvid >= 0 && nvid < vidTable.count())
                {
                    if (VID* const resolved = vidTable.slot(nvid))
                        return resolved;
                }
                return MAP::NullVid();
            };

            int i = 0;
            while (i < m_textLength)
            {
                const char* const text = m_text;
                const unsigned char ch = static_cast<unsigned char>(text[i]);

                if (ch == '\n')
                {
                    setXPosition(lineStartX);
                    setYPosition(Y() + currentFont->sizeY());
                }
                else if (ch == '\r')
                {
                    setXPosition(lineStartX);
                }
                else if (ch == '\t')
                {
                    setXPosition(X() + currentFont->sizeX() * 7.0f);
                }
                else if (ch == '<' && std::strncmp(text, "<Font=", std::strlen("<Font=")) == 0)
                {
                    // Retail compares the beginning of +0x70, not text+i.
                    STRING fontText;
                    constructRightOfFirstMarker(STRING(text), fontText, "<Font=");
                    const int fontNVid = script::ParseStackIntegerText(fontText.c_str());
                    currentFont = resolveFont(fontNVid);
                    setXPosition(X() - currentFont->sizeX());

                    STRING throughClose;
                    constructLeftOfFirstMarker(STRING(text), throughClose, ">");
                    i += throughClose.Length();
                }
                else if (ch == 0x1B && i + 1 < m_textLength)
                {
                    const int unsignedFontIndex = static_cast<unsigned char>(text[i + 1]);
                    if (unsignedFontIndex < vidTable.count() &&
                        vidTable.slot(unsignedFontIndex) != nullptr)
                    {
                        ++i;
                        const int signedFontNVid = static_cast<signed char>(text[i]);
                        currentFont = resolveFont(signedFontNVid);
                        setXPosition(X() - currentFont->sizeX());
                    }
                }
                else if (ch >= 0x20)
                {
                    setCurrentFrameDirect(static_cast<int>(ch));
                    currentFont->Draw(this);
                }

                ++i;
                setXPosition(X() + currentFont->sizeX());
            }

            setXPosition(savedX);
            setYPosition(savedY);
            setCurrentFrameDirect(savedFrame);
            return;
        }

        (void)GRAPH::CurrentGraph()->drawTextColored(X(), Y(), m_text, 0xFFFFFFFFu);
    }

    STRING STEXT::expandTextScriptExpression(const STRING& expression) const
    {
        return core::ApplicationScriptRuntime()->getVariableString(expression);
    }

    int STEXT::dispatchTextActionOpcode(int opcode, std::intptr_t actionArgument1, std::intptr_t actionArgument2, std::intptr_t actionArgument3)
    {
        switch (opcode)
        {
        case 0x50:
        {
            auto* stream = reinterpret_cast<BaseStream*>(actionArgument1);
            dispatchFrameActionOpcode(opcode, actionArgument1, actionArgument2, actionArgument3);
            stream->write_new(&m_textFlags, sizeof(m_textFlags));
            STRING(m_textClass).Write(stream);
            return 0;
        }

        case 0x51:
        {
            auto* stream = reinterpret_cast<BaseStream*>(actionArgument1);
            dispatchFrameActionOpcode(opcode, actionArgument1, actionArgument2, actionArgument3);
            stream->read_new(&m_textFlags, sizeof(m_textFlags));
            STRING tmpClass;
            readStringLineFromStream(tmpClass, stream);

            return dispatchTextActionOpcode(0x78, reinterpret_cast<std::intptr_t>(&tmpClass), 0, 0);
        }

        case 0x5E:
            return m_textFlags;

        case 0x5F:
            m_textFlags = static_cast<int>(actionArgument1);
            return 0;

        case 0x78:
        {
            const STRING* const requestedClassOwner =
                reinterpret_cast<const STRING*>(actionArgument1);
            const STRING requestedClassCopy(
                requestedClassOwner ? requestedClassOwner->c_str() : kEmptyString);
            const char* const requestedClass = requestedClassCopy.c_str();

            assignTextClass(requestedClass);

            const int mode = m_textFlags & 0x70;

            if (mode == 0x10)
            {
                STRING section("menu");
                STRING key(m_textClass);
                STRING defaultValue(m_textClass);
                STRING out;
                core::profile_p::readProfileStringInto(out, core::StartupStringsIniPath(), section, key, defaultValue);
                assignText(out.c_str());
                return 0;
            }

            if (mode == 0x20)
            {
                STRING loaded;
                loadStringFromFile(loaded, reinterpret_cast<const STRING*>(&m_textClass));
                assignText(loaded.c_str());
                return 0;
            }

            if (mode == 0)
            {
                assignText(m_textClass);
                return 0;
            }

            // Retail modes 0x40/0x50: expand [STEXT+0x74] through
            // Application+0x14C::getVariableString first, then optionally load the
            // resulting file or resolve it through the "menu" profile section.
            STRING expanded = expandTextScriptExpression(STRING(m_textClass));
            assignText(expanded.c_str());

            if (mode == 0x40)
            {
                STRING loaded;
                loadStringFromFile(loaded, reinterpret_cast<const STRING*>(&m_text));
                assignText(loaded.c_str());
                return 0;
            }

            if (mode == 0x50)
            {
                STRING section("menu");
                STRING key(m_text);
                STRING defaultValue(m_text);
                STRING out;
                core::profile_p::readProfileStringInto(out, core::StartupStringsIniPath(), section, key, defaultValue);
                assignText(out.c_str());
                return 0;
            }

            m_textLength = static_cast<int>(std::strlen(m_text));
            return 0;
        }

        case 0x79:
            return static_cast<int>(reinterpret_cast<std::intptr_t>(&m_textClass));

        case 0x7A:
            m_textLength = static_cast<int>(actionArgument1);
            return 0;

        case 0x7B:
        {
            STRING tmp;
            loadStringFromFile(tmp, reinterpret_cast<const STRING*>(actionArgument1));
            assignText(tmp.c_str());
            return 0;
        }

        case 0x82:
        {
            const char* const text = m_text;
            if (std::strcmp(text, kEmptyString) == 0)
                return 0;
            if (actionTimer() != 0)
                return 0;

            const int textLength = static_cast<int>(static_cast<std::int16_t>(std::strlen(text)));
            int cursor = m_textLength;
            if (cursor < textLength)
            {
                m_textLength = cursor + 1;
                const int ch = static_cast<int>(static_cast<signed char>(text[cursor]));
                if (!std::isspace(ch))
                {
                    ChangeAnimation(1);
                    return 0;
                }

                int sawNewLine = 0;
                while (m_textLength < textLength)
                {
                    cursor = m_textLength;
                    if (text[cursor] == '\n')
                        sawNewLine = 1;
                    m_textLength = cursor + 1;
                    const int nextCh = static_cast<int>(static_cast<signed char>(text[cursor]));
                    if (!std::isspace(nextCh))
                        break;
                }

                if (sawNewLine)
                {
                    if (m_textLength < textLength)
                        --m_textLength;
                    setActionTimer(0x96u);
                    ChangeAnimation(2);
                    return 0;
                }

                ChangeAnimation(1);
                return 0;
            }

            if (currentAnimation() != 0)
                ChangeAnimation(0);
            return 0;
        }

        default:
            return dispatchFrameActionOpcode(opcode, actionArgument1, actionArgument2, actionArgument3);
        }
    }

    void SPRITE::initializeAnimationRouteFromVid()
    {
        if (!m_vid)
        {
            m_currentAnimation = 0;
            return;
        }

        if (m_vid->noAnimCadr[0] == 0 && m_vid->noAnimCadr[15] != 0)
            m_currentAnimation = 15;
        else if (m_vid->noAnimCadr[14] != 0 && (core::ApplicationFlags() & application_flags::MapLoading) == 0u)
            m_currentAnimation = 14;
        else
            m_currentAnimation = 0;

        const int baseFrame = static_cast<int>(m_vid->animationBaseFrame[m_currentAnimation]);
        m_currentFrameBegin = baseFrame;
        m_currentFrame = baseFrame;
        m_currentFrameEnd = static_cast<int>(m_vid->animationFrameCount[m_currentAnimation]) - 1;
    }

    void SPRITE::attachChildSprite(SPRITE* child)
    {
        if (!child || child == this)
            return;

        if (insertChildChainHead(child) != 0)
            return;

    }

    int SPRITE::insertChildChainHead(SPRITE* child)
    {
        if (!child)
            return 1;
        if (child->childBacklink())
            return 1;

        SPRITE* oldHead = childChain();
        if (oldHead)
        {
            oldHead->setChildBacklink(nullptr);
            child->appendChildChain(oldHead);
        }

        setChildChain(child);
        child->setChildBacklink(this);
        return 0;
    }

    int SPRITE::appendChildChain(SPRITE* child)
    {
        if (!child)
            return 1;
        if (child->childBacklink())
            return 1;

        SPRITE* node = this;
        while (SPRITE* next = node->childChain())
            node = next;

        node->setChildChain(child);
        child->setChildBacklink(node);
        return 0;
    }

    void SPRITE::detachFromChildChain()
    {
        SPRITE* next = childChain();
        if (next)
            next->setChildBacklink(childBacklink());

        SPRITE* previous = childBacklink();
        if (previous)
        {
            previous->setChildChain(childChain());
            setChildBacklink(nullptr);
        }

        setChildChain(nullptr);
    }

    int SPRITE::deleteChildByVid(VID* childVid)
    {
        SPRITE* previous = this;
        SPRITE* child = childChain();

        while (child)
        {
            if (child->Vid() == childVid)
                break;
            previous = child;
            child = child->childChain();
        }

        if (!child)
            return 0;

        SPRITE* const next = child->childChain();
        previous->setChildChain(next);
        if (next)
            next->setChildBacklink(previous);

        child->setChildChain(nullptr);
        child->setChildBacklink(nullptr);

        DeleteSpriteThroughVirtualDeletingDestructor(child);
        return 1;
    }

    void SPRITE::initializeActionAuxState(SPRITE* ownerSprite) noexcept
    {
        SPRITE* const source = ownerSprite;

        m_actionAuxState->commandMask0 = 0;
        m_actionAuxState->commandMask1 = 0;
        hostState().actionAuxCommandMask = {0, 0};
        m_actionAuxState->state = 1;

        WEAPON* const weapon = source->m_vid->weaponRecord();
        std::uint32_t weaponValue = 0;
        std::memcpy(&weaponValue, weapon->raw.data() + 0x28, sizeof(weaponValue));
        m_actionAuxState->primaryValue = weaponValue;
        m_actionAuxState->sourceX = source->m_xyz.x;
        m_actionAuxState->sourceY = source->m_xyz.y;
        m_actionAuxState->sourceZ = source->m_xyz.z;
    }

    bool SPRITE::ensureActionAuxStateForLocalAction() noexcept
    {
        if (m_actionAuxState)
            return true;

        void* storage = ::operator new(sizeof(ActionAuxState), std::nothrow);
        if (!storage)
            return false;

        m_actionAuxState = static_cast<ActionAuxState*>(storage);
        initializeActionAuxState(this);
        return m_actionAuxState != nullptr;
    }

    void SPRITE::dispatchSpriteCommandMask(const std::uint32_t* commandMask) noexcept
    {
        if (!m_actionAuxState)
        {
            void* storage = ::operator new(sizeof(ActionAuxState), std::nothrow);
            if (storage)
            {
                m_actionAuxState = static_cast<ActionAuxState*>(storage);
                initializeActionAuxState(this);
            }
        }

        m_actionAuxState->commandMask0 = commandMask[0];
        m_actionAuxState->commandMask1 = commandMask[1];
        hostState().actionAuxCommandMask[0] = commandMask[0];
        hostState().actionAuxCommandMask[1] = commandMask[1];
    }

    int SPRITE::dispatchVirtualAction(std::uint32_t opcode, int argument1, int argument2, int argument3) noexcept
    {
        // Retail vtable +0x04 receives exactly four raw DWORD arguments after this.
        return Action(static_cast<int>(opcode), static_cast<std::intptr_t>(argument1), argument2, argument3);
    }

    void SPRITE::setGoalSprite(SPRITE* goal) noexcept
    {
        SPRITE* const oldOwner = m_goalSprite;
        if (oldOwner == goal)
            return;

        if (oldOwner)
        {
            const int nextRef = oldOwner->listReferenceCount() - 1;
            oldOwner->setListReferenceCount(nextRef);
            if (nextRef > 0)
            {
            }
            else if (nextRef == 0)
            {
                DeleteSpriteThroughVirtualDeletingDestructor(oldOwner);
            }
            else
            {
                VID* const oldVid = oldOwner->Vid();
                const int oldNvid = oldVid ? oldVid->nVid : -1;
                LOG::ResourceError("SPRITE %i", 4, "noRef	at Release", nextRef, oldNvid);
            }
        }

        m_goalSprite = goal;
        if (goal)
            goal->setListReferenceCount(goal->listReferenceCount() + 1);
    }

    void SPRITE::advanceAnimationFrameTimeCapped(int delta) noexcept
    {
        const std::uint32_t now = core::CurrentTimeMilliseconds();
        if ((now & 0xFFFFFC00u) <= core::PreviousWorldTimeMilliseconds())
            return;

        const int currentFrame = animationFrameTime();
        if (currentFrame <= 0)
            return;

        VID* const vid = Vid();
        const int bucket = armyIndex();
        const int duration = vid->animationFrameDuration(bucket);
        // Retail ADD is a low-32 register operation.  Preserve wrap rather
        // than invoking C++ signed-overflow UB for corrupted/extreme state.
        const std::uint32_t nextBits =
            static_cast<std::uint32_t>(currentFrame) + static_cast<std::uint32_t>(delta);
        int nextFrame = static_cast<std::int32_t>(nextBits);
        if (nextFrame > duration)
            nextFrame = duration;
        updateAnimationFrameTime(nextFrame);
    }

    int SPRITE::SetCommand(int argument1, SPRITE* goal) noexcept
    {
        if ((m_runtimeFlags & SPRITE::CommandBitsMask) == 0x48u && argument1 != 0x12)
            m_actionTimer = 0;

        setGoalSprite(goal);

        if (SPRITE* const child = m_childChain)
        {
            VID* const childVid = child->Vid();
            if (childVid == m_vid->linkedVid() &&
                childVid->hasWeaponChildDescriptor() != 0u &&
                childVid->weaponCount() != 0u)
            {
                child->SetCommand(argument1, goal);
            }
        }

        const DWORD preserved = m_runtimeFlags & ~CommandBitsMask;
        if (argument1 < 0x10 && m_goalSprite == nullptr)
        {
            m_runtimeFlags = preserved;
            return 1;
        }

        const DWORD commandBits = (static_cast<DWORD>(argument1) & CommandValueMask) << CommandBitsShift;
        m_runtimeFlags = preserved | commandBits;
        return 0;
    }

    int SPRITE::SetCommandWithoutLink(int argument1, SPRITE* goal) noexcept
    {
        if ((m_runtimeFlags & SPRITE::CommandBitsMask) == 0x48u && argument1 != 0x12)
            m_actionTimer = 0u;

        setGoalSprite(goal);

        const DWORD preserved = m_runtimeFlags & ~CommandBitsMask;
        if (argument1 < 0x10 && m_goalSprite == nullptr)
        {
            m_runtimeFlags = preserved;
            return 1;
        }

        m_runtimeFlags = preserved | ((static_cast<DWORD>(argument1) & CommandValueMask) << CommandBitsShift);
        return 0;
    }

    int SPRITE::Move(SPRITE* goal) noexcept
    {
        int result = SetCommandWithoutLink(1, goal);
        if (result == 0)
        {
            result = StartMove();
            if (result == 0)
                return SetCommand(0, nullptr);
        }

        SPRITE* const child = m_childChain;
        if (!child)
            return result;

        VID* const childVid = child->Vid();
        result = static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(childVid)));
        if (childVid == m_vid->linkedVid())
        {
            if (childVid->hasWeaponChildDescriptor() != 0u)
            {
                if (childVid->weaponCount() != 0u)
                {
                    result = static_cast<int>(child->runtimeFlags() & SPRITE::CommandBitsMask);
                    if (result != 0 && result != 0x10)
                        result = child->SetCommand(0, nullptr);
                }
            }
        }
        return result;
    }

    int SPRITE::ammoCount() const noexcept
    {
        int value = ammoFixedPoint();
        const int signBits = value < 0 ? -1 : 0;
        value += (signBits & 0x3F);
        return value >> 6;
    }

    int SPRITE::addAmmoUnits(int value) noexcept
    {
        const int weaponValue = m_vid->activeWeaponAmmoCapacity();
        if (weaponValue == 999999)
        {
            setAmmoFixedPoint(63999936);
            return weaponValue;
        }

        const int delta = value * 64;
        setAmmoFixedPoint(ammoFixedPoint() + delta);
        const int returned = ammoFixedPoint();
        if (ammoFixedPoint() < 0)
            setAmmoFixedPoint(0);
        if (forceAmmoCapacityAfterAdd())
            setAmmoFixedPoint(m_vid->activeWeaponAmmoCapacity() << 6);
        return returned;
    }

    int SPRITE::refillAmmoByCapacityFraction(int divisor) noexcept
    {
        VID* const vid = Vid();
        VID* metricVid = vid;
        if (VID* const link = vid->linkedVid())
        {
            if (link->hasWeaponChildDescriptor() != 0u && link->weaponCount() != 0u)
                metricVid = link;
        }
        const int units = metricVid->weaponRecordAmmoCapacity();

        const int maxFixed = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(units) << 6u);
        if (maxFixed == 0 || maxFixed <= ammoFixedPoint())
            return 0;

        int next = maxFixed;
        if (divisor != 0)
        {
            next = spriteAdd32Wrap(ammoFixedPoint(), maxFixed / divisor);
            if (next > maxFixed)
                next = maxFixed;
        }
        setAmmoFixedPoint(next);
        return 1;
    }

    int SPRITE::ammoMissingPercent() const noexcept
    {
        VID* const vid = Vid();
        VID* metricVid = vid;
        if (VID* const link = vid->linkedVid())
        {
            if (link->hasWeaponChildDescriptor() != 0u && link->weaponCount() != 0u)
                metricVid = link;
        }
        const int capacity = metricVid->weaponRecordAmmoCapacity();
        if (capacity == 0)
            return 0;
        // Retail builds 100*value through LEA/SHL, i.e. low-32 arithmetic.
        const int scaled = spriteImul32Low(ammoCount(), 100);
        return spriteSub32Wrap(100, scaled / capacity);
    }

    int SPRITE::animationRemainingPercent() const noexcept
    {
        VID* const vid = Vid();
        const int bucket = armyIndex();
        const int frame = animationFrameTime();
        const int ownDuration = vid->animationFrameDuration(bucket);
        VID* const link = vid->linkedVid();
        SPRITE* const child = childChain();
        const bool linkedChild = link && child && child->Vid() == link;

        if (frame < ownDuration)
        {
            int total = ownDuration;
            if (link && !linkedChild)
                total = spriteAdd32Wrap(total, link->animationFrameDuration(bucket));
            const int scaledFrame = spriteImul32Low(frame, 100);
            return spriteSub32Wrap(100, scaledFrame / total);
        }

        if (link && !linkedChild)
            return vid->nvid() != 35 ? 50 : 0;

        if (linkedChild && child->Vid()->spriteClassId() != 9u)
        {
            const int childFrame = child->animationFrameTime();
            const int childBucket = child->armyIndex();
            const int childDuration = child->Vid()->animationFrameDuration(childBucket);
            if (childFrame < childDuration)
            {
                const int total = spriteAdd32Wrap(ownDuration, childDuration);
                const int combinedFrame = spriteAdd32Wrap(childFrame, frame);
                const int scaledFrame = spriteImul32Low(combinedFrame, 100);
                return spriteSub32Wrap(100, scaledFrame / total);
            }
        }
        return 0;
    }

    int SPRITE::appendCommandWordValue(std::uint16_t word) noexcept
    {
        m_commandStack.appendCommandWordRetail(static_cast<std::int16_t>(word));
        return 0;
    }

    void SPRITE::growCommandWordStorage() noexcept
    {
        auto& list = m_commandStack.commandWords();
        if (static_cast<std::int32_t>(list.count) < static_cast<std::int32_t>(list.capacity))
            return;

        const std::uint32_t oldCapacity = list.capacity;
        const std::uint32_t newCapacity = oldCapacity * 2u + 4u;
        if (static_cast<std::int32_t>(newCapacity) <= static_cast<std::int32_t>(oldCapacity))
            return;

        std::int16_t* const oldArray = list.words;
        std::int16_t* const newArray = static_cast<std::int16_t*>(
            ::operator new(static_cast<std::size_t>(newCapacity) * sizeof(std::int16_t), std::nothrow));
        list.words = newArray;
        if (!newArray)
            fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", static_cast<int>(newCapacity));

        if (oldArray)
        {
            const std::int32_t signedOldCapacity = static_cast<std::int32_t>(oldCapacity);
            for (std::int32_t i = 0; i < signedOldCapacity; ++i)
                newArray[static_cast<std::uint32_t>(i)] = oldArray[static_cast<std::uint32_t>(i)];
            ::operator delete(oldArray);
        }
        list.capacity = newCapacity;
    }

    int SPRITE::commandWordAt(int index) const noexcept
    {
        if (index < 0)
            return 0;
        const std::uint32_t rawIndex = static_cast<std::uint32_t>(index);
        const std::int16_t* words = commandWordData();
        if (!words || rawIndex >= commandWordCount())
            return 0;
        return static_cast<int>(words[rawIndex]);
    }

    int SPRITE::removeCommandWordValue(std::uint16_t word) noexcept
    {
        std::int16_t localWord = static_cast<std::int16_t>(word);
        const int index = m_commandStack.findLastCommandWordPointer(&localWord);
        const int removeFailed = m_commandStack.removeCommandWordAt(index);
        return removeFailed ? 0 : 1;
    }

    int SPRITE::hasCommandOpcode(std::uint32_t opcode) const noexcept
    {
        const auto& owner = m_commandStack.m_commandRecords;
        for (std::uint32_t i = 0; i < owner.count; ++i)
        {
            if (owner.records[i].words[0] == opcode)
                return 1;
        }
        return 0;
    }

    void SPRITE::appendCommandRecord(const SpriteCommandRecord& command)
    {

        m_commandStack.appendCommandRecord(command);
    }

    void SPRITE::prependCommandRecord(const SpriteCommandRecord& command)
    {
        // Retail prependCommandRecord receives the embedded 16-byte command-list owner
        // at SPRITE+0x58.  Keep the address owner explicit while the current
        // C++ carrier remains SpriteCommandStack.
        m_commandStack.prependCommandRecord(command);
    }

    std::uint32_t SPRITE::lastCommandOpcode() const noexcept
    {
        const auto& owner = m_commandStack.m_commandRecords;
        if (owner.count == 0u)
            return 0u;
        return owner.records[owner.count - 1u].words[0];
    }

    int SPRITE::clearCommandWordList() noexcept
    {
        auto& list = m_commandStack.commandWords();
        std::int16_t* const old = list.words;
        list.capacity = 0;
        list.count = 0;
        if (old)
            ::operator delete(old);
        list.words = nullptr;
        return 0;
    }

    int SPRITE::dispatchBaseActionOpcode(int opcode, int argument1, int argument2, int argument3) noexcept
    {
        const int op = opcode;
        switch (op)
        {
        case static_cast<int>(ActionCode::ACT_ADD_AMMO):
        {
            const int weaponValue = m_vid->activeWeaponAmmoCapacity();
            if (weaponValue == 999999)
            {
                setAmmoFixedPoint(63999936);
                return weaponValue;
            }

            const std::uint32_t sum = static_cast<std::uint32_t>(ammoFixedPoint()) +
                (static_cast<std::uint32_t>(argument1) << 6);
            setAmmoFixedPoint(static_cast<std::int32_t>(sum));
            const int result = ammoFixedPoint();
            if (ammoFixedPoint() < 0)
                setAmmoFixedPoint(0);
            if (forceAmmoCapacityAfterAdd())
                setAmmoFixedPoint(static_cast<std::int32_t>(static_cast<std::uint32_t>(m_vid->activeWeaponAmmoCapacity()) << 6));
            return result;
        }

        case static_cast<int>(ActionCode::ACT_GET_AMMO):
            return ammoCount();

        case static_cast<int>(ActionCode::ACT_SET_BEHAVE):
            setBehaviorFlags(argument1);
            if (m_childChain)
                m_childChain->dispatchVirtualAction(static_cast<std::uint32_t>(op), argument1, argument2, argument3);
            return 0;

        case static_cast<int>(ActionCode::ACT_GET_BEHAVE):
            return behaviorFlags();

        case static_cast<int>(ActionCode::ACT_ADD_ITEM):
        {
            auto& list = m_commandStack.commandWords();
            if (static_cast<std::int32_t>(list.count) >= static_cast<std::int32_t>(list.capacity))
                m_commandStack.ensureCommandWordCapacityRetail(list.capacity * 2u + 4u);
            list.words[list.count++] = static_cast<std::int16_t>(argument1);
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_GET_ITEM):
        {
            if (argument1 < 0 || argument1 >= static_cast<std::int32_t>(m_commandStack.commandWords().count))
                return 0;
            return static_cast<int>(m_commandStack.commandWords().words[argument1]);
        }

        case static_cast<int>(ActionCode::ACT_HAVE_ITEM):
        {
            std::uint32_t index = m_commandStack.commandWords().count;
            if (index == 0)
                return 0;
            const std::int16_t* cursor = m_commandStack.commandWords().words + index;
            do
            {
                --cursor;
                --index;
                if (static_cast<std::uint16_t>(*cursor) == static_cast<std::uint16_t>(argument1))
                    return static_cast<std::int32_t>(index) >= 0 ? 1 : 0;
            } while (index != 0);
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_DELETE_ITEM):
        {
            const std::int16_t word = static_cast<std::int16_t>(argument1);
            const int index = m_commandStack.findLastCommandWordPointer(&word);
            return m_commandStack.removeCommandWordAt(index) == 0 ? 1 : 0;
        }

        case static_cast<int>(ActionCode::ACT_DELETE_ALL_ITEM):
        {
            auto& list = m_commandStack.commandWords();
            std::int16_t* const old = list.words;
            list.capacity = 0;
            list.count = 0;
            if (old)
                ::operator delete(old);
            list.words = nullptr;
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_NEXT_COMMAND):
        {
            if (m_currentAnimation >= 15)
                return 0;

            if (spriteFcompC3(m_speed, 0.0f))
                ChangeAnimation(0);
            else if (m_currentAnimation != 2)
                ChangeAnimation(2);

            const DWORD commandBits = m_runtimeFlags & SPRITE::CommandBitsMask;
            if (commandBits == 0x0Cu)
            {
                SPRITE* const child = m_childChain;
                if (child)
                {
                    VID* const childVid = child->m_vid;
                    if (childVid == m_vid->linkedVid() &&
                        childVid->hasWeaponChildDescriptor() != 0u &&
                        childVid->weaponCount() != 0u &&
                        m_goalSprite != nullptr &&
                        child->m_goalSprite == nullptr)
                    {
                        child->SetCommand(3, m_goalSprite);
                    }
                }
            }

            if (commandBits == 0x10u)
            {
                SPRITE* const child = m_childChain;
                if (child)
                {
                    VID* const childVid = child->m_vid;
                    if (childVid == m_vid->linkedVid() &&
                        childVid->hasWeaponChildDescriptor() != 0u &&
                        childVid->weaponCount() != 0u &&
                        m_goalSprite != nullptr &&
                        child->m_goalSprite == nullptr)
                    {
                        child->SetCommand(4, m_goalSprite);
                    }
                }
            }

            std::uint32_t delta = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
            std::uint32_t decisionDelta = static_cast<std::uint32_t>(m_vid->defaultFrameSpeed());
            if (delta > decisionDelta)
                decisionDelta = delta;

            const int decision = computeAttackDecisionCode(decisionDelta);
            setAttackDecisionCode(decision);

            if (decision == 6)
            {
                SetCommand(0, nullptr);
            }
            else if (decision == 1)
            {
                if (spriteFcompC3(m_vid->maxSpeedValue(), 0.0f))
                {
                    SPRITE* const child = m_childChain;
                    if (child)
                    {
                        VID* const childVid = child->m_vid;
                        if (childVid == m_vid->linkedVid() &&
                            childVid->hasWeaponChildDescriptor() != 0u &&
                            childVid->weaponCount() != 0u &&
                            child->m_goalSprite != nullptr)
                        {
                            SetCommand(0, nullptr);
                        }
                        else if (m_goalSprite != nullptr)
                        {
                            SetCommand(0, nullptr);
                        }
                    }
                    else if (m_goalSprite != nullptr)
                    {
                        SetCommand(0, nullptr);
                    }
                }
                else if (spriteFcompC3(m_speed, 0.0f))
                {
                    StartMove();
                }

                const DWORD postBits = m_runtimeFlags & SPRITE::CommandBitsMask;
                if (postBits == 0x0Cu || postBits == 0x10u)
                {
                    SPRITE* const child = m_childChain;
                    if (child)
                    {
                        VID* const childVid = child->m_vid;
                        if (childVid == m_vid->linkedVid() &&
                            childVid->hasWeaponChildDescriptor() != 0u &&
                            childVid->weaponCount() != 0u &&
                            (behaviorFlags() & 1) != 0)
                        {
                            if (child->m_actionTimer != 0u || (std::rand() % 4) == 0)
                            {
                                if (SPRITE* const target = SeekEnemy())
                                    child->SetCommand(4, target);
                            }
                        }
                    }
                }
            }
            else
            {
                bool outerCondition = decision != 0 || spriteFcompC3(m_speed, 0.0f);
                if (!outerCondition)
                {
                    SPRITE* const child = m_childChain;
                    if (child)
                    {
                        VID* const childVid = child->m_vid;
                        if (childVid == m_vid->linkedVid() &&
                            childVid->hasWeaponChildDescriptor() != 0u &&
                            childVid->weaponCount() != 0u &&
                            m_goalSprite != child->m_goalSprite)
                        {
                            outerCondition = true;
                        }
                    }
                }

                if (!outerCondition)
                {
                    Stop();
                }
                else if (decision == 2 && (behaviorFlags() & 2) != 0 && spriteFcompC3(m_speed, 0.0f))
                {
                    StartMove();
                }
            }

            if (decision == 5)
            {
                const int behavior = behaviorFlags();
                if ((behavior & 1) != 0)
                {
                    if ((behavior & 2) != 0)
                    {
                        if (SPRITE* const target = SeekEnemy())
                            SetCommand(4, target);
                    }
                    else
                    {
                        SPRITE* const child = m_childChain;
                        if (child)
                        {
                            VID* const childVid = child->m_vid;
                            if (childVid == m_vid->linkedVid() &&
                                childVid->hasWeaponChildDescriptor() != 0u &&
                                childVid->weaponCount() != 0u)
                            {
                                if (SPRITE* const target = SeekEnemy())
                                    child->SetCommand(4, target);
                            }
                        }
                    }
                }
            }

            if (decision == 2 || decision == 4)
            {
                if ((behaviorFlags() & 1) != 0)
                {
                    const DWORD postBits = m_runtimeFlags & SPRITE::CommandBitsMask;
                    if (postBits == 0u || postBits == 4u || postBits == 0x10u)
                    {
                        bool acquire = m_currentFrameEnd > m_currentFrameBegin;
                        if (!acquire)
                        {
                            SPRITE* const child = m_childChain;
                            std::uint32_t timer = m_actionTimer;
                            if (child)
                            {
                                VID* const childVid = child->m_vid;
                                if (childVid == m_vid->linkedVid() &&
                                    childVid->hasWeaponChildDescriptor() != 0u &&
                                    childVid->weaponCount() != 0u)
                                {
                                    timer = child->m_actionTimer;
                                }
                            }
                            acquire = timer != 0u || (std::rand() % 11) == 0;
                        }

                        if (acquire)
                        {
                            if (SPRITE* const target = SeekEnemy())
                                SetCommand(4, target);
                        }
                    }
                }
            }

            if ((m_runtimeFlags & SPRITE::CommandBitsMask) != 0u ||
                m_goalSprite != nullptr ||
                m_actionTimer != 0u)
            {
                return 0;
            }

            Stop();
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_PATH_LIMIT):
            Stop();
            return 0;

        case static_cast<int>(ActionCode::ACT_DAMAGE):
        {
            const int result = dispatchExtendedSpriteActionOpcode(op, argument1, argument2, argument3);
            if (argument1 >= 0 && (m_runtimeFlags & SPRITE::CommandBitsMask) == 0u &&
                !spriteFcompC3(m_vid->maxSpeedValue(), 0.0f))
            {
                const int direction = std::rand() % 256;
                const float x = m_xyz.x + rawDirectionSin(direction) * 64.0f;
                const float y = m_xyz.y - rawDirectionCos(direction) * 64.0f;
                SPRITE* const helper = new (std::nothrow) SPRITE(hostState().owner, MAP::NullVid(), VECTOR(x, y, m_xyz.z), ANGLE(0), nullptr);
                Move(helper);
            }
            return result;
        }

        case static_cast<int>(ActionCode::ACT_PATH_BLOCK):
        {
            const float candidateZ = m_xyz.z + static_cast<float>(argument3);
            const float candidateX = m_xyz.x + static_cast<float>(argument1);
            if (!CanPlaceWithCrush(candidateX, m_xyz.y, candidateZ))
            {
                ChangeCoor(candidateX, m_xyz.y, candidateZ);
                return 0;
            }

            const float candidateY = m_xyz.y + static_cast<float>(argument2);
            if (!CanPlaceWithCrush(m_xyz.x, candidateY, candidateZ))
            {
                ChangeCoor(m_xyz.x, candidateY, candidateZ);
                return 0;
            }

            if (turnTimer() == 0)
                setTurnTimer(10);
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_REPAIR):
            setAmmoFixedPoint(static_cast<std::int32_t>(static_cast<std::uint32_t>(m_vid->activeWeaponAmmoCapacity()) << 6));
            return dispatchExtendedSpriteActionOpcode(op, argument1, argument2, argument3);

        case static_cast<int>(ActionCode::ACT_SAVE):
        {
            dispatchExtendedSpriteActionOpcode(op, argument1, argument2, argument3);
            BaseStream* const stream = reinterpret_cast<BaseStream*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            const int rawBehavior8C = behaviorFlags();
            stream->write(&rawBehavior8C, 4u);
            auto& list = m_commandStack.commandWords();
            stream->write(&list.count, 4u);
            stream->write(list.words, list.count << 1);
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_RESTORE):
        {
            dispatchExtendedSpriteActionOpcode(op, argument1, argument2, argument3);
            BaseStream* const stream = reinterpret_cast<BaseStream*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            int rawBehavior8C = 0;
            stream->read(&rawBehavior8C, 4u);
            setBehaviorFlags(rawBehavior8C);
            auto& list = m_commandStack.commandWords();
            stream->read(&list.count, 4u);
            const std::uint32_t count = list.count;
            if (static_cast<std::int32_t>(count) > static_cast<std::int32_t>(list.capacity))
                m_commandStack.ensureCommandWordCapacityRetail(count);
            stream->read(list.words, list.count << 1);
            return 0;
        }

        case SpriteActConst::ACT_RESTORE_OLD_MAP:
        {
            dispatchExtendedSpriteActionOpcode(op, argument1, argument2, argument3);
            BaseStream* const stream = reinterpret_cast<BaseStream*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            const int mapVersion = argument2;
            if (mapVersion < 7)
            {
                std::uint8_t bucket = 0;
                stream->read(&bucket, 1u);
                changeArmyBucket(static_cast<int>(bucket));
            }

            unsigned char rawBehavior8C = 0;
            stream->read(&rawBehavior8C, 1u);
            setBehaviorFlags(static_cast<int>(rawBehavior8C));
            auto& list = m_commandStack.commandWords();
            if (mapVersion >= 6)
            {
                std::int16_t signedCount = 0;
                stream->read(&signedCount, 2u);
                const int count = static_cast<int>(signedCount);
                list.count = static_cast<std::uint32_t>(count);
                if (count > static_cast<int>(list.capacity))
                    m_commandStack.ensureCommandWordCapacityRetail(static_cast<std::uint32_t>(count));
                stream->read(list.words, list.count << 1);
            }

            if (mapVersion < 7)
            {
                std::int16_t* const old = list.words;
                list.capacity = 0;
                list.count = 0;
                if (old)
                    ::operator delete(old);
                list.words = nullptr;
            }

            if (mapVersion == 6)
            {
                std::uint8_t ignored = 0;
                stream->read(&ignored, 1u);
            }
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_SET_ARMY):
        {
            const int oldBucket = armyIndex();
            changeArmyBucket(static_cast<std::int8_t>(argument1));
            const int newBucket = armyIndex();
            if (oldBucket != newBucket && m_vid->nvid() == 104)
            {
                const int selfArg = static_cast<int>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFFFFu);
                (void)core::Application::callScriptFunction(core::scriptCallbackSlot(15u), selfArg, 0);
            }
            return 0;
        }

        default:
            return dispatchExtendedSpriteActionOpcode(op, argument1, argument2, argument3);
        }
    }

    int SPRITE::extendedStateValue(int index) const noexcept
    {
        // Retail private class-7 stores this bank physically at
        // this+0x78+index*4 (indices selected from LinkVid NVID).  UNIT's
        // +0x78..+0x9C tail and class-7's +0xA0..+0xC4 extension make the
        // whole bank contiguous on x86.  A host sidecar here breaks the
        // observable class-7 object state after ACT_CHANGE_VID.
#if UINTPTR_MAX == 0xFFFFFFFFu
        return *reinterpret_cast<const int*>(
            reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::ExtendedStateBase +
            static_cast<std::size_t>(index) * RetailSpriteLayout::WordStride);
#else
        return hostState().extendedStateValue[static_cast<std::size_t>(index)];
#endif
    }

    int SPRITE::setExtendedStateValue(int index, int value) noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::ExtendedStateBase +
            static_cast<std::size_t>(index) * RetailSpriteLayout::WordStride) = value;
#else
        hostState().extendedStateValue[static_cast<std::size_t>(index)] = value;
#endif
        return value;
    }

    int SPRITE::derivedStateValue(int index) const noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + RetailSpriteLayout::DerivedStateBase + static_cast<std::size_t>(index) * RetailSpriteLayout::WordStride);
#else

        return hostState().extendedStateValue[static_cast<std::size_t>(index + 10)];
#endif
    }

    int SPRITE::setDerivedStateValue(int index, int value) noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + RetailSpriteLayout::DerivedStateBase + static_cast<std::size_t>(index) * RetailSpriteLayout::WordStride) = value;
#else
        hostState().extendedStateValue[static_cast<std::size_t>(index + 10)] = value;
#endif
        return value;
    }

    namespace
    {
        float addIntegerToFloatRounded(float base, int addend) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            float result = 0.0f;
            __asm
            {
                fld base
                fiadd addend
                fstp result
            }
            return result;
#else

            return static_cast<float>(static_cast<long double>(base) +
                                      static_cast<long double>(addend));
#endif
        }
    }

    int SPRITE::dispatchExtendedSpriteActionOpcode(int opcode, int argument1, int argument2, int argument3) noexcept
    {
        switch (opcode)
        {
        case 0x32:
        case 0x33:
            ChangeAnimation(0);
            return 0;
        case 0x34:
            ChangeCoor(addIntegerToFloatRounded(m_xyz.x, argument1),
                       addIntegerToFloatRounded(m_xyz.y, argument2),
                       addIntegerToFloatRounded(m_xyz.z, argument3));
            ChangeAnimation(15);
            return 0;
        case 0x56:
            repairLinkedChildState(1);
            return 0;
        case 0xC8:
        {
            dispatchActionOpcode(static_cast<std::uint32_t>(opcode), argument1, argument2, argument3);
            int localArgC = argument3;
            RESOURCE* const resource = reinterpret_cast<RESOURCE*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            readResourceBytes(*resource, &localArgC, static_cast<std::size_t>((argument2 > 7) + 1));
            return 0;
        }
        default:
            return dispatchActionOpcode(static_cast<std::uint32_t>(opcode), argument1, argument2, argument3);
        }
    }

    int SPRITE::repairLinkedChildState(int createMissingLinker) noexcept
    {
        VID* const vid = m_vid;

        const int bucket = armyIndex();
        const int duration = vid->animationFrameDuration(bucket);
        updateAnimationFrameTime(duration);

        VID* const linkerVid = vid->linkedVid();
        SPRITE* const existingChild = childChain();
        const bool hasLinkerChild = existingChild && existingChild->Vid() == linkerVid;

        core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
        if (!hasLinkerChild && createMissingLinker != 0)
        {
            bool allowCreate = true;
            if (vid->nvid() == 35)
            {
                const int rawCount = table.count();
                VID* const gateA = (rawCount > 0x28) ? table.slot(0x28) : nullptr;
                VID* const gateB = (rawCount > 0x23) ? table.slot(0x23) : nullptr;
                VID* const leftVid = gateA ? gateA : MAP::NullVid();
                VID* const rightVid = gateB ? gateB : MAP::NullVid();
                const int leftCounter = leftVid->spriteCountForArmy(bucket);
                const int rightCounter = rightVid->spriteCountForArmy(bucket);
                allowCreate = leftCounter < rightCounter;
            }

            if (allowCreate)
            {
                ensureLinkedVidChild();
                const int rawCount = table.count();
                VID* const createVid = (rawCount > 0x24E) ? table.slot(0x24E) : nullptr;
                VID* const resolvedCreateVid = createVid ? createVid : MAP::NullVid();
                static_cast<void>(hostState().owner->CreateSpriteViaFactory(resolvedCreateVid, m_xyz, ANGLE(0), this, false));
                return 1;
            }
        }

        if (SPRITE* const child = childChain())
        {
            if (child->Vid() == linkerVid)
                (void)child->dispatchVirtualAction(ActionCode::ACT_REPAIR, 0, 0, 0);
        }
        return 1;
    }

    int SPRITE::dispatchPrivateClass7ActionOpcode(int opcode, int argument1, int argument2, int argument3) noexcept
    {
        switch (opcode)
        {
        case static_cast<int>(ActionCode::ACT_COOR_ATTACK):
        {
            SPRITE* const child = childChain();
            if (!child)
                return 0;

            VID* const ownVid = Vid();
            VID* const childVid = child->Vid();
            VID* const linkVid = ownVid->linkedVid();
            if (childVid != linkVid ||
                childVid->hasWeaponChildDescriptor() == 0u ||
                childVid->weaponCount() == 0u ||
                child->actionTimer() > 5000u ||
                (child->currentAnimation() == 8 && child->currentFrame() <= child->currentFrameEnd()))
            {
                return 0;
            }

            const float worldX = spriteFildToF32(argument1);
            const float worldY = spriteFildToF32(argument2);
            const auto& drawState = core::GlobalApplicationDrawDispatcherState();
            GRAPH* const graph = GRAPH::CurrentGraph();
            const float screenX = spriteFildSubF32(argument1, drawState.cameraShiftX());
            const float screenY = spriteFildSubF32(argument2, drawState.cameraShiftY());

            float height = 0.0f;
            int constructorYRaw = argument2;
            if (screenX >= static_cast<float>(graph->getViewportLeft()) &&
                screenX < static_cast<float>(graph->getViewportRight()) &&
                screenY >= static_cast<float>(graph->getViewportTop()) &&
                screenY < static_cast<float>(graph->getViewportBottom()))
            {
                const int pixelX = spriteFtolLow32(static_cast<long double>(screenX));
                const int pixelY = spriteFtolLow32(static_cast<long double>(screenY));
                const std::uint16_t* const depth = graph->softwareDepthBuffer();
                const int pitch = graph->softwareDepthPitch();
                const int pixelIndex = spriteAdd32Wrap(pixelX, spriteImul32Low(pitch, pixelY));
                height = static_cast<float>(depth[pixelIndex] >> 3) - 128.0f;
                if (height > 70.0f)
                    height = 50.0f;
            }
            else
            {
                const int weaponMode =
                    childVid->hasWeaponChildDescriptor() != 0u && childVid->weaponCount() != 0u
                        ? childVid->weaponTypeMask()
                        : ownVid->weaponTypeMask();
                if (weaponMode == 8)
                {
                    const float probeY = spriteFildAddF32(argument2, 80.0f);
                    height = mapOwner()->GetGroundZ(VECTOR2{worldX, probeY}) + 80.0f;
                }
                else
                {
                    constructorYRaw = spriteSub32Wrap(argument2, 19);
                    height = mapOwner()->GetGroundZ(VECTOR2{worldX, worldY}) + 19.0f;
                }
            }

            SPRITE* const marker = new (std::nothrow) SPRITE(
                mapOwner(),
                MAP::NullVid(),
                VECTOR{worldX, spriteFildAddF32(constructorYRaw, height), height},
                ANGLE(0),
                nullptr);
            child->SetCommand(4, marker);
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_PATH_BLOCK):
        {
            const float candidateZ = spriteFildAddF32(argument3, Z());
            const float candidateX = spriteFildAddF32(argument1, X());
            if (CanPlaceWithCrush(candidateX, Y(), candidateZ) == nullptr)
            {
                ChangeCoor(candidateX, Y(), candidateZ);
                return 0;
            }

            const float candidateY = spriteFildAddF32(argument2, Y());
            if (CanPlaceWithCrush(X(), candidateY, candidateZ) != nullptr)
            {
                setSpeedDirect(0.0f);
                return 0;
            }
            ChangeCoor(X(), candidateY, candidateZ);
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_NEXT_COMMAND):
        {
            if (currentAnimation() >= 15)
                return 0;

            SPRITE* const child = childChain();
            if (goalSprite() || (child && child->goalSprite()))
            {
                std::uint32_t delta = static_cast<std::uint32_t>(Vid()->defaultFrameSpeed());
                const std::uint32_t frameDelta = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                if (frameDelta > delta)
                    delta = frameDelta;
                setAttackDecisionCode(computeAttackDecisionCode(delta));
            }

            ChangeAnimation(x87IsZeroOrUnordered(Speed()) ? 0 : 2);
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_ADD_ITEM):
        {
            const std::uint16_t word = static_cast<std::uint16_t>(argument1);
            if (argument1 == 301 || argument1 == 235)
            {
                appendCommandWordValue(word);
                return 0;
            }

            if (findLastCommandWord(word) >= 0)
                return 0;

            growCommandWordStorage();
            appendCommandWordValue(word);
            if (argument1 < 260 || argument1 > 269)
                return 0;

            if (dispatchBaseActionOpcode(ActionCode::ACT_GET_AMMO, 0, 0, 0) != 0 &&
                argument1 - 260 <= Vid()->linkedVid()->nvid() - 10 &&
                argument1 != 260)
            {
                return 0;
            }
            switchLinkedWeaponSlot(argument1 - 260);
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_GET_AMMO):
        {
            const int index = argument1;
            if (index != 0 && index != Vid()->linkedVid()->nvid() - 10)
                return derivedStateValue(index);
            return dispatchBaseActionOpcode(ActionCode::ACT_GET_AMMO, 0, 0, 0);
        }

        case static_cast<int>(ActionCode::ACT_ADD_AMMO):
        {
            const int index = argument2;
            if (index > 9)
                return 0;
            if (index != 0 && index != Vid()->linkedVid()->nvid() - 10)
            {
                const int value = spriteAdd32Wrap(derivedStateValue(index), argument1);
                setDerivedStateValue(index, value);
                return value;
            }
            return dispatchBaseActionOpcode(ActionCode::ACT_ADD_AMMO, argument1, 0, 0);
        }

        case static_cast<int>(ActionCode::ACT_DAMAGE):
        {
            int damage = argument1;
            if (damage > 0)
            {
                VID* const ownVid = Vid();
                if (ownVid->nvid() != 350)
                {
                    for (SPRITE* child = childChain(); child; child = child->childChain())
                    {
                        const int nvid = child->Vid()->nvid();
                        if (nvid == 203 || nvid == 181)
                            return 0;
                    }

                    static constexpr int kDamagePercent[3] = {50, 70, 90};
                    for (SPRITE* child = childChain(); child; child = child->childChain())
                    {
                        const int nvid = child->Vid()->nvid();
                        if (nvid >= 200 && nvid <= 202)
                        {
                            const int percent = kDamagePercent[nvid - 200];
                            const int scaledDamage = spriteImul32Low(damage, percent);
                            const int childDamage = spriteAdd32Wrap(scaledDamage, 50) / 100;
                            child->dispatchVirtualAction(ActionCode::ACT_DAMAGE, childDamage, argument2, argument3);
                            damage = spriteAdd32Wrap(damage, scaledDamage / -100);
                        }
                    }
                }

                if (damage >= animationFrameTime() &&
                    dispatchVirtualAction(ActionCode::ACT_HAVE_ITEM, 230, 0, 0) != 0)
                {
                    static_cast<void>(dispatchVirtualAction(ActionCode::ACT_DELETE_ITEM, 230, 0, 0));
                    const int bucket = armyIndex();
                    updateAnimationFrameTime(Vid()->animationFrameDuration(bucket));

                    core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
                    VID* createVid = MAP::NullVid();
                    if (table.count() > 181)
                    {
                        if (VID* const raw = table.slot(181))
                            createVid = raw;
                    }
                    static_cast<void>(mapOwner()->CreateSpriteViaFactory(
                        createVid,
                        VECTOR{X(), Y(), Z() + 22.0f},
                        ANGLE(0),
                        this,
                        false));
                    return 0;
                }
            }
            return dispatchActionOpcode(static_cast<std::uint32_t>(ActionCode::ACT_DAMAGE), damage, argument2, argument3);
        }

        case static_cast<int>(ActionCode::ACT_CHANGE_VID):
        {
            VID* const previousVid = Vid();
            VID* const previousLink = previousVid->linkedVid();
            if (previousVid->nvid() < 20)
                setExtendedStateValue(previousLink->nvid(), dispatchBaseActionOpcode(ActionCode::ACT_GET_AMMO, 0, 0, 0));

            static_cast<void>(dispatchBaseActionOpcode(ActionCode::ACT_CHANGE_VID, argument1, argument2, argument3));

            VID* const vid = Vid();
            VID* const link = vid->linkedVid();
            if (vid->nvid() > 20)
            {
                const int weaponValue = vid->activeWeaponAmmoCapacity();
                const int currentValue = dispatchBaseActionOpcode(ActionCode::ACT_GET_AMMO, 0, 0, 0);
                static_cast<void>(dispatchBaseActionOpcode(ActionCode::ACT_ADD_AMMO, spriteSub32Wrap(weaponValue, currentValue), 0, 0));
                return 0;
            }

            int childCommandVidSlot = -1;
            if (findLastCommandWord(204u) >= 0)
                childCommandVidSlot = 200;
            else if (findLastCommandWord(205u) >= 0)
                childCommandVidSlot = 201;
            else if (findLastCommandWord(206u) >= 0)
                childCommandVidSlot = 202;

            if (childCommandVidSlot >= 0)
            {
                core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
                VID* childCommandVid = MAP::NullVid();
                if (appVidTable.count() > childCommandVidSlot)
                {
                    if (VID* const vidSlot = appVidTable.slot(childCommandVidSlot))
                        childCommandVid = vidSlot;
                }

                SPRITE* const rawHead = childChain();
                const VECTOR childXYZ(X(), Y(), Z() + vid->linkOffset().z);
                SPRITE* const created = mapOwner()->CreateSpriteViaFactory(
                    childCommandVid,
                    childXYZ,
                    Direction(),
                    nullptr,
                    false);
                static_cast<void>(rawHead->insertChildChainHead(created));
            }

            const int currentValue = dispatchBaseActionOpcode(ActionCode::ACT_GET_AMMO, 0, 0, 0);
            static_cast<void>(dispatchBaseActionOpcode(ActionCode::ACT_ADD_AMMO,
                spriteSub32Wrap(extendedStateValue(link->nvid()), currentValue), 0, 0));
            return 0;
        }

        default:
            return dispatchBaseActionOpcode(opcode, argument1, argument2, argument3);
        }
    }

    SPRITE* SPRITE::probeMovementFootprint(float x, float y) noexcept
    {
        const float mapSizeX = applicationWorldFloatAt(0x28u);
        const float mapSizeY = applicationWorldFloatAt(0x2Cu);
        if (x87LessOrUnordered(x, 0.0f) ||
            !x87LessOrUnordered(x, mapSizeX) ||
            x87LessOrUnordered(y, 0.0f) ||
            !x87LessOrUnordered(y, mapSizeY))
        {
            return this;
        }

        // The footprint probe intentionally samples ground at the CURRENT
        // sprite X/Y, not at the candidate x/y.  After FCOMPP retail tests C0:
        // GroundZ < SpriteZ (or unordered) enters CanPlace; ordered
        // GroundZ >= SpriteZ returns this.  The previous C++ gate was inverted.
        MAP* const map = mapOwner();
        const float groundZ = map->GetGroundZ(
            Vid(), VECTOR2{X(), Y()}, Direction());
        if (!x87LessOrUnordered(groundZ, Z()))
            return this;

        return CanPlace(x, y, Z());
    }

    int SPRITE::switchLinkedWeaponSlot(int value) noexcept
    {
        VID* const vid = m_vid;
        VID* const activeLinkVid = vid->linkedVid();
        if (activeLinkVid->nvid() > 20)
            return 0;

        int selectedSlot = value;
        if (selectedSlot == 10)
            selectedSlot = 0;

        int remaining = commandWordCount();
        if (remaining == 0)
            return 0;

        const std::int16_t* cursor = commandWordData() + remaining;
        const std::uint16_t selectedWord = static_cast<std::uint16_t>(selectedSlot + 260);
        bool found = false;
        do
        {
            --cursor;
            --remaining;
            if (static_cast<std::uint16_t>(*cursor) == selectedWord)
            {
                found = true;
                break;
            }
        }
        while (remaining != 0);

        if (!found)
            return 0;

        SPRITE* const child = childChain();
        if (!child || child->Vid() != activeLinkVid)
            return 0;

        const int saved = dispatchBaseActionOpcode(ActionCode::ACT_GET_AMMO, 0, 0, 0);
        setExtendedStateValue(activeLinkVid->nvid(), saved);
        child->dispatchVirtualAction(ActionCode::ACT_CHANGE_VID, selectedSlot + 10, 0, 0);

        const int nextNvid = selectedSlot + 10;
        core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
        VID* nextLinkVid = MAP::NullVid();
        if (nextNvid >= 0 && nextNvid < appVidTable.count())
        {
            if (VID* const slot = appVidTable.slot(nextNvid))
                nextLinkVid = slot;
        }
        vid->setLinkedVid(nextLinkVid);

        const int current = dispatchBaseActionOpcode(ActionCode::ACT_GET_AMMO, 0, 0, 0);
        (void)dispatchBaseActionOpcode(ActionCode::ACT_ADD_AMMO, derivedStateValue(selectedSlot) - current, 0, 0);
        return 1;
    }

    void SPRITE::playSfxAtWorldPosition(int nsfx) noexcept
    {
        const core::ApplicationDrawDispatcherState& drawState = core::GlobalApplicationDrawDispatcherState();
        GRAPH* const graph = GRAPH::CurrentGraph();
        const int graphSizeX = graph->SizeX();
        const int graphSizeY = graph->SizeY();
        const float halfScreenX = static_cast<float>(graphSizeX) * 0.5f;
        const float halfScreenY = static_cast<float>(graphSizeY) * 0.5f;
        const float soundX = m_xyz.x - drawState.cameraShiftX() - halfScreenX;
        const float soundY = m_xyz.y - m_xyz.z - drawState.cameraShiftY() - halfScreenY;
        sound::GlobalSoundEngine()->enqueueSoundRequestFromCoordinates(nsfx, soundX, soundY);

    }

    SPRITE* SPRITE::CanPlace(float x, float y, float z) noexcept
    {
        if (m_vid->movementMask() == 0)
            return nullptr;

        auto groundAtPoint = [this](float px, float py) noexcept -> float {
            return hostState().owner->GetGroundZ(VECTOR2{px, py});
        };
        auto groundAtVidFootprint = [this](float px, float py) noexcept -> float {
            return hostState().owner->GetGroundZ(m_vid, VECTOR2{px, py}, m_direction);
        };
        auto groundBlocker = []() noexcept -> SPRITE* {
            return mouseSprite();
        };

        const bool zeroZProbe = (m_vid->properties() & P_ZEROZ) != 0;
        if (zeroZProbe)
        {
            const float tolerance = m_vid->moveUpZ();
            if (tolerance < std::fabs(groundAtVidFootprint(x, y) - z))
                return groundBlocker();

            if (m_vid->spriteClassId() != 7u)
            {
                const float halfXMinus2 = m_vid->halfSizeX() - 2.0f;
                const float halfYMinus2 = m_vid->halfSizeY() - 2.0f;
                const float left = x - halfXMinus2;
                const float right = x + halfXMinus2;
                const float top = y - halfYMinus2;
                const float bottom = y + halfYMinus2;

                if (tolerance < std::fabs(groundAtPoint(left, top) - z))
                    return groundBlocker();
                if (tolerance < std::fabs(groundAtPoint(left, bottom) - z))
                    return groundBlocker();
                if (tolerance < std::fabs(groundAtPoint(right, top) - z))
                    return groundBlocker();
                if (tolerance < std::fabs(groundAtPoint(right, bottom) - z))
                    return groundBlocker();
            }
        }
        else
        {
            if (groundAtVidFootprint(x, y) > z)
                return groundBlocker();

            if (m_vid->spriteClassId() != 7u)
            {
                const float halfXMinus2 = m_vid->halfSizeX() - 2.0f;
                const float halfYMinus2 = m_vid->halfSizeY() - 2.0f;
                const float left = x - halfXMinus2;
                const float right = x + halfXMinus2;
                const float top = y - halfYMinus2;
                const float bottom = y + halfYMinus2;

                if (groundAtPoint(left, top) > z)
                    return groundBlocker();
                if (groundAtPoint(left, bottom) > z)
                    return groundBlocker();
                if (groundAtPoint(right, top) > z)
                    return groundBlocker();
                if (groundAtPoint(right, bottom) > z)
                    return groundBlocker();
            }
        }

        const float probeHalfX = m_vid->halfSizeX();
        const float probeHalfY = m_vid->halfSizeY();
        SPRITE_COLLECTOR_HASH_MAP* const spatialHash = GlobalSpriteHashMap();

        for (SPRITE* candidate = spatialHash->firstSpriteInBox(x - probeHalfX,
                                                                       y - probeHalfY,
                                                                       x + probeHalfX,
                                                                       y + probeHalfY);
             candidate;
             candidate = spatialHash->nextSpriteInBox())
        {
            if (candidate == this)
                continue;
            if (candidate->currentAnimation() >= 0x0F)
                continue;

            VID* const candidateVid = candidate->Vid();

            if (!(candidateVid->halfSizeX() + probeHalfX > std::fabs(candidate->m_xyz.x - x)))
                continue;
            if (!(candidateVid->halfSizeY() + probeHalfY > std::fabs(candidate->m_xyz.y - y)))
                continue;
            if (candidateVid->sizeZ() + candidate->m_xyz.z < z)
                continue;
            if (z + m_vid->sizeZ() < candidate->m_xyz.z)
                continue;
            if ((candidateVid->movementMask() & m_vid->movementMask()) == 0)
                continue;

            return candidate;
        }

        return nullptr;
    }

    SPRITE* SPRITE::CanPlaceWithCrush(float x, float y, float z) noexcept
    {
        if (m_vid->movementMask() == 0)
            return nullptr;

        auto groundAtPoint = [this](float px, float py) noexcept -> float {
            return hostState().owner->GetGroundZ(VECTOR2{px, py});
        };
        auto groundAtVidFootprint = [this](float px, float py) noexcept -> float {
            return hostState().owner->GetGroundZ(m_vid, VECTOR2{px, py}, m_direction);
        };
        auto groundBlocker = []() noexcept -> SPRITE* {
            return mouseSprite();
        };

        const bool zeroZProbe = (m_vid->properties() & P_ZEROZ) != 0;
        if (zeroZProbe)
        {
            const float tolerance = m_vid->moveUpZ();
            if (tolerance < std::fabs(groundAtVidFootprint(x, y) - z))
                return groundBlocker();

            if (m_vid->spriteClassId() != 7u)
            {
                const float halfXMinus2 = m_vid->halfSizeX() - 2.0f;
                const float halfYMinus2 = m_vid->halfSizeY() - 2.0f;
                const float left = x - halfXMinus2;
                const float right = x + halfXMinus2;
                const float top = y - halfYMinus2;
                const float bottom = y + halfYMinus2;

                if (tolerance < std::fabs(groundAtPoint(left, top) - z))
                    return groundBlocker();
                if (tolerance < std::fabs(groundAtPoint(left, bottom) - z))
                    return groundBlocker();
                if (tolerance < std::fabs(groundAtPoint(right, top) - z))
                    return groundBlocker();
                if (tolerance < std::fabs(groundAtPoint(right, bottom) - z))
                    return groundBlocker();
            }
        }
        else
        {
            if (groundAtVidFootprint(x, y) > z)
                return groundBlocker();

            if (m_vid->spriteClassId() != 7u)
            {
                const float halfXMinus2 = m_vid->halfSizeX() - 2.0f;
                const float halfYMinus2 = m_vid->halfSizeY() - 2.0f;
                const float left = x - halfXMinus2;
                const float right = x + halfXMinus2;
                const float top = y - halfYMinus2;
                const float bottom = y + halfYMinus2;

                if (groundAtPoint(left, top) > z)
                    return groundBlocker();
                if (groundAtPoint(left, bottom) > z)
                    return groundBlocker();
                if (groundAtPoint(right, top) > z)
                    return groundBlocker();
                if (groundAtPoint(right, bottom) > z)
                    return groundBlocker();
            }
        }

        const float probeHalfX = m_vid->halfSizeX();
        const float probeHalfY = m_vid->halfSizeY();
        SPRITE_COLLECTOR_HASH_MAP* const spatialHash = GlobalSpriteHashMap();
        for (SPRITE* candidate = spatialHash->firstSpriteInBox(x - probeHalfX,
                                                                       y - probeHalfY,
                                                                       x + probeHalfX,
                                                                       y + probeHalfY);
             candidate;
             candidate = spatialHash->nextSpriteInBox())
        {
            if (candidate == this)
                continue;
            if (candidate->currentAnimation() >= 0x0F)
                continue;

            VID* const candidateVid = candidate->Vid();

            if (!(candidateVid->halfSizeX() + probeHalfX > std::fabs(candidate->m_xyz.x - x)))
                continue;
            if (!(candidateVid->halfSizeY() + probeHalfY > std::fabs(candidate->m_xyz.y - y)))
                continue;
            if (candidateVid->sizeZ() + candidate->m_xyz.z < z)
                continue;
            if (z + m_vid->sizeZ() < candidate->m_xyz.z)
                continue;

            if ((candidateVid->movementMask() & m_vid->movementMask()) != 0)
                return candidate;

            if ((candidateVid->properties() & P_CRUSH) != 0u)
                candidate->dispatchVirtualAction(ActionCode::ACT_DAMAGE, 5, 0, 0);
        }

        return nullptr;
    }

    SPRITE* SPRITE::CanPlaceWithCrushAndGlide(float* xOut, float* yOut, float* zOut) noexcept
    {
        if (m_vid->movementMask() == 0)
            return 0;

        auto repairGroundZ = [this, xOut, yOut, zOut]() noexcept {
            if ((m_vid->properties() & P_ZEROZ) != 0u)
                *zOut = mapOwner()->GetGroundZ(
                    m_vid, VECTOR2{*xOut, *yOut}, m_direction);
        };

        SPRITE* const blocker = CanPlace(*xOut, *yOut, *zOut);
        if (!blocker)
        {
            repairGroundZ();
            return 0;
        }

        if (blocker != mouseSprite())
        {
            VID* const blockerVid = blocker->Vid();
            if ((static_cast<std::uint32_t>(blockerVid->weaponFlags()) & 0x00000040u) != 0u)
            {
                const float pushedXInitial = addThenSubtractRounded(
                    blocker->X(), *xOut, X());
                const float pushedYInitial = addThenSubtractRounded(
                    blocker->Y(), *yOut, Y());
                float pushedX = pushedXInitial;
                float pushedY = pushedYInitial;
                float pushedZ = blocker->Z();

                // INC/CMP/JGE and DEC are raw signed x86 DWORD operations.
                g_collisionPushRecursionDepth = spriteAdd32Wrap(g_collisionPushRecursionDepth, 1);
                if (g_collisionPushRecursionDepth < 5 &&
                    blocker->CanPlaceWithCrushAndGlide(&pushedX, &pushedY, &pushedZ) == nullptr)
                {
                    blocker->ChangeCoor(pushedX, pushedY, pushedZ);
                }
                if (g_collisionPushRecursionDepth != 0)
                    g_collisionPushRecursionDepth = spriteSub32Wrap(g_collisionPushRecursionDepth, 1);
            }
        }

        const float savedX = X();
        const float savedY = Y();
        const float savedZ = Z();

        if (CanPlace(savedX, *yOut, *zOut) == nullptr)
        {
            *xOut = savedX;
            repairGroundZ();
            return 0;
        }

        if (CanPlace(*xOut, savedY, *zOut) == nullptr)
        {
            *yOut = savedY;
            repairGroundZ();
            return 0;
        }

        *xOut = savedX;
        *yOut = savedY;
        *zOut = savedZ;
        return blocker;
    }

    int SPRITE::GlideDirection(int value) noexcept
    {
        const int direction = value;
        const float halfX = m_vid->sizeX() * 0.5f;
        const float halfY = m_vid->sizeY() * 0.5f;
        const float x = m_xyz.x;
        const float y = m_xyz.y;
        const float z = m_xyz.z;

        auto blocked = [this, z](float px, float py) noexcept -> bool {
            return CanPlace(px, py, z) != nullptr;
        };

        if (direction > 0x60 && direction < 0xA0)
        {
            if (blocked(x - halfX, y + halfY))
            {
                if (!blocked(x + halfX, y))
                    return 0x58;
            }

            if (blocked(x + halfX, y + halfY))
            {
                if (!blocked(x - halfX, y))
                    return 0xA8;
            }
            return value;
        }

        if (direction <= 0xE0 && direction >= 0x20)
        {
            if (direction > 0xB0 && direction < 0xD0)
            {
                if (blocked(x - halfX, y + halfY))
                {
                    if (!blocked(x, y - halfY))
                        return 0xD8;
                }

                if (blocked(x - halfX, y - halfY))
                {
                    if (!blocked(x, y + halfY))
                        return 0xA8;
                }
            }
            else if (direction > 0x30 && direction < 0x50)
            {
                if (blocked(x + halfX, y + halfY))
                {
                    if (!blocked(x, y - halfY))
                        return 0x28;
                }

                if (blocked(x + halfX, y - halfY))
                {
                    if (!blocked(x, y + halfY))
                        return 0x58;
                }
            }
            return value;
        }

        if (blocked(x - halfX, y - halfY))
        {
            if (!blocked(x + halfX, y))
                return 0x28;
        }

        if (!blocked(x + halfX, y - halfY))
            return value;

        if (!blocked(x - halfX, y))
            return 0xD8;

        return value;
    }

    void SPRITE::ChangeDirection(int direction) noexcept
    {
        const int requestedDirection = direction & 0xFF;
        int frameDirection = requestedDirection;

        if (!x87IsZeroOrUnordered(m_zSpeed) && (m_vid->property & P_VERTDIR) != 0)
        {
            int projectedX = 0;
            int projectedY = 0;
            (void)projectVerticalMotion(frameDirection, m_speed, m_zSpeed, projectedX, projectedY);
            frameDirection = AngleFromXY(projectedX, projectedY, nullptr) & 0xFF;
        }

        const int beforeDirection = directionIndex();
        if (beforeDirection == frameDirection)
            return;

        if (SPRITE* const child = childChain())
        {
            VID* const childVid = child->Vid();
            const VECTOR link = m_vid->linkOffset();
            const bool fixedLinkRoute =
                childVid == m_vid->linkedVid() &&
                (!x87IsZeroOrUnordered(link.x) ||
                 !x87IsZeroOrUnordered(link.y));

            const int steppedDirection = quantizeDirectionForVid(
                frameDirection,
                static_cast<int>(m_vid->directionQuantizationOffset()),
                m_vid->directionCount());

            if (fixedLinkRoute)
            {
                const float offsetX =
                    directionCos(steppedDirection) * link.x +
                    directionSin(steppedDirection) * link.y;
                const float offsetY =
                    directionSinAux(steppedDirection) * link.x -
                    directionCosAux(steppedDirection) * link.y;
                child->ChangeCoor(m_xyz.x + offsetX,
                                  m_xyz.y + offsetY,
                                  child->m_xyz.z);
            }
            else if (childVid->spriteClassId() == B_LINKER)
            {
                const int deltaDirection =
                    (steppedDirection - child->linkerDirection()) & 0xFF;
                const float linkerX = child->linkerX();
                const float linkerY = child->linkerY();
                const float rotatedX =
                    directionCos(deltaDirection) * linkerX -
                    directionSin(deltaDirection) * linkerY;
                const float rotatedY =
                    directionCosAux(deltaDirection) * linkerY +
                    directionSinAux(deltaDirection) * linkerX;
                SPRITE* const base = child->linkerOwner()
                    ? child->linkerOwner()
                    : child->childBacklink();
                child->ChangeCoor(base->m_xyz.x + rotatedX,
                                  base->m_xyz.y + rotatedY,
                                  child->m_xyz.z);
            }

            if (x87IsZeroOrUnordered(childVid->rotationSpeedValue()))
                child->ChangeDirection(requestedDirection);
        }

        if (m_vid->noDir != 1)
        {
            const int oldFrameDelta = m_currentFrame - m_currentFrameBegin;
            const int animationSlot = m_currentAnimation;
            int beginFrame = static_cast<int>(m_vid->animationBaseFrame[animationSlot]);
            const int noDir = static_cast<int>(m_vid->noDir);
            if (noDir != 0)
            {
                const std::uint32_t angle =
                    static_cast<std::uint32_t>(
                        static_cast<int>(m_vid->directionQuantizationOffset()) + frameDirection) & 0xFFu;
                const std::uint32_t dirProduct = static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(angle) * static_cast<std::uint32_t>(noDir));
                const std::int32_t dirIndex = static_cast<std::int32_t>(dirProduct >> 8u);
                const std::int32_t frameOffset = spriteImul32Low(
                    dirIndex, static_cast<int>(m_vid->animationFrameCount[animationSlot]));
                beginFrame = spriteAdd32Wrap(beginFrame, frameOffset);
            }

            const int frameCount = static_cast<int>(m_vid->animationFrameCount[animationSlot]);
            m_currentFrameBegin = beginFrame;
            m_currentFrameEnd = beginFrame + frameCount - 1;
            m_currentFrame = beginFrame + oldFrameDelta;
        }

        m_direction = ANGLE(requestedDirection);
    }

    int SPRITE::RotateTact(int value, std::uint32_t deltaMs) noexcept
    {
        const int target = value & 0xFF;
        const int current = directionIndex() & 0xFF;
        if (target == current)
        {
            return 0;
        }

        // Retail dereferences [SPRITE+0x1C] unconditionally here.  Do not
        // introduce a null-owner fallback: it changes the fault/branch behavior.
        const float rotationSpeed = m_vid->rotationSpeedValue();
        if (rotationSpeed == 999999.0f)
        {
            ChangeDirection(target);
            return 1;
        }

        const int step = spriteFildMulAddFtolLow32(deltaMs, rotationSpeed, 0.5f);

        auto remainingDeltaToTarget = [](int from, int to) noexcept -> int
        {
            const unsigned char fromByte = static_cast<unsigned char>(from & 0xFF);
            const unsigned char toByte = static_cast<unsigned char>(to & 0xFF);
            const unsigned char a = static_cast<unsigned char>(fromByte - toByte);
            const unsigned char b = static_cast<unsigned char>(toByte - fromByte);
            return static_cast<int>((a < b) ? a : b);
        };

        if (step == 0)
        {
            const int remaining = remainingDeltaToTarget(current, target);
            return remaining;
        }

        int absDelta = current - target;
        if (absDelta < 0)
            absDelta = -absDelta;
        int wrapDelta = 0x100 - absDelta;
        bool addStepAfterCurrent = false;
        bool subtractStepFromCurrent = false;

        if (current > target)
        {
            if (absDelta < wrapDelta)
                subtractStepFromCurrent = true;
            else
                addStepAfterCurrent = true;
        }
        else if (current < target)
        {
            if (absDelta > wrapDelta)
                subtractStepFromCurrent = true;
            else
                addStepAfterCurrent = true;
        }

        const int shortest = (absDelta < wrapDelta) ? absDelta : wrapDelta;
        if (step >= shortest)
        {
            ChangeDirection(target);
            return 0;
        }

        if (subtractStepFromCurrent)
            ChangeDirection(current - step);
        if (addStepAfterCurrent)
            ChangeDirection(directionIndex() + step);

        const int remaining = remainingDeltaToTarget(directionIndex(), target);
        return remaining;
    }

    int AS1_SPRITE_STDCALL acceptEnemyCandidate(SPRITE* /*sprite*/) noexcept
    {
        return 1;
    }

    void SPRITE::clearCommandStackAndReleaseTargets() noexcept
    {
        const std::int32_t count = static_cast<std::int32_t>(
            m_commandStack.m_commandRecords.count);
        SpriteCommandStack::CommandRecordStorage* const raw = m_commandStack.m_commandRecords.records;
        for (std::int32_t i = 0; i < count; ++i)
        {
            const std::uint32_t index = static_cast<std::uint32_t>(i);
            if ((raw[index].words[0] & 0xFFu) != 0x4Au || raw[index].words[1] == 0u)
                continue;

            SPRITE* target = nullptr;
#ifdef _WIN32
            target = reinterpret_cast<SPRITE*>(static_cast<std::uintptr_t>(raw[index].words[1]));
#endif
            if (!target)
                continue;

            const int refs = target->ReleaseListReference();
            if (refs == 0)
                DeleteSpriteThroughVirtualDeletingDestructor(target);
        }
        m_commandStack.clear();
    }

    void SPRITE::copyCommandPrefixTo(SPRITE* target) noexcept
    {
        if (!target || target == this)
            return;

        target->clearCommandStackAndReleaseTargets();
        const std::uint32_t count = m_commandStack.m_commandRecords.count;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const SpriteCommandStack::CommandRecordStorage& raw = m_commandStack.m_commandRecords.records[i];
            if (raw.words[0] == 73u)
                break;
            SpriteCommandRecord record{};
            record.opcode = raw.words[0];
            record.argument1 = raw.words[1];
            record.argument2 = raw.words[2];
            record.argument3 = raw.words[3];
            target->m_commandStack.appendCommandRecord(record);
        }
    }

    int SPRITE::enemyPriority(float candidateMetric,
                            float selectedMetric,
                            SPRITE* candidate,
                            SPRITE* selected) noexcept
    {
        if (!selected)
            return 1;

        VID* const thisVid = Vid();
        const DWORD weaponFlags = static_cast<DWORD>(thisVid->weaponFlags());
        if ((weaponFlags & 0x00000100u) != 0u)
            return selectedMetric > candidateMetric ? 1 : 0;

        if (SPRITE* const currentTarget = bestTargetSprite())
        {
            if (selected == currentTarget)
                return 0;
            if (candidate == currentTarget)
                return 1;
        }

        const DWORD candidateBucket = candidate->armyBits();
        const DWORD selectedBucket = selected->armyBits();
        if (candidateBucket == 0x0800u && selectedBucket != 0x0800u)
            return 0;
        if (selectedBucket != 0x0800u && candidateBucket == 0x0800u)
            return 1;

        const DWORD selectedType = selected->Vid()->spriteTypeId();
        const DWORD candidateType = candidate->Vid()->spriteTypeId();
        if ((selectedType & 0x08u) != 0u && (candidateType & 0x08u) == 0u)
            return 0;
        if ((selectedType & 0x08u) == 0u && (candidateType & 0x08u) != 0u)
            return 1;

        float nearRange = thisVid->weaponBattleRange();
        if (SPRITE* const child = childChain())
        {
            VID* const childVid = child->Vid();
            if (childVid == thisVid->linkedVid() &&
                childVid->hasWeaponChildDescriptor() != 0u &&
                childVid->weaponCount() != 0u &&
                nearRange == 0.0f)
            {
                nearRange = childVid->weaponBattleRange();
            }
        }
        if (selectedMetric > nearRange && candidateMetric <= nearRange)
            return 1;

        int candidateAction92 = 0;
        if ((candidateType & 0x04u) != 0u)
            candidateAction92 = candidate->dispatchVirtualAction(ActionCode::ACT_GET_AMMO, 0, 0, 0);

        if ((selectedType & 0x04u) != 0u &&
            selected->dispatchVirtualAction(ActionCode::ACT_GET_AMMO, 0, 0, 0) != 0)
        {
            if (candidateAction92 == 0)
                return 0;
        }
        else if (candidateAction92 != 0)
        {
            return 1;
        }

        auto weaponPriority3C = [](SPRITE* sprite) noexcept -> int
        {
            VID* const spriteVid = sprite->Vid();
            if (SPRITE* const child = sprite->childChain())
            {
                VID* const childVid = child->Vid();
                if (childVid == spriteVid->linkedVid() &&
                    childVid->hasWeaponChildDescriptor() != 0u &&
                    childVid->weaponCount() != 0u)
                {
                    return childVid->weaponIntAt(0x3C);
                }
            }
            return spriteVid->weaponIntAt(0x3C);
        };

        const int candidatePriority = weaponPriority3C(candidate);
        const int selectedPriority = weaponPriority3C(selected);
        if (candidatePriority > selectedPriority)
            return 1;
        if (candidatePriority >= selectedPriority && selectedMetric > candidateMetric)
            return 1;
        return 0;
    }

    SPRITE* SPRITE::SeekEnemy() noexcept
    {
        SPRITE* scanOwner = this;
        while (SPRITE* const child = scanOwner->childChain())
        {
            VID* const ownerVid = scanOwner->Vid();
            VID* const childVid = child->Vid();
            if (childVid != ownerVid->linkedVid() ||
                childVid->hasWeaponChildDescriptor() == 0u ||
                childVid->weaponCount() == 0u)
            {
                break;
            }
            scanOwner = child;
        }

        VID* const ownerVid = scanOwner->Vid();
        const DWORD ownerTypeMask = static_cast<DWORD>(ownerVid->weaponTypeMask());
        const DWORD ownerWeaponFlags = static_cast<DWORD>(ownerVid->weaponFlags());
        const float maxRange = ownerVid->weaponDetectRange();
        const float nearRange = ownerVid->weaponBattleRange();
        const float minRange = ownerVid->weaponFloatAt(0x40);
        if (maxRange == 0.0f || ownerTypeMask == 0u)
            return nullptr;

        float selectedMetric = maxRange + 1.0f;
        SPRITE* selected = nullptr;
        const DWORD ownerBucket = scanOwner->armyBits();

        SPRITE_COLLECTOR_HASH_MAP* const hash = GlobalSpriteHashMap();
        SPRITE_POINTER_LIST& overflow = hash->mutableOverflowList();
        int* const cursor = hash->reverseCursorAddress();

        SPRITE* candidate = nullptr;
        if (ownerBucket == (1u << ArmyBitsShift))
        {
            candidate = scanOwner->mapOwner()->flagmanSpriteForPlayer(0);
        }
        else
        {
            candidate = overflow.beginReverseIteration(cursor);
        }

        while (candidate)
        {
            VID* const candidateVid = candidate->Vid();
            const DWORD candidateType = candidateVid->spriteTypeId();
            const DWORD candidateBucket = candidate->armyBits();

            if ((candidateType & ownerTypeMask) != 0u &&
                candidateVid->maxHp != 0 &&
                (((scanOwner->runtimeFlags() ^ candidate->runtimeFlags()) & ArmyBitsMask) != 0u ||
                 (ownerWeaponFlags & 0x80u) != 0u) &&
                (candidateVid->properties() & P_INVISIBLEFORENEMY) == 0u &&
                !(candidateBucket == 0x0800u &&
                  (ownerWeaponFlags & 0x80u) == 0u &&
                  (((core::ApplicationFlags() & application_flags::EnemyCanAttackNeutralTrains) == 0u) ||
                   ownerBucket != 0x0400u ||
                   candidateVid->spriteClassId() != 21u)) &&
                !((candidateType & 0x08u) != 0u && candidate->childBacklink() != nullptr))
            {
                if ((ownerWeaponFlags & 0x02u) == 0u)
                {
                    // fall through to distance test
                }
                else
                {
                    const int dx = static_cast<int>(candidate->X() - scanOwner->X());
                    const int dy = static_cast<int>(candidate->Y() - scanOwner->Y());
                    const unsigned char wanted = static_cast<unsigned char>(AngleFromXY(dx, dy, nullptr));
                    const unsigned char current = static_cast<unsigned char>(scanOwner->directionIndex());
                    const unsigned char d1 = static_cast<unsigned char>(wanted - current);
                    const unsigned char d2 = static_cast<unsigned char>(current - wanted);
                    if ((d1 < d2 ? d1 : d2) >= 0x20u)
                        goto next_candidate;
                }

                {
                    float dx = std::fabs(candidate->X() - scanOwner->X());
                    float dy = std::fabs(candidate->Y() - scanOwner->Y());
                    if (dx < dy)
                        std::swap(dx, dy);
                    const float metric = dx + dy * 0.5f;
                    if (metric <= maxRange && metric >= minRange)
                    {
                        if ((ownerVid->spriteTypeId() & 0x08u) != 0u &&
                            scanOwner->bestTargetSprite() == nullptr)
                        {
                            if (metric < selectedMetric)
                            {
                                const DWORD flags = scanOwner->runtimeFlags();
                                if ((static_cast<unsigned char>((flags >> 8) & 0xFFu) & 0x0Cu) != 0u ||
                                    candidateVid->spriteClassId() != 21u ||
                                    !static_cast<ENGINE*>(candidate)->engineChainContainsArmy(0))
                                {
                                    selectedMetric = metric;
                                    selected = candidate;
                                    if ((ownerWeaponFlags & 0x08u) != 0u && (std::rand() % 3) == 0)
                                        return candidate;
                                }
                            }
                        }
                        else if (acceptEnemyCandidate(candidate) != 0 && candidateVid->nvid() != 104)
                        {
                            bool acceptCandidate = false;
                            if (selectedMetric <= nearRange)
                            {
                                if (metric <= nearRange)
                                    acceptCandidate = enemyPriority(metric, selectedMetric, candidate, selected) != 0;
                            }
                            else if (metric <= nearRange)
                            {
                                acceptCandidate = true;
                            }
                            else
                            {
                                acceptCandidate = enemyPriority(metric, selectedMetric, candidate, selected) != 0;
                            }

                            if (acceptCandidate)
                            {
                                const DWORD flags = scanOwner->runtimeFlags();
                                if ((static_cast<unsigned char>((flags >> 8) & 0xFFu) & 0x0Cu) != 0u ||
                                    candidateVid->spriteClassId() != 21u ||
                                    !static_cast<ENGINE*>(candidate)->engineChainContainsArmy(0))
                                {
                                    selectedMetric = metric;
                                    selected = candidate;
                                    if ((ownerWeaponFlags & 0x08u) != 0u && (std::rand() % 3) == 0)
                                        return candidate;
                                }
                            }
                        }
                    }
                }
            }

        next_candidate:
            if (ownerBucket == (1u << ArmyBitsShift))
                return selected;
            candidate = overflow.continueReverseIteration(cursor);
        }

        return selected;
    }

    void SPRITE::Tact()
    {
        const std::uint32_t previousFrameClock = core::PreviousWorldTimeMilliseconds();

        if (!m_childBacklink)
        {
            const std::uint32_t savedClock14 = m_applicationBucketTime;
            m_applicationBucketTime = previousFrameClock;
            MoveTact();
            m_applicationBucketTime = savedClock14;
        }

        const std::uint32_t now = core::CurrentTimeMilliseconds();
        VID* const vid = m_vid;
        const int animation = m_currentAnimation;
        VID* const childVid = vid->childVid[animation];

        if (childVid)
        {
            const bool class23ZeroOffset =
                vid->spriteClassId() == 23u &&
                x87EqualOrUnordered(vid->childX[animation], 0.0f) &&
                x87EqualOrUnordered(vid->childY[animation], 0.0f);

            if (class23ZeroOffset)
            {
                const REGION* const region = static_cast<const REGION*>(this);
                const bool applicationSized = (region->regionFlags() & REGION::FullViewportFlag) != 0u;
                const float width = applicationSized
                    ? applicationWorldFloatAt(0x28u)
                    : region->regionWidth();
                const float height = applicationSized
                    ? applicationWorldFloatAt(0x2Cu)
                    : region->regionHeight();
                const std::uint32_t tileCount = static_cast<std::uint32_t>(
                    computeRegionTileCount(width, height,
                                               childVid->sizeX(),
                                               childVid->sizeY()));
                const std::uint32_t delta = now - previousFrameClock;
                if (tileCount != 0u && delta != 0u)
                {

                    const std::uint32_t divisor = 1000u / delta / tileCount;
                    const int modulo = static_cast<int>(divisor + 1u);
                    if ((std::rand() % modulo) == 0)
                        spawnAnimationChild();
                }
            }
            else if ((childVid->properties() & P_BIRTHASSMOKE) != 0u)
            {
                ActionAuxState* const cadenceOwner = m_actionAuxState;
                std::uint32_t cadence = cadenceOwner->state;
                if (now - now % cadence > previousFrameClock)
                {
                    const bool subtractGraphMotion =
                        (childVid->properties() & P_WIND) != 0u;
                    int graphDirection = 0;
                    float graphSpeed = 0.0f;
                    if (subtractGraphMotion)
                    {
                        // Retail reads the raw GRAPH +0xDE0 DWORD and indexes
                        // the 0x478794/0x478B94 tables directly; no &0xFF is
                        // inserted by this owner.
                        GRAPH* const graph = GRAPH::CurrentGraph();
                        graphDirection = static_cast<int>(graph->windDirection());
                        graphSpeed = graph->windSpeed();
                    }

                    cadence = computeChildAnimationCadence(
                        m_direction.Int(), m_speed, m_zSpeed,
                        childVid->maximumZSpeed(),
                        subtractGraphMotion, graphDirection, graphSpeed,
                        childVid->sizeX(), childVid->sizeY());

                    if (animation == 8 &&
                        (vid->weaponFlags() & 0x10) != 0)
                    {
                        cadence >>= 1;
                    }
                    if (cadence > 30000u)
                        cadence = 30000u;
                    if (cadence == 0u)
                        cadence = 1u;
                    cadenceOwner->state = cadence;
                    spawnAnimationChild();
                }
            }
        }

        if (m_animationLastTick == now)
            return;

        const std::uint32_t frameInterval = static_cast<std::uint32_t>(vid->defaultFrameSpeed());
        if (now - now % frameInterval <= previousFrameClock)
            return;

        DWORD flags = m_runtimeFlags;
        if ((flags & CommandBitsMask) != 0u)
        {
            const int action = static_cast<int>((flags >> CommandBitsShift) & CommandValueMask);
            if (action < 16 && !m_goalSprite)
            {
                LOG::ResourceError("SPRITE %i", 10, "command need goal, but goal==NULL",
                                   action, vid ? vid->nVid : -1);
                SetCommand(0, nullptr);
            }
        }

        // SPRITE+0x50 is a remaining-time owner.  The original rewrites it
        // relative to +0x14 until it expires, then clears action 0x12/0x48.
        if (m_actionTimer != 0u)
        {
            if (now - m_applicationBucketTime < m_actionTimer)
            {
                m_actionTimer =
                    m_applicationBucketTime + m_actionTimer - now;
            }
            else
            {
                m_actionTimer = 0;
                if ((m_runtimeFlags & SPRITE::CommandBitsMask) == 0x48u)
                    SetCommand(0, nullptr);
            }
        }

        const int functionIndex = vid->scriptFunctionAt(animation);
        if (functionIndex >= 0 &&
            (m_currentFrame == m_currentFrameBegin ||
             (vid->properties() & P_TRACK) != 0u))
        {
            const int spriteArg = static_cast<int>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFFFFu);
            if (core::Application::callScriptFunction(functionIndex, spriteArg, 0) != 0)
                return;
        }

        VID* const postCallbackVid = m_vid;
        const int postCallbackAnimation = m_currentAnimation;
        const int sfx = postCallbackVid->sfxForAnimation(postCallbackAnimation);
        if (sfx != 0)
        {
            const bool firstSfx = (m_runtimeFlags & 0x00000200u) == 0u;
            const bool repeatSfx = !firstSfx &&
                sound::GlobalSoundEngine()->passesSfxRepeatGate(sfx);
            if (firstSfx || repeatSfx)
            {
                m_runtimeFlags |= 0x00000200u;
                playSfxAtWorldPosition(sfx);
            }
        }

        VID* const postCallbackChildVid =
            postCallbackVid->childVid[postCallbackAnimation];

        // Frame-bound child creation route.  Dynamic (P_80) children were
        // handled by the cadence path above and do not use this trigger.
        if (postCallbackChildVid &&
            (postCallbackChildVid->properties() & P_BIRTHASSMOKE) == 0u)
        {
            bool createChild = false;
            if (postCallbackAnimation == 8 &&
                (postCallbackVid->weaponFlags() & 0x10) != 0)
            {
                const std::int32_t midpointNumerator = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(m_currentFrameEnd) +
                    static_cast<std::uint32_t>(m_currentFrameBegin) + 1u);
                const int midpoint = midpointNumerator >= 0
                    ? midpointNumerator / 2
                    : -static_cast<int>(
                        (0u - static_cast<std::uint32_t>(midpointNumerator)) / 2u);
                if (m_currentFrame == midpoint)
                    createChild = true;
                else
                {
                    const int alternateFrame =
                        (postCallbackVid->properties() & P_CREATECHILDEND) != 0u
                            ? m_currentFrameEnd
                            : m_currentFrameBegin;
                    createChild = m_currentFrame == alternateFrame;
                }
            }
            else if ((postCallbackVid->properties() & P_TRACK) == 0u)
            {
                const int triggerFrame =
                    (postCallbackVid->properties() & P_CREATECHILDEND) != 0u
                        ? m_currentFrameEnd
                        : m_currentFrameBegin;
                createChild = m_currentFrame == triggerFrame;
            }

            if (createChild)
                spawnAnimationChild();
        }

        m_currentFrame = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(m_currentFrame) + 1u);

        // Animations 15/16 are terminal owners.  The virtual action call is
        // preserved before the scalar deleting-destructor route.
        if (m_currentAnimation >= 15 && m_currentFrame > m_currentFrameEnd)
        {
            dispatchVirtualAction(15u, 0, 0, 0);
            DeleteSpriteThroughVirtualDeletingDestructor(this);
            return;
        }

        const int actionMask = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
        if (actionMask == 0x44 || actionMask == 0x48)
        {
            if (m_currentAnimation < 15 && m_currentAnimation != 10 &&
                m_currentFrame > m_currentFrameEnd)
            {
                if (x87IsZeroOrUnordered(m_speed))
                {
                    if (m_currentAnimation == 2 || m_currentAnimation >= 7)
                        ChangeAnimation(0);
                }
                else if (m_currentAnimation != 2)
                {
                    ChangeAnimation(2);
                }
            }
        }
        else if (m_currentFrame > m_currentFrameEnd ||
                 (vid->properties() & P_TRACK) != 0u)
        {
            const std::size_t commandCount = m_commandStack.size();
            if (commandCount == 0 || actionMask != 0)
            {
                dispatchVirtualAction(ActionCode::ACT_NEXT_COMMAND, 0, 0, 0);
            }
            else
            {
                const SpriteCommandStack::CommandRecordStorage command =
                    m_commandStack.m_commandRecords.records[commandCount - 1];
                m_commandStack.setCommandRecordCount(
                    static_cast<std::uint32_t>(commandCount - 1));

                const int opcode = static_cast<int>(command.words[0]);
                const int argument1 = static_cast<int>(command.words[1]);
                const int argument2 = static_cast<int>(command.words[2]);
                const int argument3 = static_cast<int>(command.words[3]);
                if (opcode < 17)
                {
                    ChangeAnimation(opcode);
                }
                else
                {
                    if (m_currentAnimation < 15 && m_currentAnimation >= 7 &&
                        m_currentAnimation != 10)
                    {
                        ChangeAnimation(0);
                    }
                    dispatchVirtualAction(static_cast<std::uint32_t>(opcode),
                                             argument1, argument2, argument3);
                }
            }
        }

        if (m_actionAuxState)
        {
            std::uint32_t& terminalTimer = m_actionAuxState->primaryValue;
            if (terminalTimer != 999999u)
            {
                if (now - m_applicationBucketTime >= terminalTimer)
                    ChangeAnimation(15);
                else
                    terminalTimer = terminalTimer + m_applicationBucketTime - now;
            }
        }

        m_applicationBucketTime = now;
        if (m_currentFrame > m_currentFrameEnd)
            m_currentFrame = m_currentFrameBegin;
    }

    void PRIMITIVE::Tact()
    {
        (void)advancePrimitiveFrame();
    }

    void PRIMITIVE::MoveTact()
    {
    }

    void PRIMITIVE::DeletePointerToSprite(SPRITE*)
    {
    }

    void PRIMITIVE::DrawDebugOverlay()
    {
    }

    int SPRITE::advancePrimitiveFrame() noexcept
    {
        const std::uint32_t now = core::CurrentTimeMilliseconds();
        int result = static_cast<int>(now);
        const std::uint32_t frameInterval =
            static_cast<std::uint32_t>(m_vid->defaultFrameSpeed());

        if (now - m_applicationBucketTime >= frameInterval)
        {
            m_applicationBucketTime = now;
            m_currentFrame = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(m_currentFrame) + 1u);
            result = m_currentFrame;
            if (m_currentFrame > m_currentFrameEnd)
            {
                m_currentFrame = m_currentFrameBegin;
                result = m_currentFrame;
            }
        }
        return result;
    }

    int SPRITE::computeAttackDecisionCode(std::uint32_t deltaMs) noexcept
    {
        if (SPRITE* const child = m_childChain)
        {
            VID* const childVid = child->m_vid;
            if (childVid == m_vid->linkedVid() &&
                childVid->hasWeaponChildDescriptor() != 0u &&
                childVid->weaponCount() != 0u)
            {
                int result = child->computeAttackDecisionCode(deltaMs);
                if (result == 4 && m_goalSprite != nullptr)
                    result = 5;
                return result;
            }
        }

        if (m_vid->hasWeaponChildDescriptor() == 0u || m_vid->weaponCount() == 0u)
            return 7;

        SPRITE* const target = m_goalSprite;
        if (!target)
        {
            if (SPRITE* const uplink = m_childBacklink)
            {
                if (m_actionTimer == 0u)
                    RotateTact(uplink->directionIndex(), deltaMs);
            }
            return 4;
        }

        auto directionToTarget = [this, target]() noexcept -> int
        {
            return computeDirectionToTarget(
                target->m_xyz.x, target->m_xyz.y, m_xyz.x, m_xyz.y);
        };
        auto metricToTarget = [this, target]() noexcept -> float
        {
            return targetDistanceMetric(
                target->m_xyz.x, target->m_xyz.y, m_xyz.x, m_xyz.y);
        };

        const std::uint32_t waitTimer = m_actionTimer;
        if (waitTimer > 5000u || (m_currentAnimation == 8 && m_currentFrame <= m_currentFrameEnd))
        {
            RotateTact(directionToTarget(), deltaMs);
            return 3;
        }

        const DWORD commandBits = m_runtimeFlags & SPRITE::CommandBitsMask;
        if (commandBits != 0x14u && commandBits != 0x0Cu && commandBits != 0x10u)
        {
            if (SPRITE* const uplink = m_childBacklink)
            {
                if (waitTimer == 0u)
                    RotateTact(uplink->directionIndex(), deltaMs);
            }
            return 5;
        }

        const float nearRange = m_vid->weaponBattleRange();
        const float farRange = m_vid->weaponFloatAt(0x40);
        const float metric = metricToTarget();
        SPRITE* const uplink = m_childBacklink;
        const bool uplinkByNear = !x87LessOrUnordered(metric, nearRange);
        const bool uplinkByFar = x87LessEqualOrUnordered(metric, farRange);
        if (uplink && commandBits != 0x14u && (uplinkByNear || uplinkByFar))
        {
            RotateTact(uplink->directionIndex(), deltaMs);
        }
        else
        {
            const int rotateResult = RotateTact(directionToTarget(), deltaMs);
            if (rotateResult == 0 || (m_vid->weaponFlags() & 1) != 0)
            {
                if (x87LessEqualOrUnordered(metric, nearRange))
                {
                    const std::int32_t scale = static_cast<std::int32_t>(
                        m_vid->fightNoChildValue());
                    const int ammo = dispatchVirtualAction(ActionCode::ACT_GET_AMMO, 0, 0, 0);
                    if (ammo < spriteAbs32Wrap(scale))
                        return 6;

                    dispatchVirtualAction(ActionCode::ACT_ADD_AMMO, spriteNeg32Wrap(scale), 0, 0);
                    ChangeAnimation(8);
                    m_actionTimer = static_cast<std::uint32_t>(
                        static_cast<std::uint32_t>(m_vid->weaponIntAt(0x20)) +
                        5000u);
                    return 0;
                }
                return commandBits == 0x0Cu ? 1 : 2;
            }
        }

        const float postMetric = metricToTarget();
        if (x87LessEqualOrUnordered(postMetric, nearRange))
            return 3;
        if (commandBits == 0x0Cu || commandBits == 0x10u)
            return 1;
        return 2;
    }

    int SPRITE::healthRatio255() noexcept
    {
        if (SPRITE* const child = m_childChain)
        {
            VID* const thisVid = m_vid;
            VID* const childVid = child->m_vid;
            if (childVid == thisVid->linkedVid() &&
                childVid->hasWeaponChildDescriptor() != 0u &&
                childVid->weaponCount() != 0u)
            {
                const int bucket = child->armyIndex();
                const int duration = childVid->animationFrameDuration(bucket);
                if (duration != 0)
                {
                    const std::uint32_t rawNumerator =
                        (static_cast<std::uint32_t>(child->m_animationFrameTime) << 8) -
                        static_cast<std::uint32_t>(child->m_animationFrameTime);
                    const std::int32_t numerator = static_cast<std::int32_t>(rawNumerator);
                    return numerator / duration;
                }
            }
        }

        VID* const vid = m_vid;
        const int bucket = armyIndex();
        const int duration = vid->animationFrameDuration(bucket);
        if (duration == 0)
            return 0;

        const std::uint32_t rawNumerator =
            (static_cast<std::uint32_t>(m_animationFrameTime) << 8) -
            static_cast<std::uint32_t>(m_animationFrameTime);
        const std::int32_t numerator = static_cast<std::int32_t>(rawNumerator);
        return numerator / duration;
    }

    void SPRITE::Stop() noexcept
    {
        const DWORD commandBits = m_runtimeFlags & SPRITE::CommandBitsMask;
        if (commandBits == 0u || commandBits == 4u)
            SetCommandWithoutLink(0, nullptr);

        m_zSpeed = 0.0f;
        m_runtimeFlags &= 0xFFFF9F7Fu;

        if (x87EqualOrUnordered(m_vid->slowValue(), 999999.0f))
            m_speed = 0.0f;

    }

    int SPRITE::traceMovementCollisionTo(float* xOut, float* yOut, float* zOut) noexcept
    {
        return GlobalSpriteHashMap()->traceMovementCollision(
            *hostState().owner, m_vid,
            m_xyz.x, m_xyz.y, m_xyz.z,
            xOut, yOut, zOut) ? 1 : 0;
    }

    int SPRITE::StartMove() noexcept
    {
        if (m_vid->maxSpeedValue() == 0.0f)
            return 0;

        SPRITE* const target = goalSprite();
        int projectedLength = 0;

        if (target)
        {
            if (m_xyz.x == target->m_xyz.x && m_xyz.y == target->m_xyz.y)
            {
                return 0;
            }

            if (m_vid->directionCount() == 1 || (m_vid->properties() & P_RANDBIRTH) != 0)
            {

                const int dx = spriteFsubStoreF32FtolLow32(target->m_xyz.x, m_xyz.x);
                const int dy = spriteFsubFtolLow32(target->m_xyz.y, m_xyz.y);
                const int direction = AngleFromXY(dx, dy, &projectedLength) & 0xFF;
                ChangeDirection(direction);
            }

            if ((m_vid->spriteTypeId() & 0x00000200u) == 0 && m_vid->spriteClassId() != 5u)
            {
                if (target->m_xyz.z > m_xyz.z)
                    m_zSpeed = m_vid->maximumZSpeed();
                else if (target->m_xyz.z < m_xyz.z)
                    m_zSpeed = -m_vid->maximumZSpeed();
                else
                    m_zSpeed = 0.0f;
            }
            else
            {
                if (projectedLength == 0)
                {

                    const int dx = spriteFsubFtolLow32(target->m_xyz.x, m_xyz.x);
                    const int dy = spriteFsubFtolLow32(target->m_xyz.y, m_xyz.y);
                    (void)AngleFromXY(dx, dy, &projectedLength);
                    if (projectedLength == 0)
                    {
                        return 0;
                    }
                }
                m_zSpeed = m_vid->calculateMoveUpZ(target->m_xyz.z - m_xyz.z, static_cast<float>(projectedLength));
            }
        }

        m_runtimeFlags = (m_runtimeFlags & ~CrossedGoalAxesMask) | MovementStartedFlag;

        if (m_currentAnimation < 15 && m_currentAnimation != 3)
        {
            if (m_speed == 0.0f)
                ChangeAnimation(3);
            else
                ChangeAnimation(2);
        }

        if (m_vid->accelerationValue() == 999999.0f)
            m_speed = m_vid->maxSpeedValue();

        return 1;
    }

    void SPRITE::releaseActionAuxState() noexcept
    {
        if (m_actionAuxState)
        {
            ::operator delete(m_actionAuxState);
            m_actionAuxState = nullptr;
        }
        hostState().actionAuxCommandMask = {0, 0};
    }

    void SPRITE::releaseBestTargetSprite() noexcept
    {
        SPRITE* target = m_bestTargetSprite;
        if (!target)
            return;

        const int nextRef = target->m_listReferenceCount - 1;
        target->m_listReferenceCount = nextRef;
        if (nextRef < 0)
        {
            const int targetNvid = target->m_vid ? target->m_vid->nVid : -1;
            LOG::ResourceError("SPRITE %i", 4, "noRef at Release", nextRef, targetNvid);
        }
        else if (nextRef == 0)
        {
            // Retail: dynamic scalar-deleting vtable slot +0, flag 1.
            // The VS2026 bridge detaches the host MAP holder and performs
            // virtual C++ delete so the concrete derived destructor tail runs.
            DeleteSpriteThroughVirtualDeletingDestructor(target);
        }

        m_bestTargetSprite = nullptr;
    }

    void SPRITE::deleteChildChainSlot40() noexcept
    {
        while (SPRITE* child = m_childChain)
            DeleteSpriteThroughVirtualDeletingDestructor(child);
    }

    size_t SPRITE::childChainCount() const noexcept
    {
        size_t count = 0;
        const SPRITE* node = childChain();
        while (node)
        {
            ++count;
            node = node->childChain();
        }
        return count;
    }

    size_t SPRITE::childChainVidCount(const VID* vid) const noexcept
    {
        if (!vid)
            return 0;
        size_t count = 0;
        const SPRITE* node = childChain();
        while (node)
        {
            if (node->Vid() == vid)
                ++count;
            node = node->childChain();
        }
        return count;
    }

    bool SPRITE::childChainContainsVid(const VID* vid) const noexcept
    {
        return childChainVidCount(vid) != 0;
    }

    size_t SPRITE::childChainApplicationBucketCandidateCount() const noexcept
    {
        size_t count = 0;
        const SPRITE* node = childChain();
        while (node)
        {
            const VID* vid = node->Vid();
            const int layer = vid ? vid->renderLayer() : -1;
            if (layer >= 0 && layer < core::ApplicationDrawDispatcherState::PassCount)
                ++count;
            node = node->childChain();
        }
        return count;
    }

    void SPRITE::clearChildBacklinkSlot44() noexcept
    {
        if (SPRITE* backlink = m_childBacklink)
            backlink->m_childChain = nullptr;
    }

    void SPRITE::releaseCommandRecordsRetailTail() noexcept
    {
        m_commandStack.releaseCommandRecordsRetailTail();
    }

    void SPRITE::DrawDebugOverlay()
    {
        // Host virtual-slot bridge only. Retail base-family vtables point
        // slot +0x18 directly at canonical drawBaseDebugOverlay.
        drawBaseDebugOverlay();
    }

    void SPRITE::drawBaseDebugOverlay()
    {
        GRAPH* const graph = GRAPH::CurrentGraph();

        const float left = static_cast<float>(graph->getViewportLeft());
        float top = static_cast<float>(graph->getViewportTop());

        int bestNvid = 0;
        if (m_bestTargetSprite)
            bestNvid = m_bestTargetSprite->Vid()->nvid();

        int goalNvid = 0;
        if (m_goalSprite)
            goalNvid = m_goalSprite->Vid()->nvid();

        const int ammo = dispatchVirtualAction(ActionCode::ACT_GET_AMMO, 0, 0, 0);
        graph->DrawText(left + 30.0f, top,
            "Ref=%-3i cmd=%1i ani=%-2i ammo=%-3i hp=%-3i AT=%i goal=%-3i best=%-3i spd=%-3i,%-3i timer=%i moveFin=%1u%1u",
            listReferenceCount(),
            commandIndex(),
            m_currentAnimation,
            ammo,
            m_animationFrameTime,
            m_attackDecisionCode,
            goalNvid,
            bestNvid,
            spriteFmulFtolLow32(m_speed, 1000.0f),
            spriteFmulFtolLow32(m_zSpeed, 1000.0f),
            static_cast<int>(m_actionTimer),
            static_cast<unsigned>((m_runtimeFlags >> 13) & 1u),
            static_cast<unsigned>((m_runtimeFlags >> 14) & 1u));

        SPRITE* const child = m_childChain;
        if (child && child->Vid() == m_vid->linkedVid())
        {
            top += 12.0f;
            int childBestNvid = 0;
            if (child->m_bestTargetSprite)
                childBestNvid = child->m_bestTargetSprite->Vid()->nvid();
            int childGoalNvid = 0;
            if (child->m_goalSprite)
                childGoalNvid = child->m_goalSprite->Vid()->nvid();
            const int childAmmo = child->dispatchVirtualAction(ActionCode::ACT_GET_AMMO, 0, 0, 0);
            graph->DrawText(left + 30.0f, top,
                "Ref=%-3i cmd=%1i ani=%-2i ammo=%-3i hp=%-3i AT=%i goal=%-3i best=%-3i spd=%-3i,%-3i timer=%i moveFin=%1u%1u",
                child->listReferenceCount(),
                child->commandIndex(),
                child->m_currentAnimation,
                childAmmo,
                child->m_animationFrameTime,
                child->m_attackDecisionCode,
                childGoalNvid,
                childBestNvid,
                spriteFmulFtolLow32(child->m_speed, 1000.0f),
                spriteFmulFtolLow32(child->m_zSpeed, 1000.0f),
                static_cast<int>(child->m_actionTimer),
                static_cast<unsigned>((child->m_runtimeFlags >> 13) & 1u),
                static_cast<unsigned>((child->m_runtimeFlags >> 14) & 1u));
        }

        const std::uint32_t commandCount = m_commandStack.m_commandRecords.count;
        if (commandCount != 0u)
        {
            top += 12.0f;
            STRING commandText = STRING::Format("%i - ", static_cast<int>(commandCount));
            const auto* const records = m_commandStack.m_commandRecords.records;
            for (std::uint32_t index = 0; index < commandCount; ++index)
            {
                const std::uint32_t* const words = records[index].words;
                const STRING item = STRING::Format("%i(%i,%i,%i) ",
                    static_cast<int>(words[0]), static_cast<int>(words[1]),
                    static_cast<int>(words[2]), static_cast<int>(words[3]));
                commandText.Append(item);
            }
            graph->drawStringColored(left + 30.0f, top, commandText, 0xFFFFFFFFu);
        }

        DrawRelationDebugOverlay();
    }

    void SPRITE::DrawRelationDebugOverlay()
    {
        GRAPH* const graph = GRAPH::CurrentGraph();

        const auto& drawState = core::GlobalApplicationDrawDispatcherState();
        const auto screenX = [&drawState](const SPRITE* sprite) -> float
        {
            return sprite->X() - drawState.cameraShiftX();
        };
        const auto screenY = [&drawState](const SPRITE* sprite) -> float
        {
            return sprite->Y() - sprite->Z() - drawState.cameraShiftY();
        };

        if (m_goalSprite)
        {
            graph->DrawLine(screenX(this), screenY(this),
                            screenX(m_goalSprite), screenY(m_goalSprite),
                            0xFF00FF00u);
        }

        SPRITE* const child = m_childChain;
        if (child && child->m_goalSprite)
        {
            graph->DrawLine(screenX(child), screenY(child),
                            screenX(child->m_goalSprite), screenY(child->m_goalSprite),
                            0xFFFF0000u);
        }
    }

    int SPRITE::removeFromDrawBucketsRecursive()
    {
        if (SPRITE* child = childChain())
            child->removeFromDrawBucketsRecursive();
        return as1::core::Application::removeSpriteFromDrawBucket(as1::core::GlobalApplicationDrawDispatcherState(), this);
    }

    char* SPRITE::addToDrawBucketsRecursive()
    {
        if (SPRITE* child = childChain())
            child->addToDrawBucketsRecursive();
        return as1::core::Application::appendSpriteToDrawBucketAndReleaseListReference(as1::core::GlobalApplicationDrawDispatcherState(), this);
    }

    unsigned int SPRITE::serializeSpriteRecord(RESOURCE* resource) noexcept
    {
        const DWORD flags = m_runtimeFlags;
        if ((flags & 0x00000100u) != 0u)
            return flags;

        const std::uint32_t armyBits = (flags >> ArmyBitsShift) & ArmyValueMask;
        const std::uint32_t rawSprite = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFFFFu);
        resource->write(&rawSprite, 4u);
        resource->write(&m_vid->nVid, 4u);
        resource->write(&m_xyz.x, 4u);
        resource->write(&m_xyz.y, 4u);
        resource->write(&m_xyz.z, 4u);

        const int direction = m_direction.Int();
        resource->write(&direction, 4u);
        return static_cast<unsigned int>(resource->write(&armyBits, 4u));
    }

    namespace
    {
        int signedHalfTowardZero(int value) noexcept
        {
            const int sign = value < 0 ? -1 : 0;
            return (value - sign) >> 1;
        }

        int rescaleAnimationFrameTime(int frameTime, int oldDuration, int nextDuration) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            int result = 0;
            __asm
            {
                mov eax, frameTime
                shl eax, 8
                cdq
                idiv oldDuration
                imul eax, nextDuration
                cdq
                and edx, 0FFh
                add eax, edx
                sar eax, 8
                mov result, eax
            }
            return result;
#else
            const std::int32_t numerator = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(frameTime) << 8u);
            const std::int32_t quotient = numerator / oldDuration;
            const std::int32_t product = spriteImul32Low(quotient, nextDuration);
            const std::int32_t sign = product < 0 ? -1 : 0;
            const std::int32_t remainderBias = sign & 0xFF;
            const std::int32_t biased = spriteAdd32Wrap(product, remainderBias);
            return biased >> 8;
#endif
        }
    }

    void SPRITE::suppressDrawRecursive() noexcept
    {
        for (SPRITE* node = this; node; node = node->m_childChain)
            node->m_runtimeFlags |= DrawSuppressedFlag;
    }

    void SPRITE::restoreDrawRecursive() noexcept
    {
        for (SPRITE* node = this; node; node = node->m_childChain)
            node->m_runtimeFlags &= ~DrawSuppressedFlag;
    }

    void SPRITE::updateAnimationFrameTime(int frameTime) noexcept
    {
        VID* const vid = m_vid;
        const int bucket = armyIndex();
        const int duration = vid->animationFrameDuration(bucket);

        if (frameTime <= 0 && vid->maxHp != 0)
        {
            if (m_currentAnimation >= 15)
                return;

            vid->incrementKilledUnitCountForArmy(bucket);
            const int remaining = m_animationFrameTime - frameTime;
            if (remaining > duration && vid->hasDeath2ChildVid() != 0)
                ChangeAnimation(16);
            else
                ChangeAnimation(15);

            return;
        }

        const int half = signedHalfTowardZero(duration);
        if (frameTime > half && m_animationFrameTime <= half)
        {
            VID* const deleteVid = vid->woundChildVid();
            (void)deleteChildByVid(deleteVid);
        }

        if (frameTime <= half && m_animationFrameTime > half)
            ChangeAnimation(13);

        m_animationFrameTime = frameTime;
    }

    int SPRITE::changeArmyBucket(int bucketIndex) noexcept
    {
        const int oldBucket = armyIndex();
        const int nextBucket = bucketIndex & static_cast<int>(ArmyValueMask);
        m_runtimeFlags = (m_runtimeFlags & ~ArmyBitsMask) | (static_cast<DWORD>(nextBucket) << ArmyBitsShift);

        if (SPRITE* const child = m_childChain)
            (void)child->changeArmyBucket(nextBucket);

        VID* const vid = m_vid;
        const int nextDuration = vid->animationFrameDuration(nextBucket);
        const int oldDuration = vid->animationFrameDuration(oldBucket);
        if (nextDuration != oldDuration)
            updateAnimationFrameTime(rescaleAnimationFrameTime(m_animationFrameTime, oldDuration, nextDuration));

        if ((vid->properties() & P_INVISIBLEFORENEMY) != 0)
        {
            if (nextBucket == static_cast<int>(core::ActivePlayerIndex()))
            {
                m_runtimeFlags &= ~0x00008000u;
                if (SPRITE* const child = m_childChain)
                    child->restoreDrawRecursive();
            }
            else
            {
                m_runtimeFlags |= DrawSuppressedFlag;
                if (SPRITE* const child = m_childChain)
                    child->suppressDrawRecursive();
            }
        }

        if (vid->spriteCountForArmy(oldBucket) != 0u)
            vid->decrementSpriteCountForArmy(oldBucket);
        vid->setLastSpriteCountChangeTimestamp(core::RealTimeMilliseconds());
        vid->incrementSpriteCountForArmy(nextBucket);
        return nextBucket;
    }

    void SPRITE::ensureLinkedVidChild() noexcept
    {
        VID* const vid = m_vid;
        VID* const linkVid = vid->linkedVid();
        if (!linkVid)
            return;

        if (SPRITE* child = childChain())
        {
            if (child->Vid() == linkVid)
                return;
        }

        if (linkVid->isNotCreateAsChild())
            return;

        const int direction = directionIndex();
        const int directionByte = direction & 0xFF;
        const VECTOR& link = vid->linkOffset();
        const float offsetX = (directionCos(directionByte) * link.x) + (directionSin(directionByte) * link.y);
        const float offsetY = (directionSinAux(directionByte) * link.x) - (directionCosAux(directionByte) * link.y);
        const VECTOR target(m_xyz.x + offsetX, m_xyz.y + offsetY, m_xyz.z + link.z);

        SPRITE* const created = hostState().owner->CreateSpriteViaFactory(
            linkVid, target, ANGLE(direction), nullptr, false);
        insertChildChainHead(created);

        if (SPRITE* child = childChain())
        {
            child->changeArmyBucket(armyIndex());
            return;
        }

        LOG::ResourceError("SPRITE %i", 3, "link", 0, vid->nVid);
    }

    int SPRITE::spawnAnimationChild() noexcept
    {
        const int animationSlot = m_currentAnimation;

        VID* const childVid = m_vid->childVid[animationSlot];
        // Retail gates only childVid and childVid+0x418 at entry. noChild==0
        // skips the spawn loop but still reaches the animation-8 terminal tail.
        if (!childVid)
            return 0;
        if (childVid->isNotCreateAsChild() != 0)
            return 0;

        (void)CreateChildForAnimation(animationSlot, false);

        if (animationSlot == 8 &&
            (m_runtimeFlags & 0x00001000u) == 0u)
        {
            const DWORD commandBits = m_runtimeFlags & SPRITE::CommandBitsMask;
            if (commandBits == 0x10u || commandBits == 0x14u)
            {
                const bool requireEndFrame =
                    ((m_vid->properties() & P_TRACK) != 0u) ||
                    ((childVid->properties() & P_BIRTHASSMOKE) != 0u);
                if (!requireEndFrame || m_currentFrame == m_currentFrameEnd)
                {
                    SPRITE* const goal = goalSprite();
                    SPRITE* const backlink44 = childBacklink();
                    if (goal && backlink44 && backlink44->goalSprite() == goal)
                    {
                        const DWORD backlinkCommandBits = backlink44->m_runtimeFlags & SPRITE::CommandBitsMask;
                        if (backlinkCommandBits == 0x10u || backlinkCommandBits == 0x14u)
                            backlink44->SetCommand(0, nullptr);

                        VID* const backlinkVid = backlink44->Vid();
                        if (backlinkVid->spriteClassId() == 0x15u &&
                            ((backlink44->m_runtimeFlags & SPRITE::CommandBitsMask) == 0x74u))
                        {
                            backlink44->dispatchEnginePrivateCommand(0x1E, 0, 0, 0);
                        }
                    }

                    return SetCommand(0, nullptr);
                }
            }
        }

        return m_currentAnimation;
    }

    int SPRITE::hasLinkedVidChild() const noexcept
    {
        SPRITE* const child = childChain();
        return child && child->Vid() == Vid()->linkedVid() ? 1 : 0;
    }

    int SPRITE::canWeaponAffectTarget(SPRITE* owner) noexcept
    {
        if (!owner)
            return 0;

        VID* const vid = m_vid;
        const std::uint32_t ownerType = owner->m_vid->spriteTypeId();

        if (vid->hasWeaponChildDescriptor() != 0u &&
            vid->weaponCount() != 0u &&
            (static_cast<std::uint32_t>(vid->weaponTypeMask()) & ownerType) != 0u)
        {
            return 1;
        }

        SPRITE* const child = m_childChain;
        if (!child)
            return 0;

        VID* const childVid = child->m_vid;
        VID* const linkVid = vid->linkedVid();
        if (childVid != linkVid)
            return 0;
        if (childVid->hasWeaponChildDescriptor() == 0u || childVid->weaponCount() == 0u)
            return 0;

        return (static_cast<std::uint32_t>(childVid->weaponTypeMask()) & ownerType) != 0u ? 1 : 0;
    }

    int SPRITE::setAttackCommandForTarget(SPRITE* owner) noexcept
    {
        VID* const vid = m_vid;
        if (vid->spriteClassId() == 12u && vid->hasWeaponChildDescriptor() != 0u)
        {
            const int savedAnimation = m_currentAnimation;
            m_currentAnimation = 8;
            SetCommand(4, owner);
            spawnAnimationChild();
            m_currentAnimation = savedAnimation;
            SetCommand(0, nullptr);
            return 0;
        }

        if (!owner ||
            owner->Vid() == MAP::NullVid() ||
            canWeaponAffectTarget(owner) != 0 ||
            vid->spriteClassId() == 5u ||
            dispatchVirtualAction(ActionCode::ACT_IS_TRAIN, 0, 0, 0) != 0)
        {
            SPRITE* const child = childChain();
            if (child)
            {
                // Retail reads these two slots before the common tail but does
                // not branch on them in this function.
                (void)child->Vid();
                (void)vid->linkedVid();
            }
            SetCommand(3, owner);
        }
        return 0;
    }

    int SPRITE::sumLinkedChildContributions() noexcept
    {
        int result = 0;
        for (SPRITE* node = this; node; node = node->childChain())
        {
            VID* const nodeVid = node->Vid();
            VID* const metricVid = nodeVid->fightChildVid();
            if (metricVid && nodeVid->weaponCount() != 0u)
                result += metricVid->calculateLinkedContribution();
        }
        return result;
    }

    SPRITE* SPRITE::engineChainHead() noexcept
    {
        SPRITE* result = this;
        for (SPRITE* node = engineChainPreviousRef(); node; node = node->engineChainPreviousRef())
            result = node;
        return result;
    }

    bool SPRITE::isInEngineChain(SPRITE* target) noexcept
    {
        if (!target)
            return false;

        for (SPRITE* node = this; node; node = node->engineChainNextRef())
        {
            if (node == target)
                return true;
        }
        for (SPRITE* node = engineChainPreviousRef(); node; node = node->engineChainPreviousRef())
        {
            if (node == target)
                return true;
        }
        return false;
    }

    int SPRITE::minimumEngineWeaponRange() noexcept
    {
        if (!goalSprite())
            return 0;

        const std::uint32_t actionBits = runtimeFlags() & SPRITE::CommandBitsMask;
        if (actionBits != 0x70u && actionBits != 0x74u)
            return 0;

        const auto candidateHeight = [](SPRITE* node) noexcept -> float
        {
            SPRITE* const child = node->childChain();
            if (!child)
                return 10000.0f;

            VID* const nodeVid = node->Vid();
            VID* const childVid = child->Vid();
            if (childVid != nodeVid->linkedVid())
                return 10000.0f;
            if (childVid->hasWeaponChildDescriptor() == 0u || childVid->weaponCount() == 0u)
                return 10000.0f;
            if (node->ammoCount() <= 0)
                return 10000.0f;

            VID* valueOwner = childVid;
            if (nodeVid->nvid() == 35)
                valueOwner = nodeVid;

            const float value = valueOwner->weaponBattleRange();

            return x87LessOrUnordered(value, 10000.0f) ? value : 10000.0f;
        };

        float result = 10000.0f;
        if (SPRITE* const refOwner = engineCommandReferenceOwnerRef())
        {
            result = candidateHeight(refOwner);
        }
        else
        {
            for (SPRITE* node = engineChainHead(); node; node = node->engineChainNextRef())
            {
                const float value = candidateHeight(node);
                if (result > value)
                    result = value;
            }
        }

        if (x87EqualOrUnordered(result, 10000.0f))
            result = 0.0f;

        return spriteFtolLow32(static_cast<long double>(result));
    }

    int SPRITE::updateSecondaryPathPosition(core::PathPosition* pathPair) noexcept
    {
        using PathNode = core::WeakController;
        using PathEdge = core::WeakController::Link;

        const int primaryProgress = primaryPathProgressRef();
        VID* const vid = Vid();
        const float radiusFloat = vid->weaponRadius();
        const int radiusLimit = spriteFtolLow32(static_cast<long double>(radiusFloat));

        auto edgeAt = [](PathNode* node, int index) noexcept -> PathEdge&
        {
            return *node->linkAt(index);
        };

        if (!spriteFildIntLessEqualOrUnordered(primaryProgress, radiusFloat))
        {
            PathEdge& edge = edgeAt(primaryPathNodeRef(), primaryPathEdgeIndexRef());
            secondaryPathNodeRef() = edge.target;
            secondaryPathProgressRef() = spriteSub32Wrap(static_cast<int>(edge.length), primaryProgress);
            secondaryPathAuxiliaryRef() = 0;
            secondaryPathEdgeIndexRef() = edge.reciprocalIndex;
            const int result = spriteSub32Wrap(
                spriteAdd32Wrap(static_cast<int>(edge.length), radiusLimit), primaryProgress);
            secondaryPathProgressRef() = result;
            return result;
        }

        if (secondaryPathNodeRef() == primaryPathNodeRef())
        {
            const int result = spriteSub32Wrap(radiusLimit, primaryProgress);
            secondaryPathProgressRef() = result;
            return result;
        }

        PathNode* const argumentNode = pathPair->node;
        if (argumentNode == primaryPathNodeRef())
        {
            int bridgeIndex = core::findLinkIndex(primaryPathNodeRef(), secondaryPathNodeRef());
            if (bridgeIndex < 0)
            {
                LOG::ResourceError("ENGINE %i", 10, "rail\t1", 0,
                                   vid ? vid->nvid() : -1);
                unsigned char facing = 0;
                if (primaryPathNodeRef())
                    facing = static_cast<unsigned char>(edgeAt(primaryPathNodeRef(),
                                                               primaryPathEdgeIndexRef()).facing);
                bridgeIndex = core::findClosestFacingLink(primaryPathNodeRef(),
                                                static_cast<unsigned char>(facing - 128));
                secondaryPathNodeRef() = edgeAt(primaryPathNodeRef(), bridgeIndex).target;
            }

            const int duration = static_cast<int>(edgeAt(primaryPathNodeRef(), bridgeIndex).length);
            int result = 0;
            const int durationPlusPrimary = spriteAdd32Wrap(duration, primaryProgress);
            if (spriteFildIntLessEqualOrUnordered(durationPlusPrimary, radiusFloat))
            {
                result = spriteSub32Wrap(spriteSub32Wrap(radiusLimit, duration), primaryProgress);
                secondaryPathProgressRef() = result;
            }
            else if (primaryPathEdgeIndexRef() == bridgeIndex)
            {
                result = spriteSub32Wrap(spriteAdd32Wrap(duration, radiusLimit), primaryProgress);
                secondaryPathProgressRef() = result;
            }
            else
            {
                secondaryPathNodeRef() = primaryPathNodeRef();
                secondaryPathEdgeIndexRef() = bridgeIndex;
                result = spriteSub32Wrap(radiusLimit, primaryProgress);
                secondaryPathProgressRef() = result;
            }
            return result;
        }

        if (argumentNode == secondaryPathNodeRef())
        {
            const int duration = argumentNode
                ? static_cast<int>(edgeAt(argumentNode, pathPair->edgeIndex).length)
                : 0;
            const int primaryPlusDuration = spriteAdd32Wrap(primaryProgress, duration);
            if (spriteFildIntLessEqualOrUnordered(primaryPlusDuration, radiusFloat))
            {
                const int result = spriteSub32Wrap(spriteSub32Wrap(radiusLimit, primaryProgress), duration);
                secondaryPathProgressRef() = result;
                return result;
            }

            PathEdge& edge = edgeAt(argumentNode, pathPair->edgeIndex);
            secondaryPathNodeRef() = edge.target;
            secondaryPathProgressRef() = spriteSub32Wrap(
                static_cast<int>(edge.length), pathPair->progress);
            secondaryPathAuxiliaryRef() = 0;
            secondaryPathEdgeIndexRef() = edge.reciprocalIndex;
            const int result = spriteSub32Wrap(radiusLimit, primaryProgress);
            secondaryPathProgressRef() = result;
            return result;
        }

        const int argumentDuration = argumentNode
            ? static_cast<int>(edgeAt(argumentNode, pathPair->edgeIndex).length)
            : 0;
        const int argumentPlusPrimary = spriteAdd32Wrap(argumentDuration, primaryProgress);
        if (spriteFildIntLessEqualOrUnordered(argumentPlusPrimary, radiusFloat))
        {
            writeLogLine(g_fileLogger, "zmdots6");
            int bridgeIndex = core::findLinkIndex(argumentNode, secondaryPathNodeRef());
            secondaryPathEdgeIndexRef() = bridgeIndex;
            secondaryPathNodeRef() = argumentNode;
            const int result = spriteSub32Wrap(
                spriteSub32Wrap(radiusLimit, argumentDuration), primaryProgress);
            secondaryPathProgressRef() = result;
            if (bridgeIndex < 0)
            {
                LOG::ResourceError("ENGINE %i", 10, "rail\t2", 0,
                                   vid ? vid->nvid() : -1);
                unsigned char facing = 0;
                if (argumentNode)
                    facing = static_cast<unsigned char>(edgeAt(argumentNode,
                                                               pathPair->edgeIndex).facing);
                bridgeIndex = core::findClosestFacingLink(secondaryPathNodeRef(),
                                                static_cast<unsigned char>(facing - 128));
                secondaryPathEdgeIndexRef() = bridgeIndex;
                return bridgeIndex;
            }
            return result;
        }

        writeLogLine(g_fileLogger, "zmdots5");
        PathEdge& edge = edgeAt(argumentNode, pathPair->edgeIndex);
        secondaryPathNodeRef() = edge.target;
        secondaryPathProgressRef() = spriteSub32Wrap(
            static_cast<int>(edge.length), pathPair->progress);
        secondaryPathAuxiliaryRef() = 0;
        secondaryPathEdgeIndexRef() = edge.reciprocalIndex;
        const int result = spriteSub32Wrap(radiusLimit, primaryProgress);
        secondaryPathProgressRef() = result;
        return result;
    }

    SPRITE* SPRITE::findEnginePathRelationSprite() noexcept
    {
        auto edgeTarget = [](core::WeakController* node, int index) noexcept -> core::WeakController*
        {
            return node ? node->linkAt(index)->target : nullptr;
        };

        SPRITE* candidate = primaryPathNodeRef()->ownerSprite();
        if (candidate && classifyEngineChainRelation(candidate, 0))
            return primaryPathNodeRef()->ownerSprite();

        core::WeakController* target = edgeTarget(primaryPathNodeRef(),
                                                   primaryPathEdgeIndexRef());
        if (target->ownerSprite() && classifyEngineChainRelation(target->ownerSprite(), 0))
            return edgeTarget(primaryPathNodeRef(),
                              primaryPathEdgeIndexRef())->ownerSprite();

        candidate = secondaryPathNodeRef()->ownerSprite();
        if (candidate && classifyEngineChainRelation(candidate, 0))
            return secondaryPathNodeRef()->ownerSprite();

        target = edgeTarget(secondaryPathNodeRef(), secondaryPathEdgeIndexRef());
        if (target->ownerSprite() && classifyEngineChainRelation(target->ownerSprite(), 0))
            return edgeTarget(secondaryPathNodeRef(),
                              secondaryPathEdgeIndexRef())->ownerSprite();
        return nullptr;
    }

    int SPRITE::classifyEngineChainRelation(SPRITE* target, int strictProgressGate) noexcept
    {
        if (!target || isInEngineChain(target))
            return 0;

        auto edgeTarget = [](core::WeakController* node, int index) noexcept -> core::WeakController*
        {
            return node ? node->links()[static_cast<std::size_t>(index)].target : nullptr;
        };
        auto edgeDuration = [](core::WeakController* node, int index) noexcept -> int
        {
            return node ? static_cast<int>(node->links()[static_cast<std::size_t>(index)].length) : 0;
        };
        auto progressPasses = [strictProgressGate](int progress, int duration, int otherProgress) noexcept -> bool
        {
            const int threshold = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(duration) -
                static_cast<std::uint32_t>(otherProgress));
            return strictProgressGate ? progress > threshold : progress >= threshold;
        };

        core::WeakController* const primary = primaryPathNodeRef();
        if (primary)
        {
            core::WeakController* const targetPrimary = target->primaryPathNodeRef();
            if (targetPrimary &&
                edgeTarget(primary, primaryPathEdgeIndexRef()) == targetPrimary &&
                edgeTarget(targetPrimary, target->primaryPathEdgeIndexRef()) == primary &&
                progressPasses(primaryPathProgressRef(),
                               edgeDuration(primary, primaryPathEdgeIndexRef()),
                               target->primaryPathProgressRef()))
            {
                return 1;
            }

            core::WeakController* const targetSecondary = target->secondaryPathNodeRef();
            if (targetSecondary &&
                edgeTarget(primary, primaryPathEdgeIndexRef()) == targetSecondary &&
                edgeTarget(targetSecondary, target->secondaryPathEdgeIndexRef()) == primary &&
                progressPasses(primaryPathProgressRef(),
                               edgeDuration(primary, primaryPathEdgeIndexRef()),
                               target->secondaryPathProgressRef()))
            {
                return 2;
            }
        }

        core::WeakController* const secondary = secondaryPathNodeRef();
        if (secondary)
        {
            core::WeakController* const targetPrimary = target->primaryPathNodeRef();
            if (targetPrimary &&
                edgeTarget(secondary, secondaryPathEdgeIndexRef()) == targetPrimary &&
                edgeTarget(targetPrimary, target->primaryPathEdgeIndexRef()) == secondary &&
                progressPasses(secondaryPathProgressRef(),
                               edgeDuration(secondary, secondaryPathEdgeIndexRef()),
                               target->primaryPathProgressRef()))
            {
                return 3;
            }

            core::WeakController* const targetSecondary = target->secondaryPathNodeRef();
            if (targetSecondary &&
                edgeTarget(secondary, secondaryPathEdgeIndexRef()) == targetSecondary &&
                edgeTarget(targetSecondary, target->secondaryPathEdgeIndexRef()) == secondary &&
                progressPasses(secondaryPathProgressRef(),
                               edgeDuration(secondary, secondaryPathEdgeIndexRef()),
                               target->secondaryPathProgressRef()))
            {
                return 4;
            }
        }

        if (!strictProgressGate)
        {
            if (primary)
            {
                if (primary == target->primaryPathNodeRef())
                    return 100;
                if (primary == target->secondaryPathNodeRef())
                    return 200;
            }
            if (secondary)
            {
                if (secondary == target->primaryPathNodeRef())
                    return 300;
                if (secondary == target->secondaryPathNodeRef())
                    return 400;
            }
            return 0;
        }

        if (primary)
        {
            if (primary == target->primaryPathNodeRef())
                return primaryPathEdgeIndexRef() == target->primaryPathEdgeIndexRef() ? 2 : 1;
            if (primary == target->secondaryPathNodeRef())
                return 200;
        }
        if (secondary)
        {
            if (secondary == target->primaryPathNodeRef())
                return 300;
            if (secondary == target->secondaryPathNodeRef())
                return 400;
        }
        return 0;
    }

    SPRITE* SPRITE::engineChainTail() noexcept
    {
        SPRITE* result = this;
        for (SPRITE* node = engineChainNextRef(); node; node = node->engineChainNextRef())
            result = node;
        return result;
    }

    SPRITE* SPRITE::reverseEngineChain() noexcept
    {
        SPRITE* const head = engineChainHead();
        for (SPRITE* node = head; node; )
        {
            SPRITE* const oldNext = node->engineChainNextRef();
            node->engineChainNextRef() = node->engineChainPreviousRef();
            node->engineChainPreviousRef() = oldNext;

            std::swap(node->primaryPathNodeRef(), node->secondaryPathNodeRef());
            std::swap(node->primaryPathProgressRef(), node->secondaryPathProgressRef());
            std::swap(node->primaryPathAuxiliaryRef(), node->secondaryPathAuxiliaryRef());
            std::swap(node->primaryPathEdgeIndexRef(), node->secondaryPathEdgeIndexRef());
            node->setDerivedStateValue(0, node->derivedStateValue(0) ^ 1);

            if (!oldNext)
            {
                if (node == head)
                {
                    node->engineTargetSpeedRef() = -head->engineTargetSpeedRef();
                }
                else
                {
                    node->m_runtimeFlags = (node->m_runtimeFlags & ~MovementStartedFlag) | (head->m_runtimeFlags & MovementStartedFlag);
                    node->m_speed = head->m_speed;
                    node->engineTargetSpeedRef() = -head->engineTargetSpeedRef();
                    node->engineAccelerationDelayRef() = head->engineAccelerationDelayRef();
                    const int pathByteCount = head->pathBufferSizeRef();
                    std::memcpy(node->pathBufferData(),
                                head->pathBufferData(),
                                static_cast<std::size_t>(pathByteCount));
                    node->pathBufferSizeRef() = pathByteCount;
                    head->pathBufferSizeRef() = 0;
                }
            }
            node = oldNext;
        }

        return head;
    }

    int SPRITE::attachEngineChain(SPRITE* target) noexcept
    {
        if (!target || isInEngineChain(target))
            return 1;

        auto edgeAt = [](core::WeakController* node, int index) noexcept -> core::WeakController::Link&
        {
            return *node->linkAt(index);
        };
        auto distance3 = [this](core::WeakController* node) noexcept -> double
        {
            const double dx = static_cast<double>(node->x()) - static_cast<double>(m_xyz.x);
            const double dy = static_cast<double>(node->y()) - static_cast<double>(m_xyz.y);
            const double dz = static_cast<double>(node->id()) - static_cast<double>(m_xyz.z);
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        };

        if (!target->engineChainPreviousRef() && !target->engineChainNextRef())
        {
            core::WeakController* const primaryEnd =
                edgeAt(target->primaryPathNodeRef(),
                       target->primaryPathEdgeIndexRef()).target;
            core::WeakController* const secondaryEnd =
                edgeAt(target->secondaryPathNodeRef(),
                       target->secondaryPathEdgeIndexRef()).target;
            if (distance3(secondaryEnd) > distance3(primaryEnd))
                target->reverseEngineChain();
        }
        else
        {
            SPRITE* const first = target->engineChainHead();
            const double firstDx = static_cast<double>(first->m_xyz.x) - static_cast<double>(m_xyz.x);
            const double firstDy = static_cast<double>(first->m_xyz.y) - static_cast<double>(m_xyz.y);
            SPRITE* const last = target->engineChainTail();
            const double lastDx = static_cast<double>(last->m_xyz.x) - static_cast<double>(m_xyz.x);
            const double lastDy = static_cast<double>(last->m_xyz.y) - static_cast<double>(m_xyz.y);
            if (std::sqrt(lastDx * lastDx + lastDy * lastDy) >
                std::sqrt(firstDx * firstDx + firstDy * firstDy))
            {
                target->reverseEngineChain();
            }
        }

        SPRITE* const tail = target->engineChainTail();
        tail->engineChainNextRef() = this;
        engineChainPreviousRef() = tail;

        core::WeakController::Link& source =
            edgeAt(tail->secondaryPathNodeRef(), tail->secondaryPathEdgeIndexRef());
        primaryPathNodeRef() = source.target;
        primaryPathProgressRef() = static_cast<int>(source.length) - tail->secondaryPathProgressRef();
        primaryPathAuxiliaryRef() = 0;
        primaryPathEdgeIndexRef() = source.reciprocalIndex;

        const unsigned char facing = static_cast<unsigned char>(
            edgeAt(tail->secondaryPathNodeRef(), tail->secondaryPathEdgeIndexRef()).facing);
        const int secondarySourceIndex = core::findClosestFacingLink(primaryPathNodeRef(), facing);
        secondaryPathNodeRef() = edgeAt(primaryPathNodeRef(), secondarySourceIndex).target;
        secondaryPathEdgeIndexRef() = core::findClosestFacingLink(secondaryPathNodeRef(), facing);

        unsigned char currentFacing = 0;
        if (primaryPathNodeRef())
            currentFacing = static_cast<unsigned char>(
                edgeAt(primaryPathNodeRef(), primaryPathEdgeIndexRef()).facing);
        const unsigned char deltaA = static_cast<unsigned char>(directionIndex() - currentFacing);
        const unsigned char deltaB = static_cast<unsigned char>(currentFacing - directionIndex());
        const unsigned char delta = deltaA < deltaB ? deltaA : deltaB;
        if (delta > 127)
            setDerivedStateValue(0, derivedStateValue(0) | 1);

        core::PathPosition primary{
            primaryPathNodeRef(),
            primaryPathProgressRef(),
            primaryPathAuxiliaryRef(),
            primaryPathEdgeIndexRef()};
        updateSecondaryPathPosition(&primary);
        updatePositionFromPathEndpoints();
        return 0;
    }

    void SPRITE::clearPathNodeOwnership() noexcept
    {
        if (primaryPathNodeRef() && primaryPathNodeRef()->ownerSprite() == this)
            primaryPathNodeRef()->setOwnerSprite(nullptr);

        if (secondaryPathNodeRef() && secondaryPathNodeRef()->ownerSprite() == this)
            secondaryPathNodeRef()->setOwnerSprite(nullptr);

        core::WeakController* primary = primaryPathNodeRef();
        if (primary)
        {
            core::WeakController* const target =
                primary->links()[static_cast<std::size_t>(primaryPathEdgeIndexRef())].target;
            if (target && target->ownerSprite() == this)
                target->setOwnerSprite(nullptr);
        }

        core::WeakController* secondary = secondaryPathNodeRef();
        if (secondary)
        {
            core::WeakController* const target =
                secondary->links()[static_cast<std::size_t>(secondaryPathEdgeIndexRef())].target;
            if (target && target->ownerSprite() == this)
                target->setOwnerSprite(nullptr);
        }

    }

    core::WeakController* SPRITE::claimPathNodeOwnership() noexcept
    {
        clearPathNodeOwnership();
        if (primaryPathNodeRef())
            primaryPathNodeRef()->setOwnerSprite(this);
        core::WeakController* result = secondaryPathNodeRef();
        if (result && result != primaryPathNodeRef())
            result->setOwnerSprite(this);
        return result;
    }

    SPRITE* SPRITE::validateEngineChainLinks() noexcept
    {
        if (SPRITE* const previous = engineChainPreviousRef())
        {
            SPRITE* const actualNext = previous->engineChainNextRef();
            if (actualNext != this)
            {
                const int nvid = Vid() ? Vid()->nvid() : -1;
                LOG::ResourceError("ENGINE %i", 4, "PrevEngine->NextEngine!=this",
                                   static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(actualNext))),
                                   nvid);
                previous->engineChainNextRef() = this;
            }
        }

        SPRITE* result = engineChainNextRef();
        if (result)
        {
            SPRITE* const actualPrevious = result->engineChainPreviousRef();
            if (actualPrevious != this)
            {
                const int nvid = Vid() ? Vid()->nvid() : -1;
                LOG::ResourceError("ENGINE %i", 4, "NextEngine->PrevEngine!=this",
                                   static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(actualPrevious))),
                                   nvid);
                result = engineChainNextRef();
                result->engineChainPreviousRef() = this;
            }
        }
        return result;
    }

    int SPRITE::isSpriteClass(int spriteClass) const noexcept
    {
        return m_vid->spriteClassId() == static_cast<DWORD>(spriteClass) ? 1 : 0;
    }

    SPRITE::EngineChainMetrics* SPRITE::EngineChainMetrics::collectEngineChainMetrics(SPRITE* root) noexcept
    {
        categoryFlags &= 0xFFFFFFFCu;
        maxBattleRange = 0.0f;
        minBattleRange = 999999.0f;
        weapon10Sum = 0.0f;
        weapon0CSum = 0.0f;
        activeWeapon0CSum = 0.0f;
        movementDelayMs = 10000;
        spriteCount = 0;
        spriteFrameTimeSum = 0;
        vidFrameTimeSum = 0;
        routeMetricSum = 0;
        weaponMetricSum = 0;
        distanceSampleCount = 0;
        fixedDistanceSum = 0;
        distanceWeightSum = 0;
        averageDistanceRatio = 0;

        for (SPRITE* node = root; node; node = node->engineChainNext())
            accumulateEngineChainSprite(node);

        for (SPRITE* node = root->engineChainPrevious(); node; node = node->engineChainPrevious())
            accumulateEngineChainSprite(node);

        if (distanceSampleCount != 0)
            averageDistanceRatio /= distanceSampleCount;
        else
            averageDistanceRatio = 100;

        if (minBattleRange == 999999.0f)
            minBattleRange = 0.0f;

        const double denominator = static_cast<double>(weapon10Sum) -
                                   static_cast<double>(activeWeapon0CSum);

        if (!std::isnan(denominator) && denominator != 0.0)
        {
            const double numerator = denominator -
                (static_cast<double>(weapon0CSum) - static_cast<double>(activeWeapon0CSum));
            const int projected = static_cast<int>((numerator * static_cast<double>(movementDelayMs)) / denominator);
            movementDelayMs = projected;
            if (projected < 5)
                movementDelayMs = 0;
        }

        if (movementDelayMs == 10000)
            movementDelayMs = 0;
        return this;
    }

    int SPRITE::EngineChainMetrics::accumulateEngineChainSprite(SPRITE* sprite) noexcept
    {
        VID* const vid = sprite->Vid();
        const float weapon10 = vid->weaponFloatAt(0x10);
        const float weapon0C = vid->weaponFloatAt(0x0C);

        if (!x87EqualOrUnordered(weapon10, 0.0f))
        {
            const double candidateDelay = static_cast<double>(vid->maxSpeedValue()) * 1000.0;
            const double currentDelay = static_cast<double>(movementDelayMs);
            if (candidateDelay < currentDelay ||
                std::isnan(candidateDelay) || std::isnan(currentDelay))
            {
                movementDelayMs = spriteFtolLow32(static_cast<long double>(candidateDelay));
            }
        }

        weapon10Sum += weapon10;
        weapon0CSum += weapon0C;
        if (weapon10 > 0.0f)
            activeWeapon0CSum += weapon0C;

        if (vid->nvid() == 45)
            categoryFlags |= 2u;
        else
            categoryFlags |= 1u;

        spriteFrameTimeSum += sprite->animationFrameTime();

        const int fixedDistance = sprite->ammoFixedPoint() / 64;
        const int vidWeaponMetric = vid->activeWeaponAmmoCapacity();

        if (fixedDistance > 0)
        {
            float weapon18 = vid->weaponBattleRange();
            if (SPRITE* const child = sprite->childChain())
            {
                VID* const link = vid->linkedVid();
                VID* const childVid = child->Vid();
                if (childVid == link && link->hasWeaponChildDescriptor() != 0u &&
                    link->weaponCount() != 0u &&
                    x87EqualOrUnordered(weapon18, 0.0f))
                {
                    weapon18 = link->weaponBattleRange();
                }
            }

            if (weapon18 > maxBattleRange)
                maxBattleRange = weapon18;
            if (weapon18 != 0.0f && weapon18 < minBattleRange)
                minBattleRange = weapon18;
        }

        if (vidWeaponMetric != 0 && vidWeaponMetric != 999999 && vid->nvid() != 85)
        {
            ++distanceSampleCount;
            distanceWeightSum += vidWeaponMetric;
            fixedDistanceSum += fixedDistance;
            averageDistanceRatio += (100 * fixedDistance) / vidWeaponMetric;
        }

        const int bucket = sprite->armyIndex();
        vidFrameTimeSum += vid->animationFrameDuration(bucket);
        weaponMetricSum += vid->getWeaponValue24Scaled();

        int routeMetric = 0;
        if (fixedDistance != 0)
        {
            if (vid->nvid() == 82)
            {
                const core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
                VID* metricVid = MAP::NullVid();
                if (appVidTable.count() > 70)
                {
                    if (VID* const slot70 = appVidTable.slot(70))
                        metricVid = slot70;
                }
                routeMetric = metricVid->calculateLinkedContribution();
            }
            else
            {
                routeMetric = sprite->sumLinkedChildContributions();
            }
        }
        routeMetricSum += routeMetric;

        if (VID* const link = vid->linkedVid())
        {
            vidFrameTimeSum += link->animationFrameDuration(bucket);

            SPRITE* const child = sprite->childChain();
            VID* const childVid = child ? child->Vid() : nullptr;
            if (childVid != link &&
                static_cast<int>(link->spriteCountForArmy(bucket)) >=
                    static_cast<int>(vid->spriteCountForArmy(bucket)))
            {
                spriteFrameTimeSum += link->animationFrameDuration(bucket);
            }

            if (childVid == link)
            {
                weapon0CSum += childVid->weaponFloatAt(0x0C);
                if (childVid->spriteClassId() != 9u)
                    spriteFrameTimeSum += child->animationFrameTime();
                if (weapon10 > 0.0f)
                    activeWeapon0CSum += childVid->weaponFloatAt(0x0C);
            }
        }

        ++spriteCount;
        return spriteCount;
    }

    int SPRITE::EngineChainMetrics::weaponRatioScaledByEight() const noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        static const float kZero = 0.0f;
        static const float kEight = 8.0f;
        const EngineChainMetrics* const owner = this;
        unsigned short status = 0;
        __asm
        {
            mov ecx, owner
            fld dword ptr [ecx+10h]
            fcomp kZero
            fnstsw ax
            mov status, ax
        }
        if ((status & 0x4000u) != 0u)
            return 0;

        __int64 converted = 0;
        unsigned short oldControl = 0;
        unsigned short truncateControl = 0;
        __asm
        {
            mov ecx, owner
            fld dword ptr [ecx+0Ch]
            fdiv dword ptr [ecx+10h]
            fmul kEight
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
        const float denominator = weapon0CSum;
        if (denominator == 0.0f || std::isnan(denominator))
            return 0;
        const long double scaled =
            (static_cast<long double>(weapon10Sum) / static_cast<long double>(denominator)) * 8.0L;
        if (!std::isfinite(scaled) ||
            scaled >= 9223372036854775808.0L || scaled < -9223372036854775808.0L)
            return 0;
        const std::int64_t converted = static_cast<std::int64_t>(std::trunc(scaled));
        return static_cast<int>(static_cast<std::uint32_t>(converted));
#endif
    }

    SPRITE* SPRITE::findCrossingConstraintOwner() noexcept
    {
        struct RawConstraintPath
        {
            core::WeakController* node;
            int pad04;
            int edgeIndex08;
        };

        auto constraint = [](std::uint32_t rawValue) noexcept -> RawConstraintPath*
        {
            return reinterpret_cast<RawConstraintPath*>(static_cast<std::uintptr_t>(rawValue));
        };
        auto acceptableConstraintOwner = [this](RawConstraintPath* path) noexcept -> SPRITE*
        {
            SPRITE* owner = path->node->ownerSprite();
            if (owner && !owner->isInEngineChain(this))
                return owner;

            core::WeakController* const target =
                path->node->links()[static_cast<std::size_t>(path->edgeIndex08)].target;
            owner = target->ownerSprite();
            if (owner && !owner->isInEngineChain(this))
                return owner;
            return nullptr;
        };

        core::WeakController* const primary = primaryPathNodeRef();
        if (primary)
        {
            RawConstraintPath* const direct = constraint(
                primary->links()[static_cast<std::size_t>(primaryPathEdgeIndexRef())].crossingLinkToken);
            if (direct)
            {
                if (SPRITE* const owner = acceptableConstraintOwner(direct))
                    return owner;
            }
        }

        core::WeakController* const primaryAgain = primaryPathNodeRef();
        if (!primaryAgain)
            return nullptr;

        const core::WeakController::Link& current =
            primaryAgain->links()[static_cast<std::size_t>(primaryPathEdgeIndexRef())];
        if (!current.target)
            return nullptr;

        RawConstraintPath* const reciprocal = constraint(
            current.target->links()[static_cast<std::size_t>(current.reciprocalIndex)].crossingLinkToken);
        if (!reciprocal)
            return nullptr;
        return acceptableConstraintOwner(reciprocal);
    }

    SPRITE* SPRITE::resolvePathOwnerRelation(int* relationOut) noexcept
    {
        core::WeakController* const primary = primaryPathNodeRef();
        SPRITE* owner = primary->ownerSprite();
        if (owner)
        {
            const int relation = classifyEngineChainRelation(owner, 1);
            *relationOut = relation;
            if (relation != 0)
                return primary->ownerSprite();
        }

        core::WeakController* next = nullptr;
        core::WeakController* const currentPrimary = primaryPathNodeRef();
        if (currentPrimary)
            next = currentPrimary->links()[static_cast<std::size_t>(primaryPathEdgeIndexRef())].target;

        owner = next->ownerSprite();
        if (owner)
        {
            core::WeakController* currentNext = nullptr;
            core::WeakController* const source = primaryPathNodeRef();
            if (source)
                currentNext = source->links()[static_cast<std::size_t>(primaryPathEdgeIndexRef())].target;

            const int relation = classifyEngineChainRelation(currentNext->ownerSprite(), 1);
            *relationOut = relation;
            if (relation != 0)
            {
                core::WeakController* const returnSource = primaryPathNodeRef();
                core::WeakController* returnNode = nullptr;
                if (returnSource)
                    returnNode = returnSource->links()[static_cast<std::size_t>(primaryPathEdgeIndexRef())].target;
                return returnNode->ownerSprite();
            }
        }

        *relationOut = 0;
        return nullptr;
    }

    int SPRITE::canLinkEngineChain(SPRITE* target) noexcept
    {
        if (!target)
            return 0;

        const int thisAction = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
        const int targetAction = static_cast<int>(target->m_runtimeFlags & SPRITE::CommandBitsMask);

        if (target->isInEngineChain(goalSprite()) && thisAction == 0x68)
            return 1;

        if (isInEngineChain(target->goalSprite()) && targetAction == 0x68)
            return 1;

        if ((pushLineActiveRef() != 0 || target->pushLineActiveRef() != 0) &&
            (((target->m_runtimeFlags ^ m_runtimeFlags) & ArmyBitsMask) == 0))
            return 1;

        return 0;
    }

    float SPRITE::resolveEngineChainCollision(SPRITE* target, int mode) noexcept
    {
        EngineChainMetrics thisRange;
        thisRange.categoryFlags = 0;
        thisRange.collectEngineChainMetrics(this);
        EngineChainMetrics targetRange;
        targetRange.categoryFlags = 0;
        targetRange.collectEngineChainMetrics(target);

        float sharedSpeed = 0.0f;
        float relativeSpeed = 0.0f;
        computeCollisionKinematics(
            m_speed, target->m_speed,
            thisRange.weapon0CSum, targetRange.weapon0CSum,
            mode, sharedSpeed, relativeSpeed);

        if (mode == 1 || mode == 3)
            target->reverseEngineChain();

        for (SPRITE* node = target->engineChainHead(); node; node = node->engineChainNextRef())
            node->m_speed = (node->derivedStateValue(0) & 1) ? -sharedSpeed : sharedSpeed;

        float collisionLimit = 0.0f;
        const BASE_CONSTANTS* const constants = GlobalBaseConstants();
        std::memcpy(&collisionLimit, &constants->raw[24], sizeof(collisionLimit));

        const bool damageRoute =
            ((m_runtimeFlags & SPRITE::CommandBitsMask) == 108u && target->isInEngineChain(goalSprite())) ||
            ((target->m_runtimeFlags & SPRITE::CommandBitsMask) == 108u && isInEngineChain(target->goalSprite()));
        if (relativeSpeed > collisionLimit && damageRoute)
        {
            ChangeAnimation(12);
            playSfxAtWorldPosition(16);

            const int damage = spriteFmulFtolLow32(relativeSpeed, 1500.0f);
            int thisDamage = damage / 2;
            int targetDamage = damage / 2;

            VID* const targetVid = target->Vid();
            int targetActionValue = targetDamage;
            if (targetVid->nvid() == 97)
            {
                const int bucket = target->armyIndex();
                targetActionValue = targetVid->animationFrameDuration(bucket) + 10;
            }
            target->dispatchVirtualAction(ActionCode::ACT_DAMAGE, targetActionValue, 0, 0);

            for (SPRITE* node = target->engineChainPreviousRef(); node; node = node->engineChainPreviousRef())
            {
                if (targetDamage < 2)
                    break;
                targetDamage /= 2;
                node->dispatchVirtualAction(ActionCode::ACT_DAMAGE, targetDamage, 0, 0);
            }

            VID* const thisVid = Vid();
            int thisActionValue = thisDamage;
            if (thisVid->nvid() == 97)
            {
                const int bucket = armyIndex();
                thisActionValue = thisVid->animationFrameDuration(bucket) + 10;
            }
            dispatchVirtualAction(ActionCode::ACT_DAMAGE, thisActionValue, 0, 0);

            SPRITE* node = engineChainPreviousRef();
            if (node)
            {
                while (node)
                {
                    if (thisDamage < 2)
                        return 0.0f;
                    thisDamage /= 2;
                    node->dispatchVirtualAction(ActionCode::ACT_DAMAGE, thisDamage, 0, 0);
                    node = node->engineChainPreviousRef();
                }
                return 0.0f;
            }

            node = engineChainNextRef();
            if (node)
            {
                while (node)
                {
                    if (thisDamage < 2)
                        return 0.0f;
                    thisDamage /= 2;
                    node->dispatchVirtualAction(ActionCode::ACT_DAMAGE, thisDamage, 0, 0);
                    node = node->engineChainNextRef();
                }
                return 0.0f;
            }
        }
        else
        {
            playSfxAtWorldPosition(19);
        }
        return 0.0f;
    }

    int SPRITE::resolveEngineChainPathInteraction(core::PathPosition* pathPair, float* distanceOut) noexcept
    {
        int relation = -1;
        int result = 0;
        SPRITE* resolved = resolvePathOwnerRelation(&relation);
        if (resolved)
        {
            primaryPathNodeRef() = pathPair->node;
            primaryPathProgressRef() = pathPair->progress;
            primaryPathAuxiliaryRef() = pathPair->auxiliary;
            primaryPathEdgeIndexRef() = pathPair->edgeIndex;

            if (canLinkEngineChain(resolved))
            {
                if (resolved->engineChainNextRef() || relation == 1 || relation == 4)
                    resolved->reverseEngineChain();

                if (((m_runtimeFlags ^ resolved->m_runtimeFlags) & ArmyBitsMask) == 0)
                {
                    if ((m_runtimeFlags & SPRITE::CommandBitsMask) == 0x68u &&
                        resolved->isInEngineChain(goalSprite()))
                    {
                        if ((resolved->m_runtimeFlags & SPRITE::CommandBitsMask) != 0x68u ||
                            !isInEngineChain(resolved->goalSprite()))
                        {
                            resolved->dispatchVirtualAction(ActionCode::ACT_BACKUP_COMMAND, 0, 0, 0);
                        }
                    }
                    else
                    {
                        dispatchVirtualAction(ActionCode::ACT_BACKUP_COMMAND, 0, 0, 0);
                    }
                }

                if (resolved->engineChainNextRef())
                {
                    const int nvid = Vid() ? Vid()->nvid() : -1;
                    LOG::ResourceError("ENGINE %i", 10, "can't link", 0, nvid);
                }
                else
                {
                    resolved->engineChainNextRef() = this;
                    engineChainPreviousRef() = resolved;
                }

                dispatchEnginePrivateCommand(0, 0, 0, 0);
                ChangeAnimation(0x0B);

                if (((resolved->m_runtimeFlags ^ m_runtimeFlags) & ArmyBitsMask) != 0)
                {
                    for (SPRITE* node = engineChainHead(); node; node = node->engineChainNextRef())
                    {
                        if ((node->m_runtimeFlags & ArmyBitsMask) != 0)
                            node->clearCommandsTargetingThisSprite();
                    }

                    SPRITE* callbackSprite = this;
                    if ((resolved->m_runtimeFlags & ArmyBitsMask) == (1u << ArmyBitsShift))
                        callbackSprite = resolved;
                    const int callbackArg = static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(callbackSprite)));
                    (void)core::Application::callScriptFunction(core::scriptCallbackSlot(21u), callbackArg, 0);
                    return 0;
                }
            }
            else
            {
                result = 1;
                *distanceOut = resolveEngineChainCollision(resolved, relation);
                const int selfArg = static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this)));
                const int resolvedArg = static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(resolved)));
                (void)core::Application::callScriptFunction(core::scriptCallbackSlot(22u), selfArg, resolvedArg);
            }
            return result;
        }

        if (!findCrossingConstraintOwner())
            return result;

        if (*distanceOut > 0.2f)
            playSfxAtWorldPosition(0x94);

        primaryPathNodeRef() = pathPair->node;
        primaryPathProgressRef() = pathPair->progress;
        primaryPathAuxiliaryRef() = pathPair->auxiliary;
        primaryPathEdgeIndexRef() = pathPair->edgeIndex;
        *distanceOut = 0.0f;
        engineTargetSpeedRef() = -engineTargetSpeedRef();
        return 1;
    }

    void SPRITE::applyEngineChainPathMovement(core::PathPosition* pathPair, float speed, int delay) noexcept
    {
        SPRITE* const root = this;
        for (SPRITE* node = this; node; node = node->engineChainNextRef())
        {
            node->clearPathNodeOwnership();
            node->updateSecondaryPathPosition(pathPair);

            SPRITE* const next = node->engineChainNextRef();
            if (next)
            {
                pathPair->node = next->primaryPathNodeRef();
                pathPair->progress = next->primaryPathProgressRef();
                pathPair->auxiliary = next->primaryPathAuxiliaryRef();
                pathPair->edgeIndex = next->primaryPathEdgeIndexRef();

                core::WeakController* const secondaryNode = node->secondaryPathNodeRef();
                const core::WeakController::Link& secondaryEdge =
                    secondaryNode->links()[static_cast<std::size_t>(node->secondaryPathEdgeIndexRef())];
                next->primaryPathNodeRef() = secondaryEdge.target;
                next->primaryPathProgressRef() = static_cast<int>(secondaryEdge.length) - node->secondaryPathProgressRef();
                next->primaryPathAuxiliaryRef() = 0;
                next->primaryPathEdgeIndexRef() = secondaryEdge.reciprocalIndex;
            }

            node->updatePositionFromPathEndpoints();
            if ((!x87EqualOrUnordered(node->previousPathXRef(), 0.0f) ||
                 !x87EqualOrUnordered(node->previousPathYRef(), 0.0f)) &&
                x87AbsDiffGreaterOrdered(node->previousPathXRef(), node->m_xyz.x, 30.0f))
            {
                writeLogLine(g_fileLogger, kTrainCollapseBeginLog);
            }

            node->previousPathXRef() = node->m_xyz.x;
            node->previousPathYRef() = node->m_xyz.y;
            node->previousPathZRef() = node->m_xyz.z;
            node->claimPathNodeOwnership();

            node->m_speed = (node->derivedStateValue(0) & 1) ? -speed : speed;
            node->engineAccelerationDelayRef() = delay;
        }

        if (!x87EqualOrUnordered(speed, 0.0f))
        {
            VID* const rootVid = root->Vid();
            const float rootX = root->m_xyz.x;
            const float rootY = root->m_xyz.y;
            const float rootZ = root->m_xyz.z;
            const float minX = rootX - rootVid->halfSizeX();
            const float minY = rootY - rootVid->halfSizeY();
            const float maxX = rootX + rootVid->halfSizeX();
            const float maxY = rootY + rootVid->halfSizeY();

            SPRITE_COLLECTOR_HASH_MAP* const hash = GlobalSpriteHashMap();
            for (SPRITE* candidate = hash->firstSpriteInBox(minX, minY, maxX, maxY);
                 candidate;
                 candidate = hash->nextSpriteInBox())
            {
                if (candidate == root)
                    continue;

                if (candidate->m_currentAnimation >= 0x0F)
                    continue;

                VID* const candidateVid = candidate->Vid();
                if (!x87SumGreaterThanAbsDiffOrdered(
                        candidateVid->halfSizeX(), rootVid->halfSizeX(),
                        candidate->m_xyz.x, rootX))
                    continue;
                if (!x87SumGreaterThanAbsDiffOrdered(
                        candidateVid->halfSizeY(), rootVid->halfSizeY(),
                        candidate->m_xyz.y, rootY))
                    continue;
                if (x87SumLessOrUnordered(candidateVid->sizeZ(), candidate->m_xyz.z, rootZ))
                    continue;
                if (x87SumLessOrUnordered(rootZ, rootVid->sizeZ(), candidate->m_xyz.z))
                    continue;

                if ((candidateVid->properties() & P_CRUSH) != 0)
                    candidate->dispatchVirtualAction(ActionCode::ACT_DAMAGE, 5, 0, 0);
            }
        }
    }

    void SPRITE::clearCommandsTargetingThisSprite() noexcept
    {
        SPRITE_COLLECTOR_HASH_MAP* const hash = GlobalSpriteHashMap();
        SPRITE_POINTER_LIST& list = hash->mutableOverflowList();
        int* const cursor = hash->reverseCursorAddress();

        for (SPRITE* candidate = list.beginReverseIteration(cursor);
             candidate;
             candidate = list.continueReverseIteration(cursor))
        {
            SPRITE* actionSprite = candidate;
            if (candidate->goalSprite() == this)
            {
                const int action = static_cast<int>(candidate->m_runtimeFlags & SPRITE::CommandBitsMask);
                if (candidate->Vid()->spriteClassId() == 0x15u &&
                    (action == 0x70 || action == 0x74))
                {
                    candidate->dispatchEnginePrivateCommand(0, 0, 0, 0);
                    continue;
                }

                if (action != 0x14 && action != 0x0C && action != 0x10)
                    continue;
            }
            else
            {
                actionSprite = candidate->childChain();
                if (!actionSprite || actionSprite->goalSprite() != this)
                    continue;

                const int action = static_cast<int>(actionSprite->m_runtimeFlags & SPRITE::CommandBitsMask);
                if (action != 0x14 && action != 0x0C && action != 0x10 &&
                    action != 0x70 && action != 0x74)
                    continue;
            }

            actionSprite->SetCommand(0, nullptr);
        }
    }

    int SPRITE::createRouteMarkerSprites(core::WeakController* pathNode) noexcept
    {
        SPRITE_POINTER_LIST& list = g_spriteWorkList;
        createPathSpritesFromBuffer(pathNode, &list, 603);

        int result = list.activeCount();
        for (int i = 0; i < list.activeCount(); ++i)
        {
            SPRITE* const value = list.data()[i];
            const int action = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
            switch (action)
            {
            case 112:
            case 116:
                value->changeArmyBucket(1);
                break;
            case 108:
                value->changeArmyBucket(3);
                break;
            case 104:
                value->changeArmyBucket(2);
                break;
            default:
                break;
            }
            result = list.activeCount();
        }
        return result;
    }

    int SPRITE::createPathSpritesFromBuffer(core::WeakController* pathNode, SPRITE_POINTER_LIST* list, int nvid) noexcept
    {
        g_pathSearchScore1 = core::pathResultScore();
        g_pathSearchScore0 = core::pathSecondaryBestCost();
        list->releaseRepeatedReferencesRetail();

        int result = pathBufferSizeRef();
        for (int i = 0; i < pathBufferSizeRef(); ++i)
        {
            const int edgeIndex = static_cast<int>(pathBufferData()[static_cast<std::size_t>(i)]);
            if (edgeIndex < pathNode->linkCount())
            {
                pathNode = pathNode->links()[static_cast<std::size_t>(edgeIndex)].target;

                VID* createVid = nullptr;
                core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
                if (nvid < 0 || nvid >= table.count() ||
                    (createVid = table.slot(nvid)) == nullptr)
                {
                    createVid = MAP::NullVid();
                }

                SPRITE* const created = hostState().owner->CreateSprite(
                    createVid,
                    VECTOR(static_cast<float>(pathNode->x()),
                           static_cast<float>(pathNode->y()),
                           static_cast<float>(pathNode->id())),
                    ANGLE(0), nullptr, false, false);
                list->append(created);
            }
            result = pathBufferSizeRef();
        }
        return result;
    }

    int SPRITE::pathBufferReachesSecondaryTarget(core::WeakController* pathNode) noexcept
    {
        SPRITE* const tail = engineChainTail();
        core::WeakController* const secondaryNode = tail->secondaryPathNode();
        core::WeakController* const secondaryTarget = secondaryNode
            ? secondaryNode->links()[static_cast<std::size_t>(tail->secondaryPathEdgeIndex())].target
            : nullptr;

        core::WeakController* walker = pathNode;
        const int count = pathBufferSizeRef();
        for (int index = 0; index < count; ++index)
        {
            const unsigned int edgeIndex = pathBufferData()[static_cast<std::size_t>(index)];
            if (edgeIndex < static_cast<unsigned int>(walker->linkCount()))
            {
                walker = walker->links()[edgeIndex].target;
                if (walker == secondaryTarget)
                    return 1;
            }
        }
        return 0;
    }

    int SPRITE::evaluateEngineTargetRangeState() noexcept
    {
        SPRITE* const owner = goalSprite();
        if (!owner)
            return 2;

        const int action = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
        if (action != 112 && action != 116)
            return 2;

        auto linkedChild = [](SPRITE* node) noexcept -> SPRITE*
        {
            SPRITE* const child = node->childChain();
            if (!child)
                return nullptr;
            VID* const childVid = child->Vid();
            VID* const nodeVid = node->Vid();
            if (childVid != nodeVid->linkedVid())
                return nullptr;
            if (childVid->hasWeaponChildDescriptor() == 0u || childVid->weaponCount() == 0u)
                return nullptr;
            return child;
        };

        if (SPRITE* const ref = engineCommandReferenceOwnerRef())
        {
            if (SPRITE* const child = linkedChild(ref))
            {
                VID* const childVid = child->Vid();
                // Retail 0x44CCF0 stores both coordinate deltas to m32 before
                // approximatePlanarDistance, then compares the live x87 result against
                // [WEAPON+0x18]-10 with TEST AH,41h (<= or unordered).
                const float dx = owner->m_xyz.x - child->m_xyz.x;
                const float dy = owner->m_xyz.y - child->m_xyz.y;
                return metricWithinFromRoundedDeltas(
                           dx, dy, childVid->weaponBattleRange()) ? 1 : 0;
            }

            VID* const refVid = ref->Vid();
            if (refVid->nvid() != 35)
                return 2;
            return metricWithinPositions(
                       owner->m_xyz.x, owner->m_xyz.y,
                       ref->m_xyz.x, ref->m_xyz.y,
                       refVid->weaponBattleRange()) ? 1 : 0;
        }

        int result = 2;
        for (SPRITE* node = engineChainHead(); node; node = node->engineChainNextRef())
        {
            if (SPRITE* const child = linkedChild(node))
            {
                VID* const childVid = child->Vid();
                // Loop routes 0x44CDB2+ keep the coordinate subtraction live
                // in x87 instead of spilling the deltas before the metric.
                if (!metricWithinPositions(
                        owner->m_xyz.x, owner->m_xyz.y,
                        child->m_xyz.x, child->m_xyz.y,
                        childVid->weaponBattleRange()))
                    return 0;
                result = 1;
                continue;
            }

            VID* const nodeVid = node->Vid();
            if (nodeVid->nvid() == 35)
            {
                if (!metricWithinPositions(
                        owner->m_xyz.x, owner->m_xyz.y,
                        node->m_xyz.x, node->m_xyz.y,
                        nodeVid->weaponBattleRange()))
                    return 0;
                result = 1;
            }
        }
        return result;
    }

    void SPRITE::updateEngineChainRoute() noexcept
    {
        core::PathPosition pathSnapshot{
            primaryPathNodeRef(),
            primaryPathProgressRef(),
            primaryPathAuxiliaryRef(),
            primaryPathEdgeIndexRef()};

        // Retail copies the C8..D4 state once into four local values for the final
        // resolveEngineChainPathInteraction/applyEngineChainPathMovement interpolation pair, but all routing logic
        // before that works on the live SPRITE+0xC8 owner.  Keep those two
        // domains separate: mutating only the snapshot loses route progress.
        auto liveNode = [this]() noexcept -> core::WeakController* { return primaryPathNodeRef(); };
        auto liveIndex = [this]() noexcept -> int { return primaryPathEdgeIndexRef(); };

        if (!liveNode() || !secondaryPathNodeRef() || engineChainPreviousRef())
            return;

        VID* const vid = Vid();
        if (vid->nvid() != 85)
        {
            core::WeakController* const node = liveNode();
            core::WeakController::Link& edge =
                node->links()[static_cast<std::size_t>(liveIndex())];
            if (edge.target &&
                static_cast<int>(edge.target->routeClassTag()) - 4 ==
                    armyIndex() &&
                primaryPathProgressRef() > static_cast<int>(edge.length) / 2)
            {
                resetEngineChainMovement();
                m_speed = 0.0f;
                reverseEngineChain();
                return;
            }
        }

        int seenBit0 = 0;
        SPRITE* scan = this;
        while (scan)
        {
            core::WeakController* const node = scan->primaryPathNodeRef();
            if (node &&
                node->selectedLinkIndex() == scan->primaryPathEdgeIndexRef() &&
                node->pushLineValue() != 0u)
            {
                for (SPRITE* mark = this; mark; mark = mark->engineChainNextRef())
                    mark->pushLineActiveRef() = 1;
                break;
            }

            bool reciprocalSelected = false;
            if (node)
            {
                core::WeakController::Link& edge =
                    node->links()[static_cast<std::size_t>(scan->primaryPathEdgeIndexRef())];
                if (edge.target)
                    reciprocalSelected = edge.target->selectedLinkIndex() == edge.reciprocalIndex;
            }

            if (reciprocalSelected)
            {
                if (m_speed != 0.0f)
                {
                    const int action = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
                    if (action == 92)
                    {
                        bool resetAction = pathBufferSizeRef() == 0;
                        if (!resetAction)
                        {
                            const unsigned char bufferedIndex = pathBufferData()[0];
                            core::WeakController* const bufferedTarget =
                                liveNode()->links()[static_cast<std::size_t>(bufferedIndex)].target;
                            core::WeakController* const currentTarget =
                                liveNode()->links()[static_cast<std::size_t>(liveIndex())].target;
                            resetAction = bufferedTarget == currentTarget;
                        }
                        if (resetAction)
                            dispatchEnginePrivateCommand(0, 0, 0, 0);
                        else
                            m_runtimeFlags &= ~MovementStartedFlag;
                    }
                    else
                    {
                        m_runtimeFlags &= ~MovementStartedFlag;
                    }
                    m_speed = 0.0f;
                    playSfxAtWorldPosition(148);
                }

                if (engineTargetSpeedRef() > 0.0f)
                    engineTargetSpeedRef() = -engineTargetSpeedRef();
                reverseEngineChain();
                return;
            }

            scan->pushLineActiveRef() = 0;
            seenBit0 |= static_cast<int>(scan->m_runtimeFlags & 1u);
            scan = scan->engineChainNextRef();
        }

        if (!scan && seenBit0 != 0)
        {
            for (SPRITE* node = this; node; node = node->engineChainNextRef())
            {
                if ((node->m_runtimeFlags & 1u) == 0)
                    continue;
                node->m_runtimeFlags &= ~1u;

                if (node->Vid()->weaponFloatAt(16) != 0.0f)
                {
                }

                ENGINE* const engineNode = static_cast<ENGINE*>(node);
                if (engineNode->productionBatchCompletionPending() != 0)
                {
                    const int spriteArg = static_cast<int>(reinterpret_cast<std::uintptr_t>(node));
                    (void)core::Application::callScriptFunction(
                        core::scriptCallbackSlot(4u), spriteArg, 0);
                    engineNode->setProductionBatchCompletionPending(0);
                }
            }
        }

        if (pushLineActiveRef() == 0)
        {
            if ((m_runtimeFlags & MovementStartedFlag) == 0 && m_speed == 0.0f)
            {
                const int action = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
                if (action == 92 || action == 104 || action == 108 || action == 100)
                    StartMove();
                if (action == 96)
                {
                    SPRITE* node = engineChainHead();
                    while (node &&
                           (node->Vid()->nvid() != 85 || node->routeActionReadyRef() != 0))
                        node = node->engineChainNextRef();
                    if (node)
                        StartMove();
                }
            }

            if ((m_runtimeFlags & MovementStartedFlag) == 0)
            {
                const int action = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
                if ((action == 112 || action == 116) && (std::rand() % 5) == 0 && evaluateEngineTargetRangeState() != 1)
                    StartMove();
            }

            if (engineTargetSpeedRef() == 0.0f && (m_runtimeFlags & MovementStartedFlag) != 0)
            {
                updateEngineChainSpeedTarget();
                if (engineTargetSpeedRef() == 0.0f && m_speed == 0.0f)
                    dispatchEnginePrivateCommand(0, 0, 0, 0);
            }
        }

        SPRITE* const routeTarget = engineCommandArgument0Ref() != 0 ? nullptr : goalSprite();
        float speed = std::fabs(m_speed);
        approachEngineTargetSpeed(&speed);
        if (speed < 0.0f)
        {
            m_speed = 0.0f;
            reverseEngineChain();
            updateEngineChainSpeedTarget();
            return;
        }

        core::WeakController::Link& currentEdge =
            liveNode()->links()[static_cast<std::size_t>(liveIndex())];

        BASE_CONSTANTS* const constants = GlobalBaseConstants();
        float speedLimit = 0.0f;
        std::memcpy(&speedLimit, &constants->raw[6], sizeof(speedLimit));
        if (liveNode()->pathEventFlag() != 0 && speed > speedLimit)
        {
            if (Vid()->nvid() != 85)
                speed = speedLimit;

            core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
            VID* effectVid = nullptr;
            if (table.count() > 588)
                effectVid = table.slot(588);
            if (!effectVid)
                effectVid = MAP::NullVid();
            hostState().owner->CreateSprite(effectVid, m_xyz, ANGLE(0), this, false, false);
        }

        const std::uint32_t deltaMs = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
        const std::int64_t deltaFixed = static_cast<std::int64_t>(
            static_cast<double>(static_cast<std::int32_t>(deltaMs)) *
            static_cast<double>(speed) * 64000.0);
        const std::int64_t accumulated = deltaFixed + static_cast<std::int64_t>(primaryPathAuxiliaryRef());
        primaryPathAuxiliaryRef() = static_cast<int>(accumulated);
        if (primaryPathAuxiliaryRef() < 0)
        {
            primaryPathAuxiliaryRef() = 0;
        }
        else if (primaryPathAuxiliaryRef() > 65535)
        {
            primaryPathProgressRef() += primaryPathAuxiliaryRef() >> 16;
            primaryPathAuxiliaryRef() &= 65535;
        }

        if (primaryPathProgressRef() > static_cast<int>(currentEdge.length))
        {
            if (liveNode()->pathEventFlag() != 0)
            {
                const float minX = static_cast<float>(liveNode()->x() - 100);
                const float minY = static_cast<float>(liveNode()->y() - 60);
                const float maxX = static_cast<float>(liveNode()->x() + 100);
                const float maxY = static_cast<float>(liveNode()->y() + 60);
                SPRITE_COLLECTOR_HASH_MAP* const hash = GlobalSpriteHashMap();
                for (SPRITE* candidate = hash->firstSpriteInBox(minX, minY, maxX, maxY);
                     candidate;
                     candidate = hash->nextSpriteInBox())
                {
                    if (candidate->Vid()->spriteClassId() == 22u)
                        static_cast<RAIL*>(candidate)->handleRailNodeReleased(reinterpret_cast<std::uintptr_t>(liveNode()));
                }
                liveNode()->setPathEventFlag(0);
            }

            core::WeakController* const targetNode = currentEdge.target;
            if (targetNode->linkCount() < 2)
            {
                speed = 0.0f;
                primaryPathProgressRef() = currentEdge.length;

                const int action = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
                if (action == 92)
                {
                    if (pathBufferSizeRef() != 0)
                    {
                        const unsigned char bufferedIndex = pathBufferData()[0];
                        const bool sameTarget =
                            liveNode()->links()[static_cast<std::size_t>(bufferedIndex)].target == targetNode;
                        if (!sameTarget)
                            resetEngineChainMovement();
                        else
                            dispatchEnginePrivateCommand(0, 0, 0, 0);
                    }
                    else
                    {
                        dispatchEnginePrivateCommand(0, 0, 0, 0);
                    }
                }
                else
                {
                    resetEngineChainMovement();
                }
            }
            else
            {
                core::PathPosition livePath{
                    primaryPathNodeRef(), primaryPathProgressRef(),
                    primaryPathAuxiliaryRef(), primaryPathEdgeIndexRef()};
                const int routeResult =
                    core::advancePathPosition(&livePath, engineCommandArgument0Node(), routeTarget, this);
                primaryPathNodeRef() = livePath.node;
                primaryPathProgressRef() = livePath.progress;
                primaryPathAuxiliaryRef() = livePath.auxiliary;
                primaryPathEdgeIndexRef() = livePath.edgeIndex;

                SPRITE* const controlled =
                    hostState().owner->flagmanSpriteForPlayer(static_cast<int>(core::ActivePlayerIndex()));
                if (isInEngineChain(controlled) && (controlled->m_runtimeFlags & ArmyBitsMask) == 0)
                    createRouteMarkerSprites(liveNode());

                const int actionBeforeOwnerCheck = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
                if (actionBeforeOwnerCheck != 108)
                {
                    SPRITE* const nodeOwner = liveNode()->ownerSprite();
                    if (nodeOwner)
                        nodeOwner->isInEngineChain(goalSprite());
                }

                core::WeakController* const currentTarget =
                    liveNode()->links()[static_cast<std::size_t>(liveIndex())].target;
                SPRITE* const currentTargetOwner = currentTarget->ownerSprite();
                if (!currentTargetOwner || currentTargetOwner->isInEngineChain(goalSprite()))
                {
                    const int actionBeforeSign = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
                    if ((actionBeforeSign == 108 || routeResult != 0) && routeResult < 0 && engineTargetSpeedRef() > 0.0f)
                        engineTargetSpeedRef() = -engineTargetSpeedRef();
                }

                if (Vid()->nvid() != 85 && currentTarget &&
                    static_cast<int>(currentTarget->routeClassTag()) - 4 ==
                        armyIndex())
                {
                    speed *= 0.5f;
                    resetEngineChainMovement();
                }

                core::WeakController* const actionTarget = engineCommandArgument0Node();
                const bool primaryRoute =
                    (actionTarget && actionTarget == currentTarget) ||
                    routeResult == 0 || currentTarget == core::pathBestNode();

                const int finalAction = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
                if (primaryRoute)
                {
                    if (finalAction == 96)
                    {
                        for (SPRITE* node = this; node; node = node->engineChainNextRef())
                        {
                            if (node->Vid()->nvid() == 85)
                            {
                                node->routeActionReadyRef() = 1;
                                resetEngineChainMovement();
                            }
                        }
                        if ((m_runtimeFlags & MovementStartedFlag) != 0)
                            dispatchEnginePrivateCommand(0, 0, 0, 0);
                    }
                    else if (finalAction == 92)
                    {
                        if (Vid()->nvid() != 85 || !liveNode() || !currentTarget ||
                            static_cast<int>(currentTarget->routeClassTag()) - 4 !=
                                armyIndex())
                        {
                            dispatchEnginePrivateCommand(0, 0, 0, 0);
                            const int selfArg = static_cast<int>(reinterpret_cast<std::uintptr_t>(this));
                            (void)core::Application::callScriptFunction(
                                core::scriptCallbackSlot(8u), selfArg, 0);
                        }
                    }
                    else if (finalAction == 100)
                    {
                        resetEngineChainMovement();
                        StartMove();
                    }
                    else if ((finalAction == 112 || finalAction == 116) && evaluateEngineTargetRangeState() == 1)
                    {
                        resetEngineChainMovement();
                    }
                }
                else
                {
                    if (finalAction == 112 || finalAction == 116)
                    {
                        if (evaluateEngineTargetRangeState() == 1)
                            resetEngineChainMovement();
                    }
                    else if (liveNode()->linkCount() < 2 ||
                             currentTarget->linkCount() < 2)
                    {
                        if (finalAction == 92)
                        {
                            if (pathBufferSizeRef() != 0)
                            {
                                const unsigned char bufferedIndex = pathBufferData()[0];
                                if (liveNode()->links()[static_cast<std::size_t>(bufferedIndex)].target == currentTarget)
                                    dispatchEnginePrivateCommand(0, 0, 0, 0);
                                else
                                    resetEngineChainMovement();
                            }
                            else
                            {
                                dispatchEnginePrivateCommand(0, 0, 0, 0);
                            }
                        }
                        else
                        {
                            resetEngineChainMovement();
                        }
                    }
                    else if (currentTarget->ownerSprite() &&
                             !currentTarget->ownerSprite()->isInEngineChain(goalSprite()))
                    {
                        resetEngineChainMovement();
                    }
                }
            }
        }

        resolveEngineChainPathInteraction(&pathSnapshot, &speed);
        for (SPRITE* node = engineChainHead(); node; node = node->engineChainNextRef())
            node->clearPathNodeOwnership();
        applyEngineChainPathMovement(&pathSnapshot, speed, engineAccelerationDelayRef());
    }

    void SPRITE::initializeEnginePathEndpoints() noexcept
    {
        VID* const vid = Vid();
        const float radius = vid->weaponRadius() * 0.5f;
        const int baseDirection = directionIndex();

        core::WeakController* const seed =
            core::findNearestLinkedNode3D(&core::globalWeakControllerMap(),
                             spriteFtolLow32(static_cast<long double>(m_xyz.x)),
                             spriteFtolLow32(static_cast<long double>(m_xyz.y)),
                             spriteFtolLow32(static_cast<long double>(m_xyz.z)));
        if (!seed || seed->linkCount() == 0)
            return;

        int facing = 0;
        int edgeIndex = seed->linkCount() - 1;
        while (edgeIndex >= 0)
        {
            const core::WeakController::Link& entry =
                seed->links()[static_cast<std::size_t>(edgeIndex)];
            const unsigned char edgeFacing = static_cast<unsigned char>(entry.facing);
            const unsigned char deltaA =
                static_cast<unsigned char>(baseDirection - edgeFacing);
            const unsigned char deltaB =
                static_cast<unsigned char>(edgeFacing - baseDirection);
            const unsigned char delta = deltaA < deltaB ? deltaA : deltaB;

            if (delta > 108 || delta < 20)
                break;
            --edgeIndex;
        }

        if (edgeIndex >= 0)
            facing = baseDirection;
        else
            facing = seed->firstLinkFacing();

        auto loadPair = [](core::WeakController* node, int progress, int pad, int index) noexcept
        {
            core::PathPosition pair;
            pair.node = node;
            pair.progress = progress;
            pair.auxiliary = pad;
            pair.edgeIndex = index;
            return pair;
        };

        auto storePrimary = [this](const core::PathPosition& pair) noexcept
        {
            primaryPathNodeRef() = pair.node;
            primaryPathProgressRef() = pair.progress;
            primaryPathAuxiliaryRef() = pair.auxiliary;
            primaryPathEdgeIndexRef() = pair.edgeIndex;
        };

        auto storeSecondary = [this](const core::PathPosition& pair) noexcept
        {
            secondaryPathNodeRef() = pair.node;
            secondaryPathProgressRef() = pair.progress;
            secondaryPathAuxiliaryRef() = pair.auxiliary;
            secondaryPathEdgeIndexRef() = pair.edgeIndex;
        };

        auto repairToCloserTarget = [this](core::PathPosition& pair) noexcept
        {
            core::WeakController* const node = pair.node;
            const core::WeakController::Link& entry =
                node->links()[static_cast<std::size_t>(pair.edgeIndex)];
            core::WeakController* const target = entry.target;

            const double nodeDx = static_cast<double>(node->x()) - m_xyz.x;
            const double nodeDy = static_cast<double>(node->y()) - m_xyz.y;
            const double nodeDz = static_cast<double>(node->id()) - m_xyz.z;
            const double targetDx = static_cast<double>(target->x()) - m_xyz.x;
            const double targetDy = static_cast<double>(target->y()) - m_xyz.y;
            const double targetDz = static_cast<double>(target->id()) - m_xyz.z;

            const double nodeDistance = std::sqrt(nodeDx * nodeDx + nodeDy * nodeDy + nodeDz * nodeDz);
            const double targetDistance = std::sqrt(targetDx * targetDx + targetDy * targetDy + targetDz * targetDz);

            if (targetDistance < nodeDistance ||
                std::isnan(targetDistance) || std::isnan(nodeDistance))
            {
                pair.progress = static_cast<int>(entry.length) - pair.progress;
                pair.node = target;
                pair.edgeIndex = entry.reciprocalIndex;
            }
        };

        core::PathPosition primary =
            loadPair(primaryPathNodeRef(),
                     primaryPathProgressRef(),
                     primaryPathAuxiliaryRef(),
                     primaryPathEdgeIndexRef());

        core::findNearestPathPosition(seed,
                         spriteFtolLow32(static_cast<long double>(radius) *
                                             directionSin(facing) +
                                         static_cast<long double>(m_xyz.x)),
                         spriteFtolLow32(static_cast<long double>(m_xyz.y) -
                                         static_cast<long double>(radius) *
                                             directionCos(facing)),
                         spriteFtolLow32(static_cast<long double>(m_xyz.z)),
                         &primary);
        repairToCloserTarget(primary);
        storePrimary(primary);

        const int reverseFacing = static_cast<unsigned char>(facing - 128);
        core::PathPosition secondary =
            loadPair(secondaryPathNodeRef(),
                     secondaryPathProgressRef(),
                     secondaryPathAuxiliaryRef(),
                     secondaryPathEdgeIndexRef());

        core::findNearestPathPosition(seed,
                         spriteFtolLow32(static_cast<long double>(radius) *
                                             directionSin(reverseFacing) +
                                         static_cast<long double>(m_xyz.x)),
                         spriteFtolLow32(static_cast<long double>(m_xyz.y) -
                                         static_cast<long double>(radius) *
                                             directionCos(reverseFacing)),
                         spriteFtolLow32(static_cast<long double>(m_xyz.z)),
                         &secondary);
        repairToCloserTarget(secondary);
        storeSecondary(secondary);

        core::PathPosition primaryForBCF0{
            primaryPathNodeRef(),
            primaryPathProgressRef(),
            primaryPathAuxiliaryRef(),
            primaryPathEdgeIndexRef()};
        updateSecondaryPathPosition(&primaryForBCF0);
        updatePositionFromPathEndpoints();
        SPRITE* const owner = findEnginePathRelationSprite();
        attachEngineChain(owner);
        claimPathNodeOwnership();
    }

    void SPRITE::updatePositionFromPathEndpoints() noexcept
    {
        core::WeakController* const firstNode = primaryPathNodeRef();
        core::WeakController* firstTarget = nullptr;
        int firstDuration = 0;
        if (firstNode)
        {
            const core::WeakController::Link& firstEdge =
                firstNode->links()[static_cast<std::size_t>(primaryPathEdgeIndexRef())];
            firstTarget = firstEdge.target;
            firstDuration = static_cast<int>(firstEdge.length);
        }

        const int firstProgress = primaryPathProgressRef();
        // Retail conditionally resolves firstTarget/duration above, but then
        // unconditionally dereferences firstTarget and firstNode.  Preserve
        // that fault boundary instead of adding a host-only null guard.
        const int firstTargetX = firstTarget->x();
        const int firstNodeX = firstNode->x();
        const float firstX = pathInterpolateCoordinate(
            pathScaledProgressQuotient(firstProgress,
                                          firstTargetX - firstNodeX,
                                          firstDuration),
            firstNodeX);

        const int firstTargetY = firstTarget->y();
        const int firstNodeY = firstNode->y();
        const float firstY = pathInterpolateCoordinate(
            pathScaledProgressQuotient(firstProgress,
                                          firstTargetY - firstNodeY,
                                          firstDuration),
            firstNodeY);

        const int firstTargetZ = firstTarget->id();
        const int firstNodeZ = firstNode->id();
        const float firstZ = pathInterpolateCoordinate(
            pathScaledProgressQuotient(firstProgress,
                                          firstTargetZ - firstNodeZ,
                                          firstDuration),
            firstNodeZ);

        core::WeakController* const secondNode = secondaryPathNodeRef();
        if (secondaryPathEdgeIndexRef() >= secondNode->linkCount())
        {
            LOG::ResourceError("ENGINE %i", 10, kMissingLinkResourceError, 0,
                               Vid() ? Vid()->nvid() : -1);
            secondaryPathEdgeIndexRef() = secondNode->linkCount() - 1;
        }

        const core::WeakController::Link& secondEdge =
            secondNode->links()[static_cast<std::size_t>(secondaryPathEdgeIndexRef())];
        core::WeakController* const secondTarget = secondEdge.target;
        if (!secondTarget)
        {
            LOG::ResourceError("ENGINE %i", 10, kMissingTailDot2ResourceError, 0,
                               Vid() ? Vid()->nvid() : -1);
            return;
        }

        const int secondDuration = static_cast<int>(secondEdge.length);
        const int secondProgress = secondaryPathProgressRef();
        const int secondNodeX = secondNode->x();
        const int secondNodeY = secondNode->y();
        const int secondNodeZ = secondNode->id();

        const float secondX = pathInterpolateCoordinate(
            pathScaledProgressQuotient(secondProgress,
                                          secondTarget->x() - secondNodeX,
                                          secondDuration),
            secondNodeX);
        const float secondY = pathInterpolateCoordinate(
            pathScaledProgressQuotient(secondProgress,
                                          secondTarget->y() - secondNodeY,
                                          secondDuration),
            secondNodeY);
        const float secondZ = pathInterpolateCoordinate(
            pathScaledProgressQuotient(secondProgress,
                                          secondTarget->id() - secondNodeZ,
                                          secondDuration),
            secondNodeZ);

        ChangeCoor(pathAverageCoordinate(secondX, firstX),
                   pathAverageCoordinate(secondY, firstY),
                   pathAverageCoordinate(secondZ, firstZ));

        const bool reverseDirection = (derivedStateValue(0) & 1) != 0;
        const int directionY = pathDirectionDeltaYToInt(
            reverseDirection ? secondY : firstY,
            reverseDirection ? firstY : secondY);
        const int directionX = pathDirectionDeltaXToInt(
            reverseDirection ? secondX : firstX,
            reverseDirection ? firstX : secondX);
        ChangeDirection(AngleFromXY(directionX, directionY, nullptr) & 255);
    }

    void SPRITE::splitEngineChainAtPosition(float x, float y) noexcept
    {
        SPRITE* const first = engineChainHead();
        SPRITE* const last = engineChainTail();
        playSfxAtWorldPosition(15);

        bool preferFirst = false;
        if (engineChainPreviousRef() && engineChainNextRef())
        {
            preferFirst = preferFirstTrainEndpoint(
                x, y,
                engineChainPreviousRef()->X(), engineChainPreviousRef()->Y(),
                engineChainNextRef()->X(), engineChainNextRef()->Y());
        }

        if (!engineChainNextRef() || preferFirst)
        {
            if (engineChainPreviousRef())
            {
                engineChainPreviousRef()->engineChainNextRef() = nullptr;
                engineChainPreviousRef() = nullptr;
            }
        }
        else
        {
            engineChainNextRef()->engineChainPreviousRef() = nullptr;
            engineChainNextRef() = nullptr;
        }

        if (last != first)
        {
            last->resetEngineChainMovement();
            if (x87IsZeroOrUnordered(last->m_speed))
            {
                last->reverseEngineChain();
                SPRITE* const head = last->engineChainHead();
                head->m_speed = (static_cast<std::uint32_t>(head->derivedStateValue(0)) & 1u) != 0u ? -0.01f : 0.01f;
            }

            if (x87IsZeroOrUnordered(first->m_speed))
            {
                SPRITE* const head = first->engineChainHead();
                head->m_speed = (static_cast<std::uint32_t>(head->derivedStateValue(0)) & 1u) != 0u ? -0.01f : 0.01f;
            }
            else
            {
                first->updateEngineChainSpeedTarget();
            }
        }
    }

    int SPRITE::scaledEngineChainLength() noexcept
    {
        int count = 0;
        SPRITE* walker = engineChainHead();
        while (walker)
        {
            walker = walker->engineChainNextRef();
            ++count;
        }

#if defined(_MSC_VER) && defined(_M_IX86)
        const float scale = 1.33f;
        const float bias = 0.5f;
        std::int64_t converted = 0;
        unsigned short oldControl = 0;
        unsigned short truncControl = 0;
        __asm
        {
            fild dword ptr [count]
            fmul dword ptr [scale]
            fadd dword ptr [bias]
            fstcw word ptr [oldControl]
            fwait
            mov ax, word ptr [oldControl]
            or ah, 0Ch
            mov word ptr [truncControl], ax
            fldcw word ptr [truncControl]
            fistp qword ptr [converted]
            fldcw word ptr [oldControl]
        }
        return static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(converted)));
#else
        const long double value =
            static_cast<long double>(count) * static_cast<long double>(1.33f) +
            static_cast<long double>(0.5f);
        return spriteFtolLow32(value);
#endif
    }

    void SPRITE::updateEngineChainSpeedTarget() noexcept
    {
        SPRITE* head = this;
        if (head->engineChainPreviousRef())
        {
            do
            {
                head = head->engineChainHead();
            }
            while (head->engineChainPreviousRef());
        }

        EngineChainMetrics range;
        range.categoryFlags = 0;
        range.collectEngineChainMetrics(head);

        if (x87EqualOrUnordered(head->engineTargetSpeedRef(), 0.0f) &&
            !x87EqualOrUnordered(range.weapon0CSum, 0.0f) &&
            spriteFdivMulFtolLow32(range.weapon10Sum,
                                   range.weapon0CSum, 8.0f) > 7)
        {
            const std::uint32_t flags = head->m_runtimeFlags;
            if ((flags & MovementStartedFlag) != 0u && core::BulkSpriteDeleteActive() == 0u)
            {
                core::WeakController* const routeNode = head->engineCommandArgument0Node();
                SPRITE* const routeSprite = routeNode ? nullptr : head->goalSprite();

                core::PathPosition path{
                    head->primaryPathNodeRef(),
                    head->primaryPathProgressRef(),
                    head->primaryPathAuxiliaryRef(),
                    head->primaryPathEdgeIndexRef()};
                const int projection = core::scoreNextPathStep(&path,
                                                        routeNode,
                                                        routeSprite,
                                                        static_cast<int>((flags >> CommandBitsShift) & CommandValueMask),
                                                        head);

                SPRITE* const controlled =
                    head->hostState().owner->flagmanSpriteForPlayer(static_cast<int>(core::ActivePlayerIndex()));
                if (head->isInEngineChain(controlled))
                {
                    SPRITE* const controlledAgain =
                        head->hostState().owner->flagmanSpriteForPlayer(static_cast<int>(core::ActivePlayerIndex()));
                    if ((controlledAgain->m_runtimeFlags & ArmyBitsMask) == 0u)
                    {
                        core::WeakController* pathTarget = nullptr;
                        if (head->primaryPathNodeRef())
                        {
                            pathTarget = head->primaryPathNodeRef()->links()
                                [static_cast<std::size_t>(head->primaryPathEdgeIndexRef())].target;
                        }
                        head->createRouteMarkerSprites(pathTarget);
                    }
                }

                head->engineTargetSpeedRef() = projection >= 0 ? 0.001f : -0.001f;
            }
        }

        if (x87LessOrUnordered(head->engineTargetSpeedRef(), 0.0f))
        {
            const int negativeDelay = static_cast<int>(0u - static_cast<std::uint32_t>(range.movementDelayMs));
            head->engineTargetSpeedRef() = spriteFildMulStoreFloat(negativeDelay, 0.001f);
            return;
        }

        if (x87OrderedGreater(head->engineTargetSpeedRef(), 0.0f))
        {
            head->engineTargetSpeedRef() = spriteFildMulStoreFloat(range.movementDelayMs, 0.001f);
            const int delay = x87EqualOrUnordered(range.weapon0CSum, 0.0f)
                ? 0
                : spriteFdivMulFtolLow32(range.weapon10Sum,
                                         range.weapon0CSum, 8.0f);
            head->engineAccelerationDelayRef() = delay;
            if (delay == 0)
                head->m_runtimeFlags &= ~MovementStartedFlag;
        }
    }

    void SPRITE::approachEngineTargetSpeed(float* speedOut) noexcept
    {
        constexpr float immediateSpeed = 0.03500000014901161f;
        constexpr float tickScale = 0.000001f;

        if (pushLineActiveRef() != 0 &&
            x87EqualOrUnordered(engineTargetSpeedRef(), 0.0f))
        {
            *speedOut = immediateSpeed;
            engineAccelerationDelayRef() = 0;
            return;
        }

        const float target = engineTargetSpeedRef();
        // target vs speed, TEST AH,41h: acceleration runs only for an
        // ordered target > speed comparison.
        if (!x87LessEqualOrUnordered(target, *speedOut))
        {
            if (engineAccelerationDelayRef() == 0)
                updateEngineChainSpeedTarget();

            const int delay = engineAccelerationDelayRef();
            if (delay != 0)
            {
                const std::uint32_t delta = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                const std::uint32_t product =
                    static_cast<std::uint32_t>(delta) * static_cast<std::uint32_t>(delay);
                const double step = static_cast<double>(product) * static_cast<double>(tickScale);
                *speedOut = static_cast<float>(static_cast<double>(*speedOut) + step + step);
            }

            if (*speedOut >= target)
            {
                *speedOut = target;
                engineAccelerationDelayRef() = 0;
            }
            return;
        }

        // target vs speed, TEST AH,1: deceleration also runs for
        // unordered (NaN), matching the retail x87 branch.
        if (x87LessOrUnordered(target, *speedOut))
        {
            const int delay = spriteFtolLow32(
                (static_cast<long double>(*speedOut) * 1000.0L + 10.0L) * -0.5L);
            engineAccelerationDelayRef() = delay;

            const std::uint32_t delta = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
            const std::uint32_t product =
                static_cast<std::uint32_t>(delta) * static_cast<std::uint32_t>(delay);
            const std::int32_t signedProduct = static_cast<std::int32_t>(product);
            const double step = static_cast<double>(signedProduct) * static_cast<double>(tickScale);
            *speedOut = static_cast<float>(static_cast<double>(*speedOut) + step + step);

            if (x87LessEqualOrUnordered(*speedOut, target))
            {
                *speedOut = target;
                engineAccelerationDelayRef() = 0;
            }
            return;
        }

        engineAccelerationDelayRef() = 0;
    }

    void SPRITE::resetEngineChainMovement() noexcept
    {
        SPRITE* node = this;
        for (;;)
        {
            SPRITE* const controlledPlayer =
                node->hostState().owner->flagmanSpriteForPlayer(static_cast<int>(core::ActivePlayerIndex()));
            if (node->isInEngineChain(controlledPlayer))
                g_spriteWorkList.releaseRepeatedReferencesRetail();

            if (!node->engineChainPreviousRef())
                break;
            node = node->engineChainHead();
        }

        const DWORD flags = node->m_runtimeFlags;
        if ((flags & CommandBitsMask) == 0x64u)
        {
            if ((flags & MovementStartedFlag) != 0u)
            {
                node->engineCommandArgument0Ref() = node->engineCommandArgument1Ref();
                node->m_runtimeFlags = flags | MovementStartedFlag;
                node->engineCommandArgument1Ref() = node->engineCommandArgument2Ref();
                node->engineCommandArgument2Ref() = node->engineCommandArgument0Ref();
                node->engineTargetSpeedRef() = 0.0f;
                node->engineAccelerationDelayRef() = 0;

                for (SPRITE* child = node->engineChainNextRef();
                     child;
                     child = child->engineChainNextRef())
                {
                    child->engineCommandArgument0Ref() = node->engineCommandArgument0Ref();
                    child->engineCommandArgument1Ref() = node->engineCommandArgument1Ref();
                    child->engineCommandArgument2Ref() = node->engineCommandArgument2Ref();
                    child->m_runtimeFlags |= MovementStartedFlag;
                    child->engineTargetSpeedRef() = 0.0f;
                    child->engineAccelerationDelayRef() = 0;
                }
            }
            return;
        }

        for (SPRITE* iter = node; iter; iter = iter->engineChainNextRef())
        {
            iter->engineTargetSpeedRef() = 0.0f;
            iter->m_runtimeFlags &= ~MovementStartedFlag;

            if (!x87EqualOrUnordered(node->Speed(), 0.0f))
                iter->engineAccelerationDelayRef() = animationDelayFromSpeed(node->Speed());
        }
    }

    void SPRITE::dispatchEnginePrivateCommandAtPathPoint(int opcode, int x, int y) noexcept
    {
        core::WeakController* const node =
            core::findNearestLinkedNode2D(&core::globalWeakControllerMap(), x, y);
        dispatchEnginePrivateCommand(opcode, 0,
            static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(node))), 0);
    }

    void SPRITE::dispatchEnginePrivateCommand(int opcode, int argument1, int argument2, int argument3) noexcept
    {
        int action = opcode;
        SPRITE* const owner = this;
        int forcedTerminalZero = 0;

        if (action == 0x1E)
        {
            action = 0;
            forcedTerminalZero = 1;
        }

        SPRITE* target = reinterpret_cast<SPRITE*>(static_cast<std::intptr_t>(argument1));
        int actionArgument2 = argument2;
        const int actionArgument3 = argument3;

        if (!target && actionArgument2 == 0 && action != 0x1D)
        {
            action = 0;
        }
        else if (action == 0x18)
        {
            SPRITE* scan = engineChainHead();
            while (scan)
            {
                if (scan->m_vid->nvid() == 85 && scan->ammoFixedPoint() / 64 > 0)
                    break;
                scan = scan->engineChainNextRef();
            }
            if (!scan)
            {
                action = 0;
                target = nullptr;
                actionArgument2 = 0;
            }
        }

        core::WeakController* resolvedB4 = nullptr;
        int resolvedB8 = 0;
        if (action == 0x19)
        {
            resolvedB4 = actionArgument3
                ? reinterpret_cast<core::WeakController*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(actionArgument3)))
                : core::findNearestLinkedNode3D(&core::globalWeakControllerMap(),
                                   spriteFtolLow32(static_cast<long double>(m_xyz.x)),
                                   spriteFtolLow32(static_cast<long double>(m_xyz.y)),
                                   spriteFtolLow32(static_cast<long double>(m_xyz.z)));
            resolvedB8 = actionArgument2;
        }

        for (SPRITE* node = engineChainHead(); node; node = node->engineChainNextRef())
        {
            SPRITE* const refOwner = node->engineCommandReferenceOwnerRef();
            if (refOwner)
            {
                const int nextRef = refOwner->m_listReferenceCount - 1;
                refOwner->m_listReferenceCount = nextRef;
                if (nextRef < 0)
                {
                    const int nvid = refOwner->m_vid ? refOwner->m_vid->nvid() : -1;
                    LOG::ResourceError("SPRITE %i", 4, "noRef	at Release", nextRef, nvid);
                }
                else if (nextRef == 0)
                {
                    DeleteSpriteThroughVirtualDeletingDestructor(refOwner);
                }
                node->engineCommandReferenceOwnerRef() = nullptr;
            }

            if ((action == 0x1C || action == 0x1D) &&
                owner->m_vid->nvid() != 45 &&
                x87IsZeroOrUnordered(owner->m_vid->weaponFloatAt(0x10)))
            {
                node->engineCommandReferenceOwnerRef() = owner;
                ++owner->m_listReferenceCount;
            }

            node->SetCommandWithoutLink(action, target);
            node->engineCommandArgument1Ref() = static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(resolvedB4)));
            node->engineCommandArgument0Ref() = actionArgument2;
            node->engineCommandArgument2Ref() = resolvedB8;
            node->engineTargetSpeedRef() = 0.0f;
            node->routeActionStartTimeRef() = 0;
            node->routeActionReadyRef() = 0;

            SPRITE* const child = node->m_childChain;
            if (!child)
                continue;

            VID* const linkVid = node->m_vid->linkedVid();
            if (child->m_vid != linkVid ||
                child->m_vid->hasWeaponChildDescriptor() == 0u ||
                child->m_vid->weaponCount() == 0u ||
                forcedTerminalZero != 0)
            {
                continue;
            }

            if (owner->m_vid->nvid() != 45 &&
                x87IsZeroOrUnordered(owner->m_vid->weaponFloatAt(0x10)) &&
                node != owner)
            {
                child->SetCommandWithoutLink(0, nullptr);
                continue;
            }

            if (owner->canWeaponAffectTarget(target) == 0)
            {
                child->SetCommandWithoutLink(0, nullptr);
                continue;
            }

            if (action == 0x1C)
            {
                child->SetCommandWithoutLink(3, target);
                continue;
            }

            if (action != 0x1D)
            {
                child->SetCommandWithoutLink(0, nullptr);
                continue;
            }

            VID* weaponOwner = node->m_vid;
            if (child && child->m_vid == node->m_vid->linkedVid() &&
                child->m_vid->hasWeaponChildDescriptor() != 0u &&
                child->m_vid->weaponCount() != 0u)
            {
                weaponOwner = child->m_vid;
            }

            if (weaponOwner->weaponTypeMask() == 8 && target)
            {
                SPRITE* helper = new (std::nothrow) SPRITE(
                    hostState().owner,
                    MAP::NullVid(),
                    VECTOR(target->m_xyz.x, target->m_xyz.y + 70.0f, target->m_xyz.z + 70.0f),
                    ANGLE(0),
                    nullptr);
                child->SetCommandWithoutLink(4, helper);
            }
            else
            {
                child->SetCommandWithoutLink(4, target);
            }
        }

        if (action == 0x17 || action == 0x1A || action == 0x1B || action == 0x19)
        {
            engineChainHead()->StartMove();
            return;
        }

        if (action == 0x18)
        {
            const std::uint32_t c8Low = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(primaryPathNodeRef()));
            if (c8Low == static_cast<std::uint32_t>(engineCommandArgument0Ref()))
            {
                engineChainHead()->resetEngineChainMovement();
                for (SPRITE* node = engineChainHead(); node; node = node->engineChainNextRef())
                {
                    if (node->m_vid->nvid() == 85 && ammoFixedPoint() / 64 != 0)
                        node->routeActionReadyRef() = 1;
                }
                return;
            }

            engineChainHead()->StartMove();
            return;
        }

        if (action == 0)
            engineChainHead()->resetEngineChainMovement();
    }

    int SPRITE::inheritAdjacentEngineCommand() noexcept
    {
        SPRITE* const next = engineChainNextRef();
        if (next && (next->m_runtimeFlags & SPRITE::CommandBitsMask) != 0u)
            return SetCommand(next->commandIndex(), next->m_goalSprite);

        SPRITE* const previous = engineChainPreviousRef();
        if (previous && (previous->m_runtimeFlags & SPRITE::CommandBitsMask) != 0u)
        {

            return SetCommand(next->commandIndex(),
                              next->m_goalSprite);
        }

        return SetCommand(0, nullptr);
    }

    size_t SPRITE::CreateChildForAnimation(int animationSlot, bool birthConstructorRoute)
    {
        (void)birthConstructorRoute;
        VID* childVid = m_vid->childVid[animationSlot];
        const int childLayout = static_cast<int>(m_vid->nChildVid[animationSlot]);
        const std::int32_t rawChildCount = static_cast<std::int32_t>(m_vid->noChild[animationSlot]);
        const std::int32_t sign = rawChildCount < 0 ? -1 : 0;
        int childCount = static_cast<std::int32_t>(
            (static_cast<std::uint32_t>(rawChildCount) ^ static_cast<std::uint32_t>(sign)) -
            static_cast<std::uint32_t>(sign));
        // nChildVid is a layout selector consumed inside the loop; zero is a
        // valid deterministic layout and must not suppress child creation.
        if (!childVid)
            return 0;

        int startOrdinal = 0;
        const int weaponFlags04 = m_vid->weaponFlags();
        if (childCount == 2 && (weaponFlags04 & 0x10) != 0)
        {
            DWORD nextFlags = m_runtimeFlags;
            if ((nextFlags & 0x00001000u) != 0)
            {
                nextFlags &= ~0x00000200u;
                startOrdinal = 1;
            }
            else
                childCount = 1;
            nextFlags ^= 0x00001000u;
            m_runtimeFlags = nextFlags;
        }

        const int steppedDirection = quantizeDirectionForVid(
            m_direction.Int(),
            static_cast<int>(m_vid->directionQuantizationOffset()),
            static_cast<int>(m_vid->noDir));

        size_t created = 0;
        for (int ordinal = startOrdinal; ordinal < childCount; ++ordinal)
        {
            VECTOR offset{};
            float projectileBaseX = 0.0f; // retail v63
            float projectileBaseY = 0.0f; // retail v64
            const float childX = m_vid->childX[animationSlot];
            const float childY = m_vid->childY[animationSlot];
            const float primarySin = directionSin(steppedDirection);
            const float primaryCos = directionCos(steppedDirection);
            const float auxiliarySin = directionSinAux(steppedDirection);
            const float auxiliaryCos = directionCosAux(steppedDirection);
            if (childLayout >= 0)
            {
                if (ordinal == 1)
                {
                    projectileBaseX = -(primaryCos * childX);
                    projectileBaseY = -(auxiliarySin * childX);
                    offset.x = projectileBaseX + primarySin * childY;
                    offset.y = projectileBaseY - auxiliaryCos * childY;
                }
                else if (ordinal == 2)
                {
                    offset.x = primarySin * childY;
                    offset.y = auxiliaryCos * childY;
                }
                else
                {
                    projectileBaseX = primaryCos * childX;
                    projectileBaseY = auxiliarySin * childX;
                    offset.x = projectileBaseX + primarySin * childY;
                    offset.y = projectileBaseY - auxiliaryCos * childY;
                }
                offset.z = m_vid->childZ[animationSlot];
            }
            else
            {
                constexpr float kRand32767 = 0.000030518509f;
                if (m_vid->spriteClassId() == 23u &&
                    x87IsZeroOrUnordered(childX) &&
                    x87IsZeroOrUnordered(childY))
                {
                    const REGION* const region = static_cast<const REGION*>(this);
                    if ((region->regionFlags() & REGION::FullViewportFlag) == 0u)
                    {
                        const float width = region->regionWidth();
                        const float height = region->regionHeight();
                        offset.x = width * 0.5f - static_cast<float>(std::rand()) * width * kRand32767;
                        offset.y = height * 0.5f - static_cast<float>(std::rand()) * height * kRand32767 + Z();
                    }
                    else
                    {
                        offset.x = static_cast<float>(std::rand()) * hostState().owner->SizeX() * kRand32767 - X();
                        offset.y = static_cast<float>(std::rand()) * hostState().owner->SizeY() * kRand32767 - Y() + Z();
                    }
                    offset.z = m_vid->childZ[animationSlot];
                }
                else
                {
                    const float localX = childX - static_cast<float>(std::rand()) * (childX + childX) * kRand32767;
                    const float localY = childY - static_cast<float>(std::rand()) * (childY + childY) * kRand32767;
                    projectileBaseX = -(localX * primaryCos);
                    projectileBaseY = -(localX * auxiliarySin);
                    offset.x = localY * primarySin + projectileBaseX;
                    offset.y = projectileBaseY - localY * auxiliaryCos;
                    offset.z = m_vid->childZ[animationSlot];
                }
            }
            const VECTOR target(m_xyz.x + offset.x, m_xyz.y + offset.y, m_xyz.z + offset.z);

            if (childVid->spriteClassId() == B_UNIT &&
                GlobalHashQueryCellCollisionByVid(*hostState().owner, childVid, target.x, target.y, target.z) != nullptr)
            {
                continue;
            }

            if (animationSlot == 8 && goalSprite() == nullptr)
            {
                continue;
            }

            ANGLE childDirection = m_direction;
            if ((childVid->property & P_RANDBIRTH) != 0)
                childDirection = ANGLE(std::rand() & 0xFF);

            SPRITE* child = hostState().owner->CreateSpriteViaFactory(childVid,
                                                                    target,
                                                                    childDirection,
                                                                    this,
                                                                    false);
            if (child)
            {
                ++created;

                if (animationSlot == 8)
                {
                    SPRITE* const goal = goalSprite();
                    VID* const parentVid = m_vid;
                    const bool hasOwner = goal != nullptr;
                    const bool ownerHasRealVid = hasOwner && goal->Vid() != MAP::NullVid();
                    const bool weaponActionBit20 = parentVid && ((parentVid->weaponFlags() & 0x20) != 0);
                    if (hasOwner && weaponActionBit20 && ownerHasRealVid)
                    {
                        child->setAttackCommandForTarget(goal);
                        child->StartMove();
                    }
                    else if (hasOwner)
                    {
                        const float projectileRadius = parentVid ? parentVid->weaponAim() : 0.0f;
                        float helperX = goal->X() + projectileRadius + projectileBaseX;
                        float helperY = goal->Y() + projectileRadius + projectileBaseY;
                        const float ownerZ = goal->Z();

                        auto groundAt = [this](float x, float y) -> float
                        {
                            return hostState().owner->GetGroundZ(VECTOR2{x, y});
                        };

                        for (int attempt = 0; attempt < 5; ++attempt)
                        {
                            const float randomX = static_cast<float>(std::rand()) * projectileRadius * 0.000061037019f;
                            const float randomY = static_cast<float>(std::rand()) * projectileRadius * 0.000061037019f;
                            const float candidateX = goal->X() + projectileRadius + projectileBaseX - randomX;
                            const float candidateY = goal->Y() + projectileRadius + projectileBaseY - randomY;

                            const float candidateGround = groundAt(candidateX, candidateY);
                            if (candidateGround < ownerZ || std::isnan(candidateGround) || std::isnan(ownerZ))
                            {
                                const float midX = (goal->X() + candidateX) * 0.5f;
                                const float midY = (goal->Y() + candidateY) * 0.5f;
                                const float midpointGround = groundAt(midX, midY);
                                if (midpointGround < ownerZ || std::isnan(midpointGround) || std::isnan(ownerZ))
                                {
                                    helperX = candidateX;
                                    helperY = candidateY;
                                    break;
                                }
                            }

                            helperX = candidateX;
                            helperY = candidateY;
                        }

                        SPRITE* const projectileOwner = new (std::nothrow) SPRITE(
                            hostState().owner, MAP::NullVid(), VECTOR(helperX, helperY, ownerZ), ANGLE(0), nullptr);

                        child->setAttackCommandForTarget(projectileOwner);
                        child->StartMove();
                    }
                }
            }
        }
        return created;
    }

    int SPRITE::dispatchDebugOverlay()
    {
        VID* const vid = Vid();
        if (vid->spriteClassId() == B_REGION)
        {
            DrawDebugOverlay();

            return 0;
        }
        return DrawDebugOverlay(*GRAPH::CurrentGraph());
    }

    size_t SPRITE::CreateChild()
    {
        return CreateChildForAnimation(m_currentAnimation, false);
    }

    SPRITE::~SPRITE()
    {
        destroyBaseSpriteState();
    }

    void DeleteSpriteThroughVirtualDeletingDestructor(SPRITE* sprite) noexcept
    {
        if (!sprite)
            return;
        if (MAP* const owner = sprite->mapOwner())
            owner->ReleaseSpriteForScalarDeletingDestructor(sprite);
        delete sprite;
    }

    SPRITE* SPRITE::commandSpriteScalarDeletingDestructor(unsigned char flags) noexcept
    {
        SPRITE* const self = this;
        destroyCommandSpriteState();
        if ((flags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    void SPRITE::destroyCommandSpriteState() noexcept
    {
#ifdef _WIN32
        win::applicationWinInstance()->transferFrom(this);
#else
        if (hostState().owner)
            hostState().owner->releaseSpriteReferencesHost(this);
#endif
        m_commandStack.releaseCommandWordsRetailTail();
        destroyBaseSpriteState();
    }

    SPRITE* SPRITE::baseSpriteScalarDeletingDestructor(unsigned char flags) noexcept
    {
        SPRITE* const self = this;
        destroyBaseSpriteState();
        if ((flags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    SPRITE* SPRITE::linkedSpriteScalarDeletingDestructor(unsigned char flags) noexcept
    {
        SPRITE* const self = this;
        detachFromChildChain();
        destroyBaseSpriteState();
        if ((flags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    void SPRITE::destroyBaseSpriteState()
    {
        VID* const vid = m_vid;
        const bool isEmptyVid = vid == MAP::NullVid();
        const int nvid = vid->nVid;

        // [VID+0x3FC] DESTROY callback.
        const int destroyFunction = vid->destroyScriptFunction();
        if (destroyFunction >= 0)
        {
            const int spriteArg = static_cast<int>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFFFFu);
            (void)core::Application::callScriptFunction(destroyFunction, spriteArg, 0);
        }

        if (isEmptyVid && m_listReferenceCount != 0)
            LOG::ResourceError("SPRITE %i", 4, "noRef for SPRITE with EmptyVid", m_listReferenceCount, nvid);

        if (m_listReferenceCount > 1)
        {
            GlobalSpriteHashMap()->removeSprite(this);
            if (m_listReferenceCount > 1)
            {
#ifdef _WIN32
                win::applicationWinInstance()->transferFrom(this);
#else
                hostState().owner->releaseSpriteReferencesHost(this);
#endif
            }
        }

        vid->decrementSpriteCountForArmy(armyIndex());
        setGoalSprite(nullptr);

        releaseBestTargetSprite();
        deleteChildChainSlot40();
        clearChildBacklinkSlot44();

        if (!isEmptyVid)
        {
            removeFromDrawBucketsRecursive();
            --m_listReferenceCount;
        }

        if (m_listReferenceCount != 0)
            LOG::ResourceError("SPRITE %i", 10, "Reference count non zero after delete", m_listReferenceCount, nvid);

        releaseActionAuxState();

        // Retail checks +0x6C again after the release branch and reports if
        // some caller rebuilt the owner during destruction.
        if (m_bestTargetSprite)
        {
            const int ptrNvid = m_bestTargetSprite->Vid()->nVid;
            LOG::ResourceError("SPRITE %i", 10, "PTR_SPRITE with this sprite not clear", 0, ptrNvid);
        }

        releaseCommandRecordsRetailTail();

        releaseHostState();
    }

    const Gamma& SPRITE::GetGamma() const
    {
        return hostState().gamma;
    }

    Gamma SPRITE::GetUniqueGamma() const
    {
        return hostState().gamma;
    }

    void SPRITE::SetGamma(const Gamma& value)
    {
        hostState().gamma = value;
    }

    void SPRITE::addGamma(const Gamma& gammaDelta)
    {
        hostState().gamma = hostState().gamma.saturatedAdd(gammaDelta);
    }

    void SPRITE::MoveHashBeforeCoordinateWrite(float nextX, float nextY)
    {
        const VECTOR target(nextX, nextY, m_xyz.z);
        GlobalSpriteHashMap()->moveSprite(this, nextX, nextY);
    }

    void SPRITE::performBaseMovementTact() noexcept
    {
        VID* const vid = m_vid;
        if (!vid->movementTactEnabled())
            return;

        float candidateX = X();
        float candidateY = Y();
        float candidateZ = Z();
        computeNextMovementPosition(&candidateX, &candidateY, &candidateZ);

        MAP* const map = hostState().owner;
        const float currentGroundZ = map->sampleTerrainHeight(X(), Y());
        const float candidateGroundZ = map->sampleTerrainHeight(candidateX, candidateY);
        const float moveUpLimitZ = static_cast<float>(
            static_cast<long double>(candidateGroundZ) +
            static_cast<long double>(vid->moveUpZ()));

        const DWORD property = vid->properties();
        if ((vid->spriteTypeId() & 0x00000200u) != 0u &&
            (property & P_GRAVITY) != 0u &&
            x87LessEqualOrUnordered(candidateZ, candidateGroundZ) &&
            !x87LessOrUnordered(Z(), currentGroundZ))
        {
            candidateZ = Z();
            dispatchVirtualAction(ActionCode::ACT_PATH_GROUND,
                spriteFtolLow32(static_cast<long double>(candidateZ)),
                spriteFtolLow32(static_cast<long double>(candidateGroundZ)),
                spriteFtolLow32(static_cast<long double>(currentGroundZ)));
        }
        else if (!x87EqualOrUnordered(Z(), candidateZ))
        {
            if ((property & P_GRAVITY) == 0u &&
                !x87IsZeroOrUnordered(moveUpLimitZ))
            {
                bool clampCandidate = false;
                bool clearZSpeed = false;
                if (x87OrderedLess(Z(), moveUpLimitZ))
                {
                    if (!x87OrderedLess(candidateZ, moveUpLimitZ))
                    {
                        clampCandidate = true;
                        clearZSpeed = true;
                    }
                }
                else if (x87OrderedGreater(Z(), moveUpLimitZ))
                {
                    if (x87LessOrUnordered(candidateZ, moveUpLimitZ))
                    {
                        clampCandidate = true;
                        clearZSpeed = true;
                    }
                }
                else if ((property & 0x08000000u) == 0u)
                {
                    // Ordered equality currentZ == moveUpLimitZ.
                    clearZSpeed = true;
                }

                if (clampCandidate)
                    candidateZ = moveUpLimitZ;
                if (clearZSpeed)
                    setZSpeedDirect(0.0f);
            }
        }

        const bool xChanged = !x87EqualOrUnordered(X(), candidateX);
        const bool yChanged = !x87EqualOrUnordered(Y(), candidateY);
        if (xChanged || yChanged)
        {
            SPRITE* const hit = CanPlaceWithCrush(candidateX, candidateY, candidateZ);
            if (hit)
            {
                dispatchVirtualAction(ActionCode::ACT_PATH_BLOCK,
                    spriteFtolLow32(static_cast<long double>(candidateX) - static_cast<long double>(X())),
                    spriteFtolLow32(static_cast<long double>(candidateY) - static_cast<long double>(Y())),
                    spriteFtolLow32(static_cast<long double>(candidateZ) - static_cast<long double>(Z())));
            }
            else
            {
                const float appWidth = applicationWorldFloatAt(0x28u);
                const float appHeight = applicationWorldFloatAt(0x2Cu);
                const bool inside =
                    !x87LessOrUnordered(candidateX, 0.0f) &&
                    x87LessOrUnordered(candidateX, appWidth) &&
                    !x87LessOrUnordered(candidateY, 0.0f) &&
                    x87LessOrUnordered(candidateY, appHeight);
                if (inside)
                {
                    ChangeCoor(candidateX, candidateY, candidateZ);
                }
                else
                {
                    dispatchVirtualAction(ActionCode::ACT_PATH_LIMIT,
                        spriteFtolLow32(static_cast<long double>(candidateX) - static_cast<long double>(X())),
                        spriteFtolLow32(static_cast<long double>(candidateY) - static_cast<long double>(Y())),
                        spriteFtolLow32(static_cast<long double>(candidateZ) - static_cast<long double>(Z())));
                }
            }
        }

        if (!x87EqualOrUnordered(Z(), candidateZ))
            ChangeCoor(X(), Y(), candidateZ);
    }

    int SPRITE::steerAwayFromMapBoundary(float x, float y) noexcept
    {
        const std::uint32_t deltaMs = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
        if (x87LessOrUnordered(x, 0.0f))
        {
            RotateTact(0x40, deltaMs);
            return 1;
        }
        if (x87LessOrUnordered(y, 0.0f))
        {
            RotateTact(0x80, deltaMs);
            return 1;
        }

        const float appWidth = applicationWorldFloatAt(0x28u);
        const float appHeight = applicationWorldFloatAt(0x2Cu);
        if (!x87OrderedGreater(appWidth, x))
        {
            RotateTact(0xC0, deltaMs);
            return 1;
        }
        if (!x87OrderedGreater(appHeight, y))
        {
            RotateTact(0, deltaMs);
            return 1;
        }
        return 0;
    }

    void SPRITE::computeNextMovementPosition(float* xOut, float* yOut, float* zOut) noexcept
    {
        *xOut = m_xyz.x;
        *yOut = m_xyz.y;
        *zOut = m_xyz.z;

        VID* const vid = m_vid;
        if ((m_runtimeFlags & SPRITE::CommandBitsMask) == 4u && (m_runtimeFlags & MovementStartedFlag) == 0u)
        {
            LOG::ResourceError("SPRITE %i", 10, "Move without StartMove()", 0,
                               vid ? vid->nvid() : -1);
            StartMove();
        }
        if ((m_runtimeFlags & SPRITE::CommandBitsMask) == 4u && m_goalSprite == nullptr)
        {
            LOG::ResourceError("SPRITE %i", 10, "Move without goal", 0,
                               vid ? vid->nvid() : -1);
            Stop();
        }

        const std::uint32_t rawDelta = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
        const std::int32_t deltaMs = static_cast<std::int32_t>(rawDelta);
        constexpr std::uint32_t kNoSpeedBits = 0x497423F0u; // 999999.0f

        if ((m_runtimeFlags & MovementStartedFlag) != 0u)
        {
            const float maxSpeed = vid->maxSpeedValue();

            if (x87OrderedGreater(maxSpeed, m_speed))
            {
                const float accel = vid->accelerationValue();
                if (spriteBitsEqual(accel, kNoSpeedBits))
                {
                    m_speed = maxSpeed;
                }
                else if (advanceAccelerationStep(deltaMs, accel, maxSpeed, m_speed))
                {
                    m_speed = maxSpeed;
                }
            }
        }
        else if (x87OrderedGreater(m_speed, 0.0f))
        {
            const float slow = vid->slowValue();
            if (spriteBitsEqual(slow, kNoSpeedBits) ||
                advanceDecelerationStep(deltaMs, slow, m_speed))
            {
                m_speed = 0.0f;
            }
        }

        GRAPH* const graph = GRAPH::CurrentGraph();
        // Retail dereferences the global GRAPH owner directly; a missing owner is
        // an original failure boundary rather than a silent zero-wind fallback.
        const bool speedIsZeroRoute = x87IsZeroOrUnordered(m_speed);
        const float windSpeed = graph->windSpeed();
        const bool windIsZeroRoute = x87IsZeroOrUnordered(windSpeed);
        const bool windProperty = (vid->properties() & P_WIND) != 0u;
        if (!speedIsZeroRoute || (!windIsZeroRoute && windProperty))
        {
            if (spriteBitsEqual(m_speed, kNoSpeedBits))
            {
                SPRITE* const target = m_goalSprite;
                if (target && ((m_runtimeFlags & CrossedGoalXFlag) == 0u ||
                               (m_runtimeFlags & CrossedGoalYFlag) == 0u))
                {
                    float targetX = target->m_xyz.x;
                    float targetY = target->m_xyz.y;
                    float targetZ = target->m_xyz.z;
                    const int traceHit = traceMovementCollisionTo(&targetX, &targetY, &targetZ);
                    ChangeCoor(targetX, targetY, targetZ);
                    if (traceHit)
                        dispatchVirtualAction(ActionCode::ACT_PATH_BLOCK, 0, 0, 0);
                    *xOut = targetX;
                    *yOut = targetY;
                    *zOut = targetZ;
                    m_runtimeFlags |= CrossedGoalAxesMask;
                    return;
                }
                m_runtimeFlags |= CrossedGoalAxesMask;
            }
            else
            {
                const std::uint32_t direction = static_cast<std::uint32_t>(m_direction.Int());
                advancePlanarPosition(deltaMs, m_speed,
                                    spriteFloatFromBits(g_retailDirectionTrigWindow[512u + direction]),
                                    spriteFloatFromBits(g_retailDirectionTrigWindow[768u + direction]),
                                    *xOut, *yOut);
                if (windProperty)
                {
                    const int windDirection = static_cast<int>(graph->windDirection());
                    advancePlanarPosition(deltaMs, windSpeed,
                                        directionSin(windDirection), directionCos(windDirection),
                                        *xOut, *yOut);
                }
            }
        }

        const DWORD property = vid->properties();
        if ((property & P_GRAVITY) != 0u)
        {
            BASE_CONSTANTS* const constants = GlobalBaseConstants();
            const float gravity = constants ? spriteFloatFromBits(constants->raw[2]) : 0.0f;
            applyGravityStep(deltaMs, gravity, m_zSpeed);
        }
        else if ((property & P_GRAVITY2) != 0u)
        {
            BASE_CONSTANTS* const constants = GlobalBaseConstants();
            const float gravity = constants ? spriteFloatFromBits(constants->raw[3]) : 0.0f;
            applyGravityStep(deltaMs, gravity, m_zSpeed);
        }
        advanceVerticalPosition(deltaMs, m_zSpeed, *zOut);

        SPRITE* const target = m_goalSprite;
        if (!target)
            return;

        // The retail x87 branch sequence is equivalent to an ordered inclusive
        // interval test. Any unordered operand exits without setting the bit.
        const auto crossed = [](float startValue, float finishValue, float point) noexcept -> bool
        {
            if (std::isnan(startValue) || std::isnan(finishValue) || std::isnan(point))
                return false;
            if (startValue < finishValue)
                return point >= startValue && point <= finishValue;
            return point >= finishValue && point <= startValue;
        };
        if (crossed(m_xyz.x, *xOut, target->m_xyz.x))
            m_runtimeFlags |= CrossedGoalXFlag;
        if (crossed(m_xyz.y, *yOut, target->m_xyz.y))
            m_runtimeFlags |= CrossedGoalYFlag;

    }

    void SPRITE::ChangeCoor(float x, float y, float z) noexcept
    {
        const float deltaX = x - m_xyz.x;
        const float deltaY = y - m_xyz.y;
        const float deltaZ = z - m_xyz.z;

        for (SPRITE* node = this; node; node = node->m_childChain)
        {
            const VECTOR before = node->m_xyz;
            const VECTOR target(before.x + deltaX, before.y + deltaY, before.z + deltaZ);

            if ((node->m_vid->property & P_HASH) != 0)
                node->MoveHashBeforeCoordinateWrite(target.x, target.y);

            const std::uint32_t realTime = core::RealTimeMilliseconds();
            if (node->m_actionAuxState && node->m_actionAuxState->lastUpdateTime != realTime)
            {
                node->m_actionAuxState->lastUpdateTime = realTime;
                node->m_actionAuxState->sourceX = node->m_xyz.x;
                node->m_actionAuxState->sourceY = node->m_xyz.y;
                node->m_actionAuxState->sourceZ = node->m_xyz.z;
            }

            node->m_xyz = target;
        }
    }

    float SPRITE::linkerX() const noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        float value = 0.0f;
        std::memcpy(&value, reinterpret_cast<const BYTE*>(this) + RetailSpriteLayout::SharedPrimaryState, sizeof(value));
        return value;
#else
        return hostState().linkerX;
#endif
    }

    float SPRITE::linkerY() const noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        float value = 0.0f;
        std::memcpy(&value, reinterpret_cast<const BYTE*>(this) + RetailSpriteLayout::SharedSecondaryState, sizeof(value));
        return value;
#else
        return hostState().linkerY;
#endif
    }

    float SPRITE::linkerZ() const noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        float value = 0.0f;
        std::memcpy(&value, reinterpret_cast<const BYTE*>(this) + RetailSpriteLayout::ExtendedStateBase, sizeof(value));
        return value;
#else
        return hostState().linkerZ;
#endif
    }

    int SPRITE::linkerDirection() const noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        int value = 0;
        std::memcpy(&value, reinterpret_cast<const BYTE*>(this) + RetailSpriteLayout::LegacyCommandState1, sizeof(value));
        return value;
#else
        return hostState().linkerDirection;
#endif
    }

    SPRITE* SPRITE::linkerOwner() const noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        SPRITE* value = nullptr;
        std::memcpy(&value, reinterpret_cast<const BYTE*>(this) + RetailSpriteLayout::AmmoFixedPoint, sizeof(value));
        return value;
#else
        return hostState().linkerOwner;
#endif
    }

    void SPRITE::setLinkerState(float x70, float y74, float z78, int direction7C, SPRITE* owner80) noexcept
    {
#if UINTPTR_MAX == 0xFFFFFFFFu
        std::memcpy(reinterpret_cast<BYTE*>(this) + RetailSpriteLayout::SharedPrimaryState, &x70, sizeof(x70));
        std::memcpy(reinterpret_cast<BYTE*>(this) + RetailSpriteLayout::SharedSecondaryState, &y74, sizeof(y74));
        std::memcpy(reinterpret_cast<BYTE*>(this) + RetailSpriteLayout::ExtendedStateBase, &z78, sizeof(z78));
        const int direction = direction7C & 0xFF;
        std::memcpy(reinterpret_cast<BYTE*>(this) + RetailSpriteLayout::LegacyCommandState1, &direction, sizeof(direction));
        std::memcpy(reinterpret_cast<BYTE*>(this) + RetailSpriteLayout::AmmoFixedPoint, &owner80, sizeof(owner80));
#else
        hostState().linkerX = x70;
        hostState().linkerY = y74;
        hostState().linkerZ = z78;
        hostState().linkerDirection = direction7C & 0xFF;
        hostState().linkerOwner = owner80;
#endif
    }

    int SPRITE::AddListReference()
    {
        m_listReferenceCount = spriteAdd32Wrap(m_listReferenceCount, 1);
        return m_listReferenceCount;
    }

    int SPRITE::ReleaseListReference()
    {
        m_listReferenceCount = spriteSub32Wrap(m_listReferenceCount, 1);
        const int refs = m_listReferenceCount;
        if (refs < 0)
        {
            const int nvid = m_vid ? m_vid->nVid : -1;
            LOG::ResourceError("SPRITE %i", 4, "noRef\tat Release", refs, nvid);
        }
        return refs;
    }

    void SPRITE::setOldAddress(int value)
    {
        hostState().oldAddress = value;
        if (!hostState().number)
            hostState().number = value;
    }

    void SPRITE::setNumber(int value)
    {
        hostState().number = value;
    }

    std::uint32_t SPRITE::rawResolveOldSpriteHandleLow32(int oldAddress) const noexcept
    {
        if (!hostState().owner)
            return 0u;
        SPRITE* const resolved = hostState().owner->ResolveOldSpriteHandle(oldAddress);
        return resolved ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(resolved)) : 0u;
    }

    void SPRITE::ChangeAnimation(int animationId)
    {
        if (animationId >= VID::NO_ANIMATION)
        {
            const int nvid = m_vid ? m_vid->nVid : -1;
            LOG::ResourceError("SPRITE %i", 4, "new_animation	in ChangeAnimation", animationId, nvid);
            return;
        }

        if (SPRITE* child = childChain())
        {
            VID* childVid = child->m_vid;
            if (childVid == m_vid->linkVid)
            {
                bool recurseChild = false;
                if (childVid->nLinkVid != 0 || (childVid->property & P_WIND) != 0 || child->m_currentAnimation == 8 || child->m_currentAnimation >= 15)
                    recurseChild = (animationId == 15 || animationId == 16);
                else
                    recurseChild = (child->m_currentAnimation < 15);

                if (recurseChild)
                    child->ChangeAnimation(animationId);
            }
        }

        if (m_currentAnimation == animationId)
            return;

        m_runtimeFlags &= ~0x00000200u;

        int frameDirection = m_direction.Int() & 0xFF;
        if ((m_vid->property & P_VERTDIR) != 0)
        {
            int projectedX = 0;
            int projectedY = 0;
            if (projectVerticalMotion(frameDirection, m_speed, m_zSpeed, projectedX, projectedY))
                frameDirection = AngleFromXY(projectedX, projectedY, nullptr) & 0xFF;
        }

        const std::int32_t baseFrame = static_cast<std::int32_t>(m_vid->animationBaseFrame[animationId]);
        const std::int32_t directionByte =
            static_cast<std::int32_t>((m_vid->directionQuantizationOffset() + frameDirection) & 0xFF);
        const std::uint32_t scaledDirection =
            static_cast<std::uint32_t>(spriteImul32Low(directionByte, static_cast<std::int32_t>(m_vid->noDir))) >> 8;
        const std::int32_t directionFrameOffset = spriteImul32Low(
            static_cast<std::int32_t>(scaledDirection),
            static_cast<std::int32_t>(m_vid->animationFrameCount[animationId]));
        const std::int32_t currentFrame = spriteAdd32Wrap(baseFrame, directionFrameOffset);

        m_currentFrame = currentFrame;
        m_currentAnimation = animationId;

        if (animationId >= 13 && m_vid->noAnimCadr[animationId] == 0)
        {
            m_currentFrameEnd = currentFrame;
            m_currentFrameBegin = currentFrame;
        }
        else
        {
            const std::int32_t count = static_cast<std::int32_t>(m_vid->animationFrameCount[animationId]);
            m_currentFrameEnd = spriteAdd32Wrap(spriteAdd32Wrap(currentFrame, count), -1);
            m_currentFrameBegin = currentFrame;
        }

    }

    void SPRITE::setCurrentAnimation(int value)
    {
        ChangeAnimation(value);
    }

    void SPRITE::ChangeSpeed(float value)
    {
        m_speed = value;
    }

    void SPRITE::ChangeZSpeed(float value)
    {
        m_zSpeed = value;
    }

    void SPRITE::SetTimer(DWORD value)
    {
        m_actionTimer = value;
    }

    void SPRITE::ChangeArmy(int value)
    {
        changeArmyBucket(value);
    }

    void SPRITE::Draw()
    {
        m_vid->Draw(this);
    }

    int SPRITE::DrawDebugOverlay(GRAPH& graph) const
    {
        const VID* const vid = Vid();
        const DWORD spriteType = vid->spriteTypeId();

        constexpr DWORD kBlack  = 0xFF000000u;
        constexpr DWORD kWhite  = 0xFFFFFFFFu;
        constexpr DWORD kGreen  = 0xFF00FF00u;
        constexpr DWORD kPurple = 0xFF8080FFu;
        constexpr DWORD kYellow = 0xFFFFFF00u;
        constexpr DWORD kGray   = 0xFF808080u;
        constexpr DWORD kRed    = 0xFFFF0000u;
        constexpr DWORD kBlue   = 0xFF0000FFu;

        DWORD color = kRed;
        if ((spriteType & U_TERRAIN) != 0u && (vid->properties() & P_HASH) != 0u)
            color = kBlack;
        else if (vid->renderLayer() == 8)
            color = kWhite;
        else if ((spriteType & U_UNIT) != 0u)
            color = kGreen;
        else if ((spriteType & U_AVIA) != 0u)
            color = kPurple;
        else if ((spriteType & U_OBJECT) != 0u)
            color = kYellow;
        else if ((spriteType & U_RAILWAY) == 0u)
        {
            if ((spriteType & U_CANNON) != 0u)
                color = kGray;
            else if (vid->spriteClassId() == B_FRAME)
                color = kWhite;
        }

        const core::ApplicationDrawDispatcherState& drawState =
            core::GlobalApplicationDrawDispatcherState();
        const float baseX = X() - drawState.cameraShiftX();
        const float baseY = Y() - Z() - drawState.cameraShiftY();

        if ((spriteType >= U_TERRAIN && spriteType <= U_CANNON) || spriteType == U_SPRITE)
        {
            graph.DrawRect(
                X() - vid->halfSizeX() - drawState.cameraShiftX(),
                Y() - Z() - vid->halfSizeY() - drawState.cameraShiftY(),
                X() + vid->halfSizeX() - drawState.cameraShiftX(),
                Y() - Z() + vid->halfSizeY() - drawState.cameraShiftY(),
                color);
        }

        if ((spriteType & (U_OBJECT | U_UNIT)) != 0u)
            graph.DrawLine(baseX, baseY - vid->sizeZ(), baseX, baseY, kBlue);

        char nvidText[32] = {};
#if defined(_MSC_VER)
        sprintf_s(nvidText, "%i", vid->nvid());
#else
        std::snprintf(nvidText, sizeof(nvidText), "%i", vid->nvid());
#endif
        return graph.drawTextColored(baseX + 1.0f, baseY, nvidText, kWhite);
    }

    void SPRITE::DrawSelectionOverlay(GRAPH& graph) const
    {
        if (isHiddenByCliping())
            return;

        const float sx = X() - graph.cameraX();
        const float sy = Y() - Z() - graph.cameraY();
        graph.DrawRect(sx - 8.0f, sy - 8.0f, sx + 8.0f, sy + 8.0f, 0x0000FFFFu);
        graph.DrawText(sx + 10.0f, sy - 8.0f, "%i", getNumber());
    }

    int SPRITE::renderFrameOffsetForClock(std::uint32_t clockMilliseconds) const
    {
        if (!m_vid || m_currentAnimation < 0 || m_currentAnimation >= VID::NO_ANIMATION)
            return 0;

        const int count = m_vid->animationFrameCount[m_currentAnimation];
        if (count <= 1)
            return 0;

        const int speed = m_vid->hostFrameSpeedStorage(m_currentAnimation);
        if (speed <= 0)
            return 0;

        return static_cast<int>((clockMilliseconds / static_cast<std::uint32_t>(speed)) % static_cast<std::uint32_t>(count));
    }

    int SPRITE::renderFrameIndexForClock(std::uint32_t clockMilliseconds) const
    {
        if (!m_vid || m_currentAnimation < 0 || m_currentAnimation >= VID::NO_ANIMATION)
            return -1;

        const int count = m_vid->animationFrameCount[m_currentAnimation];
        if (count <= 0 || m_vid->noDir <= 0)
            return -1;

        const int realDirection = m_vid->RealDirection(m_direction);
        const int startFrame = m_vid->animationBaseFrame[m_currentAnimation] + realDirection * count;
        const int frame = startFrame + renderFrameOffsetForClock(clockMilliseconds);
        if (frame < 0 || frame >= m_vid->noCadr)
            return -1;
        return frame;
    }

    int SPRITE::SizeTo(const VECTOR2& target) const
    {
        const int dx = static_cast<int>(target.x - m_xyz.x);
        const int dy = static_cast<int>(target.y - m_xyz.y);
        return IntegerSquareRoot(dx * dx + dy * dy);
    }

    ANGLE SPRITE::DirectionTo(const VECTOR2& target) const
    {
        const int dx = static_cast<int>(target.x - m_xyz.x);
        const int dy = static_cast<int>(target.y - m_xyz.y);
        return ANGLE::FromXY(dx, dy);
    }

    int SPRITE::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        return dispatchActionOpcode(
            static_cast<std::uint32_t>(opcode),
            static_cast<int>(argument1Carrier),
            argument2Carrier,
            argument3Carrier);
    }

    SpriteCommandRecord SPRITE::buildCommandRecord(std::uint32_t opcode, int argument1, int argument2, int argument3)
    {
        SpriteCommandRecord out{};
        out.opcode = opcode;
        out.argument1 = static_cast<std::uint32_t>(argument1);
        out.argument2 = static_cast<std::uint32_t>(argument2);
        out.argument3 = static_cast<std::uint32_t>(argument3);
        return out;
    }

    void SPRITE::serializeCommandRecordsText(STRING& out) const
    {
        m_commandStack.serializeCommandRecordsText(out);
    }

    std::string SPRITE::serializeCommandRecordsText() const
    {
        return m_commandStack.serializeCommandRecordsText();
    }

    void SPRITE::parseCommandRecordsText(const STRING& text)
    {
        m_commandStack.parseCommandRecordsText(text);
    }

    void SPRITE::queueCommandBeforeStopSentinel(std::uint32_t opcode, int argument1, int argument2, int argument3)
    {
        m_commandStack.queueCommandBeforeStopSentinel(opcode, argument1, argument2, argument3);
    }

    int SPRITE::dispatchActionOpcode(std::uint32_t opcode, int argument1, int argument2, int argument3)
    {

        int returnValue = 0;

        switch (opcode & 0xFFu)
        {
        case static_cast<std::uint32_t>(AnimationCode::ANI_DEATH):
        {
            VID* const sourceVid = m_vid;
            const int damageRaw = sourceVid->deathDamageMinimumRawBits();

            returnValue = 0;
            if (damageRaw == 0)
            {

                break;
            }

            setAnimationFrameTime(0);
            const float deathRange = sourceVid->deathRangeValue();
            const float rangeX = sourceVid->halfSizeX() + deathRange;
            const float rangeY = sourceVid->halfSizeY() + deathRange;
            const float sourceSizeZ = sourceVid->sizeZ();
            const float rangeZ = x87LessEqualOrUnordered(20.0f, sourceSizeZ)
                ? sourceSizeZ
                : 20.0f;

            SPRITE_COLLECTOR_HASH_MAP* const hash = GlobalSpriteHashMap();
            for (SPRITE* candidate = hash->firstSpriteInBox(
                     X() - rangeX, Y() - rangeY, X() + rangeX, Y() + rangeY);
                 candidate;
                 candidate = hash->nextSpriteInBox())
            {
                if (candidate == this)
                    continue;

                VID* const candidateVid = candidate->Vid();
                if (candidateVid->maximumHp() == 0)
                    continue;

                if ((sourceVid->properties() & P_NOTDAMAGEFORFRIEND) != 0u &&
                    (sameArmy(*candidate)))
                {
                    continue;
                }

                if (!x87SumGreaterThanAbsDiffOrdered(
                        rangeX, candidateVid->halfSizeX(), X(), candidate->X()) ||
                    !x87SumGreaterThanAbsDiffOrdered(
                        rangeY, candidateVid->halfSizeY(), Y(), candidate->Y()) ||
                    !x87SumGreaterThanAbsDiffOrdered(
                        rangeZ, candidateVid->sizeZ(), Z(), candidate->Z()))
                {
                    continue;
                }

                const float midpointX = (candidate->X() + X()) * 0.5f;
                const float midpointY = (candidate->Y() + Y()) * 0.5f;
                const float midpointGround =
                    hostState().owner->GetGroundZ(VECTOR2{midpointX, midpointY});
                if (x87SumLessOrUnordered(
                        candidate->Z(), candidateVid->sizeZ(), midpointGround))
                    continue;

                const float nearSourceX = spriteWeightedQuarterF32(X(), candidate->X());
                const float nearSourceY = spriteWeightedQuarterF32(Y(), candidate->Y());
                const float nearSourceGround =
                    hostState().owner->GetGroundZ(VECTOR2{nearSourceX, nearSourceY});
                if (x87SumLessOrUnordered(
                        candidate->Z(), candidateVid->sizeZ(), nearSourceGround))
                    continue;

                const float nearCandidateX = spriteWeightedQuarterF32(candidate->X(), X());
                const float nearCandidateY = spriteWeightedQuarterF32(candidate->Y(), Y());
                const float nearCandidateGround =
                    hostState().owner->GetGroundZ(VECTOR2{nearCandidateX, nearCandidateY});
                if (x87SumLessOrUnordered(
                        candidate->Z(), candidateVid->sizeZ(), nearCandidateGround))
                    continue;

                int damage = damageRaw;
                if ((sourceVid->properties() & P_RADIALDAMAGE) != 0u)
                {
                    if (!computeFalloffDamage(
                            X(), Y(), candidate->X(), candidate->Y(),
                            deathRange, damageRaw, damage))
                        continue;
                }

                candidate->dispatchVirtualAction(ActionCode::ACT_DAMAGE,
                    damage,
                    static_cast<int>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFFFFu),
                    0);
            }

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_ATTACK):
        {
            SPRITE* const owner = reinterpret_cast<SPRITE*>(
                static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            setAttackCommandForTarget(owner);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_MOVE):
        {
            SPRITE* const helper = new (std::nothrow) SPRITE(
                hostState().owner, MAP::NullVid(),
                VECTOR(spriteFildToF32(argument1), spriteFildToF32(argument2), spriteFildToF32(argument3)),
                ANGLE(0), nullptr);
            Move(helper);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_MOVE_TO):
        {
            SPRITE* const owner = reinterpret_cast<SPRITE*>(
                static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            Move(owner);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_BUILD_UNIT):
        {
            core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
            if (argument1 >= 0 && argument1 < table.count())
            {
                // Retail reads Application+0x294 exactly once after the count
                // gate and reuses that pointer for virtual create slot +0x20.
                VID* const createVid = table.slot(argument1);
                if (createVid)
                {
                    SPRITE* const created = hostState().owner->CreateSpriteViaFactory(
                        createVid,
                        VECTOR(spriteFildToF32(argument2), spriteFildToF32(argument3), m_xyz.z),
                        m_direction,
                        this,
                        false);
                    if (created)
                        copyCommandPrefixTo(created);
                }
            }

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_COOR_ATTACK):
        {
            VID* metricVid = m_vid;
            if (SPRITE* const child = childChain())
            {
                VID* const childVid = child->Vid();
                if (childVid == m_vid->linkedVid() &&
                    childVid->hasWeaponChildDescriptor() != 0u &&
                    childVid->weaponCount() != 0u)
                {
                    metricVid = childVid;
                }
            }

            const int weaponType = metricVid->weaponTypeMask();
            const float x = spriteFildToF32(argument1);
            float yProbe = spriteFildToF32(argument2);
            float z = 0.0f;
            int helperY = argument2;
            if (weaponType == 8)
            {
                yProbe = spriteFildAddF32(argument2, 80.0f);
                const float ground = hostState().owner->GetGroundZ(VECTOR2{x, yProbe});
                const int zAsInt = spriteAddF32StoreAndFtolLow32(ground, 80.0f, z);
                helperY = spriteAdd32Wrap(helperY, zAsInt);
            }
            else
            {
                const float ground = hostState().owner->GetGroundZ(VECTOR2{x, yProbe});
                const int zAsInt = spriteAddF32StoreAndFtolLow32(ground, 19.0f, z);
                helperY = spriteAdd32Wrap(helperY, spriteAdd32Wrap(zAsInt, -19));
            }

            SPRITE* const helper = new (std::nothrow) SPRITE(
                hostState().owner, MAP::NullVid(), VECTOR(x, spriteFildToF32(helperY), z), ANGLE(0), nullptr);
            SetCommand(4, helper);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_FLAGMAN_TRIGGER):
        {
            MAP* const firstOwner = MAP::Current();
            SPRITE* const firstControlled = firstOwner->flagmanSpriteForPlayer(
                static_cast<int>(core::ActivePlayerIndex()));
            if (firstControlled)
            {
                MAP* const secondOwner = MAP::Current();
                SPRITE* const controlled = secondOwner->flagmanSpriteForPlayer(
                    static_cast<int>(core::ActivePlayerIndex()));
                if (shouldSuppressFlagmanCommand(
                        argument1, argument2, argument3, controlled->X(), controlled->Y()))
                {

                    break;
                }
            }
            SpriteCommandRecord command = buildCommandRecord(opcode, argument1, argument2, argument3);
            m_commandStack.appendCommandRecord(command);

            break;
        }

        case 74:
        {
            SPRITE* const target = reinterpret_cast<SPRITE*>(
                static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            SetCommand(static_cast<int>((opcode >> 8) & 0xFFu), target);
            if (target)
            {
                const int refs = target->ReleaseListReference();
                if (refs == 0)
                    DeleteSpriteThroughVirtualDeletingDestructor(target);
            }

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_DESTROY_UNIT):
        {
            core::ApplicationVidTable& vidTable = core::GlobalApplicationVidTable();
            if (argument1 >= 0 &&
                argument1 < vidTable.count() &&
                vidTable.slot(argument1) != nullptr)
            {
                SPRITE* const hit = core::Application::findSpriteAtPointByFilter(
                    *hostState().owner,
                    core::GlobalApplicationDrawDispatcherState(),
                    spriteAdd32Wrap(argument1, 2048),
                    spriteFildToF32(argument2),
                    spriteFildToF32(argument3));
                if (hit)
                    DeleteSpriteThroughVirtualDeletingDestructor(hit);
            }

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_LOGIC_RUN):
        {
            (void)core::Application::callScriptFunction(
                argument1,
                static_cast<int>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFFFFu),
                0);

            break;
        }

        case static_cast<std::uint32_t>(AnimationCode::ANI_SALUT):
            if (m_currentAnimation != 8)
                ChangeAnimation(9);

            break;

        case static_cast<std::uint32_t>(ActionCode::ACT_RANDOM):
        {
            if ((std::rand() % 5) == 0)
            {
                ChangeAnimation(0);
            }
            else if ((std::rand() % 5) == 0)
            {
                ChangeAnimation(12);
            }
            else
            {
                if (m_currentAnimation == 4 && (std::rand() % 3) != 0)
                {
                    VID* const vid = m_vid;
                    const std::uint32_t now = as1::core::CurrentTimeMilliseconds();
                    const std::uint32_t previous = as1::core::PreviousWorldTimeMilliseconds();
                    const std::uint32_t frameDefault =
                        static_cast<std::uint32_t>(vid->defaultFrameSpeed());
                    const std::uint32_t delta = now - previous;
                    const std::uint32_t stepMs = delta > frameDefault ? delta : frameDefault;
                    RotateTact(spriteSub32Wrap(m_direction.Int(), 64), stepMs);
                    }
                else if (m_currentAnimation == 5 && (std::rand() % 3) != 0)
                {
                    VID* const vid = m_vid;
                    const std::uint32_t now = as1::core::CurrentTimeMilliseconds();
                    const std::uint32_t previous = as1::core::PreviousWorldTimeMilliseconds();
                    const std::uint32_t frameDefault =
                        static_cast<std::uint32_t>(vid->defaultFrameSpeed());
                    const std::uint32_t delta = now - previous;
                    const std::uint32_t stepMs = delta > frameDefault ? delta : frameDefault;
                    RotateTact(spriteAdd32Wrap(m_direction.Int(), 64), stepMs);
                    }
                else if ((std::rand() & 0x3) == 0)
                {
                    const int nextDirection = (std::rand() & 1) != 0
                        ? spriteSub32Wrap(m_direction.Int(), 64)
                        : spriteAdd32Wrap(m_direction.Int(), 64);
                    VID* const vid = m_vid;
                    const std::uint32_t previous = as1::core::PreviousWorldTimeMilliseconds();
                    const std::uint32_t frameDefault =
                        static_cast<std::uint32_t>(vid->defaultFrameSpeed());
                    const std::uint32_t now = as1::core::CurrentTimeMilliseconds();
                    const std::uint32_t delta = now - previous;
                    const std::uint32_t stepMs = delta > frameDefault ? delta : frameDefault;
                    RotateTact(nextDirection, stepMs);
                    }
            }

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_STOP):
            Stop();
            if (argument1 != 0)
                m_speed = 0.0f;

            break;

        case static_cast<std::uint32_t>(ActionCode::ACT_PAUSE):
        {
            if (!x87IsZeroOrUnordered(m_speed))
                Stop();
            SetCommand(18, nullptr);
            const int pauseRange = spriteAdd32Wrap(argument2, 1);
            const int pauseDelta = std::rand() % pauseRange;
            m_actionTimer = static_cast<DWORD>(spriteAdd32Wrap(argument1, pauseDelta));

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_ROTATE):
        {
            VID* const vid = m_vid;
            const std::uint32_t now = as1::core::CurrentTimeMilliseconds();
            const std::uint32_t previous = as1::core::PreviousWorldTimeMilliseconds();
            const std::uint32_t frameDefault =
                static_cast<std::uint32_t>(vid->defaultFrameSpeed());
            const std::uint32_t delta = now - previous;
            const std::uint32_t stepMs = delta > frameDefault ? delta : frameDefault;
            RotateTact(argument1, stepMs);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_CLEAR_COMMAND):
        {
            SetCommand(0, nullptr);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_PATH_BLOCK):
            m_zSpeed = 0.0f;
            m_speed = 0.0f;

            break;

        case static_cast<std::uint32_t>(ActionCode::ACT_PATH_LIMIT):
        {
            ChangeCoor(spriteFildAddF32(argument1, m_xyz.x),
                       spriteFildAddF32(argument2, m_xyz.y),
                       spriteFildAddF32(argument3, m_xyz.z));

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_CHANGE_DIRECTION):
        {
            ChangeDirection(static_cast<unsigned char>(argument1));

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_CHANGE_ANIMATION):
        {
            ChangeAnimation(argument1);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_CHANGE_VID):
        {
        core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
        if (argument1 < 0 || argument1 >= table.count())
            return 0;

        VID* nextVid = table.slot(argument1);
        if (!nextVid)
            return 0;

        VID* const oldVid = m_vid;
        if (oldVid->nVid == argument1)
            return 0;

        const int oldDirection = m_direction.Int();
        const int oldAnimation = m_currentAnimation;
        const DWORD oldClass = oldVid->spriteClassId();
        const DWORD nextClass = nextVid->spriteClassId();

        if (oldClass != nextClass)
        {
            LOG::ResourceError("SPRITE %i", 4, "ACT_CHANGE_VID", argument1, oldVid->nVid);
        }

        for (VID* link = oldVid->linkedVid(); link; link = link->linkedVid())
            deleteChildByVid(link);

        m_runtimeFlags &= ~0x00001000u;
        RemoveSpriteFromGlobalHashForActionSwitch(this);
        removeFromDrawBucketsRecursive();

        oldVid->decrementSpriteCountForArmy(armyIndex());

        VID* swapVid = nullptr;
        if (argument1 < table.count())
            swapVid = table.slot(argument1);
        m_vid = swapVid ? swapVid : MAP::NullVid();
        m_vid->setLastSpriteCountChangeTimestamp(core::RealTimeMilliseconds());
        m_vid->incrementSpriteCountForArmy(armyIndex());

        const int requestedAnimation = argument2 >= 0 ? argument2 : oldAnimation;
        m_direction = ANGLE(0);
        m_currentAnimation = 0;
        m_currentFrame = 0;
        m_currentFrameBegin = 0;
        m_currentFrameEnd = m_vid->animationFrameCountFor(0) - 1;

        if (m_vid->actionAuxStateRequired() != 0)
        {
            if (!m_actionAuxState)
            {
                void* const storage = ::operator new(0x20u, std::nothrow);
                m_actionAuxState = static_cast<ActionAuxState*>(storage);
                if (m_actionAuxState)
                    initializeActionAuxState(this);
            }
            else
            {
                const WEAPON* const weapon = m_vid->weaponRecord();
                std::uint32_t weaponValue = 0;
                std::memcpy(&weaponValue, weapon->raw.data() + 0x28, sizeof(weaponValue));
                m_actionAuxState->primaryValue = weaponValue;
            }
        }

        addToDrawBucketsRecursive();
        AddSpriteToGlobalHashForActionSwitch(this);
        ensureLinkedVidChild();
        ChangeAnimation(requestedAnimation);
        ChangeDirection(oldDirection);

        return 0;
    
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_CHANGE_COOR):
        {
            ChangeCoor(spriteFildToF32(argument1),
                       spriteFildToF32(argument2),
                       spriteFildToF32(argument3));

            break;
        }

        case static_cast<std::uint32_t>(InternalActionCode::SetAnimationAndDirection):
        {
            ChangeAnimation(argument1);
            ChangeDirection(static_cast<unsigned char>(argument2));

            break;
        }

        case static_cast<std::uint32_t>(InternalActionCode::ChangeCoordinateXY):
        {
            ChangeCoor(spriteFildToF32(argument1),
                       spriteFildToF32(argument2),
                       m_xyz.z);

            break;
        }

        case static_cast<std::uint32_t>(InternalActionCode::ChangeCoordinateZ):
        {
            ChangeCoor(m_xyz.x,
                       m_xyz.y,
                       spriteFildToF32(argument1));

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_DAMAGE):
        {
            VID* const vid = m_vid;
            const int amount = argument1;
            const int currentFrameTime = animationFrameTime();
            const int maxFrameTime = vid->animationFrameDuration(armyIndex());

            returnValue = 0;

            if (currentFrameTime >= maxFrameTime && amount < 0)
            {
                returnValue = 1;

                break;
            }

            if (vid->maxHp != 0)
                updateAnimationFrameTime(spriteSub32Wrap(currentFrameTime, amount));

            int afterRawHp = animationFrameTime();
            if (afterRawHp > maxFrameTime && amount < 0)
            {
                setAnimationFrameTime(maxFrameTime);
                afterRawHp = maxFrameTime;
            }

            if (amount > 0)
            {
                if (m_currentAnimation == 0 || m_currentAnimation == 2)
                {
                    ChangeAnimation(7);
                }
                else
                {
                    const int damageScript = vid->damageScriptFunction();
                    if (damageScript >= 0 &&
                        core::Application::callScriptFunction(
                            damageScript,
                            static_cast<int>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFFFFu),
                            argument2) != 0)
                    {

                        break;
                    }

                    VID* const postDamageCallbackVid = m_vid;
                    const int damageSfx = postDamageCallbackVid->damageSfxId();
                    if (damageSfx != 0)
                        playSfxAtWorldPosition(damageSfx);

                    if (postDamageCallbackVid->hasHitChildVid() != 0)
                    {
                        const int savedAnimation = m_currentAnimation;
                        m_currentAnimation = 7;
                        spawnAnimationChild();
                        m_currentAnimation = savedAnimation;
                    }
                }
            }

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_BEHAVE):
        {

            returnValue = 0;

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_REPAIR):
        {

            VID* const vid = m_vid;
            VID* const deleteVid = vid->woundChildVid();
            (void)deleteChildByVid(deleteVid);

            const int bucket = armyIndex();
            const int repairFrameTime = vid->animationFrameDuration(bucket);
            setAnimationFrameTime(repairFrameTime);

            bool childRepairDispatched = false;
            if (SPRITE* const child = childChain())
            {
                VID* const linkVid = vid->linkedVid();
                if (child->Vid() == linkVid)
                {
                    (void)child->dispatchVirtualAction(ActionCode::ACT_REPAIR, 0, 0, 0);
                    childRepairDispatched = true;
                }
            }

            if (!childRepairDispatched)
                ensureLinkedVidChild();

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_HP):
        {

            returnValue = animationFrameTime();

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_HP):
        {

            int targetFrameTime = argument1;
            if (targetFrameTime == 0)
            {
                const int bucket = armyIndex();
                targetFrameTime = spriteImul32Low(
                    m_vid->animationFrameDuration(bucket), argument2) / 100;
            }
            updateAnimationFrameTime(targetFrameTime);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_PERCENT_HP):
        {
            const int raw255 = healthRatio255();
            const int percent = raw255 * 100 / 255;

            returnValue = percent;

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_GOAL):
        {

            returnValue = static_cast<int>(reinterpret_cast<std::uintptr_t>(goalSprite()) & 0xFFFFFFFFu);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_AMMO):
        case static_cast<std::uint32_t>(ActionCode::ACT_ADD_AMMO):
        {
            SPRITE* const uplink = childBacklink();
            if (!uplink)
            {

                returnValue = 0;

                break;
            }

            const int partResult = uplink->Action(static_cast<int>(opcode), argument1, argument2, argument3);

            returnValue = partResult;

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_BATTLE_RANGE):
        {
            VID* selectedVid = m_vid;
            SPRITE* const child = childChain();
            if (child)
            {
                VID* const childVid = child->Vid();
                if (childVid == m_vid->linkedVid() &&
                    childVid->hasWeaponChildDescriptor() != 0u &&
                    childVid->weaponCount() != 0u &&
                    x87IsZeroOrUnordered(m_vid->weaponBattleRange()))
                {
                    selectedVid = childVid;
                }
            }

            returnValue = spriteFtolLow32(
                static_cast<long double>(selectedVid->weaponBattleRange()));

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_ANIMATION):
        case static_cast<std::uint32_t>(InternalActionCode::GetAnimation):
        {
            // Original shared route returns raw [SPRITE+0x48].

            returnValue = m_currentAnimation;

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_ARMY):
        {
            // Original route returns ([SPRITE+0x28] >> 10) & 3.
            // This is a pure raw-flag query; no army setter/helper route is called.

            returnValue = armyIndex();

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_ARMY):
        {
            changeArmyBucket(argument1);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_INVISIBLE):
        {
            m_runtimeFlags = argument1 != 0 ? (m_runtimeFlags | DrawSuppressedFlag) : (m_runtimeFlags & ~DrawSuppressedFlag);
            if (SPRITE* child = childChain())
            {
                if (argument1 != 0)
                    child->suppressDrawRecursive();
                else
                    child->restoreDrawRecursive();
            }

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_LINK):
        {

            returnValue = static_cast<int>(reinterpret_cast<std::uintptr_t>(childChain()) & 0xFFFFFFFFu);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_UPLINK):
        {
            // Original route returns the raw back-link/up-link pointer from
            // [SPRITE+0x44] without mutating sprite state.

            returnValue = static_cast<int>(reinterpret_cast<std::uintptr_t>(childBacklink()) & 0xFFFFFFFFu);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_TIMER):
        {
            // Original route returns the raw timer DWORD from [SPRITE+0x50]
            // without mutating sprite state.

            returnValue = static_cast<int>(m_actionTimer);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_TIMER):
        {
            m_actionTimer = static_cast<DWORD>(argument1);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_ZSPEED):
        {

            returnValue = spriteFtolLow32(
                static_cast<long double>(m_zSpeed) * 1000.0L);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_ZSPEED):
        {
            m_zSpeed = spriteFildMulF32(argument1, 0.001f);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_SPEED):
        {

            returnValue = spriteFtolLow32(
                static_cast<long double>(m_speed) * 1000.0L);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_SPEED):
        {
            m_speed = spriteFildMulF32(argument1, 0.001f);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_COMMAND):
        {
            // Original route returns ([SPRITE+0x28] >> 2) & 0x1F.
            // This is a pure raw-flag query; command mutation routes remain closed.

            returnValue = commandIndex();

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_DEATH_TIMER):
        {
            const std::uint32_t requestedValue10 = static_cast<std::uint32_t>(argument1);

            if (!m_actionAuxState)
            {
                void* const storage = ::operator new(0x20u, std::nothrow);
                m_actionAuxState = static_cast<ActionAuxState*>(storage);
                if (m_actionAuxState)
                    initializeActionAuxState(this);
            }

            m_actionAuxState->primaryValue = requestedValue10;

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_BACKUP_COMMAND):
        {
            SPRITE* const goal = goalSprite();
            const std::uint32_t backupOpcode = (static_cast<std::uint32_t>(commandIndex()) << 8) + 74u;
            const int goalArg = static_cast<int>(reinterpret_cast<std::uintptr_t>(goal) & 0xFFFFFFFFu);
            const SpriteCommandRecord command = buildCommandRecord(backupOpcode, goalArg, 0, 0);
            m_commandStack.appendCommandRecord(command);

            if (goal)
                goal->setListReferenceCount(
                    spriteAdd32Wrap(goal->listReferenceCount(), 1));

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_CYCLE_STACK):
        {
            const std::uint32_t nextCount = static_cast<std::uint32_t>(argument1) + 1u;
            m_commandStack.setCommandRecordCount(nextCount);
            if (static_cast<std::int32_t>(nextCount) >
                static_cast<std::int32_t>(m_commandStack.m_commandRecords.capacity))
            {
                m_commandStack.ensureCommandRecordCapacityRetail(nextCount);
            }
            m_currentFrameBegin = m_currentFrameEnd;

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_CLEAR_STACK):
            // Original route clears command count/index and frees command-array storage at this+0x64.
            m_commandStack.clear();

            break;

        case static_cast<std::uint32_t>(ActionCode::ACT_STOP_STACK):
        {
            const SpriteCommandRecord command = buildCommandRecord(opcode, argument1, argument2, argument3);
            m_commandStack.appendCommandRecord(command);

            (void)dispatchVirtualAction(ActionCode::ACT_NEXT_COMMAND, 0, 0, 0);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SAVE):
        {
            BaseStream* const stream = reinterpret_cast<BaseStream*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            m_commandStack.saveCommandRecordsToStream(stream);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_RESTORE):
        {
            BaseStream* const stream = reinterpret_cast<BaseStream*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            m_commandStack.restoreCommandRecordsFromStream(stream, this);

            break;
        }

        case static_cast<std::uint32_t>(SpriteActConst::ACT_RESTORE_OLD_MAP):
        {
            BaseStream* const stream = reinterpret_cast<BaseStream*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            int armyBucket = 0;
            const bool hasArmyBucket = m_commandStack.restoreOldMapCommandRecordsFromStream(stream, argument2, this, &armyBucket);
            if (hasArmyBucket)
                changeArmyBucket(armyBucket);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_UNDO_REMOVE):
        {
            m_runtimeFlags |= SpatialHashRemovedFlag;
            RemoveSpriteFromGlobalHashForActionSwitch(this);
            removeFromDrawBucketsRecursive();

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_UNDO_INSERT):
        {
            m_runtimeFlags &= ~SpatialHashRemovedFlag;
            AddSpriteToGlobalHashForActionSwitch(this);
            addToDrawBucketsRecursive();

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_NEXT_COMMAND):
        {
            if (m_currentAnimation >= 15)
            {

                break;
            }

            if (childBacklink() != nullptr ||
                x87IsZeroOrUnordered(m_vid->maxSpeedValue()))
            {
                GRAPH* const graph = GRAPH::CurrentGraph();
                if ((m_vid->properties() & P_WIND) != 0u &&
                    !x87IsZeroOrUnordered(graph->windSpeed()))
                {
                    const std::uint32_t previous = as1::core::PreviousWorldTimeMilliseconds();
                    const std::uint32_t frameDefault = static_cast<std::uint32_t>(m_vid->defaultFrameSpeed());
                    const std::uint32_t now = as1::core::CurrentTimeMilliseconds();
                    const std::uint32_t deltaMs = now - previous;
                    const std::uint32_t stepMs = deltaMs > frameDefault ? deltaMs : frameDefault;
                    RotateTact(graph->windDirection(), stepMs);
                }
            }

            if (x87IsZeroOrUnordered(m_speed))
            {
                if (m_currentAnimation >= 6 && m_currentAnimation != 10)
                    ChangeAnimation(0);
            }
            else
            {
                if (m_currentAnimation != 2)
                    ChangeAnimation(2);
            }

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_PLAY_SFX):
        {
            const int requestSfx = argument1;
            playSfxAtWorldPosition(requestSfx);

            break;
        }

        default:
        {
            const int nvid = m_vid ? m_vid->nvid() : -1;
            LOG::ResourceError(
                "SPRITE %i", 10, "Action() have not this act",
                static_cast<int>(opcode), nvid);
            break;
        }
        }

        return returnValue;
    }

    void SPRITE::MoveTact()
    {
        performBaseMovementTact();
    }

    void SPRITE::DeletePointerToSprite(SPRITE* sprite)
    {
        if (!sprite)
            return;

        if (m_childChain)
            m_childChain->DeletePointerToSprite(sprite);

        if (m_goalSprite == sprite)
        {
            if (m_currentAnimation == 8)
            {
                SPRITE* const replacement = new (std::nothrow) SPRITE(
                    hostState().owner, MAP::NullVid(), sprite->xyz(), ANGLE(0), nullptr);
                SetCommand(4, replacement);
            }
            else
                SetCommand(0, nullptr);
        }

        m_commandStack.clearTargetReferences(sprite);
    }

}
