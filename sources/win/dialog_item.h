#pragma once
#include <string>
#include "core/as_string.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace as1::win
{
#ifdef _WIN32
    struct DialogItemRef
    {
        HWND dialog = nullptr;
        int controlId = 0;

        // Retail sendControlMessage: the owner is exactly the pair {HWND, controlId}.
        LRESULT sendControlMessage(UINT message, WPARAM wparam, LPARAM lparam) const;
    };

#if INTPTR_MAX == INT32_MAX
#endif

    STRING& readDialogItemText(const DialogItemRef& item, STRING& out);
    std::string GetDlgItemTextString(HWND dialog, int controlId);
    void SetDlgItemVisible(HWND dialog, int controlId, bool visible);
    void SetDlgItemEnabled(HWND dialog, int controlId, bool enabled);
#endif
}
