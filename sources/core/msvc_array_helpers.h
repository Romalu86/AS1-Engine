#pragma once
#include "types.h"

#if defined(_MSC_VER) && defined(_M_IX86)
#define AS1_MSVC_HELPER_STDCALL __stdcall
#define AS1_MSVC_HELPER_FASTCALL __fastcall
#elif defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
#define AS1_MSVC_HELPER_STDCALL __attribute__((stdcall))
#define AS1_MSVC_HELPER_FASTCALL __attribute__((fastcall))
#else
#define AS1_MSVC_HELPER_STDCALL
#define AS1_MSVC_HELPER_FASTCALL
#endif

namespace as1
{
    #if (defined(_MSC_VER) && defined(_M_IX86)) || (defined(__i386__) && (defined(__GNUC__) || defined(__clang__)))
    // A raw address is passed by the retail helper.  MSVC does not permit
    // __thiscall on a free-function pointer type, so x86 invocation is
    // performed through an ABI-compatible __fastcall bridge in the .cpp:
    // ECX=element, EDX=dummy, no stack arguments.
    using MsvcThiscallElementRoutine = void (*)();
#else
    using MsvcThiscallElementRoutine = int (*)(void* element);
#endif

    // Canonical owner names retained for the retail 0x00401000/0x00401030 helpers.
    int AS1_MSVC_HELPER_STDCALL msvcInvokeElementRoutineForward(void* firstElement, int elementStride, int elementCount, MsvcThiscallElementRoutine routine);
    int AS1_MSVC_HELPER_STDCALL msvcInvokeElementRoutineReverse(void* firstElement, int elementStride, int elementCount, MsvcThiscallElementRoutine routine);

    int AS1_MSVC_HELPER_STDCALL msvc_eh_vector_for_each_forward(void* firstElement, int elementStride, int elementCount, MsvcThiscallElementRoutine routine);

    int AS1_MSVC_HELPER_STDCALL msvc_eh_vector_for_each_reverse(void* firstElement, int elementStride, int elementCount, MsvcThiscallElementRoutine routine);

    void* msvcStringRecordDeletingDestructor(void* rawThis, unsigned char flags) noexcept;

    // Descriptive compatibility helper retained for non-owner callers.
    void msvc_destroy_cstring_pointer_array(char** firstSlot, int count, const char* emptySentinel);
}
