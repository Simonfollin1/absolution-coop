#include "Features/PositionBookmarks.h"
#include "Memory/GameOffsets.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdio>
#include <cstring>

#include <imgui.h>
#include <Glacier/ZGameLoopManager.h>
#include <Glacier/ZHM5BaseCharacter.h>
#include <Glacier/Actor/ZActor.h>
#include <Glacier/Render/ZSpatialEntity.h>

void PositionBookmarks::OnEngineInitialized()
{
    GameLoopManager->RegisterForFrameUpdate(this, &PositionBookmarks::OnFrameUpdate);

    // Default: Numpad1-5 teleports to slot 1-5.
    // Override via mods.ini with key "SRT_Teleport1" etc.
    const char* defaultKeys[SLOT_COUNT] = { "Numpad1","Numpad2","Numpad3","Numpad4","Numpad5" };
    for (int i = 0; i < SLOT_COUNT; ++i)
    {
        char actionName[32];
        std::snprintf(actionName, sizeof(actionName), "SRT_Teleport%d", i + 1);
        m_teleportActions[i] = ZInputAction(GenerateBindingExpression(actionName, defaultKeys[i]));
    }
}

void PositionBookmarks::OnFrameUpdate(const SGameUpdateEvent& /*updateEvent*/)
{
    // Auto save/load bookmarks when the level or section changes.
    int level   = GameOffsets::Read<int>(GameOffsets::CURRENT_LEVEL);
    int section = GameOffsets::Read<int>(GameOffsets::CURRENT_SECTION);

    if (level != m_lastLevel || section != m_lastSection)
    {
        if (m_lastLevel >= 0)
            PersistToDisk(m_lastLevel, m_lastSection);
        LoadFromDisk(level, section);
        m_lastLevel   = level;
        m_lastSection = section;
    }

    for (int i = 0; i < SLOT_COUNT; ++i)
    {
        if (m_teleportActions[i].IsTriggered())
            TeleportToSlot(i);
    }
}

void PositionBookmarks::SaveSlot(int slot)
{
    if (!m_pPlayer) return;

    Bookmark& bm = m_slots[slot];

    // Save player spatial state.
    ZSpatialEntity* pSpatial = m_pPlayer->QueryInterfacePtr<ZSpatialEntity>();
    if (pSpatial)
    {
        bm.playerPosition = pSpatial->GetWorldPosition();
        bm.playerMatrix   = pSpatial->GetObjectToWorldMatrix();
    }

    // Save NPC snapshots.
    bm.actorSnapshots.clear();
    if (m_pActors)
    {
        for (const auto& actorRef : *m_pActors)
        {
            ZActor* pActor = actorRef.GetRawPointer();
            if (!pActor) continue;

            ZSpatialEntity* pActorSpatial = pActor->QueryInterfacePtr<ZSpatialEntity>();
            if (!pActorSpatial) continue;

            ActorSnapshot snap;
            snap.actorRef = actorRef;
            snap.position = pActorSpatial->GetWorldPosition();
            snap.matrix   = pActorSpatial->GetObjectToWorldMatrix();
            bm.actorSnapshots.push_back(std::move(snap));
        }
    }

    bm.isSet = true;
}

void PositionBookmarks::TeleportToSlot(int slot)
{
    if (!m_slots[slot].isSet) return;
    if (!m_pPlayer) return;

    const Bookmark& bm = m_slots[slot];

    // Restore player transform.
    ZSpatialEntity* pSpatial = m_pPlayer->QueryInterfacePtr<ZSpatialEntity>();
    if (pSpatial)
        pSpatial->SetObjectToWorldMatrix(bm.playerMatrix);

    // Restore NPC positions.
    // Note: this resets world position only; AI state (alert level, patrol logic,
    // scripted event state) is not restored. Speedrunners should be aware of this.
    for (const auto& snap : bm.actorSnapshots)
    {
        ZActor* pActor = snap.actorRef.GetRawPointer();
        if (!pActor) continue;

        ZSpatialEntity* pActorSpatial = pActor->QueryInterfacePtr<ZSpatialEntity>();
        if (!pActorSpatial) continue;

        pActorSpatial->SetObjectToWorldMatrix(snap.matrix);
    }
}

