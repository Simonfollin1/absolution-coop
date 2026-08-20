#include <imgui.h>

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <format>
#include <iterator>
#include <string>
#include <vector>

#include <Glacier/ZGraphicsSettingsManager.h>
#include <Glacier/UI/ZHUDManager.h>
#include <Glacier/ZLevelManager.h>
#include <Glacier/Player/ZHitman5.h>
#include <Glacier/ZGameLoopManager.h>
#include <Glacier/Input/ZInputActionManager.h>
#include <Glacier/Math/SVector3.h>
#include <SDK.h>
#include <Global.h>

#include "CoopMod.h"
#include "Game/BuildInfo.h"
#include "Game/DebugDump.h"
#include "Game/HitInspector.h"
#include "Game/ScaleformProbe.h"
#include "Memory/GameOffsets.h"
#include "Game/ModPresence.h"
#include "UI/CursorFocus.h"
#include "Diag/Diag.h"

using namespace Coop;

namespace
{
    constexpr size_t kLogCapacity = 200;

    // Every action the mod binds, with the key it takes when the ini does not
    // say otherwise, and whether it wants the key held or tapped.
    //
    // Held is the whole reason this table exists rather than a call to
    // ModInterface::AddBindings: that one compiles everything to tap(kb,key),
    // and a spectator camera driven by taps moves in single steps no matter
    // how long you lean on the key.
    struct BindingSpec
    {
        const char* action;
        const char* defaultKey;
        bool        held;
    };

    constexpr BindingSpec kBindingSpecs[] = {
        { "CoopToggle",          "f6",    false },
        { "CoopFollow",          "f7",    false },
        { "CoopMarker",          "f8",    false },
        { "CoopSpectateLeft",    "left",  true  },
        { "CoopSpectateRight",   "right", true  },
        { "CoopSpectateUp",      "up",    true  },
        { "CoopSpectateDown",    "down",  true  },
        { "CoopSpectateCloser",  "pgup",  true  },
        { "CoopSpectateFurther", "pgdn",  true  },
    };

    std::string Trim(const std::string& text)
    {
        const size_t first = text.find_first_not_of(" \t\r\n");

        if (first == std::string::npos)
        {
            return {};
        }

        return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
    }

    std::vector<std::string> SplitOn(const std::string& text, char separator)
    {
        std::vector<std::string> parts;
        size_t                   start = 0;

        while (true)
        {
            const size_t at    = text.find(separator, start);
            std::string  piece = Trim(text.substr(start, at == std::string::npos
                                                        ? std::string::npos
                                                        : at - start));

            if (!piece.empty())
            {
                parts.push_back(std::move(piece));
            }

            if (at == std::string::npos)
            {
                break;
            }

            start = at + 1;
        }

        return parts;
    }

    // One "lctrl+f6" style hotkey, as the engine spells it. Modifiers are held
    // and the last key carries the tap or hold, matching the shape the SDK
    // generates for its own mods.
    std::string HotkeyExpression(const std::string& hotkey, bool held)
    {
        const std::vector<std::string> parts = SplitOn(hotkey, '+');

        if (parts.empty())
        {
            return {};
        }

        const char* verb = held ? "hold" : "tap";
        std::string primary = std::format("{}(kb,{})", verb, parts.back());

        if (parts.size() == 1)
        {
            return primary;
        }

        std::string modifiers;

        for (size_t i = 0; i + 1 < parts.size(); ++i)
        {
            modifiers += std::format(" hold(kb,{})", parts[i]);
        }

        // "& <mods> <key>" for one modifier; more than one has to be an
        // explicit conjunction of holds first.
        return parts.size() == 2
            ? std::format("&{} {}", modifiers, primary)
            : std::format("& |{} {}", modifiers, primary);
    }

    // A whole binding value, which may list alternatives separated by commas.
    std::string BindingExpression(const std::string& action, const std::string& keys, bool held)
    {
        std::vector<std::string> alternatives;

        for (const std::string& hotkey : SplitOn(keys, ','))
        {
            std::string expression = HotkeyExpression(hotkey, held);

            if (!expression.empty())
            {
                alternatives.push_back(std::move(expression));
            }
        }

        if (alternatives.empty())
        {
            return {};
        }

        std::string combined = alternatives.front();

        for (size_t i = 1; i < alternatives.size(); ++i)
        {
            combined = std::format("| {} {}", combined, alternatives[i]);
        }

        return std::format("{}={};", action, combined);
    }

    ImVec4 StatusColour(Net::SessionMode mode)
    {
        switch (mode)
        {
        case Net::SessionMode::Hosting:
        case Net::SessionMode::Connected:
            return ImVec4(0.45f, 0.85f, 0.45f, 1.f);
        case Net::SessionMode::Connecting:
            return ImVec4(1.f, 0.80f, 0.35f, 1.f);
        case Net::SessionMode::Failed:
            return ImVec4(1.f, 0.45f, 0.45f, 1.f);
        default:
            return ImVec4(0.65f, 0.65f, 0.65f, 1.f);
        }
    }

    // Hex and floats side by side, sixteen bytes to a line. Both, because
    // which one is meaningful is the question being asked.
    void LogBytes(const char* indent, const uint8_t* bytes, size_t count)
    {
        for (size_t offset = 0; offset + 16 <= count; offset += 16)
        {
            const uint8_t* row = bytes + offset;

            float asFloat[4] = {};
            std::memcpy(asFloat, row, sizeof(asFloat));

            Diag::Log("%s+%02zX  %02X%02X%02X%02X %02X%02X%02X%02X "
                      "%02X%02X%02X%02X %02X%02X%02X%02X   "
                      "%12.4f %12.4f %12.4f %12.4f",
                      indent, offset,
                      row[3],  row[2],  row[1],  row[0],
                      row[7],  row[6],  row[5],  row[4],
                      row[11], row[10], row[9],  row[8],
                      row[15], row[14], row[13], row[12],
                      asFloat[0], asFloat[1], asFloat[2], asFloat[3]);
        }
    }

    // The SDK shows the OS cursor exactly when it has taken input for its own
    // panels. That makes cursor visibility the honest test for "the player is
    // clicking menus rather than looking around", and it needs no offsets -
    // unlike reading ImGuiRenderer::imguiHasFocus, which is private.
    bool CursorIsVisible()
    {
        CURSORINFO info{};
        info.cbSize = sizeof(info);

        if (!GetCursorInfo(&info))
        {
            return false;
        }

        return (info.flags & CURSOR_SHOWING) != 0;
    }

    // Centre of the game window, in screen coordinates.
    bool WindowCentre(POINT& centreOut)
    {
        if (!GraphicsSettingsManager)
        {
            return false;
        }

        const HWND window = GraphicsSettingsManager->GetHWND();

        if (!window)
        {
            return false;
        }

        RECT client{};

        if (!GetClientRect(window, &client))
        {
            return false;
        }

        centreOut.x = (client.right - client.left) / 2;
        centreOut.y = (client.bottom - client.top) / 2;

        return ClientToScreen(window, &centreOut) != 0;
    }

    const char* StatusText(Net::SessionMode mode)
    {
        switch (mode)
        {
        case Net::SessionMode::Hosting:    return "Hosting";
        case Net::SessionMode::Connected:  return "Connected";
        case Net::SessionMode::Connecting: return "Connecting";
        case Net::SessionMode::Failed:     return "Failed";
        default:                           return "Offline";
        }
    }
}

namespace
{
    // The phases the game thread moves through in a frame, in order. A stall
    // report reads one of these back, so they name the work rather than a line
    // number - the log has to be readable by whoever is holding the frozen game.
    const char* const kFramePhases[] = {
        "the game's own work, between the mod's frames",
        "probe update",
        "jump state",
        "downed flow",
        "markers and keys",
        "instinct",
        "engine health",
        "reading the scene name",
        "announcing the scene",
        "starting a jump",
        "committing a jump",
        "reading what changed about the player",
        "publishing local state",
        "frame end",
    };
}

float CoopMod::LoadPatienceSeconds() const
{
    // Three times the slowest load this machine has actually done, with a floor
    // for the case where nothing has been measured yet. Three, because a load
    // that is going to arrive arrives in about the time the last one took, and
    // anything past three times that is not slow - it is not coming.
    const float slowest = m_longestStallMs.load(std::memory_order_relaxed) / 1000.f;

    return std::max(60.f, slowest * 3.f);
}

void CoopMod::WatchdogMain()
{
    Diag::NameCurrentThread("watchdog");

    uint64_t lastCount   = 0;
    uint64_t lastMovedAt = GetTickCount64();
    uint64_t reportedAt   = 0;
    uint64_t stallBeganAt = lastMovedAt;
    bool     reported     = false;

    while (m_watchdogRunning.load(std::memory_order_relaxed))
    {
        Sleep(250);

        const uint64_t count = m_frameCount.load(std::memory_order_relaxed);
        const uint64_t now   = GetTickCount64();

        if (count != lastCount)
        {
            lastCount   = count;
            lastMovedAt = now;

            if (reported)
            {
                reported = false;

                // A stall that ended was the machine working, not the machine
                // broken - almost always a level load, during which this game
                // completes no frames at all. Its length is the best answer
                // anyone has to "how long does a load take here", so it sets
                // the timings rather than a number written in advance.
                const uint64_t stalled = now - stallBeganAt;

                if (stalled > m_longestStallMs.load(std::memory_order_relaxed))
                {
                    m_longestStallMs.store(static_cast<uint32_t>(stalled),
                                           std::memory_order_relaxed);

                    m_stallThresholdMs.store(
                        static_cast<uint32_t>(std::max<uint64_t>(15000, stalled / 2)),
                        std::memory_order_relaxed);

                    Diag::Log("timing: this machine went %llu ms without a frame and "
                              "came back - taking that as how long a load takes here, "
                              "so the mod will now wait up to %.0f s for one",
                              static_cast<unsigned long long>(stalled),
                              LoadPatienceSeconds());
                }
                else
                {
                    Diag::Log("watchdog: the game thread is running again after %llu ms",
                              static_cast<unsigned long long>(stalled));
                }
            }

            stallBeganAt = now;

            continue;
        }

        // What counts as a stall is not a constant - it is a property of the
        // machine. The game thread goes quiet in long stretches while it waits
        // on a mechanical drive, and a fixed five seconds cried wolf on every
        // one of them. The threshold is set from the slowest load actually
        // measured here (see LoadPatienceSeconds), so it tightens on a fast
        // machine and stretches on a slow one. The engine's own loading state
        // is reported next to it, because a disk wait and a hang look identical
        // from out here and only the engine knows which is which.
        if (!reported && now - lastMovedAt >= m_stallThresholdMs.load(std::memory_order_relaxed))
        {
            reported = true;

            const uint32_t phase = m_framePhase.load(std::memory_order_relaxed);

            const char* const name =
                phase < std::size(kFramePhases) ? kFramePhases[phase] : "unknown";

            const bool loading = m_engineLoading.load(std::memory_order_relaxed);

            Diag::Log("watchdog: the game thread has not finished a frame for %llu ms "
                      "- last seen in \"%s\" (phase %u). The engine %s when it went "
                      "quiet, so this is %s.",
                      static_cast<unsigned long long>(now - lastMovedAt), name, phase,
                      loading ? "was loading" : "was NOT loading",
                      loading ? "most likely the disk rather than a hang - leave it "
                                "running and it should come back"
                              : "a real stall");

            reportedAt = now;
        }
        else if (reported && now - reportedAt >= 30000)
        {
            // Keep saying so, every thirty seconds. A single line and then
            // silence cannot be told apart from the process dying, and the one
            // question that matters about a freeze is whether it ever ends -
            // which only a log that keeps counting can answer.
            reportedAt = now;

            Diag::Log("watchdog: still no frame after %llu ms - the game is alive "
                      "enough to run this thread, so it is stuck rather than dead",
                      static_cast<unsigned long long>(now - lastMovedAt));
        }
    }
}

CoopMod::CoopMod() = default;

