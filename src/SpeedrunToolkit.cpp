#include <imgui.h>

#include "SpeedrunToolkit.h"

#include <Glacier/ZGameLoopManager.h>
#include <Glacier/ZLevelManager.h>
#include <Glacier/Render/ZRenderManager.h>
#include <Glacier/Render/IRenderDestinationEntity.h>
#include <Glacier/Render/ZSpatialEntity.h>
#include <Glacier/Camera/ZCameraEntity.h>
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

    m_toggleAction = ZInputAction(GenerateBindingExpression("SRT_Toggle", "L"));

    m_overlay.OnEngineInitialized();
    m_bookmarks.OnEngineInitialized();
    m_triggerViz.OnEngineInitialized();
}

void SpeedrunToolkit::OnFrameUpdate(const SGameUpdateEvent& updateEvent)
{
    if (m_toggleAction.IsTriggered())
        m_isOpen = !m_isOpen;

    // Resolve player ZSpatialEntity via the same pattern as FreeCamera::TeleportMainCharacter:
    //   const TEntityRef<ZHitman5>& hitman = LevelManager->GetHitman();
    //   ZSpatialEntity* spatial = hitman.GetEntityRef().QueryInterfacePtr<ZSpatialEntity>();
    ZSpatialEntity* pPlayerSpatial = nullptr;
    {
        const auto& hitmanRef = LevelManager->GetHitman();
        if (hitmanRef.GetRawPointer())
            pPlayerSpatial = hitmanRef.GetEntityRef().QueryInterfacePtr<ZSpatialEntity>();
    }

    // Resolve the active render camera for world-to-screen projection.
    ZCameraEntity* pCamera = nullptr;
    {
        auto renderDest = RenderManager->GetGameRenderDestinationEntity();
        if (renderDest.GetRawPointer())
        {
            auto camRef = renderDest.GetRawPointer()->GetSource();
            pCamera = camRef.GetRawPointer();
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
