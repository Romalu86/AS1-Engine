#pragma once

#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace as1::script
{
    // Retail SCRIPT byte/source owners are raw operator-new allocations.  The
    // Win32/x86 executable does not value-initialize those bytes before fread
    // or compiler writes.  std::vector<uint8_t>::resize() normally zeros new
    // bytes, which changes malformed/short-read behavior.  This allocator
    // keeps vector storage/API but default-initializes trivial bytes on the
    // retail target, leaving the freshly allocated representation untouched.
    template <class T>
    class RetailNoInitAllocator : public std::allocator<T>
    {
    public:
        using value_type = T;

        RetailNoInitAllocator() noexcept = default;
        template <class U>
        RetailNoInitAllocator(const RetailNoInitAllocator<U>&) noexcept {}

        template <class U>
        struct rebind { using other = RetailNoInitAllocator<U>; };

        template <class U, class... Args>
        void construct(U* p, Args&&... args)
        {
#if defined(_WIN32) && defined(_M_IX86)
            if constexpr (sizeof...(Args) == 0 && std::is_trivially_default_constructible_v<U>)
                ::new (static_cast<void*>(p)) U;
            else
                ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...);
#else
            ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...);
#endif
        }
    };

    template <class T, class U>
    inline bool operator==(const RetailNoInitAllocator<T>&, const RetailNoInitAllocator<U>&) noexcept { return true; }
    template <class T, class U>
    inline bool operator!=(const RetailNoInitAllocator<T>&, const RetailNoInitAllocator<U>&) noexcept { return false; }

    using RetailByteBuffer = std::vector<std::uint8_t, RetailNoInitAllocator<std::uint8_t>>;
}
