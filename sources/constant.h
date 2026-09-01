#pragma once
#include "core/types.h"
#include <array>
#include <cstddef>

namespace as1
{
    class RESOURCE;

    struct BASE_CONSTANTS
    {
        static constexpr std::size_t AS1_DWORD_COUNT = 26;
        static constexpr std::size_t AS1_RECORD_SIZE = AS1_DWORD_COUNT * sizeof(DWORD);

        std::array<DWORD, AS1_DWORD_COUNT> raw{};

        bool Load(RESOURCE* res);
    };


    BASE_CONSTANTS* loadBaseConstantsFromResource(BASE_CONSTANTS* owner, RESOURCE* res);
    BASE_CONSTANTS* GlobalBaseConstants() noexcept;
    void BindGlobalBaseConstants(BASE_CONSTANTS* constants) noexcept;
}
