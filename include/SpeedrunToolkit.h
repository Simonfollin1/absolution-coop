#pragma once

#include <vector>

#include <Glacier/ZGameLoopManager.h>
#include <Glacier/Input/ZInputAction.h>
#include <Glacier/Entity/ZEntityRef.h>

#include <ModInterface.h>

#include "Features/SpeedrunOverlay.h"
#include "Features/PositionBookmarks.h"
#include "Features/TriggerZoneVisualizer.h"

class ZEntitySceneContext;
class ZString;
class ZHM5BaseCharacter;
class ZActor;
class ZCameraEntity;

// Forward declarations for the scene hooks (must be free functions / fastcall).
void __fastcall ZEntitySceneContext_CreateSceneHook(ZEntitySceneContext* pThis, int edx,
                                                    const ZString& sStreamingState);
void __fastcall ZEntitySceneContext_ClearSceneHook(ZEntitySceneContext* pThis, int edx,
                                                   bool bFullyUnloadScene);

class SpeedrunToolkit : public ModInterface
{
public:
    SpeedrunToolkit();
    ~SpeedrunToolkit() override;

    void Initialize()           override;
    void OnEngineInitialized()  override;
    void OnDrawMenu()           override;
    void OnDrawUI(bool hasFocus) override;
    void OnDraw3D()             override;

    // Called by scene hooks to manage cached entity references.
    void OnCreateScene(ZEntitySceneContext* pCtx, const ZString& streamingState);
    void OnClearScene(ZEntitySceneContext* pCtx, bool fullyUnload);

    // Provides the current player ZHM5BaseCharacter* to features.
    // Returns nullptr if the player entity has not yet been resolved.
    static ZHM5BaseCharacter* GetPlayer();

private:
    void OnFrameUpdate(const SGameUpdateEvent& updateEvent);
    void RefreshEntityRefs();
    void RenderMainWindow();

    // Features
    SpeedrunOverlay       m_overlay;
    PositionBookmarks     m_bookmarks;
    TriggerZoneVisualizer m_triggerViz;

    // UI state
    bool         m_isOpen = false;
    ZInputAction m_toggleAction;

    // Cached entity references — populated on scene creation, cleared on unload.
    static TEntityRef<ZHM5BaseCharacter>   s_playerRef;
    static std::vector<TEntityRef<ZActor>> s_actorRefs;
    static ZCameraEntity*                  s_pCamera;
};

DECLARE_MOD(SpeedrunToolkit)
