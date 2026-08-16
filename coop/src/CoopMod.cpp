#include <imgui.h>

#include <algorithm>
#include <format>

#include <Glacier/ZLevelManager.h>
#include <Glacier/Player/ZHitman5.h>
#include <Glacier/ZGameLoopManager.h>
#include <Glacier/Input/ZInputActionManager.h>
#include <Glacier/Math/SVector3.h>
#include <SDK.h>
#include <Global.h>

#include "CoopMod.h"
#include "Game/BuildInfo.h"

using namespace Coop;

namespace
{
    constexpr size_t kLogCapacity = 200;

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
}

void CoopMod::Initialize()
{
    ModInterface::Initialize();
}

void CoopMod::OnEngineInitialized()
{
    ModInterface::OnEngineInitialized();

    const ZMemberDelegate<CoopMod, void(const SGameUpdateEvent&)>
        delegate(this, &CoopMod::OnFrameUpdate);

    GameLoopManager->RegisterForFrameUpdate(delegate, 1);

    // Bindings come from mods/Coop.ini's [Bindings] section.
    // GenerateBindingExpression is protected and unexported, so building the
    // expression here would not link against the SDK DLL - AddBindings is the
    // intended route and the only one available.
    AddBindings();

    m_toggleAction = ZInputAction("CoopToggle");
    m_followAction = ZInputAction("CoopFollow");

    m_session.SetBuildFingerprint(Game::BuildInfo::Get().Fingerprint());

    AddLogLine(Game::BuildInfo::Get().Describe());

    if (!Game::BuildInfo::Get().OffsetsUsable())
    {
        AddLogLine("Chapter tracking is off on this build, so peers cannot tell "
                   "whether they are in the same level. Everything else works.");
    }
}

void CoopMod::AddLogLine(const std::string& line)
{
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
    const bool toggleDown = m_toggleAction.Digital();

    if (toggleDown && !m_prevToggle)
    {
        m_isOpen = !m_isOpen;
    }

    m_prevToggle = toggleDown;

    m_probe.Update(updateEvent);

    // Real time, not game time: co-op has to keep running while one player is
    // sitting in a pause menu.
    const float deltaSeconds = updateEvent.m_RealTimeDelta;

    UpdateSceneTransition();
    UpdateDownedFlow(deltaSeconds);

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

    if (downed.Phase() == Game::DownedPhase::Downed)
    {
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
    if (ImGui::MenuItem("Co-op", "F6", m_isOpen))
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
    RenderHudOverlay();

    if (!m_isOpen || !hasFocus)
    {
        return;
    }

    RenderWindow();
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
            ImGui::TextDisabled("Back in at the next checkpoint");
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

        ImGui::TextDisabled("Every hit costs the same: the real figure lives behind");
        ImGui::TextDisabled("SHitInfo::GetBaseDamage, which has no known address yet.");
    }
}

void CoopMod::RenderDiagnosticsTab()
{
    const Net::SessionStatus status  = m_session.Status();
    const Game::BuildInfo&   build   = Game::BuildInfo::Get();
    Game::DownedState&       downed  = Game::TheDownedState();

    ImGui::SeparatorText("Game");
    ImGui::Text("%s", build.Describe().c_str());
    ImGui::Text("Chapter %u, section %u, jump point %d",
                m_probe.LocalState().level, m_probe.LocalState().section,
                m_probe.CurrentJumpPoint());
    ImGui::Text("Alive actors: %u (engine caps this list at 50)", m_probe.AliveActorCount());

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
    ImGui::Text("Health %.0f / %.0f, %u hits taken, downed %u times",
                downed.HitPoints(), downed.Settings().maxHitPoints,
                downed.HitsTaken(), downed.TimesDowned());

    ImGui::SeparatorText("Network");
    ImGui::Text("Local peer id: %u", status.localPeerId);
    ImGui::Text("Bound port: %u", status.boundPort);
    ImGui::Text("Host round trip: %u ms", status.hostRoundTripMs);
    ImGui::Text("Packets sent %llu, received %llu, dropped %llu",
                static_cast<unsigned long long>(status.packetsSent),
                static_cast<unsigned long long>(status.packetsReceived),
                static_cast<unsigned long long>(status.packetsDropped));

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
