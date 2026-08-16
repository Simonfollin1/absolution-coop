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

CoopMod::CoopMod() = default;

CoopMod::~CoopMod()
{
    // Order matters on the way out. The session's thread has to stop before
    // anything it touches goes away, and the vtable patch has to be undone
    // before this DLL unloads - a patched entry pointing into unmapped memory
    // is a crash with no stack to explain it.
    m_session.Leave();
    m_spectator.Leave();
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

    m_log.push_back(line);

    while (m_log.size() > kLogCapacity)
    {
        m_log.pop_front();
    }
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
    const Diag::FrameCostScope costScope(m_cost);

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

    m_probe.Update(updateEvent);

    // Real time, not game time: co-op has to keep running while one player is
    // sitting in a pause menu. ZGameTime holds ticks and converts on request -
    // it does not implicitly become a float.
    const float deltaSeconds = static_cast<float>(updateEvent.m_RealTimeDelta.ToSeconds());

    UpdateSceneTransition();
    UpdateDownedFlow(deltaSeconds);

    // Markers fade on their own clock, and should keep fading after a session
    // ends rather than hanging in the world.
    m_avatars.TickMarkers(deltaSeconds);

    // Both of these used to sit below the session check, which meant the marker
    // key and the kill feed did nothing at all until somebody else connected -
    // including while you were testing the mod on your own, where the whole
    // question is whether the keys work.
    UpdateMarkerKey();
    UpdateKillFeed();

    // Instinct drives how teammates are drawn, so it is read before anything
    // draws and handed across rather than read from the render thread.
    m_instinct.Update(deltaSeconds);
    m_avatars.SetInstinct(m_instinct.Active());

    TraceWorldChanges();
    TraceKeys();
    AutoDumpWhenReady(deltaSeconds);

    m_passThroughWaited += deltaSeconds;
    UpdatePlayerDiff();

    if (!m_session.IsActive())
    {
        return;
    }

    PublishLocalState();
    PumpEvents();

    // Local progress, turned into something the others can act on.
    Net::EventMessage localEvent;

    if (m_progression.ObserveLocal(m_probe.CurrentJumpPoint(),
                                  m_probe.LocalState().level,
                                  m_probe.HasPlayer(),
                                  (m_probe.LocalState().flags & Net::SF_Dead) != 0,
                                  localEvent))
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

    // Where everyone is, resolved for drawing.
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

    const uint8_t phase = static_cast<uint8_t>(Game::TheDownedState().Phase());

    if (phase != m_tracedPhase)
    {
        Diag::Log("player is now %s (health %.0f, %u hits, downed %u times)",
                  phase == 1 ? "DOWN" : phase == 2 ? "up again, briefly immune" : "up",
                  Game::TheDownedState().HitPoints(),
                  Game::TheDownedState().HitsTaken(),
                  Game::TheDownedState().TimesDowned());

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

    std::string error;

    const std::string path = Game::WriteDump(
        m_configVars, m_probe, m_bindingExpression,
        std::vector<std::string>(m_log.begin(), m_log.end()), error);

    if (path.empty())
    {
        Diag::Log("automatic dump failed: %s", error.c_str());
        AddLogLine(std::format("Automatic dump failed: {}", error));

        return;
    }

    m_lastDumpPath = path;

    Diag::Log("automatic dump written to %s", path.c_str());
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
            m_playerDiffNote      = "nothing hit you within a minute - disarmed";

            AddLogLine(m_playerDiffNote);
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
        m_playerDiffNote = "could not read the player object on both sides of the hit";
        AddLogLine(m_playerDiffNote);

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

    m_playerDiffNote = std::format("{} dwords changed across one hit", m_playerDeltas.size());

    AddLogLine(m_playerDiffNote);

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
    case Net::EventType::PlayerLeft:        return std::format("{} left", text.empty() ? name : text);
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

        // Watch a teammate in this level if there is one, otherwise our own
        // body, which is at least somewhere the player recognises.
        SVector3 target(m_probe.PlayerPosition().x,
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
    m_avatars.Draw3D();
}

void CoopMod::OnDrawUI(const bool hasFocus)
{
    m_loaded.OnDrawUI(hasFocus);

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

void CoopMod::RenderHurtOverlay()
{
    Game::DownedState& downed = Game::TheDownedState();

    if (!m_showHurt || !downed.Settings().enabled || !downed.IsArmed())
    {
        return;
    }

    // Absolution does not draw a health bar. Damage is a red vignette that
    // closes in from the edges and a desaturation of everything inside it, and
    // health coming back is that receding - so when this mod takes the health
    // away from the engine, it takes the only feedback the player had with it.
    // Which is exactly what happened: the health went down in a panel nobody
    // had open while the screen stayed clean.
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

    const Game::PendingJump& pending = m_progression.Pending();
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

        if (ImGui::BeginTabItem("Engine"))
        {
            RenderEngineTab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Research"))
        {
            RenderResearchTab();
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

    ImGui::Spacing();
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
        }
    }

    ImGui::EndDisabled();

    ImGui::TextDisabled("Only the host needs a reachable port. Forward UDP %d,", m_hostPort);
    ImGui::TextDisabled("or put everyone on the same LAN or VPN.");

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
        ImGui::Spacing();

        if (ImGui::Button("Leave", ImVec2(180.f, 0.f)))
        {
            m_session.Leave();
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

    ImGui::SeparatorText("How you appear to them");

    Game::AvatarSettings& settings = m_avatars.Settings();

    ImGui::Checkbox("Draw teammates in the world", &settings.drawInWorld);
    ImGui::Checkbox("Nameplates", &settings.drawNameplates);
    ImGui::Checkbox("Marker beam", &settings.drawBeam);
    ImGui::Checkbox("Distance on the nameplate", &settings.drawDistance);
    ImGui::SliderFloat("Draw out to", &settings.maxDrawDistance, 25.f, 500.f, "%.0f m");
    ImGui::Checkbox("HUD overlay", &m_showOverlay);

    ImGui::SeparatorText("Instinct");

    ImGui::Checkbox("Mark teammates in Instinct", &settings.instinctHighlight);

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Instinct is the game's own see-through-walls view and it already\n"
            "draws points of interest in it. This puts teammates there too: a\n"
            "diamond over the head, in their colour, at any distance.");
    }

    ImGui::Checkbox("And only there", &settings.instinctOnly);

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Teammates disappear the rest of the time. This is the better game -\n"
            "co-op stops being a permanent wallhack and knowing where the others\n"
            "are costs you Instinct, like everything else does.\n\n"
            "Off by default because detecting Instinct means reading the focus\n"
            "controller, and nobody has confirmed that reading yet. If the state\n"
            "below never says 'held' while you hold it, leave this off.");
    }

    ImGui::Text("Instinct: %s",
                !m_instinct.Readable() ? "cannot read the focus controller"
              : m_instinct.Active()    ? "held"
                                       : "not held");

    if (m_instinct.Readable())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(meter %.2f)", m_instinct.Amount());
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
        ImGui::TextDisabled("Long enough to finish a fight before you are moved.");
    }

    ImGui::TextDisabled("Forward only. Nobody is ever pulled backwards - that");
    ImGui::TextDisabled("would delete progress somebody made.");

    ImGui::SeparatorText("Dying");

    int death = static_cast<int>(progression.deathPolicy);

    ImGui::RadioButton("Everyone restarts the section together", &death, 0);
    ImGui::RadioButton("Reload on your own, the others carry on", &death, 1);
    ImGui::RadioButton("Spectate, back in at the next checkpoint", &death, 2);

    progression.deathPolicy = static_cast<Game::DeathPolicy>(death);

    m_progression.SetSettings(progression);

    ImGui::Spacing();

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

    if (downed.enabled)
    {
        ImGui::SliderFloat("Health pool", &downed.maxHitPoints, 20.f, 400.f, "%.0f");
        ImGui::SliderFloat("Cost per hit", &downed.fallbackDamagePerHit, 1.f, 100.f, "%.0f");
        ImGui::SliderFloat("Regeneration", &downed.regenPerSecond, 0.f, 60.f, "%.0f /s");
        ImGui::SliderFloat("Regen delay", &downed.regenDelaySeconds, 0.f, 20.f, "%.1f s");
        ImGui::SliderFloat("Get up by yourself after", &downed.selfReviveSeconds,
                           0.f, 120.f, "%.0f s");
        ImGui::SliderFloat("Immune after getting up", &downed.recoveryGraceSeconds,
                           0.f, 15.f, "%.1f s");

        ImGui::Checkbox("Go limp when you go down", &downed.ragdollWhenDown);

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "Hands the body to physics the way the engine does on a real\n"
                "death, so 47 falls where he stood instead of standing there at\n"
                "zero health looking fine. Getting up blends back to animation.\n\n"
                "If it ever leaves you stuck on the floor, turn it off - and the\n"
                "log will say exactly which calls ran before it happened.");
        }

        ImGui::Checkbox("Also set the engine's god-mode flag",
                        &downed.alsoUseEngineImmunity);

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "The backstop for damage that never reaches the interception:\n"
                "falls, drowning, scripted kills. Without it those still run the\n"
                "engine's death sequence, which reloads a checkpoint and resets\n"
                "your whole copy of the level while everyone else carries on.\n\n"
                "It is the same flag the SDK's Player mod exposes, and it is put\n"
                "back the way it was when the mod lets go. Steam only - the\n"
                "address is not known for the GOG build.");
        }

        ImGui::TextDisabled("%s", Game::TheDownedState().ImmunityNote().c_str());

        if (downed.selfReviveSeconds <= 0.f)
        {
            ImGui::TextColored(ImVec4(1.f, 0.8f, 0.35f, 1.f),
                "At zero you stay down until a teammate reaches a checkpoint or");
            ImGui::TextColored(ImVec4(1.f, 0.8f, 0.35f, 1.f),
                "the level changes. Alone, that never happens.");
        }

        ImGui::TextDisabled("Every hit costs the same: the real figure lives behind");
        ImGui::TextDisabled("SHitInfo::GetBaseDamage, which has no known address yet.");
    }

    ImGui::SeparatorText("Being shot at, and watching");

    ImGui::Checkbox("Red screen when you are hurt", &m_showHurt);

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Absolution has no health bar - damage is a red vignette closing in\n"
            "from the edges. Taking the health off the engine took that with it,\n"
            "so this draws it back from the pool the mod owns. Fake, and the\n"
            "only feedback there is.");
    }

    ImGui::Checkbox("Mouse look while spectating", &m_mouseLook);

    if (m_mouseLook)
    {
        ImGui::SliderFloat("Sensitivity", &m_mouseSensitivity, 0.02f, 0.60f, "%.2f deg/px");
        ImGui::SliderFloat("Smoothing", &m_mouseSmoothing, 0.f, 0.95f, "%.2f");
        ImGui::Checkbox("Invert Y", &m_invertMouseY);

        ImGui::TextDisabled("Arrow keys still work, and PgUp/PgDn move in and out.");
    }
}