CoopMod::~CoopMod()
{
    // Before Diag::Shutdown below, because it logs.
    m_watchdogRunning.store(false);

    if (m_watchdog.joinable())
    {
        m_watchdog.join();
    }

    // Order matters on the way out. The session's thread has to stop before
    // anything it touches goes away, and the vtable patch has to be undone
    // before this DLL unloads - a patched entry pointing into unmapped memory
    // is a crash with no stack to explain it.
    m_session.Leave();

    // Before the thread this is on goes away, and before the process can exit
    // with a hole still open in the router.
    m_portMapper.Release();

    m_spectator.Leave();

    // Before anything else: leaving the guards blind and deaf after the mod has
    // gone would be a bug with no possible explanation from inside the game.
    m_perception.Restore();

    Game::TheDownedState().Disarm();

    const ZMemberDelegate<CoopMod, void(const SGameUpdateEvent&)>
        delegate(this, &CoopMod::OnFrameUpdate);

    if (GameLoopManager)
    {
        GameLoopManager->UnregisterForFrameUpdate(delegate);
    }

    Diag::Log("unloaded");
    Diag::Shutdown();
}

void CoopMod::Initialize()
{
    // Before anything else, so a crash during startup still leaves a report.
    // The SDK's own logger writes to a console window that dies with the game;
    // this writes mods/Coop.log and flushes every line.
    Diag::Initialize("Coop");
    Diag::InstallCrashHandler();
    Diag::Log("Initialize");

    // Not ModInterface::Initialize(): its only act is an MH_Initialize whose
    // already-initialised result it reports as an ERROR, once per mod, every
    // launch. Nothing here hooks anything - the damage interception is a vtable
    // write on one instance - so there is nothing to bring up in the first
    // place, and no reason to make the console cry wolf.
}

void CoopMod::OnEngineInitialized()
{
    ModInterface::OnEngineInitialized();

    const ZMemberDelegate<CoopMod, void(const SGameUpdateEvent&)>
        delegate(this, &CoopMod::OnFrameUpdate);

    GameLoopManager->RegisterForFrameUpdate(delegate, 1);

    // Started once the frame update is registered, so it only ever watches a
    // loop that is actually meant to be running. Guarded because assigning over
    // a joinable std::thread calls std::terminate, and this runs again on every
    // engine re-initialisation.
    if (!m_watchdog.joinable())
    {
        m_watchdogRunning.store(true);
        m_watchdog = std::thread(&CoopMod::WatchdogMain, this);
    }

    // Bindings come from mods/Coop.ini's [Bindings] section.
    InstallBindings();

    m_toggleAction = ZInputAction("CoopToggle");
    m_followAction = ZInputAction("CoopFollow");
    m_markerAction = ZInputAction("CoopMarker");

    m_specLeft    = ZInputAction("CoopSpectateLeft");
    m_specRight   = ZInputAction("CoopSpectateRight");
    m_specUp      = ZInputAction("CoopSpectateUp");
    m_specDown    = ZInputAction("CoopSpectateDown");
    m_specCloser  = ZInputAction("CoopSpectateCloser");
    m_specFurther = ZInputAction("CoopSpectateFurther");

    m_session.SetBuildFingerprint(Game::BuildInfo::Get().Fingerprint());

    m_loaded.Initialize("Co-op", 0);
    m_cost.Configure("coop");

    AddLogLine(Game::BuildInfo::Get().Describe());

    Diag::Log("engine up: %s, detected as %s, offsets %s",
              Game::BuildInfo::Get().Describe().c_str(),
              GameOffsets::GetBuildName(),
              Game::BuildInfo::Get().OffsetsUsable() ? "usable" : "NOT usable");
    Diag::Log("bindings: accepted=%d  %s",
              m_bindingsAccepted ? 1 : 0, m_bindingExpression.c_str());

    // Both mods drive the render destination, and whichever writes last wins.
    // Nothing here can arbitrate that, so it is said once rather than left for
    // somebody to discover while lying on the floor.
    if (ModPresence::IsLoaded(ModPresence::CINEMATIC_CAMERA))
    {
        AddLogLine("Cinematic Camera is loaded. Both it and the spectator camera "
                   "take over the view, so being down while its free camera is on "
                   "will fight. Turn one of them off.");
    }

    if (!Game::BuildInfo::Get().OffsetsUsable())
    {
        AddLogLine("Chapter tracking is off on this build, so peers cannot tell "
                   "whether they are in the same level. Everything else works.");
    }
}

