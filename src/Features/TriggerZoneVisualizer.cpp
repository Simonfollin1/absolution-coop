#include "Features/TriggerZoneVisualizer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <Glacier/ZGameLoopManager.h>
#include <Glacier/Camera/ZCameraEntity.h>
#include <Glacier/Render/ZSpatialEntity.h>

// TODO: Once the SDK exposes a scene entity iteration API (or a ZCheckPointManager
// accessor that returns individual ZCheckPointEntity refs), populate m_boxes here
// by querying each checkpoint's GetWorldPosition() and GetLocalHalfSize().
// FreeCamera.cpp hooks ZEntitySceneContext_CreateSceneHook for scene access —
// use the same pattern to enumerate trigger entities on scene load.

void TriggerZoneVisualizer::OnEngineInitialized()
{
    // No frame update needed; drawing happens in OnDraw3D via ImGui draw list.
}

void TriggerZoneVisualizer::OnDraw3D()
{
    if (!m_enabled || !m_pCamera) return;

    // Placeholder: nothing to draw until trigger entity data is populated.
    // When trigger data is available, call DrawWireBox for each entry.
}

void TriggerZoneVisualizer::RenderTab()
{
    ImGui::Checkbox("Enable trigger visualizer", &m_enabled);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Draws wireframe boxes around checkpoint trigger zones.\n"
            "Requires scene entity enumeration — implementation pending.\n"
            "Framework is complete; see TriggerZoneVisualizer.cpp TODO.");

    if (!m_enabled) return;

    ImGui::Checkbox("Draw boxes",  &m_drawBoxes);
    ImGui::Checkbox("Draw labels", &m_drawLabels);

    ImGui::ColorButton("Box color", ImVec4(
        ((m_boxColor >> 0)  & 0xFF) / 255.f,
        ((m_boxColor >> 8)  & 0xFF) / 255.f,
        ((m_boxColor >> 16) & 0xFF) / 255.f,
        ((m_boxColor >> 24) & 0xFF) / 255.f));

    ImGui::Spacing();
    ImGui::TextDisabled("No trigger data available yet.");
    ImGui::TextDisabled("Populate via scene entity enumeration (see source TODO).");
}

// ---- Projection helpers ----

bool TriggerZoneVisualizer::WorldToScreen(const float4& worldPos, float& outX, float& outY) const
{
    if (!m_pCamera) return false;

    // ZCameraEntity::Project maps a world-space point directly to
    // normalised device coordinates [-1, 1]. w > 0 means in front of camera.
    float4 ndc = m_pCamera->Project(worldPos);
    if (ndc.w <= 0.f) return false;

    ImGuiIO& io = ImGui::GetIO();
    outX = (ndc.x * 0.5f + 0.5f) * io.DisplaySize.x;
    outY = (1.f - (ndc.y * 0.5f + 0.5f)) * io.DisplaySize.y;
    return true;
}

void TriggerZoneVisualizer::DrawWireBox(const Box& box) const
{
    // Build the 8 corners in world space from center + half-extents,
    // then transform by the entity's object-to-world matrix.
    const float hx = box.halfExtents.x;
    const float hy = box.halfExtents.y;
    const float hz = box.halfExtents.z;

    float4 localCorners[8] = {
        { box.center.x - hx, box.center.y - hy, box.center.z - hz, 1.f },
        { box.center.x + hx, box.center.y - hy, box.center.z - hz, 1.f },
        { box.center.x + hx, box.center.y + hy, box.center.z - hz, 1.f },
        { box.center.x - hx, box.center.y + hy, box.center.z - hz, 1.f },
        { box.center.x - hx, box.center.y - hy, box.center.z + hz, 1.f },
        { box.center.x + hx, box.center.y - hy, box.center.z + hz, 1.f },
        { box.center.x + hx, box.center.y + hy, box.center.z + hz, 1.f },
        { box.center.x - hx, box.center.y + hy, box.center.z + hz, 1.f },
    };

    float sx[8], sy[8];
    bool  visible[8];
    for (int i = 0; i < 8; ++i)
        visible[i] = WorldToScreen(box.transform * localCorners[i], sx[i], sy[i]);

    // Bottom face, top face, pillars.
    constexpr int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},   // bottom
        {4,5},{5,6},{6,7},{7,4},   // top
        {0,4},{1,5},{2,6},{3,7}    // pillars
    };

    for (auto& e : edges)
    {
        if (visible[e[0]] && visible[e[1]])
            DrawLine(sx[e[0]], sy[e[0]], sx[e[1]], sy[e[1]], m_boxColor);
    }
}

void TriggerZoneVisualizer::DrawLine(float x0, float y0, float x1, float y1, ImU32 color) const
{
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    drawList->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), color, 1.5f);
}
