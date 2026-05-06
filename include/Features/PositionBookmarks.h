#pragma once

#include <array>
#include <string>

#include <Glacier/ZGameLoopManager.h>
#include <Glacier/Input/ZInputAction.h>
#include <Glacier/Math/float4.h>
#include <Glacier/Math/SMatrix.h>

class ZSpatialEntity;

struct Bookmark
{
    float4  playerPosition{};
    SMatrix playerMatrix{};
    bool    isSet = false;
    char    label[64]{};
};

class PositionBookmarks
{
public:
    static constexpr int SLOT_COUNT = 5;

    void OnEngineInitialized();
    void OnFrameUpdate(const SGameUpdateEvent& updateEvent);

    // Called by SpeedrunToolkit each frame with the resolved player spatial entity.
    void SetPlayerSpatial(ZSpatialEntity* p) { m_pPlayerSpatial = p; }

    // Renders the Bookmarks tab inside the parent ImGui window.
    void RenderTab();

    void PersistToDisk(int level, int section);
    void LoadFromDisk(int level, int section);

private:
    void SaveSlot(int slot);
    void TeleportToSlot(int slot);
    static std::string BuildFilePath(int level, int section);

    std::array<Bookmark, SLOT_COUNT>     m_slots{};
    std::array<ZInputAction, SLOT_COUNT> m_teleportActions;

    // Rising-edge state for one-shot teleport key detection
    bool m_prevTeleport[SLOT_COUNT] = {};

    ZSpatialEntity* m_pPlayerSpatial = nullptr;

    int m_lastLevel   = -1;
    int m_lastSection = -1;
};
