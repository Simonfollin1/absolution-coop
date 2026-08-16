#pragma once

#include <string>
#include <vector>

class SettingsStore;

namespace UI
{
    // Named snapshots of a mod's settings.
    //
    // The point of the panel is that a preset never surprises you: you pick
    // which groups it covers before saving, every saved preset lists the groups
    // and value count it holds, and loading one says plainly what it is about to
    // overwrite. A preset that silently reset a setting you had tuned would be
    // worse than no presets at all.
    class PresetPanel
    {
    public:
        void Render(SettingsStore& settings);

    private:
        void RefreshIfNeeded(SettingsStore& settings);

        char m_name[48] = "my-setup";

        // Which groups a newly saved preset will cover. Indices line up with
        // SettingsStore::GetGroups().
        std::vector<bool>        m_included;
        std::vector<std::string> m_groups;

        std::vector<std::string> m_presetNames;
        std::vector<std::string> m_presetSummaries;

        bool        m_scanned = false;
        std::string m_status;
    };
}
