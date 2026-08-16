#pragma once

#include <deque>
#include <string>

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
    void RenderHudOverlay();

    // Restyles the shared ImGui context to neutral greys. The SDK's default is
    // a saturated purple, and this mod's panels sit next to the game's own art
    // rather than on a desktop.
    void ApplyTheme();

    void AddLogLine(const std::string& line);
    std::string DescribeEvent(const Coop::Net::EventMessage& event) const;
    std::string PeerName(uint8_t peerId) const;

    Coop::Net::Session     m_session;
    Coop::Game::WorldProbe m_probe;
    Coop::Game::Progression m_progression;
    Coop::Game::PeerAvatars m_avatars;
    Coop::Game::Spectator   m_spectator;
    Coop::Game::ConfigVars  m_configVars;

    // UI state
    bool m_isOpen      = false;
    bool m_showOverlay = true;
    bool m_themeApplied = false;

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
};

DECLARE_MOD(CoopMod)
