#include "win/dialog_item.h"

namespace as1 { namespace win
{
    STRING& readDialogItemText(const DialogItemRef& item, STRING& out)
    {
        char buffer[0x200];
        ::GetDlgItemTextA(item.dialog, item.controlId, buffer, 0x200);
        ::new (static_cast<void*>(&out)) as1::STRING(buffer);
        return out;
    }
} }
