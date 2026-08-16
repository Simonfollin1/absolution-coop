#include "UI/Presets.h"
#include "UI/Help.h"
#include "Config/SettingsStore.h"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

namespace
{
    // Turns a key prefix into something a person would recognise. Anything not
    // listed falls back to the raw group name, which is still readable.
    const char* PrettyGroupName(const std::string& group)
    {
        if (group == "cinecam")      return "Camera";
        if (group == "path")         return "Camera paths";
        if (group == "noclip")       return "Noclip";
        if (group == "visuals")      return "Colour grade, grain, vignette";
        if (group == "hud")          return "HUD element visibility";
        if (group == "overlay")      return "On-screen readout";
        if (group == "timer")        return "Timer and splits";
        if (group == "route")        return "Route recording";
        if (group == "actors")       return "NPC tools";
        if (group == "inputDisplay") return "Input display";
        if (group == "drill")        return "Drill loop";
        if (group == "bookmarks")    return "Bookmarks";
        if (group == "loadedMarker") return "Loaded marker";
        if (group == "srt")          return "Window behaviour";
        if (group == "practice")     return "Time control and warps";

        return group.c_str();
    }
}

void UI::PresetPanel::RefreshIfNeeded(SettingsStore& settings)
{
    if (m_scanned)
    {
        return;
    }

    m_scanned = true;
    m_groups  = settings.GetGroups();

    // Default to a preset that covers everything, which is what most people
    // mean the first time.
    m_included.assign(m_groups.size(), true);

    m_presetNames.clear();
    m_presetSummaries.clear();

    for (const SettingsStore::PresetInfo& preset : settings.ListPresets())
    {
        m_presetNames.push_back(preset.name);

        std::string summary;

        for (size_t i = 0; i < preset.groups.size(); ++i)
        {
            summary += (i ? ", " : "");
            summary += PrettyGroupName(preset.groups[i]);
        }

        if (summary.empty())
        {
            summary = "no groups recorded";
        }

        char counted[32];
        std::snprintf(counted, sizeof(counted), "  (%d settings)", preset.valueCount);

        m_presetSummaries.push_back(summary + counted);
    }
}

void UI::PresetPanel::Render(SettingsStore& settings)
{
    ImGui::SeparatorText("Presets");

    RefreshIfNeeded(settings);

    ImGui::TextDisabled("A preset stores the settings in the groups you tick, and "
                        "loading it changes only those.");

    // ---- What a new preset will contain ----

    if (ImGui::TreeNodeEx("What goes in", ImGuiTreeNodeFlags_DefaultOpen))
    {
        int totalGroups = 0;
        int totalValues = 0;

        for (size_t i = 0; i < m_groups.size(); ++i)
        {
            bool included = m_included[i];

            ImGui::PushID(static_cast<int>(i));

            char label[96];
            std::snprintf(label, sizeof(label), "%s  (%d)",
                PrettyGroupName(m_groups[i]), settings.CountInGroup(m_groups[i]));

            if (ImGui::Checkbox(label, &included))
            {
                m_included[i] = included;
            }

            ImGui::PopID();

            if (included)
            {
                ++totalGroups;
                totalValues += settings.CountInGroup(m_groups[i]);
            }
        }

        ImGui::TextDisabled("%d group%s, %d settings",
            totalGroups, totalGroups == 1 ? "" : "s", totalValues);

        ImGui::SameLine();

        if (ImGui::SmallButton("All"))
        {
            m_included.assign(m_groups.size(), true);
        }

        ImGui::SameLine();

        if (ImGui::SmallButton("None"))
        {
            m_included.assign(m_groups.size(), false);
        }

        ImGui::TreePop();
    }

    // ---- Save ----

    ImGui::SetNextItemWidth(200.f);
    ImGui::InputText("##presetname", m_name, sizeof(m_name));

    ImGui::SameLine();

    if (ImGui::Button("Save preset"))
    {
        std::vector<std::string> groups;

        for (size_t i = 0; i < m_groups.size(); ++i)
        {
            if (m_included[i])
            {
                groups.push_back(m_groups[i]);
            }
        }

        if (groups.empty())
        {
            m_status = "Tick at least one group first.";
        }
        else if (settings.SavePreset(m_name, groups))
        {
            m_status = std::string("Saved ") + m_name;
            m_scanned = false;
        }
        else
        {
            m_status = "Could not write the preset.";
        }
    }

    UI::Help("Writes the current values of the ticked groups to "
             "mods/<mod>/presets/, so you can come back to this exact setup.");

    // ---- Existing presets ----

    if (m_presetNames.empty())
    {
        ImGui::TextDisabled("No presets saved yet.");
    }
    else if (ImGui::BeginTable("UI_Presets", 3,
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp))
    {
        // Measured, not guessed. A fixed pixel width has to be right at every
        // font scale, and this one was not: three small buttons carry 100px of
        // padding and spacing before a single glyph is drawn, so at 1080p the
        // "X" started well outside a 120px cell. A table cell clips without
        // complaint, so the delete button was never drawn and never returned
        // pressed - the only way to remove a preset was to delete its file.
        const ImGuiStyle& style = ImGui::GetStyle();

        const float actionsWidth =
            ImGui::CalcTextSize("Load").x +
            ImGui::CalcTextSize("Use name").x +
            ImGui::CalcTextSize("X").x +
            style.FramePadding.x * 6.f +      // three buttons, both sides
            style.ItemSpacing.x  * 2.f +      // two gaps between them
            style.CellPadding.x  * 2.f;

        const float nameWidth = std::max(ImGui::CalcTextSize("Preset name here").x, 120.f);

        ImGui::TableSetupColumn("Preset",   ImGuiTableColumnFlags_WidthFixed, nameWidth);
        ImGui::TableSetupColumn("Contains");
        ImGui::TableSetupColumn("",         ImGuiTableColumnFlags_WidthFixed, actionsWidth);
        ImGui::TableHeadersRow();

        int deleteIndex = -1;

        for (size_t i = 0; i < m_presetNames.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m_presetNames[i].c_str());

            // The whole point: you can read what a preset will change before
            // you click the thing that changes it.
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", m_presetSummaries[i].c_str());

            ImGui::TableNextColumn();

            if (ImGui::SmallButton("Load"))
            {
                m_status = settings.LoadPreset(m_presetNames[i])
                    ? "Loaded " + m_presetNames[i]
                    : "Could not read " + m_presetNames[i];
            }

            ImGui::SameLine();

            if (ImGui::SmallButton("Use name"))
            {
                std::snprintf(m_name, sizeof(m_name), "%s", m_presetNames[i].c_str());
            }

            ImGui::SameLine();

            if (ImGui::SmallButton("X"))
            {
                deleteIndex = static_cast<int>(i);
            }

            ImGui::PopID();
        }

        ImGui::EndTable();

        if (deleteIndex >= 0)
        {
            settings.DeletePreset(m_presetNames[deleteIndex]);
            m_status  = "Deleted " + m_presetNames[deleteIndex];
            m_scanned = false;
        }
    }

    if (ImGui::SmallButton("Rescan"))
    {
        m_scanned = false;
    }

    if (!m_status.empty())
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.5f, 1.f), "%s", m_status.c_str());
    }
}
