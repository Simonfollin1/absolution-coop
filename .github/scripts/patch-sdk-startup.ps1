# Patches the SDK's renderer startup, applied to the checked-out SDK before it
# is built. Two changes, both from reading the crash dumps of a game that needed
# four or five launch attempts before one stuck.
#
# Every failed launch faulted in the same place: a null read inside msvcp140,
# called from DirectXTK, called from the SDK's renderer setup, underneath d3d11
# and the display driver. No mod code anywhere on the stack.
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
# SDK.cpp: do not enable the render hooks while the mods are still loading.
#
# The constructor enables the Present hook as its last act, and only then does
# SDK::Setup() run LoadAllMods, which LoadLibrarys the mod DLLs. So the render
# thread can be building the overlay's DirectXTK objects for the first time
# while the main thread is inside the loader. Moving the two render hooks to
# after LoadAllMods closes that window.
#
# Only those two move. ZHitman5Module::Initialize and ZEngineAppCommon::Initialize
# can fire during LoadAllMods, and a mod that missed OnEngineInitialized would be
# a worse bug than the one being fixed.
# ---------------------------------------------------------------------------

$sdkFrom = @(
@"
    Hooks::ZRenderDevice_PresentHook.EnableHook();
    Hooks::ZRenderSwapChain_ResizeHook.EnableHook();
    Hooks::ZApplicationEngineWin32_MainWindowProc.EnableHook();
"@,
@"
void SDK::Setup()
{
    modManager->LoadAllMods();
"@
)

$sdkTo = @(
@"
    Hooks::ZApplicationEngineWin32_MainWindowProc.EnableHook();
"@,
@"
void SDK::Setup()
{
    modManager->LoadAllMods();

    // Moved here from the constructor. Enabled there, the render thread can be
    // inside the overlay's first DirectXTK setup at the same moment this thread
    // is inside LoadLibrary for the mod DLLs, holding the loader lock. Nothing
    // needs the overlay during those few hundred milliseconds.
    Hooks::ZRenderDevice_PresentHook.EnableHook();
    Hooks::ZRenderSwapChain_ResizeHook.EnableHook();
"@
)

Edit-Source -Path "$SdkRoot/HitmanAbsolutionSDK/src/SDK.cpp" -From $sdkFrom -To $sdkTo

# ---------------------------------------------------------------------------
# DirectXRenderer.cpp: two smaller defects, both found while reading the above.
#
# Cleanup() releases inputLayout unconditionally, and Setup() returns false
# without ever assigning it when CreateInputLayout fails - and that failure path
# is precisely the one that calls Cleanup(). A recoverable setup failure ends as
# a null dereference.
#
# DirectXTK's constructors also throw: SpriteFont on a font file it cannot read,
# the effects on a failed device call. Setup() runs inside the game's Present,
# underneath the display driver, and a C++ exception has nothing to unwind
# through there. That is not the crash the dumps show - those are access
# violations, which catch(...) does not see - but it is a real way to lose the
# process and it costs a few lines to close.
#
# This file is indented with tabs; the replacements match.
# ---------------------------------------------------------------------------

$rendererFrom = @(
@"
void DirectXRenderer::OnPresent(ZRenderDevice* renderDevice)
{
`tif (!Setup())
"@,
"`tinputLayout->Release();"
)

$rendererTo = @(
@"
void DirectXRenderer::OnPresent(ZRenderDevice* renderDevice)
{
`tbool setupSucceeded = false;

`ttry
`t{
`t`tsetupSucceeded = Setup();
`t}
`tcatch (...)
`t{
`t`tsetupSucceeded = false;
`t}

`tif (!setupSucceeded)
"@,
@"
`tif (inputLayout)
`t{
`t`tinputLayout->Release();

`t`tinputLayout = nullptr;
`t}
"@
)

Edit-Source -Path "$SdkRoot/HitmanAbsolutionSDK/src/Renderer/DirectXRenderer.cpp" -From $rendererFrom -To $rendererTo

# ---------------------------------------------------------------------------
# DirectInputProxy.cpp: stop calling LoadLibrary from DllMain.
#
# This is the one that makes the game need four or five launch attempts.
#
# The proxy's DllMain calls LoadLibrary three times: for the real dinput8, for
# HitmanAbsolutionSDK, and - through the SDK's own DllMain, which starts a
# thread that runs LoadAllMods - for every mod DLL. Calling LoadLibrary from
# DllMain is documented as forbidden: the loader lock is held for the duration
# of DllMain and LoadLibrary wants the same lock. Most of the time the loader
# untangles it. The rest of the time the game never finishes starting.
#
# The confirming detail came from the person hitting it: when the SDK's console
# window appears the game works every time, and the problem is that the window
# does not always appear. AllocConsole is in the SDK's Logger, so the console is
# the first visible sign that the SDK got through initialisation - exactly what
# you would expect if the failure is the load itself rather than anything later.
#
# The fix is the standard one for a proxy DLL. A thread created from DllMain
# cannot start until the loader lock is released, so it can load safely. The
# same work is also attempted from DirectInput8Create, which the game calls
# during its own start-up, well outside the lock. Whichever arrives first does
# it; InitOnceExecuteOnce makes the other wait rather than repeat it.
#
# DLL_PROCESS_DETACH gets a smaller correction on the way past: FreeLibrary is
# on the same forbidden list, and lpReserved being non-null means the process is
# terminating, where unloading is both pointless and a way to crash on exit.
#
# And one thing the loader never did at all: look at what it is loading into.
#
# The SDK this is built against calls and hooks the engine at fixed addresses,
# and those addresses are the Steam executable's. The GOG release is the same
# game version with a different layout, so there the SDK's first engine call -
# its memory manager, RVA 0x389F10 - lands in the middle of something else.
# The report that found it: access violation in HMA.exe at offset 0x389F1E,
# before the launcher, with every mod disabled, and no mod log because no mod
# ever ran. Nothing a player can do about that except remove the loader.
#
# So the loader checks the executable's SizeOfImage - the same discriminator
# common/src/Memory/GameOffsets.cpp uses - and loads the SDK only into the
# build it was written for. Anywhere else it loads the real dinput8 and nothing
# more, so the game runs exactly as it would unmodded, and says why in a
# message box. An allowlist rather than "not GOG": on any layout but the one
# the addresses came from, the SDK is a crash, whatever that layout is called.
#
# The here-strings below are double-quoted, so PowerShell expands dollar signs
# and backticks in them. The C++ contains neither; keep it that way.
# ---------------------------------------------------------------------------

$proxyFrom = @(
@"
static DirectInput8Create_t originalDirectInput8Create = nullptr;
"@,
@"
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
"@,
@"
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        char dinput8Path[MAX_PATH] = { 0 };

        GetSystemDirectoryA(dinput8Path, MAX_PATH);
        strcat_s(dinput8Path, MAX_PATH, "\\dinput8.dll");

        originalDirectInput = LoadLibraryA(dinput8Path);

        if (!originalDirectInput)
        {
            return false;
        }

        originalDirectInput8Create = reinterpret_cast<DirectInput8Create_t>(GetProcAddress(originalDirectInput, "DirectInput8Create"));

        if (!originalDirectInput8Create)
        {
            return false;
        }

        sdk = LoadLibraryA("HitmanAbsolutionSDK");

        if (!sdk)
        {
            return false;
        }
    }
    else if (fdwReason == DLL_PROCESS_DETACH)
    {
"@,
@"
    if (originalDirectInput8Create)
    {
        return originalDirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
    }
"@
)

$proxyTo = @(
@"
static DirectInput8Create_t originalDirectInput8Create = nullptr;
static INIT_ONCE loadOnce = INIT_ONCE_STATIC_INIT;

// SizeOfImage from the PE header of the two releases of 1.0.447.0. The SDK's
// engine addresses are the Steam executable's, and only that one is loaded
// into. The GOG value is here so the message can name it.
static const DWORD STEAM_IMAGE_SIZE = 36777984;
static const DWORD GOG_IMAGE_SIZE = 35799040;

static char unsupportedMessage[768] = { 0 };
"@,
@"
// SizeOfImage of the executable this DLL was loaded into, read from the
// headers the loader has already mapped. Zero if they do not look like a PE.
static DWORD GetGameImageSize()
{
    const BYTE* base = reinterpret_cast<const BYTE*>(GetModuleHandleA(nullptr));

    if (!base)
    {
        return 0;
    }

    const IMAGE_DOS_HEADER* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);

    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE || dosHeader->e_lfanew <= 0)
    {
        return 0;
    }

    const IMAGE_NT_HEADERS* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dosHeader->e_lfanew);

    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
    {
        return 0;
    }

    return ntHeaders->OptionalHeader.SizeOfImage;
}

// On its own thread, so the game is never held up waiting for somebody to
// press OK: DirectInput8Create below waits for LoadEverything, not for this.
static DWORD WINAPI UnsupportedBuildThread(LPVOID)
{
    MessageBoxA(nullptr, unsupportedMessage, "Hitman Absolution SDK loader", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);

    return 0;
}

static void ReportUnsupportedBuild(const DWORD imageSize)
{
    char build[96] = { 0 };

    if (imageSize == GOG_IMAGE_SIZE)
    {
        strcpy_s(build, sizeof(build), "the GOG release");
    }
    else
    {
        wsprintfA(build, "a build the loader does not recognise (image size %lu)", imageSize);
    }

    wsprintfA(unsupportedMessage,
        "The mods in this folder were not loaded.\n\n"
        "They are built for the Steam release of Hitman: Absolution 1.0.447.0, "
        "and this HMA.exe is %s. Loading them into any other build crashes the "
        "game while it starts, so the loader has left the game untouched.\n\n"
        "The game will run normally, without mods. To stop seeing this message, "
        "remove dinput8.dll from the folder HMA.exe is in.",
        build);

    HANDLE thread = CreateThread(nullptr, 0, UnsupportedBuildThread, nullptr, 0, nullptr);

    if (thread)
    {
        CloseHandle(thread);
    }
}

// Loads the real dinput8 and, into the one build it is written for, the SDK.
// Runs exactly once, on whichever of the two callers below reaches it first -
// never from inside DllMain.
static BOOL CALLBACK LoadEverything(PINIT_ONCE, PVOID, PVOID*)
{
    char dinput8Path[MAX_PATH] = { 0 };

    GetSystemDirectoryA(dinput8Path, MAX_PATH);
    strcat_s(dinput8Path, MAX_PATH, "\\dinput8.dll");

    originalDirectInput = LoadLibraryA(dinput8Path);

    if (originalDirectInput)
    {
        originalDirectInput8Create = reinterpret_cast<DirectInput8Create_t>(GetProcAddress(originalDirectInput, "DirectInput8Create"));
    }

    // The real dinput8 is loaded whatever happens next: the game needs it,
    // modded or not. The SDK goes only where its addresses are true.
    const DWORD imageSize = GetGameImageSize();

    if (imageSize == STEAM_IMAGE_SIZE)
    {
        sdk = LoadLibraryA("HitmanAbsolutionSDK");
    }
    else
    {
        ReportUnsupportedBuild(imageSize);
    }

    return TRUE;
}

// A thread created from DllMain cannot begin running until the loader lock is
// released, which is what makes loading from here safe.
static DWORD WINAPI LoaderThread(LPVOID)
{
    InitOnceExecuteOnce(&loadOnce, LoadEverything, nullptr, nullptr);

    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
"@,
@"
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        // Deliberately nothing but this. Every LoadLibrary that used to be here
        // ran while the loader lock was held, which is what made the game start
        // roughly one time in four.
        HANDLE loader = CreateThread(nullptr, 0, LoaderThread, nullptr, 0, nullptr);

        if (loader)
        {
            CloseHandle(loader);
        }
    }
    else if (fdwReason == DLL_PROCESS_DETACH && !lpReserved)
    {
"@,
@"
    // The game calls this during its own start-up, outside the loader lock. If
    // the loader thread has already been through, this returns immediately.
    InitOnceExecuteOnce(&loadOnce, LoadEverything, nullptr, nullptr);

    if (originalDirectInput8Create)
    {
        return originalDirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
    }
"@
)

Edit-Source -Path "$SdkRoot/DirectInputProxy/src/DirectInputProxy.cpp" -From $proxyFrom -To $proxyTo
