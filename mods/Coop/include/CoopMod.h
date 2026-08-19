#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <string>
#include <thread>
#include <vector>

#include <Glacier/ZGameLoopManager.h>
#include <Glacier/Input/ZInputAction.h>

#include <ModInterface.h>

#include "Net/Session.h"
#include "Net/PortMapper.h"
#include "Game/WorldProbe.h"
#include "Game/Progression.h"
#include "Game/PeerAvatars.h"
#include "Game/DownedState.h"
#include "Game/Spectator.h"
#include "Game/ConfigVars.h"
#include "Game/Instinct.h"
#include "Game/EngineHealth.h"
#include "Game/Perception.h"
#include "Game/SceneSync.h"

#include "UI/LoadedMarker.h"
#include "Diag/FrameCost.h"
#include "Threading/Lock.h"

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
    // The button in Diagnostics does the same on demand, but nobody should have
    // to remember to press it, and nobody should have to photograph a panel to
    // tell somebody else what it said.
    void AutoDumpWhenReady(float deltaSeconds);

    // Logs every key going down and coming up, so "the key does nothing" is
    // answered by the file rather than by watching a row of indicators.
    void TraceKeys();

    // Mouse movement while spectating, added to whatever the keys contributed.
    // Ported from the cinematic camera's free look rather than rewritten -
    // that one is the version people use and it has the details right.
    void MouseLook(float deltaSeconds, float& yawDelta, float& pitchDelta);

    // Writes the mod's health into the field the health ring is drawn from.
    // Called from OnDrawUI rather than the frame update: the game rewrites that
    // field during its own HUD pass, which happens between the two.
    void DriveHudHealth();

    // The red that says you are being shot, drawn from the pool this mod owns
    // rather than from the engine's health, which no longer moves.
    void RenderHurtOverlay();

    void AddLogLine(const std::string& line);

    // A consistent copy for the dump writers, taken under the lock.
    std::vector<std::string> SnapshotLog() const;
    std::string DescribeEvent(const Coop::Net::EventMessage& event) const;
    std::string PeerName(uint8_t peerId) const;

    // Takes back the offer to follow somebody, and says in the log why.
    void ForgetPeerScene(const char* why);

    Coop::Net::Session     m_session;
    Coop::Net::PortMapper  m_portMapper;
    Coop::Game::WorldProbe m_probe;
    Coop::Game::Progression m_progression;
    Coop::Game::PeerAvatars m_avatars;
    Coop::Game::Spectator   m_spectator;
    Coop::Game::ConfigVars  m_configVars;
    Coop::Game::Instinct     m_instinct;
    Coop::Game::EngineHealth m_engineHealth;
    Coop::Game::Perception   m_perception;

    // Blinding and deafening the AI while somebody is down, which is the real
    // answer to being shot at on the floor. The body-hiding below was the
    // workaround for not having found these switches yet.
    bool m_suppressPerceptionWhenDown = true;

    // The same two every mod in this set carries: the line on the main menu
    // that says what is installed, and a per-frame cost the log can answer
    // "does this slow the game down" with.
    LoadedMarker    m_loaded;
    Diag::FrameCost m_cost;

    // UI state
    bool m_isOpen      = false;
    bool m_showOverlay = true;

    bool m_useUpnp = true;

    // Which level we last told everybody we were in, and which one somebody
    // else says they are in. The second is what "go to them" acts on.
    //
    // The strings in this group cross threads: the game thread writes them in
    // PumpEvents and the transition code, and the panel reads them - and the
    // Go there button writes some back - on the render thread inside Present.
    // A std::string reassigned under a reader is a freed pointer in the
    // reader's hands, so every cross-thread touch holds this lock.
    Threading::Lock m_sceneShareLock;
    std::string m_publishedScene;
    float       m_sceneAnnounceIn = 0.f;
    std::string m_peerScene;
    std::string m_peerSceneOwner;
    uint8_t     m_peerSceneOwnerId = Coop::Net::kInvalidPeerId;
    std::string m_sceneError;
    int         m_peerSceneCheckpoint = -1;

    // A load asked for from the panel and carried out on the game thread. The
    // button runs inside Present, and rebuilding the world from there with a
    // frame half drawn is what crashed the first person who pressed it.
    //
    // The flag is atomic because both threads read it bare - the strings stay
    // under the lock.
    std::string       m_sceneLoadRequest;
    int               m_sceneLoadCheckpoint = -1;
    std::atomic<bool> m_sceneLoadPending{ false };

    // Same shape for the dump button: pressed inside Present, written on the
    // game thread. The dump walks sixteen hundred engine config entries and
    // reads engine memory, none of which belongs inside a frame being drawn.
    std::atomic<bool> m_dumpRequested{ false };

    // For noticing the stage flip to Failed, which happens inside
    // SceneSync::Update and returns no error string.
    Coop::Game::SceneSync::Stage m_lastSceneStage = Coop::Game::SceneSync::Stage::Idle;

    // Watching whether the load actually happened. Writing the scene name makes
    // the game claim to be somewhere it is not, so a failed attempt has to be
    // taken back or the mod believes it arrived.
    //
    // Arrival is the player entity being replaced, not just the chapter byte
    // moving: half the campaign's scenes share a chapter with their
    // neighbours (l04b-e, l05a-c...), and on an unrecognised build the byte
    // never moves at all - and a watchdog that cannot see an arrival would
    // tear down a level somebody is standing in.
    std::string m_sceneLoadWasIn;
    uint8_t     m_sceneLoadFromLvl = 0xFF;
    void*       m_sceneLoadPlayerWas = nullptr;
    float       m_sceneLoadWatch   = 0.f;

    // True while a Go there is in flight - armed and waiting for the teardown,
    // committed, or still streaming. Computed once a frame on the game thread;
    // the render thread reads it to keep from drawing peers into a world that
    // is being torn down. While it is set the mod holds NO contact with the
    // world: the damage hook stays disarmed (the downed flow would otherwise
    // re-arm it every frame), the spectator stays out, and the peer avatars are
    // neither updated against the local player nor drawn. Everything re-arms by
    // itself once the new world is up and the stage returns to Idle.
    std::atomic<bool> m_worldContactSuspended{ false };
    bool              m_loadActivePrev = false;

    // How long the level has read as torn down. The latch that says so clears
    // the moment the game-mode object is destroyed, which is the *start* of the
    // teardown, not the end of it - entities and packages are still going away
    // behind it. Loading into that is loading into a level still being taken
    // apart, so the commit waits for the reading to hold still first.
    float m_tornDownFor = 0.f;

    // The stall watchdog.
    //
    // A freeze during a level teardown leaves nothing behind: no crash, no last
    // line, just a log that stops. The game thread stamps a phase as it moves
    // through the frame and bumps a counter at the end of it; this thread
    // samples both, and when the counter stops moving it writes down which
    // phase the game thread died in. That turns "it froze" into a line naming
    // the call, which is the difference between fixing it and guessing again.
    void WatchdogMain();

    std::thread           m_watchdog;
    std::atomic<bool>     m_watchdogRunning{ false };
    std::atomic<uint32_t> m_framePhase{ 0 };
    std::atomic<uint64_t> m_frameCount{ 0 };

    char m_hostAddress[64] = "127.0.0.1:47474";
    char m_playerName[32]  = "Agent";
    char m_password[32]    = "";
    int  m_hostPort        = 47474;
    char m_chatInput[160]  = "";

    // Written by AddLogLine from both threads - the panel's buttons run on
    // the render thread and PumpEvents runs on the game thread - and read by
    // the panel every frame. A deque being pushed while being iterated is heap
    // corruption on a timer, so every touch goes through the lock. The same
    // lock covers the research strings below (m_lastDumpPath, m_lastDumpError,
    // m_playerDiffNote), which cross the same two threads at the same rate.
    Threading::Lock         m_logLock;
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

    // Written on the game thread, shown in the panel on the render thread -
    // so these three strings ride under m_logLock, same as the log they
    // usually land in anyway.
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
    // Writes the mod's pool into the field the health ring is drawn from, so
    // the only health the player can see is the one the mod is actually
    // keeping. On: the write was proven in the game, the ring moved.
    bool     m_driveHudHealth = true;
    bool     m_loggedHudDrive = false;
    int      m_hudDriveFrames = 0;
    float    m_lastRingWrite  = -1.f;

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
