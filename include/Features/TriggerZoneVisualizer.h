#pragma once

#include <Glacier/ZGameLoopManager.h>
#include <Glacier/Math/float4.h>
#include <Glacier/Math/SMatrix.h>
#include <Glacier/Math/SMatrix44.h>

class ZCameraEntity;

// Renders checkpoint trigger zone bounds as wireframe boxes on screen.
// Uses world-to-screen projection from the active camera's projection matrix.
//
// Current limitation: individual checkpoint entity enumeration requires scene
// entity iteration that depends on RE work for the exact API. The framework is
// in place; see TODO comment in TeleportZoneVisualizer.cpp.
class TriggerZoneVisualizer
{
public:
    void OnEngineInitialized();
    void OnDraw3D();
    void RenderTab();

    void SetActiveCamera(ZCameraEntity* pCamera) { m_pCamera = pCamera; }

private:
    struct Box
    {
        float4 center{};
        float4 halfExtents{};
        SMatrix transform{};
        const char* label = nullptr;
    };

    // Projects a world-space point to ImGui screen coordinates.
    // Returns false if the point is behind the camera.
    bool WorldToScreen(const float4& worldPos, float& outX, float& outY) const;

    // Draws a single axis-aligned wire box given its 8 world corners projected to screen.
    void DrawWireBox(const Box& box) const;

    void DrawLine(float x0, float y0, float x1, float y1, ImU32 color) const;

    ZCameraEntity* m_pCamera = nullptr;

    bool m_enabled   = false;
    bool m_drawBoxes = true;
    bool m_drawLabels = true;
    ImU32 m_boxColor  = IM_COL32(255, 220, 30, 200);
};
