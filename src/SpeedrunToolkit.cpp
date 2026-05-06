#include <imgui.h>

#include "SpeedrunToolkit.h"

#include <Glacier/ZGameLoopManager.h>
#include <Glacier/ZLevelManager.h>
#include <Glacier/Render/ZRenderManager.h>
#include <Glacier/Render/IRenderDestinationEntity.h>
#include <Glacier/Render/ZSpatialEntity.h>
#include <Glacier/Camera/ZCameraEntity.h>
#include <Glacier/Input/ZInputActionManager.h>
#include <Glacier/Templates/TEntityRef.h>

SpeedrunToolkit::SpeedrunToolkit() = default;

SpeedrunToolkit::~SpeedrunToolkit()
{
    const ZMemberDelegate<SpeedrunToolkit, void(const SGameUpdateEvent&)>
        delegate(this, &SpeedrunToolkit::OnFrameUpdate);
    GameLoopManager->UnregisterForFrameUpdate(delegate);
}

void SpeedrunToolkit::Initialize()
{
    ModInterface::Initialize();
}

void SpeedrunToolkit::OnEngineInitialized()
{
    ModInterface::OnEngineInitialized();

    const ZMemberDelegate<SpeedrunToolkit, void(const SGameUpdateEvent&)>
        delegate(this, &SpeedrunToolkit::OnFrameUpdate);
    GameLoopManager->RegisterForFrameUpdate(delegate, 1);

    // Build and register all binding expressions.
    // GenerateBindingExpression is a protected ModInterface helper that formats
    // the expression; InputActionManager registers it with the engine.
    std::string bindings;
    bindings += GenerateBindingExpression("SRT_Toggle",         "l");
    bindings += GenerateBindingExpression("SRT_TimerStartStop", "f1");
    bindings += GenerateBindingExpression("SRT_TimerReset",     "f2");
    bindings += GenerateBindingExpression("SRT_Teleport1",      "numpad1");
    bindings += GenerateBindingExpression("SRT_Teleport2",      "numpad2");
    bindings += GenerateBindingExpression("SRT_Teleport3",      "numpad3");
    bindings += GenerateBindingExpression("SRT_Teleport4",      "numpad4");
    bindings += GenerateBindingExpression("SRT_Teleport5",      "numpad5");
    InputActionManager->AddBindings(bindings.c_str());

    m_toggleAction = ZInputAction("SRT_Toggle");

    m_overlay.OnEngineInitialized();
    m_bookmarks.OnEngineInitialized();
    m_triggerViz.OnEngineInitialized();
}

void SpeedrunToolkit::OnFrameUpdate(const SGameUpdateEvent& updateEvent)
{
    const bool toggleDown = m_toggleAction.Digital();
    if (toggleDown && !m_prevToggle)
        m_isOpen = !m_isOpen;
    m_prevToggle = toggleDown;

    // Resolve player ZSpatialEntity.
    ZSpatialEntity* pPlayerSpatial = nullptr;
    {
        const auto& hitmanRef = LevelManager->GetHitman();
        if (hitmanRef.GetRawPointer())
            pPlayerSpatial = hitmanRef.GetEntityRef().QueryInterfacePtr<ZSpatialEntity>();
    }

    // Resolve the active render camera.
    ZCameraEntity* pCamera = nullptr;
    {
        auto renderDest = RenderManager->GetGameRenderDestinationEntity();
        if (renderDest.GetRawPointer())
        {
            const ZEntityRef& camEntityRef = renderDest.GetRawPointer()->GetSource();
            pCamera = camEntityRef.QueryInterfacePtr<ZCameraEntity>();
        }
    }

    if (pPlayerSpatial)
        m_overlay.SetPlayerPosition(pPlayerSpatial->GetWorldPosition());

    m_bookmarks.SetPlayerSpatial(pPlayerSpatial);
    m_triggerViz.SetActiveCamera(pCamera);

    m_overlay.OnFrameUpdate(updateEvent);
    m_bookmarks.OnFrameUpdate(updateEvent);
}

void SpeedrunToolkit::OnDrawMenu()
{
    if (ImGui::MenuItem("Speedrun Toolkit", "L", m_isOpen))
        m_isOpen = !m_isOpen;
}

void SpeedrunToolkit::OnDrawUI(bool hasFocus)
{
    m_overlay.OnDrawUI(hasFocus);

    if (!m_isOpen) return;
    RenderMainWindow();
}

void SpeedrunToolkit::OnDraw3D()
{
    m_triggerViz.OnDraw3D();
}

void SpeedrunToolkit::RenderMainWindow()
{
    ImGui::SetNextWindowSize(ImVec2(560.f, 420.f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(100.f, 100.f), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Speedrun Toolkit", &m_isOpen))
    {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("SRT_Tabs"))
    {
        if (ImGui::BeginTabItem("Bookmarks"))
        {
            m_bookmarks.RenderTab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Overlay"))
        {
            m_overlay.RenderSettingsPanel();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Triggers"))
        {
            m_triggerViz.RenderTab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Info"))
        {
            ImGui::TextDisabled("Speedrun Toolkit for Hitman Absolution");
            ImGui::TextDisabled("Built on HitmanAbsolutionSDK by pavledev.");
            ImGui::Spacing();
            ImGui::SeparatorText("Notes");
            ImGui::TextWrapped(
                "Bookmarks save and restore player transform. "
                "AI state, alert level, patrol logic, and scripted events are NOT "
                "restored — engine limitation. "
                "NPC position snapshots: framework in place, requires actor "
                "enumeration API (TODO).");
            ImGui::Spacing();
            ImGui::TextDisabled("Memory offsets: Steam version (LiveSplit.HitmanAbsolution).");
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}