void CoopMod::InstallBindings()
{
    // mINI's operator[] inserts, so the ini having no [Bindings] at all still
    // reads as an empty section rather than as an error - which is what we
    // want: the defaults below then apply on their own.
    auto& section = iniStructure["Bindings"];

    if (section.has("EnableBindings") && Trim(section.get("EnableBindings")) == "false")
    {
        m_bindingsEnabled = false;

        AddLogLine("Keys are off (EnableBindings = false in mods\\Coop.ini). "
                   "Open the panel from the SDK menu instead.");

        return;
    }

    // LoadConfiguration only sets modName when it found the ini, so an empty
    // one means mods\Coop.ini is not installed. The defaults above still bind;
    // saying so is what turns "my keys do nothing" into something fixable.
    if (modName.empty())
    {
        AddLogLine("mods\\Coop.ini was not found, so the built-in keys apply: "
                   "F6 panel, F7 get up or follow, F8 marker.");
    }

    std::string expression = (modName.empty() ? std::string("Coop") : modName) + "Input={";

    for (const BindingSpec& spec : kBindingSpecs)
    {
        std::string keys = section.has(spec.action) ? Trim(section.get(spec.action)) : std::string();

        if (keys.empty())
        {
            keys = spec.defaultKey;
        }

        expression += BindingExpression(spec.action, keys, spec.held);

        std::string label = keys;
        std::transform(label.begin(), label.end(), label.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

        if      (std::strcmp(spec.action, "CoopToggle") == 0) m_keyToggle = label;
        else if (std::strcmp(spec.action, "CoopFollow") == 0) m_keyFollow = label;
        else if (std::strcmp(spec.action, "CoopMarker") == 0) m_keyMarker = label;
    }

    expression += "};";

    m_bindingExpression = expression;

    if (!InputActionManager)
    {
        AddLogLine("No input action manager - keys will not work this run");
        return;
    }

    m_bindingsAccepted = InputActionManager->AddBindings(expression.c_str());

    if (!m_bindingsAccepted)
    {
        AddLogLine("The engine rejected the key bindings. Check the Diagnostics "
                   "tab for the expression it was given.");
    }
}

void CoopMod::AddLogLine(const std::string& line)
{
    // Everything the panel says goes in the file too. The panel scrolls, the
    // panel is behind a key that only works when the panel is closed, and the
    // panel cannot be sent to anybody. The file is none of those things.
    Diag::Log("%s", line.c_str());

    Threading::WriteGuard guard(m_logLock);

    m_log.push_back(line);

    while (m_log.size() > kLogCapacity)
    {
        m_log.pop_front();
    }
}

std::vector<std::string> CoopMod::SnapshotLog() const
{
    Threading::ReadGuard guard(m_logLock);

    return std::vector<std::string>(m_log.begin(), m_log.end());
}

void CoopMod::ForgetPeerScene(const char* why)
{
    if (m_peerScene.empty() && m_peerSceneOwnerId == Net::kInvalidPeerId)
    {
        return;
    }

    Diag::Log("scene: dropping the offer to join %s - %s",
              m_peerScene.empty() ? "(nowhere)" : m_peerScene.c_str(), why);

    Threading::WriteGuard guard(m_sceneShareLock);

    m_peerScene.clear();
    m_peerSceneOwner.clear();
    m_peerSceneOwnerId    = Net::kInvalidPeerId;
    m_peerSceneCheckpoint = -1;
}

std::string CoopMod::PeerName(uint8_t peerId) const
{
    for (const Game::AvatarView& view : m_avatars.Views())
    {
        if (view.peerId == peerId)
        {
            return view.name;
        }
    }

    return std::format("player {}", peerId);
}

// ---- Frame ----------------------------------------------------------------

void CoopMod::OnFrameUpdate(const SGameUpdateEvent& updateEvent)
{
    // Once per thread would do; once per frame costs a thread-local store and
    // buys a crash report that says which loop died.
    Diag::NameCurrentThread("game");

    const Diag::FrameCostScope costScope(m_cost);

    // A breadcrumb per stage, for the watchdog thread to read back if this
    // frame never finishes. A relaxed store of one word costs nothing and is
    // the only thing that survives a hard freeze, which leaves no crash and no
    // last log line - just a log that stops.
    const auto phase = [this](const uint32_t stage)
    {
        m_framePhase.store(stage, std::memory_order_relaxed);
    };

    // Bumped however this frame leaves. The function has several early returns,
    // and a counter that missed them would read to the watchdog as a stall.
    //
    // It also puts the phase back to 0 - "waiting for the next frame". Without
    // that, the last stage stamped stays behind after the mod is done, and a
    // stall anywhere in the game's own code reads as a stall in whatever the
    // mod happened to do last. The first watchdog build blamed "session update"
    // for exactly that reason. Phase 0 means the mod finished and the game has
    // not called it back: the stall is the game's, not the mod's.
    struct FrameTick
    {
        std::atomic<uint32_t>& stage;
        std::atomic<uint64_t>& counter;

        ~FrameTick()
        {
            stage.store(0, std::memory_order_relaxed);
            counter.fetch_add(1, std::memory_order_relaxed);
        }
    } frameTick{ m_framePhase, m_frameCount };

    const bool toggleDown = m_toggleAction.Digital();

    if (toggleDown && !m_prevToggle)
    {
        // Opens, rather than toggles, and asks for the cursor on the way.
        //
        // The SDK turns the game's bindings off while it holds the keyboard,
        // so this key can only ever fire when the SDK does *not* have focus -
        // which is also exactly when the panel is not being drawn. It used to
        // toggle a flag under those conditions, which meant pressing it showed
        // nothing and pressing it twice showed nothing twice.
        //
        // Closing is the key left of 1, or the window's own X.
        m_isOpen = true;

        UI::RequestImGuiFocus();
    }

    m_prevToggle = toggleDown;

    phase(1);
    m_probe.Update(updateEvent);

    // Real time, not game time: co-op has to keep running while one player is
    // sitting in a pause menu. ZGameTime holds ticks and converts on request -
    // it does not implicitly become a float.
    const float deltaSeconds = static_cast<float>(updateEvent.m_RealTimeDelta.ToSeconds());

    // Whether a Go there is in flight - armed and waiting for the teardown,
    // committed, or streaming. While it is, the mod lets go of the world the
    // way the commit path always has, but up front rather than at the last
    // moment: the world is about to be torn down (the player returns to the
    // main menu to complete the jump), and everything the mod holds that points
    // into it - the damage hook patched into the player's vtable, the spectator
    // camera, the peer avatars positioned against the local player - must not
    // touch it while it dies. The last freeze was exactly this: the teardown
    // ran with the mod still clamped on. Everything re-arms by itself once the
    // new world is up.
    phase(2);

    {
        const Game::SceneSync::Stage stage = Game::SceneSync::CurrentStage();

        const bool loadActive = stage == Game::SceneSync::Stage::Ready
                             || stage == Game::SceneSync::Stage::Committed
                             || stage == Game::SceneSync::Stage::Streaming;

        if (loadActive && !m_loadActivePrev)
        {
            Diag::Log("scene: jump in flight - releasing world contact (damage "
                      "hook, spectator, peer drawing) so the teardown runs clean; "
                      "god mode is off until the new level is up");

            Game::TheDownedState().Disarm();
            Game::TheDownedState().ResetForNewSession();

            m_spectator.Leave();
        }

        m_loadActivePrev = loadActive;

        m_worldContactSuspended.store(loadActive, std::memory_order_relaxed);
    }

    phase(10);
    UpdateSceneTransition();

    // The downed flow re-arms the damage hook whenever it sees a player, so it
    // must sit out the whole jump or the release above lasts exactly one frame.
    if (!m_worldContactSuspended.load(std::memory_order_relaxed))
    {
        phase(3);
        UpdateDownedFlow(deltaSeconds);
    }

    // Markers fade on their own clock, and should keep fading after a session
    // ends rather than hanging in the world.
    phase(4);
    m_avatars.TickMarkers(deltaSeconds);

    // Both of these used to sit below the session check, which meant the marker
    // key and the kill feed did nothing at all until somebody else connected -
    // including while you were testing the mod on your own, where the whole
    // question is whether the keys work.
    UpdateMarkerKey();
    UpdateKillFeed();

    // Instinct drives how teammates are drawn, so it is read before anything
    // draws and handed across rather than read from the render thread.
    phase(5);
    m_instinct.Update(deltaSeconds);
    m_avatars.SetInstinct(m_instinct.Active());

    // The engine's own health, read from where the HUD reads it. Nothing acts
    // on this yet - it is being watched for a session so the offsets can be
    // trusted before the whole damage model is rebuilt on top of them. It reads
    // the player's health object, so it sits the jump out with everything else.
    if (!m_worldContactSuspended.load(std::memory_order_relaxed))
    {
        phase(6);
        m_engineHealth.Update();
    }

    // Which level we are in, tracked whether or not anybody is connected - the
    // panel compares against it on the render thread, and reading it there
    // every frame would mean walking into the level manager from the wrong
    // thread for the sake of a string that changes twice an hour.
    //
    // Nothing replicated this before. Only where somebody was standing inside a
    // level was ever sent, so a host who started a mission simply became
    // "elsewhere" to everybody else, with no way for them to find out where
    // elsewhere was, and no way to follow.
    phase(7);
    const std::string scene = Game::SceneSync::CurrentScene();

    if (scene != m_publishedScene)
    {
        {
            Threading::WriteGuard guard(m_sceneShareLock);
            m_publishedScene = scene;
        }

        Diag::Log("scene: we are in %s", scene.empty() ? "(menu)" : scene.c_str());

        // Say it now as well as on the timer below, so a level change is not
        // sitting behind a three second wait.
        m_sceneAnnounceIn = 0.f;
    }

    // Repeated, not just sent on change.
    //
    // Announcing only on change meant a host who was already in a mission when
    // the session started never announced anything at all: the scene had been
    // recorded while offline, so it never changed again, so nobody was ever
    // told where the host was and the button to follow never appeared. The same
    // hole swallowed anyone who joined late, and any packet that went missing.
    //
    // A scene name every few seconds is nothing next to the position updates
    // already going out sixty times a second, and it makes the whole thing
    // self-healing: whatever anybody missed, they learn within three seconds of
    // being connected.
    phase(8);

    if (m_session.IsActive())
    {
        m_sceneAnnounceIn -= deltaSeconds;

        if (m_sceneAnnounceIn <= 0.f)
        {
            constexpr float kAnnounceEvery = 3.f;

            m_sceneAnnounceIn = kAnnounceEvery;

            // The menu is a scene like any other, and announcing it would
            // offer everybody still playing a button out of their own mission.
            if (Game::SceneSync::IsMissionScene(m_publishedScene))
            {
                Net::EventMessage announcement;
                announcement.type = Net::EventType::LevelChanged;
                announcement.x    = static_cast<float>(Game::SceneSync::CurrentCheckpoint());
                announcement.text = m_publishedScene;

                m_session.SendEvent(announcement);
            }
        }
    }
    else
    {
        // Connecting should announce immediately, not up to three seconds later.
        m_sceneAnnounceIn = 0.f;
    }

    // A scene load somebody asked for from the panel. Started here, on the
    // game thread, with no frame in progress - never from the button itself.
    //
    // Starting it only puts three resource requests in. Nothing is torn down
    // until they have all arrived, which is a few seconds later and several
    // frames along, so the player who pressed the button keeps a working game
    // in the meantime and gets it back if the level turns out not to load.
    phase(9);

    if (m_sceneLoadPending)
    {
        std::string request;
        int         checkpoint = 0;

        {
            Threading::WriteGuard guard(m_sceneShareLock);

            m_sceneLoadPending = false;
            request            = m_sceneLoadRequest;
            checkpoint         = m_sceneLoadCheckpoint;
        }

        std::string error;

        // What to put back if nothing happens, and what a transition would
        // look like if it did.
        m_sceneLoadWasIn   = m_publishedScene;
        m_sceneLoadFromLvl = m_probe.LocalState().level;
        m_sceneLoadWatch   = 0.f;

        const bool started = Game::SceneSync::Begin(request, checkpoint, error);

        {
            Threading::WriteGuard guard(m_sceneShareLock);
            m_sceneError = started ? std::string() : error;
        }

        if (started)
        {
            // The load is armed, not fired: from a live level SwitchToScene
            // hangs, so the mod holds it until the level is torn down to the
            // clean front-end (the +0x6c latch back to 0) and only then commits.
            // Say so, in the log file and on the panel, so the manual step is
            // an instruction rather than a mystery.
            Diag::Log("scene: Go there armed for %s - holding until the level is "
                      "torn down; return to the main menu to complete the jump",
                      request.c_str());

            AddLogLine("Go there armed - open the pause menu and Return to Main "
                       "Menu, and I'll load their level from there");
        }
        else
        {
            AddLogLine(std::format("Could not go there: {}", error));
        }
    }

    Game::SceneSync::Update(deltaSeconds);

    // A load can now fail inside Update - the mount never finishing, a
    // resource missing - and nothing returns an error string from there. The
    // stage transition is the signal, and the player deserves a sentence for
    // it, not just a button that quietly comes back.
    {
        const Game::SceneSync::Stage stage = Game::SceneSync::CurrentStage();

        if (stage == Game::SceneSync::Stage::Failed && m_lastSceneStage != stage)
        {
            const char* note = "the level did not load - mods/Coop.log has the last step that ran";

            {
                Threading::WriteGuard guard(m_sceneShareLock);
                m_sceneError = note;
            }

            AddLogLine(note);
        }

        m_lastSceneStage = stage;
    }

    // How long the torn-down reading has held. The latch clears when the
    // game-mode object is destroyed, which is where the teardown *begins* -
    // entities and packages are still being released behind it, and the engine
    // is still showing a loading screen. Committing on the first frame it reads
    // clear means loading into a level that is still coming apart, so the
    // reading has to hold still, and the engine has to be done loading, first.
    const bool engineLoading = GameOffsets::IsLoading();

    m_engineLoading.store(engineLoading, std::memory_order_relaxed);


    if (Game::SceneSync::LevelTornDown() && !engineLoading)
    {
        m_tornDownFor += deltaSeconds;
    }
    else
    {
        m_tornDownFor = 0.f;
    }

    // Committing waits out any transition the engine is already running - the
    // player restarting a checkpoint from the pause menu while the level
    // streamed in, say. Writing the parameters mid-consumption would hand the
    // engine half of one destination and half of another.
    constexpr float kSettleSeconds = 1.5f;

    if (Game::SceneSync::IsReadyToCommit()
        && !Game::SceneSync::EngineMidTransition()
        && m_tornDownFor >= kSettleSeconds)
    {
        // The level has been torn down to the clean front-end state, so the
        // engine will actually complete the switch now instead of hanging on a
        // second game-mode object standing beside the first. This is the moment
        // an armed Go there fires - the log marks it so the wait, the teardown
        // (the +0x6c latch dropping to 0), and the commit read as one story.
        Diag::Log("scene: level torn down and settled for %.1fs (clean front-end, "
                  "engine not loading) - committing the armed load now", m_tornDownFor);

        // Let go of everything the mod is holding that points into the world
        // about to be destroyed. The damage hook is a patched entry in the
        // player's vtable and the player is about to stop existing; the
        // spectator camera is orbiting a position in a level that is going
        // away. Both re-arm by themselves once the new world is up.
        Game::TheDownedState().Disarm();
        Game::TheDownedState().ResetForNewSession();

        m_spectator.Leave();

        std::string error;

        // Measured, not assumed - see LoadPatienceSeconds.
        m_sceneLoadWatch     = LoadPatienceSeconds();
        m_sceneLoadPlayerWas = LevelManager ? LevelManager->GetHitman().GetRawPointer() : nullptr;

        if (!Game::SceneSync::Commit(error))
        {
            {
                Threading::WriteGuard guard(m_sceneShareLock);
                m_sceneError = error;
            }

            m_sceneLoadWatch = 0.f;

            AddLogLine(std::format("Could not go there: {}", error));
        }

        // Nothing after this can assume the world still exists.
        return;
    }

    // Did it actually go anywhere?
    //
    // Asking for a load changes what the game says about itself whether or not
    // it loads: the scene name is written first, so the mod reads back the
    // destination and believes it has arrived. In the game it did exactly that
    // - the name changed, the chapter did not, the player stayed in the menu,
    // and the button offering to go quietly disappeared because the two names
    // now matched. Believing a load happened is worse than admitting it did
    // not, so this waits for the chapter to move and puts the name back when it
    // does not.
    if (m_sceneLoadWatch > 0.f)
    {
        // Arrival is the player entity being rebuilt, with the chapter byte as
        // a second witness. The byte alone is not enough: half the campaign's
        // scenes share a chapter with their neighbours, and on an unrecognised
        // build it never moves at all - and a watchdog that cannot see an
        // arrival ends up tearing down a level somebody is standing in.
        ZHitman5* nowPlayer =
            LevelManager ? LevelManager->GetHitman().GetRawPointer() : nullptr;

        const bool chapterMoved  = m_probe.LocalState().level != m_sceneLoadFromLvl;
        const bool playerSwapped = nowPlayer != nullptr &&
                                   static_cast<void*>(nowPlayer) != m_sceneLoadPlayerWas;

        if (chapterMoved || playerSwapped)
        {
            m_sceneLoadWatch = 0.f;

            Diag::Log("scene: the load happened - %s",
                      chapterMoved ? "the chapter moved" : "the player was rebuilt");

            Game::SceneSync::ConfirmArrived();
        }
        else if (GameOffsets::IsLoading())
        {
            // The engine is visibly mid-load. However long the drive takes is
            // however long it takes - the countdown holds rather than expiring
            // into a teardown of a level that is half built, and the ten
            // seconds are the grace left once the loading screen goes away.
            const float grace =
                std::max(20.f, m_longestStallMs.load(std::memory_order_relaxed) / 2000.f);

            if (m_sceneLoadWatch < grace)
            {
                m_sceneLoadWatch = grace;
            }
        }
        else
        {
            m_sceneLoadWatch -= deltaSeconds;

            if (m_sceneLoadWatch <= 0.f)
            {
                // The flag route went nowhere. Before giving up, run the whole
                // pipeline by hand - the resources are still resident and the
                // vtable slots are verified against known anchors. One press,
                // two attempts, and the log says which one worked.
                //
                // The same letting-go as the commit path first: the fallback
                // opens with ClearScene, and the re-armed damage hook and the
                // spectator camera both point into the world it destroys.
                Game::TheDownedState().Disarm();
                Game::TheDownedState().ResetForNewSession();

                m_spectator.Leave();

                std::string fallbackError;

                if (Game::SceneSync::TryFallback(fallbackError))
                {
                    m_sceneLoadWatch = LoadPatienceSeconds();

                    AddLogLine("The engine ignored the handover - ran its pipeline "
                               "by hand instead, watching again");

                    return;
                }

                Game::SceneSync::SetSceneName(m_sceneLoadWasIn);
                Game::SceneSync::AbortEngineTransition();
                Game::SceneSync::Cancel();

                const std::string note = std::format(
                    "the level did not load either way ({}) - the log has "
                    "every step and the vtable dump",
                    fallbackError.empty() ? "no reason given" : fallbackError);

                {
                    Threading::WriteGuard guard(m_sceneShareLock);

                    m_publishedScene = m_sceneLoadWasIn;
                    m_sceneError     = note;
                }

                AddLogLine(note);

                Diag::Log("scene: no transition thirty seconds after the "
                          "handover, name put back to %s",
                          m_sceneLoadWasIn.empty() ? "(menu)" : m_sceneLoadWasIn.c_str());
            }
        }
    }

    TraceWorldChanges();
    Game::SceneSync::TraceTransitionWindow();
    TraceKeys();
    AutoDumpWhenReady(deltaSeconds);

    // The dump button, serviced here so the walk over the engine's
    // configuration happens on the thread that owns the engine. The button
    // itself runs inside Present and only raises its hand.
    if (m_dumpRequested.exchange(false))
    {
        std::string error;

        const std::string path = Game::WriteDump(
            m_configVars, m_probe, m_bindingExpression, SnapshotLog(), error);

        {
            Threading::WriteGuard guard(m_logLock);

            m_lastDumpPath  = path;
            m_lastDumpError = error;
        }

        AddLogLine(path.empty() ? std::format("Dump failed: {}", error)
                                : std::format("Wrote {}", path));
    }

    m_passThroughWaited += deltaSeconds;

    // Reads the player to see what changed about them, so it sits the jump out
    // with the rest of the world contact.
    if (!m_worldContactSuspended.load(std::memory_order_relaxed))
    {
        phase(11);
        UpdatePlayerDiff();
    }

    if (!m_session.IsActive())
    {
        // Nobody is out there, so nobody is anywhere worth going.
        ForgetPeerScene("the session ended");

        return;
    }

    // Publishing reads the local player's position and state out of the world;
    // during a jump that world is being torn down. Incoming events still get
    // pumped - the session stays alive across the jump, it just stops
    // describing a player who is on the way out.
    if (!m_worldContactSuspended.load(std::memory_order_relaxed))
    {
        phase(12);
        PublishLocalState();
    }
    PumpEvents();

    // PlayerLeft is one reliable message and reliable is not the same as
    // guaranteed - the link can time out before it lands. So the offer is also
    // checked against who is actually still here.
    if (m_peerSceneOwnerId != Net::kInvalidPeerId)
    {
        bool stillHere = false;

        for (const Net::PeerSnapshot& peer : m_session.SnapshotPeers())
        {
            if (peer.peerId == m_peerSceneOwnerId)
            {
                stillHere = true;
                break;
            }
        }

        if (!stillHere)
        {
            ForgetPeerScene("they are no longer in the session");
        }
    }

    // Local progress, turned into something the others can act on.
    std::vector<Net::EventMessage> localEvents;

    m_progression.ObserveLocal(m_probe.CurrentJumpPoint(),
                               m_probe.LocalState().level,
                               m_probe.HasPlayer(),
                               (m_probe.LocalState().flags & Net::SF_Dead) != 0,
                               localEvents);

    for (const Net::EventMessage& localEvent : localEvents)
    {
        m_session.SendEvent(localEvent);
    }

    const bool followDown = m_followAction.Digital();

    if (followDown && !m_prevFollow && m_progression.Pending().active)
    {
        m_progression.ConfirmPending();
    }

    m_prevFollow = followDown;

    int  jumpPoint   = -1;
    bool resetHitman = false;

    if (m_progression.Update(deltaSeconds, jumpPoint, resetHitman))
    {
        if (Game::WorldProbe::JumpToCheckpoint(jumpPoint, resetHitman))
        {
            AddLogLine(std::format("Jumping to checkpoint {}", jumpPoint));
        }
        else
        {
            AddLogLine("Could not reach the checkpoint manager - staying put");
        }
    }

    // Where everyone is, resolved for drawing. Skipped while a jump is in
    // flight: this positions every peer against the local player, and during
    // the teardown that player is being destroyed. The views simply stay as
    // they were until the new world is up.
    if (m_worldContactSuspended.load(std::memory_order_relaxed))
    {
        return;
    }

    const float4& position = m_probe.PlayerPosition();

    m_avatars.Update(m_session.SnapshotPeers(),
                     SVector3(position.x, position.y, position.z),
                     m_probe.LocalState().level,
                     m_probe.LocalState().section);
}

void CoopMod::UpdateMarkerKey()
{
    // A marker is placed where the player is standing. Placing it wherever they
    // are looking would be better and needs a raycast; this needs nothing, and
    // "come here" is most of what anyone uses a ping for.
    const bool markerDown = m_markerAction.Digital();

    if (markerDown && !m_prevMarker && m_probe.HasPlayer())
    {
        const float4& position = m_probe.PlayerPosition();

        // Drawn locally either way. Alone that is the whole of it, and it is
        // also how anyone checks the key is reaching the mod at all.
        m_avatars.AddMarker(SVector3(position.x, position.y, position.z),
                            "you", m_session.Status().localPeerId);

        if (m_session.IsActive())
        {
            Net::EventMessage marker;
            marker.type = Net::EventType::Marker;
            marker.x    = position.x;
            marker.y    = position.y;
            marker.z    = position.z;
            marker.text = "marked";

            m_session.SendEvent(marker);
        }
        else
        {
            AddLogLine("Marker placed. Nobody else is in the session to see it.");
        }
    }

    m_prevMarker = markerDown;
}

void CoopMod::UpdateKillFeed()
{
    // Anyone who died in the local world. Each client only ever reports its
    // own, which is the only honest thing it can do when the worlds are not
    // shared.
    for (const Game::ActorDeath& death : m_probe.DrainDeaths())
    {
        if (m_session.IsActive())
        {
            Net::EventMessage event;
            event.type = Net::EventType::ActorDied;
            event.x    = death.position.x;
            event.y    = death.position.y;
            event.z    = death.position.z;
            event.text = death.name;

            m_session.SendEvent(event);
        }

        AddLogLine(std::format("{} down", Game::PeerAvatars::SanitiseForDisplay(death.name, 40)));
    }
}

void CoopMod::TraceWorldChanges()
{
    // A record of when the things we cannot predict actually happen, on disk,
    // with timestamps. Every line here answers a question that only a running
    // game can: whether the checkpoint index moves when you walk into a new
    // area, whether a killed guard leaves the alive list or stays in it with a
    // flag, how often the player entity is replaced.
    const int     jumpPoint = m_probe.CurrentJumpPoint();
    const uint8_t level     = m_probe.LocalState().level;
    const uint8_t section   = m_probe.LocalState().section;

    if (jumpPoint != m_tracedJumpPoint || level != m_tracedLevel || section != m_tracedSection)
    {
        Diag::Log("world: chapter %u.%u checkpoint %d  (was %u.%u / %d)",
                  level, section, jumpPoint,
                  m_tracedLevel, m_tracedSection, m_tracedJumpPoint);

        m_tracedJumpPoint = jumpPoint;
        m_tracedLevel     = level;
        m_tracedSection   = section;
    }

    // Instinct, on the edges only. The question is whether the focus
    // controller can be read at all and whether holding the key is visible in
    // it - both answered by two lines rather than by a value every frame.
    if (m_instinct.Active() != m_tracedInstinct)
    {
        m_tracedInstinct = m_instinct.Active();

        Diag::Log("instinct %s (meter %.3f, %s)",
                  m_tracedInstinct ? "held" : "released",
                  m_instinct.Amount(),
                  m_instinct.Readable() ? "readable" : "UNREADABLE");
    }

    // The engine's health, every time it moves. This is the number that decides
    // whether the mod can throw away its shadow pool: if it drops for a fall,
    // for drowning, and for a close-combat sequence - none of which reach the
    // interception - then mirroring it replaces the whole arrangement.
    if (m_engineHealth.Readable() && m_engineHealth.Current() != m_tracedEngineHealth)
    {
        m_tracedEngineHealth = m_engineHealth.Current();

        // God mode goes in the same line on purpose. The first session watching
        // this reported the health "stuck" at full, which was correct and not a
        // fault: with the flag set and the interception armed, nothing in the
        // game is permitted to move that number. A reading of it is worthless
        // without knowing that, so they travel together.
        Diag::Log("engine health %.2f (max %.2f, dropped %.2f) | mod pool %.0f | %s",
                  m_engineHealth.Current(), m_engineHealth.Max(),
                  m_engineHealth.LastDrop(), Game::TheDownedState().HitPoints(),
                  Game::TheDownedState().Settings().alsoUseEngineImmunity
                      ? "god mode ON - nothing can move this"
                      : "god mode off");

        // Once, the object the maximum lives in. The writable current health
        // should be a float near it, and this is how we find out which.
        if (!m_dumpedHealthObject && m_engineHealth.HealthObject() != 0)
        {
            m_dumpedHealthObject = true;

            constexpr size_t kBytes = 0x240;

            std::vector<uint8_t> bytes(kBytes, 0);

            if (m_engineHealth.SnapshotHealthObject(bytes.data(), kBytes) == kBytes)
            {
                Diag::Log("health object at %08X, %zu bytes - the maximum is at +0x21C,"
                          " so the writable current value is a float near it:",
                          static_cast<unsigned>(m_engineHealth.HealthObject()), kBytes);

                LogBytes("  ", bytes.data(), kBytes);
            }
            else
            {
                Diag::Log("health object at %08X could not be read",
                          static_cast<unsigned>(m_engineHealth.HealthObject()));
            }
        }
    }

    const uint8_t phase = static_cast<uint8_t>(Game::TheDownedState().Phase());

    if (phase != m_tracedPhase)
    {
        Diag::Log("player is now %s (health %.0f, %u hits, downed %u times, body %s)",
                  phase == 1 ? "DOWN" : phase == 2 ? "up again, briefly immune" : "up",
                  Game::TheDownedState().HitPoints(),
                  Game::TheDownedState().HitsTaken(),
                  Game::TheDownedState().TimesDowned(),
                  Game::TheDownedState().BodyHidden() ? "hidden" : "in the world");

        m_tracedPhase = phase;
    }

    if (Game::TheDownedState().IsArmed() != m_tracedArmed)
    {
        m_tracedArmed = Game::TheDownedState().IsArmed();

        Diag::Log("interception %s: %s | %s",
                  m_tracedArmed ? "armed" : "let go",
                  Game::TheDownedState().Diagnostic().c_str(),
                  Game::TheDownedState().ImmunityNote().c_str());
    }

    // Which of the two death signals the engine actually uses. Whichever
    // counter moves when a guard goes down is the answer, and the log gives it
    // a timestamp so it can be matched against what happened on screen.
    const uint32_t deaths = m_probe.DeathsByFlag() + m_probe.DeathsByVanish();

    if (deaths != m_tracedDeaths)
    {
        Diag::Log("actor deaths: %u by flag, %u by leaving the list (%u listed alive)",
                  m_probe.DeathsByFlag(), m_probe.DeathsByVanish(),
                  m_probe.AliveActorCount());

        m_tracedDeaths = deaths;
    }

    const uint32_t hits = Game::TheDownedState().HitsTaken();

    if (hits != m_tracedHits)
    {
        const std::vector<Game::HitCapture> captures = Game::TheHitInspector().Snapshot();

        if (!captures.empty())
        {
            const Game::HitCapture& latest = captures.front();

            Diag::Log("hit %u: SHitInfo at %08X, called from HMA.exe+%08X, %u bytes, "
                      "health now %.0f",
                      latest.ordinal, static_cast<unsigned>(latest.address),
                      static_cast<unsigned>(latest.callerRva), latest.byteCount,
                      Game::TheDownedState().HitPoints());

            // The whole struct, every time, in the file. This is the thing the
            // damage figure is hiding in, and a panel showing it is only useful
            // to somebody photographing their own screen.
            LogBytes("  ", latest.bytes, latest.byteCount);

            // And the projectile it came from, which is where GetBaseDamage
            // reads. The hit struct itself was byte-identical across a whole
            // session past the hit normal, so if a damage figure is reachable
            // at all it is in here.
            if (latest.projectileByteCount > 0)
            {
                Diag::Log("  projectile at %08X:", static_cast<unsigned>(latest.projectile));
                LogBytes("    ", latest.projectileBytes, latest.projectileByteCount);
            }
            else
            {
                Diag::Log("  no projectile on this hit - melee or close combat");
            }
        }

        m_tracedHits = hits;
    }
}

void CoopMod::MouseLook(float deltaSeconds, float& yawDelta, float& pitchDelta)
{
    // Lifted from the cinematic camera's free look, which is the one in this
    // set that people actually use, rather than reinvented. Three things in it
    // are not obvious and all three matter:
    //
    //   1. Cursor visibility is the test, not our own panel flag. The SDK shows
    //      the OS cursor exactly when it has taken input for its own panels, so
    //      it is the honest answer to "is the player clicking menus", and it
    //      needs no offsets - unlike ImGuiRenderer::imguiHasFocus, which is
    //      private.
    //   2. The mouse is a displacement, not a velocity. Routed through the same
    //      axes the held keys use it would keep turning the camera after the
    //      hand stops, at a rate scaled by frame time.
    //   3. The cursor is sampled once a frame and quantised to whole pixels, so
    //      the per-frame delta is often 0 or 1. Spent raw that is visible
    //      steppiness rather than a pan, which is what the smoothing below is
    //      for - and it smooths the displacement, so total travel is conserved
    //      exactly and the camera still stops when the hand does.
    float mouseYaw   = 0.f;
    float mousePitch = 0.f;

    const bool menuActive = HUDManager && HUDManager->IsPauseMenuActive();

    if (m_mouseLook && !menuActive && !CursorIsVisible())
    {
        POINT centre{};

        if (WindowCentre(centre))
        {
            POINT cursor{};

            if (GetCursorPos(&cursor))
            {
                if (m_mouseLookActive)
                {
                    mouseYaw   += static_cast<float>(cursor.x - centre.x) * m_mouseSensitivity;
                    mousePitch -= static_cast<float>(cursor.y - centre.y) * m_mouseSensitivity;
                }

                // Re-centred every frame so the cursor can never run out of
                // screen, which is what makes an unbounded look possible at
                // all. The first frame only warps, so entering the camera does
                // not snap the view by however far the cursor happened to be.
                SetCursorPos(centre.x, centre.y);

                m_mouseLookActive = true;
            }
        }
    }
    else
    {
        m_mouseLookActive = false;
    }

    m_pendingMouseYaw   += mouseYaw;
    m_pendingMousePitch += mousePitch;

    const float retained = std::clamp(m_mouseSmoothing, 0.f, 0.95f);
    const float release  = retained <= 0.f
        ? 1.f
        : 1.f - std::pow(retained, deltaSeconds * 60.f);

    mouseYaw   = m_pendingMouseYaw   * release;
    mousePitch = m_pendingMousePitch * release;

    m_pendingMouseYaw   -= mouseYaw;
    m_pendingMousePitch -= mousePitch;

    // Spectator::Nudge works in radians; the sensitivity above is degrees per
    // pixel, which is the unit anybody setting it would expect.
    constexpr float kDegToRad = 0.01745329f;

    yawDelta   += mouseYaw * kDegToRad;
    pitchDelta += (m_invertMouseY ? -mousePitch : mousePitch) * kDegToRad;
}

void CoopMod::TraceKeys()
{
    // Named in the order the binding table declares them, so a line in the log
    // reads the same as a row in the panel.
    static const char* const kNames[] = {
        "toggle", "follow", "marker",
        "spectate left", "spectate right", "spectate up", "spectate down",
        "spectate closer", "spectate further",
    };

    ZInputAction* const actions[] = {
        &m_toggleAction, &m_followAction, &m_markerAction,
        &m_specLeft, &m_specRight, &m_specUp, &m_specDown,
        &m_specCloser, &m_specFurther,
    };

    static_assert(std::size(kNames) == std::size(actions));

    for (size_t i = 0; i < std::size(actions); ++i)
    {
        const bool down = actions[i]->Digital();

        if (down != m_tracedKeys[i])
        {
            Diag::Log("key: %s %s", kNames[i], down ? "down" : "up");

            m_tracedKeys[i] = down;
        }
    }
}

void CoopMod::AutoDumpWhenReady(float deltaSeconds)
{
    if (m_autoDumped)
    {
        return;
    }

    if (!m_probe.HasPlayer())
    {
        // A level that went away before the timer ran out does not count.
        m_secondsWithPlayer = 0.f;

        return;
    }

    m_secondsWithPlayer += deltaSeconds;

    // Long enough that the scene has settled and the actors are listed, short
    // enough that it happens before anybody does anything interesting.
    constexpr float kSettleSeconds = 6.f;

    if (m_secondsWithPlayer < kSettleSeconds)
    {
        return;
    }

    m_autoDumped = true;

    // The configuration walk is the expensive part of the dump and it is only
    // done on demand elsewhere. Doing it here is what makes the file complete
    // without anybody having visited the Engine tab first.
    if (!m_configVars.Walked())
    {
        m_configVars.Refresh();
        Diag::Log("config: %s", m_configVars.Diagnostic().c_str());
    }

    // The HUD has a health ring around the radar, and this mod has been
    // emptying a pool the ring knows nothing about. Whether the ring can be
    // reached by name is a question only the running game answers.
    Game::LogHudProbe();

    std::string error;

    const std::string path = Game::WriteDump(
        m_configVars, m_probe, m_bindingExpression, SnapshotLog(), error);

    if (path.empty())
    {
        Diag::Log("automatic dump failed: %s", error.c_str());
        AddLogLine(std::format("Automatic dump failed: {}", error));

        return;
    }

    {
        Threading::WriteGuard guard(m_logLock);

        m_lastDumpPath = path;
    }

    Diag::Log("automatic dump written to %s", path.c_str());

    // One block, near the top of every log, answering every question anybody
    // would otherwise have to open a panel to answer - which cannot be done
    // while playing, because the SDK takes the keyboard the moment it has one
    // open. Nobody should have to test by hand what the mod can test itself.
    Diag::Log("");
    Diag::Log("---- what the mod can tell you without being asked ----");
    Diag::Log("  build           %s (%s)",
              Game::BuildInfo::Get().Describe().c_str(), GameOffsets::GetBuildName());
    Diag::Log("  keys            %s",
              !m_bindingsEnabled ? "off in the ini"
              : m_bindingsAccepted ? "registered and accepted"
                                   : "REJECTED by the engine");
    Diag::Log("  interception    %s | %s",
              Game::TheDownedState().IsArmed() ? "armed" : "NOT ARMED",
              Game::TheDownedState().Diagnostic().c_str());
    Diag::Log("  engine backstop %s", Game::TheDownedState().ImmunityNote().c_str());
    Diag::Log("  engine health   %s", m_engineHealth.Note().c_str());
    Diag::Log("  health ring     %s",
              m_driveHudHealth ? "driven from the mod's pool" : "left alone");
    Diag::Log("  perception      %s",
              m_suppressPerceptionWhenDown
                  ? "will be switched off while down"
                  : "left alone while down - they will keep shooting you");
    Diag::Log("  instinct        %s",
              m_instinct.Readable() ? "focus controller readable"
                                    : "focus controller UNREADABLE");
    Diag::Log("  actor deaths    %u by flag, %u by leaving the list",
              m_probe.DeathsByFlag(), m_probe.DeathsByVanish());
    Diag::Log("  chapter         %u.%u, checkpoint %d",
              m_probe.LocalState().level, m_probe.LocalState().section,
              m_probe.CurrentJumpPoint());
    Diag::Log("-------------------------------------------------------");
    Diag::Log("");
    AddLogLine(std::format("Wrote {} by itself - no need to press anything", path));
}

void CoopMod::UpdatePlayerDiff()
{
    if (!m_awaitingPassThrough)
    {
        return;
    }

    Game::DownedState& downed = Game::TheDownedState();

    // Still waiting to be shot. Give up after a while rather than leaving a
    // hit armed indefinitely - somebody who forgets this is on will die to it.
    if (downed.PassThroughRemaining() > 0)
    {
        constexpr float kGiveUpSeconds = 60.f;

        if (m_passThroughWaited > kGiveUpSeconds)
        {
            downed.ArmPassThrough(0);

            m_awaitingPassThrough = false;

            const char* note = "nothing hit you within a minute - disarmed";

            {
                Threading::WriteGuard guard(m_logLock);

                m_playerDiffNote = note;
            }

            AddLogLine(note);
        }

        return;
    }

    // It was consumed, so the engine has now seen a hit. Whatever moved in the
    // player object between the two snapshots is what the engine did about it.
    m_playerAfter.assign(kPlayerSnapshotBytes, 0);

    const size_t read = downed.SnapshotPlayer(m_playerAfter.data(), kPlayerSnapshotBytes);

    m_awaitingPassThrough = false;
    m_playerDeltas.clear();

    if (read == 0 || m_playerBefore.size() != kPlayerSnapshotBytes)
    {
        const char* note = "could not read the player object on both sides of the hit";

        {
            Threading::WriteGuard guard(m_logLock);

            m_playerDiffNote = note;
        }

        AddLogLine(note);

        return;
    }

    for (size_t offset = 0; offset + sizeof(uint32_t) <= kPlayerSnapshotBytes;
         offset += sizeof(uint32_t))
    {
        uint32_t before = 0;
        uint32_t after  = 0;

        std::memcpy(&before, m_playerBefore.data() + offset, sizeof(before));
        std::memcpy(&after,  m_playerAfter.data()  + offset, sizeof(after));

        if (before != after)
        {
            m_playerDeltas.push_back({ offset, before, after });
        }
    }

    std::string note = std::format("{} dwords changed across one hit", m_playerDeltas.size());

    {
        Threading::WriteGuard guard(m_logLock);

        m_playerDiffNote = note;
    }

    AddLogLine(note);

    Diag::Log("player diff: %zu dwords changed across one engine-visible hit",
              m_playerDeltas.size());

    for (const PlayerDelta& delta : m_playerDeltas)
    {
        float beforeFloat = 0.f;
        float afterFloat  = 0.f;

        std::memcpy(&beforeFloat, &delta.before, sizeof(beforeFloat));
        std::memcpy(&afterFloat,  &delta.after,  sizeof(afterFloat));

        Diag::Log("  +0x%03zX  %08X -> %08X   %.3f -> %.3f",
                  delta.offset, delta.before, delta.after, beforeFloat, afterFloat);
    }
}

void CoopMod::PublishLocalState()
{
    Net::StateMessage state = m_probe.LocalState();

    if (Game::TheDownedState().IsDowned())
    {
        state.flags |= Net::SF_Dead;
    }

    m_session.PublishLocalState(state);
}

void CoopMod::PumpEvents()
{
    for (const Net::EventMessage& event : m_session.DrainEvents())
    {
        const std::string name = PeerName(event.originPeerId);

        m_progression.ObserveRemote(event, name);

        // Somebody reaching a checkpoint is a way back in for anyone down.
        if (event.type == Net::EventType::CheckpointReached)
        {
            Game::TheDownedState().ReviveOnCheckpoint();
        }

        // Somebody said which level they are in. Remembered rather than acted
        // on: loading a level throws away everything the player was doing, and
        // that is not a decision to make because a packet arrived.
        // The announcement repeats every three seconds so that a late joiner
        // and a lost packet both heal themselves. That is right on the wire and
        // wrong in the log, which filled with one identical line per repeat -
        // sixty of them in the session this came from. Only a change is news.
        // Somebody left, and they were the reason a ride was on offer.
        if (event.type == Net::EventType::PlayerLeft && event.originPeerId == m_peerSceneOwnerId)
        {
            ForgetPeerScene("they left the session");
        }

        if (event.type == Net::EventType::LevelChanged)
        {
            if (!Game::SceneSync::IsMissionScene(event.text))
            {
                // They went back to the front end. The offer was theirs, so it
                // goes with them - the first version only ever *set* the peer's
                // level and never cleared it, so a player who quit to the menu
                // left a Go there button behind pointing at a level nobody was
                // in, and it stayed there for the rest of the session.
                if (event.originPeerId == m_peerSceneOwnerId)
                {
                    ForgetPeerScene("they went back to the menu");
                }

                continue;
            }

            if (event.text == m_peerScene && event.originPeerId == m_peerSceneOwnerId)
            {
                // The three second repeat, which exists so a late joiner and a
                // lost packet both heal themselves. Not news.
                continue;
            }

            {
                Threading::WriteGuard guard(m_sceneShareLock);

                m_peerScene           = event.text;

                // Off the wire, so bounded here - the engine's checkpoint
                // machinery indexes with it and cannot be guarded.
                m_peerSceneCheckpoint = std::clamp(static_cast<int>(event.x), 0, 63);
                m_peerSceneOwner      = name;
                m_peerSceneOwnerId    = event.originPeerId;
            }

            Diag::Log("scene: %s is in %s at checkpoint %d",
                      name.c_str(), m_peerScene.c_str(), m_peerSceneCheckpoint);
        }

        if (event.type == Net::EventType::Marker)
        {
            m_avatars.AddMarker(SVector3(event.x, event.y, event.z), name, event.originPeerId);
        }
        else if (event.type == Net::EventType::ActorDied)
        {
            m_avatars.AddMarker(SVector3(event.x, event.y, event.z),
                                Game::PeerAvatars::SanitiseForDisplay(event.text, 40),
                                event.originPeerId);
        }

        AddLogLine(DescribeEvent(event));
    }
}

std::string CoopMod::DescribeEvent(const Net::EventMessage& event) const
{
    // Peer-supplied text reaches ImGui, which is safe with arbitrary bytes -
    // unlike the 3D text renderer, which throws on a glyph it lacks. Sanitised
    // anyway so both paths behave the same and neither becomes a surprise.
    const std::string name = Game::PeerAvatars::SanitiseForDisplay(
        PeerName(event.originPeerId), Net::kMaxNameLength);

    const std::string text = Game::PeerAvatars::SanitiseForDisplay(event.text, 160);

    switch (event.type)
    {
    case Net::EventType::Chat:              return std::format("{}: {}", name, text);
    case Net::EventType::Marker:            return std::format("{} marked a spot", name);
    case Net::EventType::ActorDied:         return std::format("{} took someone down", name);
    case Net::EventType::CheckpointReached: return std::format("{} reached {}", name, text);
    case Net::EventType::PlayerJoined:      return std::format("{} joined", text.empty() ? name : text);
    case Net::EventType::PlayerLeft:        return text.empty()
                                                   ? std::format("{} left", name)
                                                   : std::format("{}: {}", name, text);
    case Net::EventType::LevelChanged:      return std::format("{} moved to another chapter", name);
    case Net::EventType::SessionReset:      return "Session reset";
    default:                                return std::format("{} {}", name, text);
    }
}

void CoopMod::UpdateSceneTransition()
{
    ZHitman5* hitman = LevelManager ? LevelManager->GetHitman().GetRawPointer() : nullptr;

    const uint8_t level   = m_probe.LocalState().level;
    const uint8_t section = m_probe.LocalState().section;

    const bool playerReplaced = hitman != m_lastPlayerEntity;
    const bool areaChanged    = level != m_lastLevel || section != m_lastSection;

    if (playerReplaced || areaChanged)
    {
        // GameOffsets caches which pages it has probed, and a level change is
        // exactly when those pages go back to the allocator.
        GameOffsets::InvalidateProbeCache();

        // Everyone in the old scene is about to stop being listed. That is not
        // a massacre, it is a level change.
        m_probe.ForgetActors();

        // A new scene reloads the configuration, so the values we saved belong
        // to a game state that no longer exists. Let go of them rather than
        // writing them back over whatever the new level set up.
        if (m_perception.Suppressed())
        {
            m_perception.Abandon();
            AddLogLine("Level changed while down - perception left as the new scene set it");
        }
    }

    if ((playerReplaced || areaChanged) && hitman)
    {
        // A new scene is a clean slate: back on your feet, camera back to the
        // game. This is the other way out of being down, alongside a teammate
        // reaching a checkpoint.
        Game::TheDownedState().ReviveOnSceneChange();
    }

    m_lastPlayerEntity = hitman;
    m_lastLevel        = level;
    m_lastSection      = section;

    // Re-arms itself whenever the player entity is replaced, and lets go of
    // the old vtable rather than restoring through a pointer to a freed object.
    Game::TheDownedState().Arm(hitman);
}

void CoopMod::UpdateDownedFlow(float deltaSeconds)
{
    Game::DownedState& downed = Game::TheDownedState();

    // The guards' senses, off while somebody is on the floor.
    //
    // This is what the body-hiding was standing in for. The engine keeps its
    // detection cone, its hearing and its target tracker as configuration
    // variables, all of which turned up in the enumeration the mod already
    // does - so the AI can simply be made unable to notice anything, rather
    // than given nothing to notice.
    if (m_suppressPerceptionWhenDown)
    {
        if (downed.IsDowned() && !m_perception.Suppressed())
        {
            m_perception.Suppress(m_configVars);
            AddLogLine(m_perception.Note());
        }
        else if (!downed.IsDowned() && m_perception.Suppressed())
        {
            m_perception.Restore();
            AddLogLine(m_perception.Note());
        }
    }
    else if (m_perception.Suppressed())
    {
        m_perception.Restore();
        AddLogLine(m_perception.Note());
    }

    downed.Update(deltaSeconds, (m_probe.LocalState().flags & Net::SF_Dead) != 0);

    // A hit landed since the last frame, so kick the red up and let it fall.
    // Without this a hit that costs a quarter of the pool is a slightly darker
    // edge, and four of them arriving in a second are indistinguishable from
    // one - which is not what being shot four times should look like.
    if (downed.HitsTaken() != m_hurtLastHit)
    {
        m_hurtLastHit = downed.HitsTaken();
        m_hurtFlash   = std::min(1.f, m_hurtFlash + 0.45f);
    }

    constexpr float kFlashFadePerSecond = 1.6f;

    m_hurtFlash = std::max(0.f, m_hurtFlash - kFlashFadePerSecond * deltaSeconds);

    if (downed.Phase() == Game::DownedPhase::Downed)
    {
        // There used to be a second, shorter timer here: five seconds, applied
        // when playing alone, on the theory that a solo player has nobody to
        // wait for. What it actually did was contradict the countdown on the
        // screen - the overlay said twenty and you were up in five, every time,
        // because nobody testing the mod is in a session. The self-revive timer
        // is the only one now, and it is the one being displayed.
        //
        // The confirm key doubles as "get up now". Waiting out a timer you did
        // not choose is not a mechanic.
        if (m_followAction.Digital() && !m_prevFollow)
        {
            downed.ReviveOnSceneChange();
            AddLogLine("Back up");

            m_prevFollow = true;

            return;
        }

        m_prevFollow = m_followAction.Digital();

        m_spectator.Enter();

        // Watch a teammate in this level if there is one, otherwise the spot
        // where we fell - not the player's live position, which is under the
        // level once the body has been taken out of the fight.
        SVector3 target = downed.HaveDownPosition()
            ? SVector3(downed.DownX(), downed.DownY(), downed.DownZ())
            : SVector3(m_probe.PlayerPosition().x,
                       m_probe.PlayerPosition().y,
                       m_probe.PlayerPosition().z);

        for (const Game::AvatarView& view : m_avatars.Views())
        {
            if (view.sameArea && !view.stale && !view.dead)
            {
                target = view.position;
                break;
            }
        }

        // Orbit, so a downed player can watch rather than stare.
        constexpr float kTurnPerSecond = 1.8f;
        constexpr float kZoomPerSecond = 6.f;

        float yawDelta      = 0.f;
        float pitchDelta    = 0.f;
        float distanceDelta = 0.f;

        // The mouse is what anybody reaches for to look around, so it drives
        // this and the keys stay as a fallback. Taken from the cursor rather
        // than from an input action because the binding grammar's spelling for
        // a mouse axis is not documented anywhere we can check, and guessing it
        // wrong would have the engine reject the whole block - which is to say
        // every key in the mod, to add one.
        MouseLook(deltaSeconds, yawDelta, pitchDelta);

        if (m_specLeft.Digital())    yawDelta      -= kTurnPerSecond * deltaSeconds;
        if (m_specRight.Digital())   yawDelta      += kTurnPerSecond * deltaSeconds;
        if (m_specUp.Digital())      pitchDelta    += kTurnPerSecond * deltaSeconds;
        if (m_specDown.Digital())    pitchDelta    -= kTurnPerSecond * deltaSeconds;
        if (m_specCloser.Digital())  distanceDelta -= kZoomPerSecond * deltaSeconds;
        if (m_specFurther.Digital()) distanceDelta += kZoomPerSecond * deltaSeconds;

        if (yawDelta != 0.f || pitchDelta != 0.f || distanceDelta != 0.f)
        {
            m_spectator.Nudge(yawDelta, pitchDelta, distanceDelta);
        }

        m_spectator.Update(deltaSeconds, target, true);

        return;
    }

    if (m_spectator.IsActive())
    {
        m_spectator.Leave();

        if (downed.Phase() == Game::DownedPhase::Recovering)
        {
            AddLogLine("Back in the run");
        }
    }
}

// ---- UI --------------------------------------------------------------------

void CoopMod::OnDrawMenu()
{
    if (ImGui::MenuItem("Co-op", m_bindingsEnabled ? m_keyToggle.c_str() : nullptr, m_isOpen))
    {
        m_isOpen = !m_isOpen;
    }
}

void CoopMod::OnDraw3D()
{
    // Render thread. While a jump is in flight the world is being torn down
    // underneath this, so nothing of the mod's is drawn into it - the renderer
    // it would draw through belongs to the level that is going away.
    if (m_worldContactSuspended.load(std::memory_order_relaxed))
    {
        return;
    }

    m_avatars.Draw3D();
}

void CoopMod::OnDrawUI(const bool hasFocus)
{
    Diag::NameCurrentThread("render");

    m_loaded.OnDrawUI(hasFocus);

    // The health ring, driven from here rather than from the frame update.
    //
    // Both were tried. A write from the game thread moved nothing, and the same
    // write from a button on this thread moved the ring immediately - because
    // this runs inside Present, after the game has finished its own HUD pass,
    // and the frame update runs before it. Written there, the game overwrote it
    // every frame before anyone saw it.
    DriveHudHealth();

    // Before the rest: it is the game's feedback, not the mod's interface, and
    // it belongs behind everything the mod draws on top.
    RenderHurtOverlay();

    RenderHudOverlay();

    if (!m_isOpen || !hasFocus)
    {
        return;
    }

    RenderWindow();
}

void CoopMod::DriveHudHealth()
{
    Game::DownedState& downed = Game::TheDownedState();

    if (!m_driveHudHealth || !downed.Settings().enabled || !downed.IsArmed())
    {
        return;
    }

    if (!m_engineHealth.Readable() && m_engineHealth.Max() <= 0.f)
    {
        return;
    }

    const float max = m_engineHealth.Max() > 0.f ? m_engineHealth.Max() : 100.f;

    // Reading before writing is the self-test. If the value still holds what we
    // put there last frame, the write survives the game's own HUD pass and the
    // ring is genuinely ours; if the game has put its own number back, it does
    // not. Answered in the log, once, rather than by anybody watching a panel
    // they cannot have open while playing.
    if (m_hudDriveFrames == 60)
    {
        const float readBack = m_engineHealth.ReadRaw();
        const float expected = m_lastRingWrite;

        Diag::Log("ring readback: wrote %.2f, field holds %.2f - %s",
                  expected, readBack,
                  std::fabs(readBack - expected) < 0.5f
                      ? "the write survives, the ring is ours"
                      : "the game put its own value back, the ring is not ours");
    }

    if (m_hudDriveFrames < 1000)
    {
        ++m_hudDriveFrames;
    }

    m_lastRingWrite = downed.HitPointsFraction() * max;

    m_engineHealth.Write(m_lastRingWrite);

    if (!m_loggedHudDrive)
    {
        m_loggedHudDrive = true;

        Diag::Log("driving the health ring from the mod's pool (max %.1f)", max);
    }
}

void CoopMod::RenderHurtOverlay()
{
    Game::DownedState& downed = Game::TheDownedState();

    if (!m_showHurt || !downed.Settings().enabled || !downed.IsArmed())
    {
        return;
    }

    // Damage in Absolution is a red vignette closing in from the edges, and
    // when this mod took the health off the engine it took that with it - the
    // pool emptied in a panel nobody had open while the screen stayed clean.
    // This draws it back from the pool we own.
    //
    // It is not the whole of what the game shows, though. There is a health
    // ring around the radar, driven by the engine's own health, and that one
    // still reads full while this empties - so a player watching the HUD is
    // being told they are fine right up until they fall over. Fixing that means
    // either driving the ring through Scaleform or letting the engine keep its
    // health and mirroring it, and Game/ScaleformProbe.h is what will decide
    // which. Until then this is honest about being half the answer.
    const float missing = 1.f - downed.HitPointsFraction();

    // Below this the game would not have shown anything either.
    constexpr float kFloor = 0.15f;

    const float wound = missing <= kFloor ? 0.f : (missing - kFloor) / (1.f - kFloor);
    const float total = std::clamp(wound + m_hurtFlash, 0.f, 1.f);

    if (total <= 0.001f)
    {
        return;
    }

    ImDrawList* draw = ImGui::GetBackgroundDrawList();

    if (!draw)
    {
        return;
    }

    // DisplaySize rather than the viewport API, which not every ImGui the SDK
    // might be built against exposes the same way.
    const ImVec2 size = ImGui::GetIO().DisplaySize;

    if (size.x <= 0.f || size.y <= 0.f)
    {
        return;
    }

    const ImVec2 top{ 0.f, 0.f };

    const float bottomY = size.y;
    const float rightX  = size.x;

    // How far in the red reaches. A quarter of the screen when nearly dead,
    // which is about where the game's own effect sits.
    const float depthX = size.x * 0.28f * total;
    const float depthY = size.y * 0.34f * total;

    const float alpha = std::clamp(0.10f + 0.62f * total, 0.f, 0.80f);

    const ImU32 solid = ImGui::GetColorU32(ImVec4(0.42f, 0.02f, 0.02f, alpha));
    const ImU32 clear = ImGui::GetColorU32(ImVec4(0.42f, 0.02f, 0.02f, 0.f));

    // Four gradients rather than a texture: no asset to ship, no shader, and
    // ImGui interpolates the corners for us. The corners double up, which is
    // what makes them darker than the edges - which is also what the real
    // effect does.
    draw->AddRectFilledMultiColor(ImVec2(top.x, top.y), ImVec2(rightX, top.y + depthY),
                                  solid, solid, clear, clear);

    draw->AddRectFilledMultiColor(ImVec2(top.x, bottomY - depthY), ImVec2(rightX, bottomY),
                                  clear, clear, solid, solid);

    draw->AddRectFilledMultiColor(ImVec2(top.x, top.y), ImVec2(top.x + depthX, bottomY),
                                  solid, clear, clear, solid);

    draw->AddRectFilledMultiColor(ImVec2(rightX - depthX, top.y), ImVec2(rightX, bottomY),
                                  clear, solid, solid, clear);

    // Down is the whole screen going, not just the edges.
    if (downed.IsDowned())
    {
        const ImU32 wash = ImGui::GetColorU32(ImVec4(0.28f, 0.01f, 0.01f, 0.34f));

        draw->AddRectFilled(top, ImVec2(rightX, bottomY), wash);
    }
}

void CoopMod::RenderHudOverlay()
{
    if (!m_showOverlay)
    {
        return;
    }

    const Net::SessionStatus status = m_session.Status();

    const Game::PendingJump pending = m_progression.PendingSnapshot();
    const bool               downed  = Game::TheDownedState().IsDowned();

    if (status.mode == Net::SessionMode::Offline && !downed && !pending.active)
    {
        return;
    }

    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::SetNextWindowPos(ImVec2(18.f, 18.f), ImGuiCond_FirstUseEver);

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
                                     | ImGuiWindowFlags_AlwaysAutoResize
                                     | ImGuiWindowFlags_NoFocusOnAppearing
                                     | ImGuiWindowFlags_NoNav
                                     | ImGuiWindowFlags_NoInputs;

    if (ImGui::Begin("##CoopOverlay", nullptr, flags))
    {
        ImGui::TextColored(StatusColour(status.mode), "CO-OP  %s", StatusText(status.mode));

        for (const Game::AvatarView& view : m_avatars.Views())
        {
            const char* where = view.loading  ? "loading"
                              : view.stale    ? "no signal"
                              : view.dead     ? "down"
                              : view.sameArea ? "here"
                                              : "elsewhere";

            if (view.sameArea && !view.stale && !view.loading && !view.dead)
            {
                ImGui::Text("%s  %.0f m  %u ms", view.name.c_str(), view.distance, view.pingMs);
            }
            else
            {
                ImGui::Text("%s  %s", view.name.c_str(), where);
            }
        }

        // The one thing worth interrupting for: everybody else is in a level
        // you are not in, and the panel is where you fix that. Copies, because
        // this runs on the render thread and the game thread owns the strings.
        std::string peerScene, peerOwner, published;

        {
            Threading::ReadGuard guard(m_sceneShareLock);

            peerScene = m_peerScene;
            peerOwner = m_peerSceneOwner;
            published = m_publishedScene;
        }

        if (!peerScene.empty() && peerScene != published)
        {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.f, 0.82f, 0.35f, 1.f), "%s is in another mission",
                               peerOwner.empty() ? "Somebody" : peerOwner.c_str());
            ImGui::TextDisabled("Open co-op and press Go there");
        }

        if (downed)
        {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.f, 0.45f, 0.45f, 1.f), "DOWN");

            const float untilUp = Game::TheDownedState().SecondsUntilSelfRevive();

            if (untilUp > 0.f)
            {
                ImGui::TextDisabled("Up in %.0fs, or press %s now", untilUp, m_keyFollow.c_str());
            }
            else
            {
                ImGui::TextDisabled("Press %s to get up, or wait for a checkpoint",
                                    m_keyFollow.c_str());
            }

            ImGui::TextDisabled("Arrow keys look around, PgUp and PgDn move in and out");
        }

        if (pending.active)
        {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.f, 0.82f, 0.35f, 1.f), "%s", pending.reason.c_str());

            if (m_progression.Settings().followPolicy == Game::FollowPolicy::AutoFollow)
            {
                ImGui::Text("Moving in %.0fs", std::max(0.f, pending.remaining));
            }
            else
            {
                ImGui::TextDisabled("Press the follow key to go");
            }
        }
    }

    ImGui::End();
}

