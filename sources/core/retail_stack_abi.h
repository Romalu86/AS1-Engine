#pragma once

#include <cstdint>

namespace as1::core
{
    inline std::uint32_t retailReadStackDword(const void* address) noexcept
    {
#if defined(_MSC_VER) && defined(_M_IX86)
        // Retail AS1 occasionally consumes stack dwords which were not
        // initialized on every input path.  Reading them through MSVC inline
        // assembly preserves the physical x86 stack-byte contract without
        // asking the C++ optimizer to evaluate an indeterminate scalar.
        std::uint32_t value;
        __asm
        {
            mov ecx, address
            mov eax, dword ptr [ecx]
            mov value, eax
        }
        return value;
#else
        // Non-x86 builds are audit/syntax hosts only.  The authoritative
        // behavioral target remains VS2026 Win32/x86.
        (void)address;
        return 0u;
#endif
    }
}
