#include "UI/CursorFocus.h"

#include <Windows.h>

#include <Global.h>
#include <Glacier/ZGraphicsSettingsManager.h>

namespace UI
{
    void RequestImGuiFocus()
    {
        if (!GraphicsSettingsManager)
        {
            return;
        }

        const HWND window = GraphicsSettingsManager->GetHWND();

        if (!window)
        {
            return;
        }

        // lParam layout for WM_KEYDOWN: repeat count in bits 0-15, scan code in
        // bits 16-23. The SDK reads only the scan code, so the virtual key we
        // pair it with is cosmetic - VK_OEM_3 is what sits there on a US layout.
        constexpr LPARAM SCAN_CODE_LEFT_OF_ONE = 0x00290001;

        PostMessageA(window, WM_KEYDOWN, VK_OEM_3, SCAN_CODE_LEFT_OF_ONE);
        PostMessageA(window, WM_KEYUP,   VK_OEM_3, SCAN_CODE_LEFT_OF_ONE | 0xC0000000);
    }
}
