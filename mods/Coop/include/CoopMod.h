#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include <Glacier/ZGameLoopManager.h>
#include <Glacier/Input/ZInputAction.h>

#include <ModInterface.h>

#include "Net/Session.h"
#include "Game/WorldProbe.h"
#include "Game/Progression.h"
#include "Game/PeerAvatars.h"
#include "Game/DownedState.h"
#include "Game/Spectator.h"
#include "Game/ConfigVars.h"
#include "Game/Instinct.h"
#include "Game/EngineHealth.h"

#include "UI/LoadedMarker.h"
#include "Diag/FrameCost.h"

// Absolution Co-op.
//
// Everyone runs their own copy of the level; the mod replicates the players to
// each other and synchronises the parts of the run that matter - who is where,
// who has opened the way ahead, and who is down. That is the same shape every
// co-op mod for a single-player game takes, and it is the only one this engine
// allows: ZLevelManager holds exactly one ZHitman5, and the AI is not
// deterministic, so two machines cannot be made to simulate one world.
//
// What it does not do, and is not pretending to: NPCs, bodies, doors and alarms
// are local to each player. Two people in the same room see the same level and
// different guards.
class CoopMod : public ModInterface
{
public:
    CoopMod();
    ~CoopMod() override;

    void Initialize()            override;
    void OnEngineInitialized()   override;
    void OnDrawMenu()            override;
    void OnDrawUI(bool hasFocus) override;
    void OnDraw3D()              override;

private:
    void OnFrameUpdate(const SGameUpdateEvent& updateEvent);

    // Compiles [Bindings] into one engine binding block and registers it.
    //
    // Not ModInterface::AddBindings, which builds every action as tap(kb,key).
    // A tap is one frame of Digital() per press, which is right for a toggle
    // and useless for the spectator camera - holding an arrow key would nudge
    // the view once and then stop. These need hold(), so the block is built
    // here instead of there.
    void InstallBindings();

    void UpdateMarkerKey();
    void UpdateKillFeed();
    void PublishLocalState();
    void PumpEvents();
    void UpdateSceneTransition();
    void UpdateDownedFlow(float deltaSeconds);

    void RenderWindow();
    void RenderSessionTab();
    void RenderPlayersTab();
    void RenderRulesTab();
    void RenderDiagnosticsTab();
    void RenderEngineTab();
    void RenderResearchTab();
    void RenderHudOverlay();

    // Watches for the things worth having a timestamped record of, and writes
    // them to mods/Coop.log. Everything in here is a question the game answers
    // and nothing else can: when a checkpoint actually fires, whether a killed
    // actor leaves the list, when the player entity is replaced.
    void TraceWorldChanges();

    // The one-hit research probe: snapshots the player object, lets a single
    // hit reach the engine, and reports which bytes moved.
    void UpdatePlayerDiff();

    // Writes the full dump by itself, once, shortly after a level is up.
    //
    // The button on the Research tab does the same thing on demand, but nobody
    // should have to remember to press it - and nobody should have to
    // photograph a panel to tell somebody else what it said.
    void AutoDumpWhenReady(float deltaSeconds);

    // Logs every key going down and coming up, so "the key does nothing" is
    // answered by the file rather than by watching a row of indicators.
    void TraceKeys();

    // Mouse movement while spectating, added to whatever the keys contributed.
    // Ported from the cinematic camera's free look rather than rewritten -
    // that one is the version people use and it has the details right.
    void MouseLook(float deltaSeconds, float& yawDelta, float& pitchDelta);

    // The red that says you are being shot, drawn from the pool this mod owns
    // rather than from the engine's health - which no longer moves.
    //
    // Half the answer. The other half is the health ring around the radar,
    // which is the engine's and still reads full; see Game/ScaleformProbe.h.
    void RenderHurtOverlay();

    void AddLogLine(const std::string& line);
    std::string DescribeEvent(const Coop::Net::EventMessage& event) const;
    std::string PeerName(uint8_t peerId) const;

    Coop::Net::Session     m_session;
    Coop::Game::WorldProbe m_probe;
    Coop::Game::Progression m_progression;
    Coop::Game::PeerAvatars m_avatars;
    Coop::Game::Spectator   m_spectator;
    Coop::Game::ConfigVars  m_configVars;
    Coop::Game::Instinct     m_instinct;
    Coop::Game::EngineHealth m_engineHealth;

    // The same two every mod in this set carries: the line on the main menu
    // that says what is installed, and a per-frame cost the log can answer
    // "does this slow the game down" with.
    LoadedMarker    m_loaded;
    Diag::FrameCost m_cost;

