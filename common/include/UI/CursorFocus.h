#pragma once

namespace UI
{
    // Asks the SDK to take mouse and keyboard focus, so a window we just opened
    // is actually clickable.
    //
    // The SDK owns that state in ImGuiRenderer::imguiHasFocus, which is private
    // with no accessor, and the only thing that flips it is a key event with
    // scan code 0x29 - the key left of "1". So we post exactly that message to
    // the game window and let the SDK's own handler do the work. Nothing is
    // bypassed; this is the same path a real key press takes.
    //
    // Worth knowing: while the SDK holds focus it calls
    // InputActionManager->SetEnabled(false), so our own hotkeys stop working.
    // That is why this can only ever be called from a hotkey - if our hotkey
    // fired, bindings were enabled, so the SDK did NOT have focus, so posting
    // the toggle can only turn it on. Never call it from anywhere else, or it
    // will happily toggle focus back off.
    void RequestImGuiFocus();
}