void CoopMod::RenderEngineTab()
{
    ImGui::TextWrapped(
        "Every configuration variable the engine has registered, read straight "
        "off ZConfigCommand's list. No offsets and no hooks, so this works the "
        "same on Steam and GOG.");

    ImGui::Spacing();

    if (ImGui::Button("Read them", ImVec2(150.f, 0.f)))
    {
        m_configVars.Refresh();
        AddLogLine(m_configVars.Diagnostic());
    }

    ImGui::SameLine();
    ImGui::TextDisabled("%s", m_configVars.Diagnostic().c_str());

    if (!m_configVars.Walked())
    {
        return;
    }

    ImGui::Spacing();
    ImGui::InputText("Filter", m_configFilter, sizeof(m_configFilter));

    const std::vector<const Game::ConfigVar*> matches = m_configVars.Find(m_configFilter);

    ImGui::TextDisabled("%zu of %zu", matches.size(), m_configVars.All().size());

    if (ImGui::BeginChild("cvars", ImVec2(0.f, 260.f), true))
    {
        if (ImGui::BeginTable("cvartable", 3,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY))
        {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70.f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 110.f);
            ImGui::TableHeadersRow();

            for (const Game::ConfigVar* entry : matches)
            {
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text("%s", entry->name.c_str());

                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s",
                    entry->type == Game::ConfigVarType::Float  ? "float"
                  : entry->type == Game::ConfigVarType::Int    ? "int"
                  : entry->type == Game::ConfigVarType::String ? "string"
                                                               : "?");

                ImGui::TableNextColumn();
                ImGui::Text("%s", entry->ValueText().c_str());
            }

            ImGui::EndTable();
        }
    }

    ImGui::EndChild();
}