    // UI state
    bool m_isOpen      = false;
    bool m_showOverlay = true;

    char m_hostAddress[64] = "127.0.0.1:47474";
    char m_playerName[32]  = "Agent";
    char m_password[32]    = "";
    int  m_hostPort        = 47474;
    char m_chatInput[160]  = "";
    char m_configFilter[64] = "";

    std::deque<std::string> m_log;

    // Scene-change detection without a hook. CreateScene is hookable, but the
    // SDK's FreeCamera, Editor and Camera mods all already detour it, and
    // MinHook gives one owner per address. Watching for the player entity
    // being replaced costs nothing and cannot collide with anything.
    void*   m_lastPlayerEntity = nullptr;
    uint8_t m_lastLevel        = 0xFF;
    uint8_t m_lastSection      = 0xFF;

    ZInputAction m_toggleAction;
    ZInputAction m_followAction;
    ZInputAction m_markerAction;

    // Orbit controls for the spectator camera. A player who is down should be
    // able to look around rather than stare at one fixed angle.
    ZInputAction m_specLeft;
    ZInputAction m_specRight;
    ZInputAction m_specUp;
    ZInputAction m_specDown;
    ZInputAction m_specCloser;
    ZInputAction m_specFurther;

    bool m_prevToggle = false;
    bool m_prevFollow = false;
    bool m_prevMarker = false;

    // What InstallBindings actually handed the engine, and what it said back.
    // Both go in the diagnostics tab: "my keys do nothing" is otherwise a
    // question nobody can answer from outside the process.
    std::string m_bindingExpression;
    bool        m_bindingsAccepted = false;
    bool        m_bindingsEnabled  = true;

    // The keys as they ended up, for anything that tells the player which key
    // to press. Saying "F7" beats saying "the follow key" when the ini may have
    // made it something else entirely.
    std::string m_keyToggle = "F6";
    std::string m_keyFollow = "F7";
    std::string m_keyMarker = "F8";

    // --- Research ---------------------------------------------------------

    std::string m_lastDumpPath;
    std::string m_lastDumpError;

    // What TraceWorldChanges compares against.
    int      m_tracedJumpPoint = -2;
    uint8_t  m_tracedLevel     = 0xFE;
    uint8_t  m_tracedSection   = 0xFE;
    uint32_t m_tracedHits      = 0;
    bool     m_tracedArmed     = false;
    uint32_t m_tracedDeaths    = 0;
    uint8_t  m_tracedPhase     = 0xFF;
    bool     m_tracedInstinct  = false;
    float    m_tracedEngineHealth = -2.f;
    bool     m_dumpedHealthObject = false;

    // --- Spectator mouse look ---------------------------------------------

    bool  m_mouseLook        = true;
    bool  m_invertMouseY     = false;
    float m_mouseSensitivity = 0.15f;   // degrees per pixel
    float m_mouseSmoothing   = 0.5f;    // fraction of the bank kept per frame

    bool  m_mouseLookActive   = false;
    float m_pendingMouseYaw   = 0.f;
    float m_pendingMousePitch = 0.f;

    // --- Hurt overlay ------------------------------------------------------

    // Rises on every hit and falls on its own, so a hit that costs a quarter of
    // the pool still reads as a hit rather than as a slightly darker edge.
    bool     m_showHurt    = true;
    float    m_hurtFlash   = 0.f;
    uint32_t m_hurtLastHit = 0;

    // One entry per bound action, in the order the binding table declares them.
    bool  m_tracedKeys[9]    = {};
    float m_secondsWithPlayer = 0.f;
    bool  m_autoDumped        = false;

    // The player object, before and after one hit the engine was allowed to
    // see. ZHitman5 is 0xD20 bytes; the whole thing is cheap to keep twice.
    static constexpr size_t kPlayerSnapshotBytes = 0xD20;

    std::vector<uint8_t> m_playerBefore;
    std::vector<uint8_t> m_playerAfter;
    bool                 m_awaitingPassThrough = false;
    float                m_passThroughWaited   = 0.f;

    // Offset -> what it was, what it became. Only the dwords that moved.
    struct PlayerDelta
    {
        size_t   offset = 0;
        uint32_t before = 0;
        uint32_t after  = 0;
    };

    std::vector<PlayerDelta> m_playerDeltas;
    std::string              m_playerDiffNote;
};

DECLARE_MOD(CoopMod)
