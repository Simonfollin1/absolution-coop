#include "SpeedrunToolkit.h"

#include <imgui.h>

#include <Glacier/ZGameLoopManager.h>
#include <Glacier/Entity/ZEntitySceneContext.h>
#include <Glacier/Entity/ZEntityRef.h>
#include <Glacier/ZHM5BaseCharacter.h>
#include <Glacier/Actor/ZActor.h>
#include <Glacier/Camera/ZCameraEntity.h>
#include <Glacier/Render/ZSpatialEntity.h>

// ---- Static members ----

TEntityRef<ZHM5BaseCharacter>   SpeedrunToolkit::s_playerRef;
std::vector<TEntityRef<ZActor>> SpeedrunToolkit::s_actorRefs;
ZCameraEntity*                  SpeedrunToolkit::s_pCamera = nullptr;

// ---- Scene hooks (fastcall free functions) ----

// Original function pointers, filled by MinHook.
static void (__fastcall* OrigCreateScene)(ZEntitySceneContext*, int, const ZString&) = nullptr;
static void (__fastcall* OrigClearScene) (ZEntitySceneContext*, int, bool)           = nullptr;

void __fastcall ZEntitySceneContext_CreateSceneHook(ZEntitySceneContext* pThis, int edx,
                                                    const ZString& sStreamingState)
{
    if (OrigCreateScene) OrigCreateScene(pThis, edx, sStreamingState);
    SpeedrunToolkit* inst = static_cast<SpeedrunToolkit*>(GetModInterface());
    if (inst) inst->OnCreateScene(pThis, sStreamingState);
}

void __fastcall ZEntitySceneContext_ClearSceneHook(ZEntitySceneContext* pThis, int edx,
                                                   bool bFullyUnloadScene)
{
    SpeedrunToolkit* inst = static_cast<SpeedrunToolkit*>(GetModInterface());
    if (inst) inst->OnClearScene(pThis, bFullyUnloadScene);
    if (OrigClearScene) OrigClearScene(pThis, edx, bFullyUnloadScene);
}

// ---- SpeedrunToolkit ----

SpeedrunToolkit::SpeedrunToolkit() = default;

SpeedrunToolkit::~SpeedrunToolkit()
{
    GameLoopManager->UnregisterForFrameUpdate(this);
}

void SpeedrunToolkit::Initialize()
{
    ModInterface::Initialize();

    // Hook scene creation/clear using the same offsets as FreeCamera.
    // Adjust hook addresses if they differ from FreeCamera's version.
    // TODO: Replace nullptr with the scanned or offset-based function addresses
    //       once confirmed against the SDK build for the target game version.
    // Example (pseudo):
    //   uintptr_t base = GameOffsets::GetModuleBase();
    //   CreateHook(base + 0xXXXXXX, ZEntitySceneContext_CreateSceneHook, &OrigCreateScene);
    //   CreateHook(base + 0xYYYYYY, ZEntitySceneContext_ClearSceneHook,  &OrigClearScene);
}

void SpeedrunToolkit::OnEngineInitialized()
{
    ModInterface::OnEngineInitialized();

    GameLoopManager->RegisterForFrameUpdate(this, &SpeedrunToolkit::OnFrameUpdate);

    // L opens/closes the toolkit panel.
    m_toggleAction = ZInputAction(GenerateBindingExpression("SRT_Toggle", "L"));

    m_overlay.OnEngineInitialized();
    m_bookmarks.OnEngineInitialized();
    m_triggerViz.OnEngineInitialized();
}

void SpeedrunToolkit::OnFrameUpdate(const SGameUpdateEvent& updateEvent)
{
    if (m_toggleAction.IsTriggered())
        m_isOpen = !m_isOpen;

    // Push current player position into the overlay and bookmark system.
    ZHM5BaseCharacter* pPlayer = GetPlayer();
    if (pPlayer)
    {
        ZSpatialEntity* pSpatial = pPlayer->QueryInterfacePtr<ZSpatialEntity>();
        if (pSpatial)
            m_overlay.SetPlayerPosition(pSpatial->GetWorldPosition());
    }

    m_bookmarks.SetPlayerEntity(pPlayer);
    m_bookmarks.SetActorList(s_actorRefs);
    m_triggerViz.SetActiveCamera(s_pCamera);

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
    // The always-on overlay renders regardless of whether the panel is open.
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

            ImGui::TextDisabled("NPC note:");
            ImGui::TextWrapped(
                "Bookmarks restore player position and NPC world positions. "
                "AI state, alert level, patrol logic, and scripted event state "
                "are NOT restored. This is a known limitation of the checkpoint-"
                "bookmark approach.");
            ImGui::Spacing();
            ImGui::TextDisabled("Memory offsets: Steam version (LiveSplit.HitmanAbsolution by SuiMachine).");
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

// ---- Entity management ----

void SpeedrunToolkit::OnCreateScene(ZEntitySceneContext* /*pCtx*/, const ZString& /*state*/)
{
    // Populate s_playerRef and s_actorRefs from the scene.
    //
    // TODO: Replace with the correct scene entity iteration API once confirmed.
    // Pattern: same as FreeCamera::OnCreateScene — iterate scene entities,
    // QueryInterfacePtr<ZHM5BaseCharacter> for the player,
    // QueryInterfacePtr<ZActor> for NPCs.
    //
    // When the ZEntitySceneContext entity iteration API is known, remove this
    // TODO and implement accordingly.
    s_playerRef = {};
    s_actorRefs.clear();
    s_pCamera   = nullptr;
}

void SpeedrunToolkit::OnClearScene(ZEntitySceneContext* /*pCtx*/, bool /*fullyUnload*/)
{
    s_playerRef = {};
    s_actorRefs.clear();
    s_pCamera   = nullptr;
}

ZHM5BaseCharacter* SpeedrunToolkit::GetPlayer()
{
    return s_playerRef.GetRawPointer();
}