void CoopMod::RenderResearchTab()
{
    Game::HitInspector& hits   = Game::TheHitInspector();
    Game::DownedState&  downed = Game::TheDownedState();

    ImGui::TextWrapped(
        "Things only a running game can answer. None of this changes how the "
        "mod plays except where it says so.");

    ImGui::Spacing();
    ImGui::TextWrapped(
        "All of it is written to two files by itself, so there is nothing here "
        "worth photographing: mods\\Coop.log is the timeline - every hit with "
        "its bytes, every key going down, every checkpoint, every death - and "
        "coop-dump-NNN.txt next to HMA.exe holds the facts that do not change, "
        "written six seconds into the first level without anybody pressing "
        "anything.");

    // ---- The dump ---------------------------------------------------------

    ImGui::SeparatorText("Write the facts out again");

    if (ImGui::Button("Dump now", ImVec2(150.f, 0.f)))
    {
        m_lastDumpPath = Game::WriteDump(m_configVars, m_probe, m_bindingExpression,
                                         std::vector<std::string>(m_log.begin(), m_log.end()),
                                         m_lastDumpError);

        AddLogLine(m_lastDumpPath.empty()
            ? std::format("Dump failed: {}", m_lastDumpError)
            : std::format("Wrote {}", m_lastDumpPath));
    }

    ImGui::SameLine();
    ImGui::TextDisabled("only needed if you want a second one, later in the level");

    if (!m_lastDumpPath.empty())
    {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.f), "%s", m_lastDumpPath.c_str());
    }
    else if (!m_lastDumpError.empty())
    {
        ImGui::TextColored(ImVec4(1.f, 0.45f, 0.45f, 1.f), "%s", m_lastDumpError.c_str());
    }

    // ---- Hits -------------------------------------------------------------

    ImGui::SeparatorText("What is in a hit");

    ImGui::TextWrapped(
        "Every intercepted hit is captured whole. The damage figure is behind "
        "an accessor with no known address, but the struct it reads from "
        "arrives here by reference - so the slots that differ between a pistol "
        "and a shotgun are worth more than the accessor.");

    bool capturing = hits.Enabled();

    if (ImGui::Checkbox("Capture hits", &capturing))
    {
        hits.SetEnabled(capturing);
    }

    ImGui::SameLine();

    if (ImGui::Button("Clear"))
    {
        hits.Clear();
    }

    ImGui::SameLine();
    ImGui::Text("%u seen, %zu kept", hits.Total(), hits.Kept());

    const std::vector<std::pair<uintptr_t, uint32_t>> callers = hits.Callers();

    if (!callers.empty())
    {
        ImGui::Spacing();
        ImGui::TextDisabled("Called from (these are where the damage is computed):");

        for (const auto& [rva, count] : callers)
        {
            ImGui::Text("  HMA.exe + %08X   %u hits", static_cast<unsigned>(rva), count);
        }
    }

    if (hits.Kept() > 0 &&
        ImGui::BeginTable("hitslots", 3,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY,
            ImVec2(0.f, 200.f)))
    {
        ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 70.f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60.f);
        ImGui::TableSetupColumn("Values seen (hex, float, int)");
        ImGui::TableHeadersRow();

        constexpr size_t kDwords = Game::kHitCaptureBytes / sizeof(uint32_t);

        for (size_t i = 0; i < kDwords; ++i)
        {
            const std::vector<uint32_t> values = hits.ValuesAt(i);

            if (values.empty())
            {
                continue;
            }

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Text("+0x%02zX", i * sizeof(uint32_t));

            ImGui::TableNextColumn();

            if (values.size() > 1)
            {
                ImGui::TextColored(ImVec4(1.f, 0.82f, 0.35f, 1.f), "varies");
            }
            else
            {
                ImGui::TextDisabled("same");
            }

            ImGui::TableNextColumn();

            std::string line;

            for (size_t v = 0; v < values.size() && v < 4; ++v)
            {
                line += Game::DescribeDword(values[v]);
                line += "   ";
            }

            ImGui::Text("%s", line.c_str());
        }

        ImGui::EndTable();
    }

    // ---- The one-hit probe ------------------------------------------------

    ImGui::SeparatorText("Where the engine keeps your health");

    ImGui::TextWrapped(
        "The mod carries its own health pool because the engine's is at an "
        "offset nobody has found. This is how to find it: snapshot the player "
        "object, let exactly one hit reach the engine, and see what moved.");

    ImGui::TextColored(ImVec4(1.f, 0.82f, 0.35f, 1.f),
        "That hit is real. It can kill you, and dying reloads a checkpoint.");
    ImGui::TextColored(ImVec4(1.f, 0.82f, 0.35f, 1.f),
        "Do it at full health, somewhere you do not mind losing.");

    ImGui::BeginDisabled(m_awaitingPassThrough || !downed.IsArmed());

    if (ImGui::Button("Let the next hit through", ImVec2(220.f, 0.f)))
    {
        m_playerBefore.assign(kPlayerSnapshotBytes, 0);

        if (downed.SnapshotPlayer(m_playerBefore.data(), kPlayerSnapshotBytes) == 0)
        {
            m_playerDiffNote = "could not read the player object - not arming";
        }
        else
        {
            downed.ArmPassThrough(1);

            m_awaitingPassThrough = true;
            m_passThroughWaited   = 0.f;
            m_playerDiffNote      = "armed - go and get shot once";
        }

        AddLogLine(m_playerDiffNote);
    }

    ImGui::EndDisabled();

    if (m_awaitingPassThrough)
    {
        ImGui::SameLine();

        if (ImGui::Button("Cancel"))
        {
            downed.ArmPassThrough(0);

            m_awaitingPassThrough = false;
            m_playerDiffNote      = "disarmed";
        }
    }

    if (!downed.IsArmed())
    {
        ImGui::TextDisabled("Interception is not armed, so there is nothing to let through.");
    }

    if (!m_playerDiffNote.empty())
    {
        ImGui::Text("%s", m_playerDiffNote.c_str());
    }

    if (!m_playerDeltas.empty() &&
        ImGui::BeginTable("playerdiff", 3,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY,
            ImVec2(0.f, 180.f)))
    {
        ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 80.f);
        ImGui::TableSetupColumn("Before");
        ImGui::TableSetupColumn("After");
        ImGui::TableHeadersRow();

        for (const PlayerDelta& delta : m_playerDeltas)
        {
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Text("+0x%03zX", delta.offset);

            ImGui::TableNextColumn();
            ImGui::Text("%s", Game::DescribeDword(delta.before).c_str());

            ImGui::TableNextColumn();
            ImGui::Text("%s", Game::DescribeDword(delta.after).c_str());
        }

        ImGui::EndTable();

        ImGui::TextDisabled("A float that dropped by a plausible amount is the");
        ImGui::TextDisabled("health. The whole list is also in mods\\Coop.log.");
    }

    // ---- Live key state ---------------------------------------------------

    ImGui::SeparatorText("Are the keys reaching us");

    ImGui::TextWrapped(
        "Live. Every one of these reads false while this panel has focus, "
        "because the SDK switches the game's input off while it holds the "
        "keyboard - so this is only meaningful with the panel closed, which "
        "means the log below is the part to read afterwards.");

    const struct { const char* name; ZInputAction* action; } watched[] = {
        { "toggle",   &m_toggleAction },
        { "follow",   &m_followAction },
        { "marker",   &m_markerAction },
        { "spec <",   &m_specLeft     },
        { "spec >",   &m_specRight    },
        { "spec ^",   &m_specUp       },
        { "spec v",   &m_specDown     },
        { "spec in",  &m_specCloser   },
        { "spec out", &m_specFurther  },
    };

    for (const auto& entry : watched)
    {
        const bool down = entry.action->Digital();

        ImGui::TextColored(down ? ImVec4(0.45f, 0.85f, 0.45f, 1.f)
                                : ImVec4(0.55f, 0.55f, 0.55f, 1.f),
                           "%-9s %s", entry.name, down ? "DOWN" : "-");
    }
}

