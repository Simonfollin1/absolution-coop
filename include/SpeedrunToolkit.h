#pragma once

#include <Glacier/ZGameLoopManager.h>
#include <Glacier/ZLevelManager.h>
#include <Glacier/Input/ZInputAction.h>

#include <ModInterface.h>

#include "Features/SpeedrunOverlay.h"
#include "Features/PositionBookmarks.h"
#include "Features/TriggerZoneVisualizer.h"

class SpeedrunToolkit : public ModInterface
{
public:
    SpeedrunToolkit();
    ~SpeedrunToolkit() override;

    void Initialize()            override;
    void OnEngineInitialized()   override;
    void OnDrawMenu()            override;
    void OnDrawUI(bool hasFocus) override;
    void OnDraw3D()              override;

private:
    void OnFrameUpdate(const SGameUpdateEvent& updateEvent);
    void RenderMainWindow();

    SpeedrunOverlay       m_overlay;
    PositionBookmarks     m_bookmarks;
    TriggerZoneVisualizer m_triggerViz;

    bool         m_isOpen = false;
    ZInputAction m_toggleAction;
};

DECLARE_MOD(SpeedrunToolkit)
