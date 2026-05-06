#pragma once

#include <array>
#include <vector>
#include <string>

#include <Glacier/ZGameLoopManager.h>
#include <Glacier/Input/ZInputAction.h>
#include <Glacier/Math/float4.h>
#include <Glacier/Math/SMatrix.h>
#include <Glacier/Entity/ZEntityRef.h>

class ZHM5BaseCharacter;
class ZActor;

// Snapshot of a single actor's spatial state at save time.
struct ActorSnapshot
{
    TEntityRef<ZActor> actorRef;
    float4             position{};
    SMatrix            matrix{};
};

// One saved position slot: player transform + all actor positions at that moment.
struct Bookmark
{
    float4               playerPosition{};
    SMatrix              playerMatrix{};
    bool                 isSet = false;
    char                 label[64]{};
    std::vector<ActorSnapshot> actorSnapshots;
};

class PositionBookmarks
{
public:
    static constexpr int SLOT_COUNT = 5;

    void OnEngineInitialized();
    void OnFrameUpdate(const SGameUpdateEvent& updateEvent);

    // Called by SpeedrunToolkit to provide the current player entity each frame.
    void SetPlayerEntity(ZHM5BaseCharacter* pPlayer) { m_pPlayer = pPlayer; }

    // Called by SpeedrunToolkit to provide the actor list for NPC snapshots.
    void SetActorList(const std::vector<TEntityRef<ZActor>>& actors) { m_pActors = &actors; }

    // Renders the Bookmarks tab contents inside the parent ImGui window.
    void RenderTab();

    void PersistToDisk(int level, int section);
    void LoadFromDisk(int level, int section);

private:
    void SaveSlot(int slot);
    void TeleportToSlot(int slot);
    static std::string BuildFilePath(int level, int section);

    std::array<Bookmark, SLOT_COUNT>        m_slots{};
    std::array<ZInputAction, SLOT_COUNT>    m_teleportActions;

    ZHM5BaseCharacter*                       m_pPlayer  = nullptr;
    const std::vector<TEntityRef<ZActor>>*   m_pActors  = nullptr;

    int m_lastLevel   = -1;
    int m_lastSection = -1;
};
