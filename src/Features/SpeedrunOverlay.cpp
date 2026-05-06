#include "Features/SpeedrunOverlay.h"
#include "Memory/GameOffsets.h"

#include <cstdio>
#include <cmath>

#include <imgui.h>

void SpeedrunOverlay::OnEngineInitialized()
{
    m_timerStartStopAction = ZInputAction(GenerateBindingExpression("SRT_TimerStartStop", "F1"));
    m_timerResetAction     = ZInputAction(GenerateBindingExpression("SRT_TimerReset",     "F2"));

    auto now = std::chrono::steady_clock::now();
    m_segmentStart  = now;
    m_lastFrameTime = now;
}

void SpeedrunOverlay::OnFrameUpdate(const SGameUpdateEvent& /*updateEvent*/)
{
    const bool isLoading = GameOffsets::IsLoading();

    auto now = std::chrono::steady_clock::now();
    double frameDelta = std::chrono::duration<double>(now - m_lastFrameTime).count();
    m_lastFrameTime = now;

    if (m_timerStartStopAction.IsTriggered())
    {
        m_timerRunning = !m_timerRunning;
        if (m_timerRunning)
            m_segmentStart = now;
    }

    if (m_timerResetAction.IsTriggered())
    {
        m_timerRunning    = false;
        m_accumulatedSecs = 0.0;
    }

    // Accumulate time, skipping loading screens when auto-pause is enabled.
    if (m_timerRunning && !(m_autoPauseDuringLoad && isLoading))
    {
        double delta = std::chrono::duration<double>(now - m_segmentStart).count();
        m_accumulatedSecs += delta;
        m_segmentStart = now;
    }
    else
    {
        m_segmentStart = now;
    }

    m_wasLoading = isLoading;

    // Speed: distance between this frame and the last, over wall-clock delta.
    if (m_hasPlayerPos && frameDelta > 0.0)
    {
        float dx = m_currentPos.x - m_lastPos.x;
        float dy = m_currentPos.y - m_lastPos.y;
        float dz = m_currentPos.z - m_lastPos.z;
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        m_speed = dist / static_cast<float>(frameDelta);
    }
    m_lastPos = m_currentPos;
}

void SpeedrunOverlay::OnDrawUI(bool /*hasFocus*/)
{
    if (m_overlayVisible)
        RenderOverlayWindow();
}

void SpeedrunOverlay::RenderOverlayWindow()
{
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar      |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoCollapse       |
        ImGuiWindowFlags_NoNav            |
        ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::SetNextWindowBgAlpha(0.72f);
    ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 6.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(4.f, 3.f));

    if (ImGui::Begin("##SRT_Overlay", nullptr, flags))
    {
        const bool isLoading = GameOffsets::IsLoading();

        if (m_showTimer)
        {
            char timeBuf[32];
            FormatTime(m_accumulatedSecs, timeBuf, sizeof(timeBuf));

            ImVec4 timerColor = m_timerRunning
                ? (isLoading && m_autoPauseDuringLoad
                    ? ImVec4(1.f, 0.6f, 0.1f, 1.f)
                    : ImVec4(0.2f, 1.f, 0.3f, 1.f))
                : ImVec4(0.7f, 0.7f, 0.7f, 1.f);

            ImGui::TextColored(timerColor, "SEG  %s", timeBuf);
        }

        if (m_showChapter)
        {
            int level   = GameOffsets::Read<int>(GameOffsets::CURRENT_LEVEL);
            int section = GameOffsets::Read<int>(GameOffsets::CURRENT_SECTION);
            ImGui::Text("LVL  %d   SEC %d", level, section);
        }

        if (m_showPosition && m_hasPlayerPos)
        {
            ImGui::Text("POS  %.1f  %.1f  %.1f",
                m_currentPos.x, m_currentPos.y, m_currentPos.z);
        }

        if (m_showSpeed)
            ImGui::Text("SPD  %.1f u/s", m_speed);

        if (m_showLoadingIndicator && isLoading)
            ImGui::TextColored(ImVec4(1.f, 0.2f, 0.2f, 1.f), "LOADING");
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
}

void SpeedrunOverlay::RenderSettingsPanel()
{
    ImGui::SeparatorText("Overlay");
    ImGui::Checkbox("Show overlay",            &m_overlayVisible);
    ImGui::Checkbox("Timer",                   &m_showTimer);
    ImGui::Checkbox("Position",                &m_showPosition);
    ImGui::Checkbox("Speed",                   &m_showSpeed);
    ImGui::Checkbox("Chapter / Section",       &m_showChapter);
    ImGui::Checkbox("Loading indicator",       &m_showLoadingIndicator);
    ImGui::Checkbox("Auto-pause during loads", &m_autoPauseDuringLoad);

    ImGui::SeparatorText("Timer controls");
    ImGui::Text("Start / Stop:  F1  (configurable in mods.ini)");
    ImGui::Text("Reset:         F2");

    ImGui::Spacing();
    if (ImGui::Button("Start / Stop##btn", ImVec2(-1.f, 0.f)))
    {
        m_timerRunning = !m_timerRunning;
        if (m_timerRunning)
            m_segmentStart = std::chrono::steady_clock::now();
    }
    if (ImGui::Button("Reset##btn", ImVec2(-1.f, 0.f)))
    {
        m_timerRunning    = false;
        m_accumulatedSecs = 0.0;
    }
}

void SpeedrunOverlay::FormatTime(double seconds, char* buf, size_t bufSize)
{
    int    minutes   = static_cast<int>(seconds) / 60;
    double remaining = seconds - minutes * 60.0;
    std::snprintf(buf, bufSize, "%02d:%06.3f", minutes, remaining);
}