void CoopMod::RenderWindow()
{
    ImGui::SetNextWindowSize(ImVec2(620.f, 500.f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(120.f, 120.f), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Absolution Co-op", &m_isOpen))
    {
        ImGui::End();
        return;
    }

    // Four tabs. There were six: one browsed all 1648 engine configuration
    // variables, which the automatic dump now writes to a file in full, and one
    // was a research bench full of live readouts of things that cannot be read
    // live - the SDK takes the keyboard whenever this panel is open, so nobody
    // has ever seen a key indicator light up or a hit land. Those readings all
    // go to the log now, where they can be read afterwards and sent to somebody.
    if (ImGui::BeginTabBar("CoopTabs"))
    {
        if (ImGui::BeginTabItem("Session"))
        {
            RenderSessionTab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Players"))
        {
            RenderPlayersTab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Rules"))
        {
            RenderRulesTab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Diagnostics"))
        {
            RenderDiagnosticsTab();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void CoopMod::RenderSessionTab()
{
    const Net::SessionStatus status = m_session.Status();

    ImGui::TextColored(StatusColour(status.mode), "%s", StatusText(status.mode));

    if (!status.lastError.empty())
    {
        ImGui::TextColored(ImVec4(1.f, 0.5f, 0.5f, 1.f), "%s", status.lastError.c_str());
    }

    // Following somebody into a mission. First thing on the tab, because it is
    // the first thing anybody needs after connecting. All copies: this runs on
    // the render thread and the game thread owns these strings.
    std::string peerScene, peerOwner, published, sceneError;
    int         peerCheckpoint = -1;

    {
        Threading::ReadGuard guard(m_sceneShareLock);

        peerScene      = m_peerScene;
        peerOwner      = m_peerSceneOwner;
        published      = m_publishedScene;
        sceneError     = m_sceneError;
        peerCheckpoint = m_peerSceneCheckpoint;
    }

    if (m_session.IsActive() && !peerScene.empty() && published != peerScene)
    {
        ImGui::SeparatorText("They are somewhere else");

        ImGui::TextWrapped("%s is in a level you are not in.",
                           peerOwner.empty() ? "Somebody" : peerOwner.c_str());

        ImGui::TextDisabled("%s", peerScene.c_str());

        const Game::SceneSync::Stage stage = Game::SceneSync::CurrentStage();

        // Committed counts as loading too: the engine is building the level
        // and a second press would tear the mount out from under it.
        const bool loading = m_sceneLoadPending
                          || stage == Game::SceneSync::Stage::Streaming
                          || stage == Game::SceneSync::Stage::Ready
                          || stage == Game::SceneSync::Stage::Committed;

        ImGui::BeginDisabled(loading);

        if (ImGui::Button("Go there", ImVec2(180.f, 0.f)))
        {
            // Asked for here, done on the game thread. This button runs inside
            // Present, and a scene load tears down and rebuilds the world -
            // doing that mid-frame with the renderer's state live is what
            // crashed the first person who pressed it.
            {
                Threading::WriteGuard guard(m_sceneShareLock);

                m_sceneLoadRequest    = peerScene;
                m_sceneLoadCheckpoint = peerCheckpoint;
                m_sceneLoadPending    = true;

                m_sceneError.clear();
            }

            AddLogLine(std::format("Going to {}", peerScene));
        }

        ImGui::EndDisabled();

        ImGui::SameLine();

        if (loading)
        {
            // The level streams in over several seconds and the game keeps
            // running the whole time, so without this the button just goes
            // grey and nothing appears to happen.
            ImGui::TextDisabled("%s", Game::SceneSync::StatusLine());
        }
        else
        {
            ImGui::TextDisabled("this restarts you into their mission");
        }

        if (!sceneError.empty())
        {
            ImGui::TextColored(ImVec4(1.f, 0.45f, 0.45f, 1.f), "%s", sceneError.c_str());
        }
    }

    ImGui::SeparatorText("You");
    ImGui::InputText("Name", m_playerName, sizeof(m_playerName));
    ImGui::InputText("Password", m_password, sizeof(m_password), ImGuiInputTextFlags_Password);

    ImGui::SeparatorText("Host");
    ImGui::InputInt("Port", &m_hostPort);

    m_hostPort = std::clamp(m_hostPort, 1024, 65535);

    ImGui::BeginDisabled(m_session.IsActive());

    if (ImGui::Button("Host a session", ImVec2(180.f, 0.f)))
    {
        if (m_session.Host(static_cast<uint16_t>(m_hostPort), m_playerName, m_password))
        {
            m_progression.ResetForNewSession();
            Game::TheDownedState().ResetForNewSession();
            AddLogLine(std::format("Hosting on UDP {}", m_hostPort));

            if (m_useUpnp)
            {
                m_portMapper.RequestMap(static_cast<uint16_t>(m_hostPort));
            }
        }
    }

    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::Checkbox("Open the port for me", &m_useUpnp);

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Asks the router over UPnP, which saves finding the port forwarding\n"
            "page and getting the protocol right - that field defaults to TCP and\n"
            "every byte here is UDP. Closed again when you leave.\n\n"
            "If the router says no, hosting still works on a LAN or a VPN.");
    }

    switch (m_portMapper.GetState())
    {
    case Net::PortMapper::State::Working:
        ImGui::TextColored(ImVec4(1.f, 0.80f, 0.35f, 1.f), "%s", m_portMapper.Note().c_str());
        break;

    case Net::PortMapper::State::Mapped:
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.f), "%s", m_portMapper.Note().c_str());
        break;

    case Net::PortMapper::State::Failed:
        ImGui::TextColored(ImVec4(1.f, 0.55f, 0.45f, 1.f), "%s", m_portMapper.Note().c_str());
        break;

    default:
        break;
    }

    const std::string external = m_portMapper.ExternalAddress();

    if (!external.empty())
    {
        ImGui::Text("Give them this:");
        ImGui::SameLine();
        ImGui::InputText("##external", const_cast<char*>(external.c_str()),
                         external.size() + 1, ImGuiInputTextFlags_ReadOnly);
    }

    ImGui::SeparatorText("Join");
    ImGui::InputText("Host address", m_hostAddress, sizeof(m_hostAddress));

    ImGui::BeginDisabled(m_session.IsActive());

    if (ImGui::Button("Join", ImVec2(180.f, 0.f)))
    {
        if (m_session.Join(m_hostAddress, static_cast<uint16_t>(m_hostPort), m_playerName, m_password))
        {
            m_progression.ResetForNewSession();
            Game::TheDownedState().ResetForNewSession();
            AddLogLine(std::format("Connecting to {}", m_hostAddress));
        }
    }

    ImGui::EndDisabled();

    if (m_session.IsActive())
    {
        ImGui::SameLine();

        if (ImGui::Button("Leave", ImVec2(120.f, 0.f)))
        {
            m_session.Leave();
            m_portMapper.Release();

            AddLogLine("Left the session");
        }
    }

    ImGui::SeparatorText("Say something");

    const bool submitted = ImGui::InputText("##chat", m_chatInput, sizeof(m_chatInput),
                                            ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::SameLine();

    if ((ImGui::Button("Send") || submitted) && m_chatInput[0] != '\0')
    {
        Net::EventMessage message;
        message.type = Net::EventType::Chat;
        message.text = m_chatInput;

        m_session.SendEvent(message);
        AddLogLine(std::format("you: {}", m_chatInput));

        m_chatInput[0] = '\0';
    }
}

void CoopMod::RenderPlayersTab()
{
    const std::vector<Game::AvatarView>& views = m_avatars.Views();

    if (views.empty())
    {
        ImGui::TextDisabled("Nobody else here yet.");
    }
    else if (ImGui::BeginTable("peers", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders))
    {
        ImGui::TableSetupColumn("Player");
        ImGui::TableSetupColumn("Chapter");
        ImGui::TableSetupColumn("Distance");
        ImGui::TableSetupColumn("Ping");
        ImGui::TableSetupColumn("State");
        ImGui::TableHeadersRow();

        for (const Game::AvatarView& view : views)
        {
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Text("%s", view.name.c_str());

            ImGui::TableNextColumn();

            if (view.level == 0xFF)
            {
                ImGui::TextDisabled("unknown");
            }
            else
            {
                ImGui::Text("%u.%u", view.level, view.section);
            }

            ImGui::TableNextColumn();

            if (view.sameArea)
            {
                ImGui::Text("%.0f m", view.distance);
            }
            else
            {
                ImGui::TextDisabled("-");
            }

            ImGui::TableNextColumn();
            ImGui::Text("%u ms", view.pingMs);

            ImGui::TableNextColumn();
            ImGui::Text("%s", view.stale   ? "no signal"
                            : view.loading ? "loading"
                            : view.dead    ? "down"
                            : view.running ? "running"
                                           : "ok");
        }

        ImGui::EndTable();
    }

    Game::AvatarSettings& settings = m_avatars.Settings();

    ImGui::SeparatorText("How they are drawn");

    ImGui::Checkbox("In the world", &settings.drawInWorld);
    ImGui::Checkbox("Nameplates", &settings.drawNameplates);
    ImGui::Checkbox("Marker beam", &settings.drawBeam);
    ImGui::Checkbox("Distance on the nameplate", &settings.drawDistance);
    ImGui::SliderFloat("Out to", &settings.maxDrawDistance, 25.f, 500.f, "%.0f m");
    ImGui::Checkbox("HUD overlay", &m_showOverlay);

    ImGui::SeparatorText("Instinct");

    ImGui::Checkbox("Mark teammates in Instinct", &settings.instinctHighlight);

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Instinct is the game's own see-through-walls view and it already\n"
            "draws points of interest. This puts teammates there too: a diamond\n"
            "over the head, in their colour, at any distance.");
    }

    ImGui::Checkbox("And only there", &settings.instinctOnly);

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Teammates disappear the rest of the time, which is the better game:\n"
            "co-op stops being a permanent wallhack and knowing where the others\n"
            "are costs Instinct, like everything else does.");
    }
}

void CoopMod::RenderRulesTab()
{
    Game::ProgressionSettings progression = m_progression.Settings();

    ImGui::SeparatorText("Moving through the level");

    int follow = static_cast<int>(progression.followPolicy);

    ImGui::RadioButton("Whoever gets there first pulls everyone", &follow, 0);
    ImGui::RadioButton("Ask me first", &follow, 1);
    ImGui::RadioButton("Never move me", &follow, 2);

    progression.followPolicy = static_cast<Game::FollowPolicy>(follow);

    if (progression.followPolicy == Game::FollowPolicy::AutoFollow)
    {
        ImGui::SliderFloat("Grace period", &progression.followGraceSeconds, 0.f, 30.f, "%.0f s");
    }

    ImGui::TextDisabled("Forward only - nobody is ever pulled backwards.");

    int death = static_cast<int>(progression.deathPolicy);

    ImGui::Spacing();
    ImGui::RadioButton("Everyone restarts the section together", &death, 0);
    ImGui::RadioButton("Reload on your own, the others carry on", &death, 1);
    ImGui::RadioButton("Spectate, back in at the next checkpoint", &death, 2);

    progression.deathPolicy = static_cast<Game::DeathPolicy>(death);

    m_progression.SetSettings(progression);

    // ---- Dying ------------------------------------------------------------

    ImGui::SeparatorText("Dying");

    Game::DownedSettings& downed = Game::TheDownedState().Settings();

    ImGui::Checkbox("Intercept damage instead of letting the engine kill you",
                    &downed.enabled);

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Absolution answers a death by reloading a checkpoint, which resets\n"
            "one player's whole world while the others carry on. With this on,\n"
            "the engine is never told: damage is caught first, scored against a\n"
            "pool this mod owns, and running it out puts you on a spectator\n"
            "camera instead of a loading screen.");
    }

    if (!downed.enabled)
    {
        return;
    }

    ImGui::SliderFloat("Health pool", &downed.maxHitPoints, 20.f, 400.f, "%.0f");
    ImGui::SliderFloat("Cost per hit", &downed.fallbackDamagePerHit, 1.f, 100.f, "%.0f");
    ImGui::SliderFloat("Regeneration", &downed.regenPerSecond, 0.f, 60.f, "%.0f /s");
    ImGui::SliderFloat("Regen delay", &downed.regenDelaySeconds, 0.f, 20.f, "%.1f s");
    ImGui::SliderFloat("Get up by yourself after", &downed.selfReviveSeconds, 0.f, 120.f, "%.0f s");
    ImGui::SliderFloat("Immune after getting up", &downed.recoveryGraceSeconds, 0.f, 15.f, "%.1f s");

    if (downed.selfReviveSeconds <= 0.f)
    {
        ImGui::TextColored(ImVec4(1.f, 0.8f, 0.35f, 1.f),
            "At zero you stay down until a teammate reaches a checkpoint.");
    }

    ImGui::Spacing();

    ImGui::Checkbox("Go limp when you go down", &downed.ragdollWhenDown);

    ImGui::Checkbox("And leave the fight", &downed.hideBodyWhenDown);

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "The engine is never told you went down, so the AI has a live hostile\n"
            "lying on the floor and keeps shooting it. This hides the body once\n"
            "the fall has read, and puts it back exactly where it was when you\n"
            "get up.\n\n"
            "Turning the guards' senses off was tried instead and did not work:\n"
            "perception decides whether they find a target, not whether they let\n"
            "one go.");
    }

    if (downed.hideBodyWhenDown)
    {
        ImGui::SliderFloat("Lie there for", &downed.hideAfterSeconds, 0.f, 8.f, "%.1f s");
    }

    ImGui::Checkbox("Also set the engine's god-mode flag", &downed.alsoUseEngineImmunity);

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "The backstop for damage that never reaches the interception: falls,\n"
            "drowning, scripted kills. Without it those still run the engine's\n"
            "death sequence, which reloads a checkpoint.\n\n"
            "Same flag the SDK's Player mod exposes, put back on the way out.\n"
            "Steam only - the address is not known for the GOG build.");
    }

    ImGui::Checkbox("Blind the guards while you are down", &m_suppressPerceptionWhenDown);

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Switches off the detection cone, hearing and the target tracker\n"
            "through the engine's own configuration, and puts every one back.\n\n"
            "It does not stop guards already shooting at you - that is what\n"
            "leaving the fight above is for - but it stops new ones noticing.");
    }

    // ---- Seeing -----------------------------------------------------------

    ImGui::SeparatorText("What you see");

    ImGui::Checkbox("Red screen when you are hurt", &m_showHurt);

    ImGui::Checkbox("Drive the health ring from this pool", &m_driveHudHealth);

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "The ring around the radar reads the engine's health, which cannot\n"
            "move while damage is being intercepted - so it sat at full while\n"
            "this pool emptied. With this on it shows the pool instead.\n\n"
            "It writes one float that only the HUD reads. The engine decides\n"
            "nothing from it, so it cannot make you die or stop you dying.");
    }

    ImGui::Checkbox("Mouse look while spectating", &m_mouseLook);

    if (m_mouseLook)
    {
        ImGui::SliderFloat("Sensitivity", &m_mouseSensitivity, 0.02f, 0.60f, "%.2f deg/px");
        ImGui::SliderFloat("Smoothing", &m_mouseSmoothing, 0.f, 0.95f, "%.2f");
        ImGui::Checkbox("Invert Y", &m_invertMouseY);
    }
}

