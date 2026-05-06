#pragma once

#include <chrono>

#include <Glacier/ZGameLoopManager.h>
#include <Glacier/Input/ZInputAction.h>
#include <Glacier/Math/float4.h>

class SpeedrunOverlay
{
public:
    void OnEngineInitialized();
    void OnFrameUpdate(const SGameUpdateEvent& updateEvent);
    void OnDrawUI(bool hasFocus);

    // Called by SpeedrunToolkit when the player entity is available each frame.
    void SetPlayerPosition(const float4& pos) { m_currentPos = pos; m_hasPlayerPos = true; }

    // Rendered inside the parent window's "Overlay" tab.
    void RenderSettingsPanel();

private:
    void RenderOverlayWindow();

    static void FormatTime(double seconds, char* buf, size_t bufSize);

    // Timer
    bool   m_timerRunning      = false;
    bool   m_wasLoading        = false;
    double m_accumulatedSecs   = 0.0;
    std::chrono::steady_clock::time_point m_segmentStart;
    std::chrono::steady_clock::time_point m_lastFrameTime;

    // Position / speed
    float4 m_currentPos{};
    float4 m_lastPos{};
    bool   m_hasPlayerPos      = false;
    float  m_speed             = 0.f;

    // Display config
    bool m_overlayVisible       = true;
    bool m_showTimer            = true;
    bool m_showPosition         = true;
    bool m_showSpeed            = true;
    bool m_showChapter          = true;
    bool m_showLoadingIndicator = true;
    bool m_autoPauseDuringLoad  = true;

    // Keybinds (names registered by SpeedrunToolkit::OnEngineInitialized)
    ZInputAction m_timerStartStopAction;
    ZInputAction m_timerResetAction;

    // Rising-edge state for one-shot key detection
    bool m_prevStartStop = false;
    bool m_prevReset     = false;
};