void PositionBookmarks::RenderTab()
{
    ImGui::TextDisabled("Teleport keys: Numpad1-5 (configurable in mods.ini)");
    ImGui::TextDisabled("Save: click [Save] button below, or use Ctrl+Numpad1-5.");
    ImGui::Spacing();

    for (int i = 0; i < SLOT_COUNT; ++i)
    {
        Bookmark& bm = m_slots[i];

        ImGui::PushID(i);
        ImGui::Text("Slot %d", i + 1);
        ImGui::SameLine();

        if (ImGui::Button("Save"))
            SaveSlot(i);
        ImGui::SameLine();

        if (bm.isSet)
        {
            if (ImGui::Button("Teleport"))
                TeleportToSlot(i);
            ImGui::SameLine();

            ImGui::Text("(%.0f, %.0f, %.0f)  %d NPCs",
                bm.playerPosition.x, bm.playerPosition.y, bm.playerPosition.z,
                static_cast<int>(bm.actorSnapshots.size()));
        }
        else
        {
            ImGui::TextDisabled("(empty)");
        }

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 160.f);
        ImGui::SetNextItemWidth(160.f);
        ImGui::InputText("##label", bm.label, sizeof(bm.label));

        ImGui::PopID();
    }

    ImGui::Spacing();
    if (ImGui::Button("Save all to disk", ImVec2(-1.f, 0.f)))
    {
        int level   = GameOffsets::Read<int>(GameOffsets::CURRENT_LEVEL);
        int section = GameOffsets::Read<int>(GameOffsets::CURRENT_SECTION);
        PersistToDisk(level, section);
    }
}

// ---- Serialisation (simple key=value text, no external deps) ----

std::string PositionBookmarks::BuildFilePath(int level, int section)
{
    std::filesystem::create_directories("mods/SpeedrunToolkit/bookmarks");
    char path[256];
    std::snprintf(path, sizeof(path),
        "mods/SpeedrunToolkit/bookmarks/L%d_S%d.ini", level, section);
    return path;
}

void PositionBookmarks::PersistToDisk(int level, int section)
{
    std::ofstream f(BuildFilePath(level, section));
    if (!f) return;

    for (int i = 0; i < SLOT_COUNT; ++i)
    {
        const Bookmark& bm = m_slots[i];
        f << "[Slot" << i << "]\n";
        f << "IsSet=" << (bm.isSet ? 1 : 0) << "\n";
        f << "Label=" << bm.label << "\n";

        if (bm.isSet)
        {
            // Player matrix (16 floats).
            f << "M=";
            for (int j = 0; j < 16; ++j)
                f << bm.playerMatrix.flt[j] << (j < 15 ? "," : "\n");
        }
    }
}

void PositionBookmarks::LoadFromDisk(int level, int section)
{
    std::ifstream f(BuildFilePath(level, section));
    if (!f) return;

    int currentSlot = -1;
    std::string line;

    while (std::getline(f, line))
    {
        if (line.size() > 2 && line.front() == '[')
        {
            // Section header e.g. [Slot0]
            int s = -1;
            std::sscanf(line.c_str(), "[Slot%d]", &s);
            if (s >= 0 && s < SLOT_COUNT) currentSlot = s;
            continue;
        }

        if (currentSlot < 0) continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        Bookmark& bm = m_slots[currentSlot];

        if (key == "IsSet")  bm.isSet = (val == "1");
        if (key == "Label")  std::strncpy(bm.label, val.c_str(), sizeof(bm.label) - 1);
        if (key == "M" && bm.isSet)
        {
            std::istringstream ss(val);
            std::string token;
            int j = 0;
            while (std::getline(ss, token, ',') && j < 16)
            {
                bm.playerMatrix.flt[j++] = std::stof(token);
            }
            // Reconstruct position from matrix translation column.
            bm.playerPosition.x = bm.playerMatrix.Trans.x;
            bm.playerPosition.y = bm.playerMatrix.Trans.y;
            bm.playerPosition.z = bm.playerMatrix.Trans.z;
            bm.playerPosition.w = bm.playerMatrix.Trans.w;
        }
    }
}
