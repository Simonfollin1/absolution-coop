#pragma once

// Diagnostics that survive the process dying.
//
// The SDK's own Logger writes to a console window and nothing else, so when the
// game crashes the console goes with it and there is nothing left to read. This
// writes to disk instead, flushing every line, and installs an exception
// handler that dumps a readable report before the game's own crash handler
// takes over.
//
// Everything here is safe to call before the engine exists.
namespace Diag
{
    // Opens mods/<modName>.log and records what we know at load time. Safe to
    // call twice; the second call is ignored.
    void Initialize(const char* modName);

    // Appends a timestamped line and flushes it immediately.
    void Log(const char* format, ...);

    // Records where we are without touching the disk, so it costs nothing to
    // call every frame. The crash report prints the last value set.
    //
    // The string must outlive the call - use literals.
    void Breadcrumb(const char* stage);

    // Tells the crash handler what the calling thread is - "game", "render",
    // "net". A report that says which loop died answers half the question
    // before anybody opens a disassembler.
    void NameCurrentThread(const char* role);

    // Installs a vectored exception handler that writes mods/<modName>-crash.log
    // on the first few access violations and then gets out of the way, so the
    // game's own minidump still happens.
    void InstallCrashHandler();

    // Removes the handler and closes the log. Call from the mod destructor.
    void Shutdown();

    // Checks that the Visual C++ runtime the process actually loaded is new
    // enough for binaries built with VS 2022 17.10 or later. It is not: a
    // std::mutex built by 14.40+ crashes on an older msvcp140, which looks like
    // a mod bug and is not one.
    //
    // Returns false when the runtime is too old, and fills reasonOut with a
    // sentence fit to show the user.
    bool CheckVisualCppRuntime(char* reasonOut, unsigned int reasonSize);

    // Empty unless the check above failed. Safe to show in a panel.
    const char* GetRuntimeWarning();

    // Scoped breadcrumb: sets a stage on entry and restores the previous one on
    // exit, so a crash report names the innermost stage that was running.
    class Stage
    {
    public:
        explicit Stage(const char* stage);
        ~Stage();

        Stage(const Stage&) = delete;
        Stage& operator=(const Stage&) = delete;

    private:
        const char* m_previous;
    };
}

// One line per scope, cheap enough for per-frame use.
#define DIAG_STAGE(name) const Diag::Stage diagStage__(name)
