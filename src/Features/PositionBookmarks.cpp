#include "Features/PositionBookmarks.h"
#include "Memory/GameOffsets.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdio>
#include <cstring>

#include <imgui.h>
#include <Glacier/Render/ZSpatialEntity.h>

void PositionBookmarks::OnEngineInitialized()
{
    // Bindings were already registered by SpeedrunToolkit::OnEngineInitialized.
    // Default keys: Numpad1-5.
    for (int i = 0; i < SLOT_COUNT; ++i)
    {
        char actionName[32];
        std::snprintf(actionName, sizeof(actionName), "SRT_Teleport%d", i + 1);
        m_teleportActions[i] = ZInputAction(actionName);
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

    // Rising-edge detection for one-shot teleport.
    for (int i = 0; i < SLOT_COUNT; ++i)
    {
        const bool down = m_teleportActions[i].Digital();
        if (down && !m_prevTeleport[i])
            TeleportToSlot(i);
        m_prevTeleport[i] = down;
    }
}

void PositionBookmarks::SaveSlot(int slot)
{
    if (!m_pPlayerSpatial) return;

    Bookmark& bm = m_slots[slot];
    bm.playerPosition = m_pPlayerSpatial->GetWorldPosition();
    bm.playerMatrix   = m_pPlayerSpatial->GetObjectToWorldMatrix();
    bm.isSet = true;
}

void PositionBookmarks::TeleportToSlot(int slot)
{
    if (!m_slots[slot].isSet) return;
    if (!m_pPlayerSpatial) return;

    m_pPlayerSpatial->SetObjectToWorldMatrix(m_slots[slot].playerMatrix);
}

void PositionBookmarks::RenderTab()
{
    ImGui::TextDisabled("Teleport keys: Numpad1-5");
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

            ImGui::Text("(%.0f, %.0f, %.0f)",
                bm.playerPosition.x, bm.playerPosition.y, bm.playerPosition.z);
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

// ---- Serialisation ----

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

        if (key == "IsSet") bm.isSet = (val == "1");
        if (key == "Label") std::strncpy(bm.label, val.c_str(), sizeof(bm.label) - 1);
        if (key == "M" && bm.isSet)
        {
            std::istringstream ss(val);
            std::string token;
            int j = 0;
            while (std::getline(ss, token, ',') && j < 16)
                bm.playerMatrix.flt[j++] = std::stof(token);

            bm.playerPosition.x = bm.playerMatrix.Trans.x;
            bm.playerPosition.y = bm.playerMatrix.Trans.y;
            bm.playerPosition.z = bm.playerMatrix.Trans.z;
            bm.playerPosition.w = bm.playerMatrix.Trans.w;
        }
    }
}
