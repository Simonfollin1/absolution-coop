# Patches how the SDK's own interface looks, applied to the checked-out SDK
# before it is built. Two changes, neither of which touches what anything does.
#
# The mod loader draws a menu bar across the top of the screen whenever the
# panel key is held down. Out of the box it opens with the words "Hitman
# Absolution SDK" in bold, then a MODS button and a Settings button, and only
# after those come the mods themselves. Anyone who sees the screen sees the
# name of a developer SDK before they see the mod they installed.
#
# Each replacement asserts the source still looks the way it did when this was
# written, and fails the build rather than silently doing nothing if the SDK
# changes underneath us.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $SdkRoot
)

$ErrorActionPreference = 'Stop'

function Edit-Source
{
    param(
        [string]   $Path,
        [string[]] $From,
        [string[]] $To
    )

    if (-not (Test-Path $Path))
    {
        throw "Cannot patch $Path - it is not there"
    }

    $text = (Get-Content $Path -Raw) -replace "`r`n", "`n"

    for ($i = 0; $i -lt $From.Length; $i++)
    {
        $needle = $From[$i] -replace "`r`n", "`n"

        if (-not $text.Contains($needle))
        {
            throw "$Path no longer looks the way this patch expects - check it before shipping"
        }

        $text = $text.Replace($needle, ($To[$i] -replace "`r`n", "`n"))
    }

    Set-Content $Path $text -NoNewline

    Write-Host "patched $Path"
}

# ---------------------------------------------------------------------------
# MainMenu.cpp: the menu bar carries the mods and nothing else.
#
# The SDK's own three items come out - the bold "Hitman Absolution SDK", the
# MODS button and the Settings button - and OnDrawMenu stays, because that call
# is what draws each loaded mod's own button. Removing the whole Draw would have
# taken the mods with it.
#
# What this costs: Show() on the mod selector and the settings window is called
# from nowhere else in the SDK, so both become unreachable. Neither is needed to
# play. Every mod in the archive is named in mods.ini and loads on its own, and
# the SDK settings this build ships are written by the workflow rather than
# chosen in game. The panels are left compiled in rather than cut out, so this
# stays a patch about presentation.
# ---------------------------------------------------------------------------

$menuFrom = @(
@"
    ImGui::PushFont(SDK::GetInstance().GetBoldFont());
    ImGui::Text("Hitman Absolution SDK");
    ImGui::PopFont();

    if (ImGui::Button(ICON_MD_TOKEN " MODS"))
    {
        SDK::GetInstance().GetModSelector()->Show();
    }

    if (ImGui::Button(ICON_MD_SETTINGS " Settings"))
    {
        SDK::GetInstance().GetSettings()->Show();
    }

    SDK::GetInstance().OnDrawMenu();
"@
)

$menuTo = @(
@"
    // The SDK's own heading and its MODS and Settings buttons used to be here.
    // The bar now opens with the mods themselves.
    SDK::GetInstance().OnDrawMenu();
"@
)

Edit-Source -Path "$SdkRoot/HitmanAbsolutionSDK/src/UI/MainMenu.cpp" -From $menuFrom -To $menuTo

# ---------------------------------------------------------------------------
# ImGuiRenderer.cpp: neutral greys instead of the SDK's magenta.
#
# Title bars, buttons, tick marks and tabs are all a saturated magenta, which
# is a strong colour to sit on top of somebody's game and reads as a tool
# rather than as part of the interface. These are the same greys the panel
# backgrounds already use, one and two steps lighter.
#
# The block is appended to the end of SetStyle rather than edited into the
# assignments above it. Those run first and are then overwritten, so this patch
# does not have to match twenty individual lines of somebody else's colour
# scheme, and it keeps working if the SDK restyles something we do not name.
#
# The tab colours are set explicitly for a reason: the SDK derives them with
# ImLerp from Header and TitleBgActive at the point they are assigned, so
# changing those two afterwards would leave the tabs the only magenta left.
# ---------------------------------------------------------------------------

$styleFrom = @(
@"
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
}
"@
)

$styleTo = @(
@"
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);

    // ---- Neutral greys, applied over everything set above ----
    const ImVec4 sunken  = ImVec4(0.110f, 0.110f, 0.114f, 1.f);
    const ImVec4 recess  = ImVec4(0.176f, 0.176f, 0.184f, 1.f);
    const ImVec4 raised  = ImVec4(0.220f, 0.220f, 0.231f, 1.f);
    const ImVec4 hovered = ImVec4(0.298f, 0.298f, 0.310f, 1.f);
    const ImVec4 pressed = ImVec4(0.361f, 0.361f, 0.373f, 1.f);
    const ImVec4 bright  = ImVec4(0.850f, 0.850f, 0.859f, 1.f);

    colors[ImGuiCol_TitleBg] = recess;
    colors[ImGuiCol_TitleBgActive] = raised;
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.176f, 0.176f, 0.184f, 0.60f);

    colors[ImGuiCol_Button] = raised;
    colors[ImGuiCol_ButtonHovered] = hovered;
    colors[ImGuiCol_ButtonActive] = pressed;

    colors[ImGuiCol_Header] = raised;
    colors[ImGuiCol_HeaderHovered] = hovered;
    colors[ImGuiCol_HeaderActive] = pressed;

    // Light, because these are marks read against a dark frame rather than
    // surfaces. Grey here would be a tick you cannot see.
    colors[ImGuiCol_CheckMark] = bright;
    colors[ImGuiCol_SliderGrab] = ImVec4(0.650f, 0.650f, 0.659f, 1.f);
    colors[ImGuiCol_SliderGrabActive] = bright;

    colors[ImGuiCol_ScrollbarBg] = sunken;
    colors[ImGuiCol_ScrollbarGrab] = hovered;
    colors[ImGuiCol_ScrollbarGrabHovered] = pressed;
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.439f, 0.439f, 0.451f, 1.f);

    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.450f, 0.450f, 0.459f, 1.f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.600f, 0.600f, 0.612f, 1.f);
    colors[ImGuiCol_ResizeGripHovered] = hovered;
    colors[ImGuiCol_ResizeGripActive] = pressed;

    colors[ImGuiCol_Tab] = recess;
    colors[ImGuiCol_TabHovered] = hovered;
    colors[ImGuiCol_TabActive] = raised;
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.157f, 0.157f, 0.165f, 1.f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.196f, 0.196f, 0.204f, 1.f);

    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.200f, 0.200f, 0.208f, 1.f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.300f, 0.300f, 0.310f, 1.f);

    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.600f, 0.600f, 0.620f, 0.35f);
    colors[ImGuiCol_NavHighlight] = hovered;
    colors[ImGuiCol_DockingPreview] = ImVec4(0.298f, 0.298f, 0.310f, 0.70f);
}
"@
)

Edit-Source -Path "$SdkRoot/HitmanAbsolutionSDK/src/Renderer/ImGuiRenderer.cpp" -From $styleFrom -To $styleTo
