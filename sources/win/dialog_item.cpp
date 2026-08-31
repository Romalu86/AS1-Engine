#include "win/dialog_item.h"

#ifdef _WIN32
namespace as1::win
{
    LRESULT DialogItemRef::sendControlMessage(UINT message, WPARAM wparam, LPARAM lparam) const
    {
        return ::SendDlgItemMessageA(dialog, controlId, message, wparam, lparam);
    }

    STRING& readDialogItemText(const DialogItemRef& item, STRING& out)
    {
        char buffer[0x200];
        ::GetDlgItemTextA(item.dialog, item.controlId, buffer, 0x200);
        out.AssignAllocatedCopyWithoutRelease(buffer);
        return out;
    }

    std::string GetDlgItemTextString(HWND dialog, int controlId)
    {
        STRING out;
        readDialogItemText(DialogItemRef{dialog, controlId}, out);
        return std::string(out.c_str());
    }

    void SetDlgItemVisible(HWND dialog, int controlId, bool visible)
    {
        HWND item = GetDlgItem(dialog, controlId);
        ShowWindow(item, visible ? SW_SHOW : SW_HIDE);
    }

    void SetDlgItemEnabled(HWND dialog, int controlId, bool enabled)
    {
        HWND item = GetDlgItem(dialog, controlId);
        EnableWindow(item, enabled ? TRUE : FALSE);
    }
}
#endif
