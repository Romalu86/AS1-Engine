// Source route: recovered low-address MSVC helper block from AlienShooter.exe .text.
#include "msvc_array_helpers.h"
#include "as_string.h"
#include <cstdint>

namespace as1
{
    namespace
    {
        int invokeMsvcThiscallElementRoutine(MsvcThiscallElementRoutine routine, void* element)
        {
#if (defined(_MSC_VER) && defined(_M_IX86)) || (defined(__i386__) && (defined(__GNUC__) || defined(__clang__)))
            using Bridge = int (AS1_MSVC_HELPER_FASTCALL *)(void*, void*);
            return reinterpret_cast<Bridge>(routine)(element, nullptr);
#else
            return routine(element);
#endif
        }
    }
    int AS1_MSVC_HELPER_STDCALL msvcInvokeElementRoutineForward(void* firstElement, int elementStride, int elementCount, MsvcThiscallElementRoutine routine)
    {
        int result = elementCount - 1;
        if (result >= 0)
        {
            auto* current = static_cast<std::uint8_t*>(firstElement);
            int remaining = elementCount;
            do
            {
                result = invokeMsvcThiscallElementRoutine(routine, current);
                current += elementStride;
                --remaining;
            }
            while (remaining);
        }
        return result;
    }

    int AS1_MSVC_HELPER_STDCALL msvcInvokeElementRoutineReverse(void* firstElement, int elementStride, int elementCount, MsvcThiscallElementRoutine routine)
    {
        auto* current = static_cast<std::uint8_t*>(firstElement) + elementCount * elementStride;
        int result = elementCount - 1;
        if (result >= 0)
        {
            int remaining = elementCount;
            do
            {
                current -= elementStride;
                result = invokeMsvcThiscallElementRoutine(routine, current);
                --remaining;
            }
            while (remaining);
        }
        return result;
    }

    int AS1_MSVC_HELPER_STDCALL msvc_eh_vector_for_each_forward(void* firstElement, int elementStride, int elementCount, MsvcThiscallElementRoutine routine)
    {
        return msvcInvokeElementRoutineForward(firstElement, elementStride, elementCount, routine);
    }

    int AS1_MSVC_HELPER_STDCALL msvc_eh_vector_for_each_reverse(void* firstElement, int elementStride, int elementCount, MsvcThiscallElementRoutine routine)
    {
        return msvcInvokeElementRoutineReverse(firstElement, elementStride, elementCount, routine);
    }

    void* msvcStringRecordDeletingDestructor(void* rawThis, unsigned char flags) noexcept
    {

        auto* self = static_cast<std::uint8_t*>(rawThis);
#if defined(_WIN32) && defined(_M_IX86)
        if ((flags & 0x02u) != 0)
        {
            auto* header = reinterpret_cast<std::uint32_t*>(self) - 1;
            const std::uint32_t count = *header;
            for (std::uint32_t i = count; i != 0; --i)
            {
                auto* record = self + static_cast<std::size_t>(i - 1u) * 12u;
                char* const text = *reinterpret_cast<char**>(record + 8u);
                if (text != STRING::SharedEmptyText())
                    ::operator delete(static_cast<void*>(text));
            }
            if ((flags & 0x01u) != 0)
                ::operator delete(static_cast<void*>(header));
            return header;
        }
#endif
        char* const text = *reinterpret_cast<char**>(self + 8u);
        if (text != STRING::SharedEmptyText())
            ::operator delete(static_cast<void*>(text));
        if ((flags & 0x01u) != 0)
            ::operator delete(rawThis);
        return rawThis;
    }

    void msvc_destroy_cstring_pointer_array(char** firstSlot, int count, const char* emptySentinel)
    {
        if (!firstSlot || count <= 0)
            return;
        for (int i = count - 1; i >= 0; --i)
        {
            char* value = firstSlot[i];
            if (value && value != emptySentinel)
                ::operator delete(value);
            firstSlot[i] = const_cast<char*>(emptySentinel);
        }
    }

}