void CoopMod::RenderDiagnosticsTab()
{
    const Net::SessionStatus status  = m_session.Status();
    const Game::BuildInfo&   build   = Game::BuildInfo::Get();
    Game::DownedState&       downed  = Game::TheDownedState();

    ImGui::SeparatorText("Game");
    ImGui::Text("%s", build.Describe().c_str());
    ImGui::Text("Chapter %u, section %u, checkpoint %d",
                m_probe.LocalState().level, m_probe.LocalState().section,
                m_probe.CurrentJumpPoint());

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "The engine calls these jump points: numbered restart positions the\n"
            "level defines, one per checkpoint. It is what \"jump to where the\n"
            "others are\" moves you to, and it is the only unit of progress the\n"
            "game exposes to us.\n\n"
            "It only moves when a real checkpoint fires - the on-screen kind.\n"
            "Walking into the next part of the map is not one of those, so\n"
            "seeing 0 while the level clearly moved on is normal.");
    }

    ImGui::SeparatorText("Keys");

    if (!m_bindingsEnabled)
    {
        ImGui::TextColored(ImVec4(1.f, 0.80f, 0.35f, 1.f),
                           "off - EnableBindings = false in mods\\Coop.ini");
    }
    else if (m_bindingsAccepted)
    {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.f), "registered");
    }
    else
    {
        ImGui::TextColored(ImVec4(1.f, 0.45f, 0.45f, 1.f), "the engine did not accept them");
    }

    ImGui::Text("%s panel, %s get up or follow, %s marker, arrows to spectate",
                m_keyToggle.c_str(), m_keyFollow.c_str(), m_keyMarker.c_str());

    ImGui::TextWrapped(
        "None of them do anything while this panel has focus: the SDK switches "
        "the game's input off whenever it holds the keyboard. Close it with the "
        "key left of 1, then press them.");

    if (!m_bindingExpression.empty())
    {
        ImGui::TextDisabled("%s", m_bindingExpression.c_str());
    }

    ImGui::SeparatorText("Actors");
    ImGui::Text("Listed as alive: %u (the engine caps this list at 50), %u flagged dead",
                m_probe.AliveActorCount(), m_probe.DeadFlaggedCount());
    ImGui::Text("Deaths seen: %u by the dead flag, %u by leaving the list",
                m_probe.DeathsByFlag(), m_probe.DeathsByVanish());
    ImGui::TextDisabled("Whether a killed actor stays listed with a flag set or");
    ImGui::TextDisabled("simply leaves is not documented, so both are counted.");

    ImGui::SeparatorText("Damage interception");

    if (downed.IsArmed())
    {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.f), "armed");
    }
    else
    {
        ImGui::TextColored(ImVec4(1.f, 0.80f, 0.35f, 1.f), "not armed");
    }

    ImGui::TextWrapped("%s", downed.Diagnostic().c_str());
    ImGui::TextWrapped("%s", downed.ImmunityNote().c_str());

    ImGui::Text("Health %.0f / %.0f, %u hits taken, downed %u times",
                downed.HitPoints(), downed.Settings().maxHitPoints,
                downed.HitsTaken(), downed.TimesDowned());

    ImGui::Text("State: %s",
                downed.Phase() == Game::DownedPhase::Downed     ? "down"
              : downed.Phase() == Game::DownedPhase::Recovering ? "just up, briefly immune"
                                                                : "up");

    if (downed.IsDowned())
    {
        ImGui::Text("Down for %.0fs, self-revive in %.0fs",
                    downed.DownedSeconds(), downed.SecondsUntilSelfRevive());
    }
    else if (downed.SecondsOfGraceLeft() > 0.f)
    {
        ImGui::Text("Immune for another %.1fs", downed.SecondsOfGraceLeft());
    }

    ImGui::SeparatorText("Network");
    ImGui::Text("Local peer id: %u", status.localPeerId);
    ImGui::Text("Bound port: %u", status.boundPort);
    ImGui::Text("Host round trip: %u ms", status.hostRoundTripMs);
    ImGui::Text("Packets sent %llu, received %llu, dropped %llu",
                static_cast<unsigned long long>(status.packetsSent),
                static_cast<unsigned long long>(status.packetsReceived),
                static_cast<unsigned long long>(status.packetsDropped));

    ImGui::SeparatorText("Cost");
    ImGui::Text("%s: last %.0f us, average %.0f us, worst %.0f us over %u frames",
                m_cost.Label(), m_cost.LastMicros(), m_cost.AverageMicros(),
                m_cost.WorstMicros(), m_cost.TotalFrames());

    ImGui::SeparatorText("Log");

    if (ImGui::BeginChild("log", ImVec2(0.f, 160.f), true))
    {
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