void CoopMod::RenderDiagnosticsTab()
{
    const Net::SessionStatus status = m_session.Status();
    const Game::BuildInfo&   build  = Game::BuildInfo::Get();
    Game::DownedState&       downed = Game::TheDownedState();

    // Deliberately short. Everything worth knowing is written to mods\Coop.log
    // as it happens, including all of this - the panel cannot be open while the
    // game has input, so anything that only lives here is unreadable by anyone
    // actually playing.
    ImGui::TextWrapped(
        "All of this, and everything that happens while you play, is written to "
        "mods\\Coop.log as it happens. Send that file rather than reading this.");

    ImGui::SeparatorText("Game");
    ImGui::Text("%s", build.Describe().c_str());
    ImGui::Text("Chapter %u.%u, checkpoint %d, %u actors alive",
                m_probe.LocalState().level, m_probe.LocalState().section,
                m_probe.CurrentJumpPoint(), m_probe.AliveActorCount());

    {
        Threading::ReadGuard guard(m_sceneShareLock);

        if (!m_publishedScene.empty())
        {
            ImGui::TextDisabled("%s", m_publishedScene.c_str());
        }
    }

    ImGui::SeparatorText("Mod");

    ImGui::Text("Keys: %s",
                !m_bindingsEnabled  ? "off in the ini"
              : m_bindingsAccepted  ? "registered"
                                    : "REJECTED by the engine");

    ImGui::Text("Damage interception: %s", downed.IsArmed() ? "armed" : "not armed");
    ImGui::TextDisabled("%s", downed.ImmunityNote().c_str());

    ImGui::Text("Health %.0f / %.0f, %u hits, downed %u times",
                downed.HitPoints(), downed.Settings().maxHitPoints,
                downed.HitsTaken(), downed.TimesDowned());

    ImGui::Text("Engine health: %s", m_engineHealth.Note().c_str());

    ImGui::SeparatorText("Network");
    ImGui::Text("Peer %u on port %u, %u ms to the host",
                status.localPeerId, status.boundPort, status.hostRoundTripMs);
    ImGui::Text("Sent %llu, received %llu, dropped %llu",
                static_cast<unsigned long long>(status.packetsSent),
                static_cast<unsigned long long>(status.packetsReceived),
                static_cast<unsigned long long>(status.packetsDropped));

    ImGui::Text("Frame cost: %.0f us average, %.0f us worst",
                m_cost.AverageMicros(), m_cost.WorstMicros());

    ImGui::SeparatorText("Write a dump");

    if (ImGui::Button("Dump now", ImVec2(150.f, 0.f)))
    {
        // Raised here, written on the game thread. The dump walks the whole
        // configuration chain and reads engine memory, and this button runs
        // inside Present - the crash the friend hit on the first joint test
        // was exactly this collision.
        m_dumpRequested = true;
    }

    ImGui::SameLine();
    ImGui::TextDisabled("one is written by itself six seconds into every level");

    {
        std::string lastDumpPath;

        {
            Threading::ReadGuard guard(m_logLock);

            lastDumpPath = m_lastDumpPath;
        }

        if (!lastDumpPath.empty())
        {
            ImGui::TextDisabled("%s", lastDumpPath.c_str());
        }
    }

    // The one research probe still worth a button: it is the only way to find
    // where the engine keeps the health, and it needs a deliberate hit.
    ImGui::SeparatorText("Find the engine's health field");

    ImGui::TextWrapped(
        "Snapshots the player, lets exactly one hit reach the engine, and logs "
        "which bytes moved. That hit is real and can kill you.");

    ImGui::BeginDisabled(m_awaitingPassThrough || !downed.IsArmed());

    if (ImGui::Button("Let the next hit through", ImVec2(220.f, 0.f)))
    {
        m_playerBefore.assign(kPlayerSnapshotBytes, 0);

        const char* note = nullptr;

        if (downed.SnapshotPlayer(m_playerBefore.data(), kPlayerSnapshotBytes) == 0)
        {
            note = "could not read the player object - not arming";
        }
        else
        {
            downed.ArmPassThrough(1);

            m_awaitingPassThrough = true;
            m_passThroughWaited   = 0.f;

            note = "armed - go and get shot once";
        }

        {
            Threading::WriteGuard guard(m_logLock);

            m_playerDiffNote = note;
        }

        AddLogLine(note);
    }

    ImGui::EndDisabled();

    if (m_awaitingPassThrough)
    {
        ImGui::SameLine();

        if (ImGui::Button("Cancel"))
        {
            downed.ArmPassThrough(0);

            m_awaitingPassThrough = false;

            Threading::WriteGuard guard(m_logLock);

            m_playerDiffNote = "disarmed";
        }
    }

    {
        std::string diffNote;

        {
            Threading::ReadGuard guard(m_logLock);

            diffNote = m_playerDiffNote;
        }

        if (!diffNote.empty())
        {
            ImGui::TextDisabled("%s", diffNote.c_str());
        }
    }

    ImGui::SeparatorText("Log");

    if (ImGui::BeginChild("log", ImVec2(0.f, 140.f), true))
    {
        Threading::ReadGuard guard(m_logLock);

        for (const std::string& line : m_log)
        {
            ImGui::TextWrapped("%s", line.c_str());
        }

        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.f)
        {
            ImGui::SetScrollHereY(1.f);
        }
    }

    ImGui::EndChild();
}

DEFINE_MOD(CoopMod);
