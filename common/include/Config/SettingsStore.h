#pragma once

#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

// Tiny key/value persistence for mod settings.
//
// Features bind their members once; the store then applies whatever was loaded
// from disk and remembers where to read each value back at save time. Kept
// separate from SpeedrunToolkit.ini so saving settings can never clobber the
// user's key bindings.
//
// Keys are dotted - "cinecam.moveSpeed", "visuals.grainAmount" - and the part
// before the first dot is treated as a group. That is what lets presets say
// plainly which parts of the mod they cover, without anyone maintaining a
// second list that would drift out of date.
class SettingsStore
{
public:
    // Each mod keeps its own settings file, so two mods loaded side by side
    // never overwrite each other's configuration.
    explicit SettingsStore(std::string modName) : m_modName(std::move(modName)) {}

    // Reads the settings file. Call before any Bind() so bound values pick up
    // the stored value immediately.
    void Load();

    // Writes every bound value back to disk.
    void Save();

    // Binding applies the stored value to the referenced variable if one was
    // loaded, and registers the variable for the next Save().
    void Bind(const char* key, bool& value);
    void Bind(const char* key, int& value);
    void Bind(const char* key, float& value);

    bool HasUnsavedChanges() const { return m_dirty; }
    void MarkDirty() { m_dirty = true; }

    std::string GetFilePath() const;

    // ---- Presets ----
    //
    // A preset is a named snapshot of the bound values, limited to whichever
    // groups it was saved with. Loading one touches only those groups, so a
    // "look" preset can leave your camera speeds exactly where you left them.

    struct PresetInfo
    {
        std::string name;
        std::vector<std::string> groups;
        int valueCount = 0;
    };

    // Every group that currently has at least one bound setting, sorted.
    std::vector<std::string> GetGroups() const;

    // How many settings a group holds. Shown next to the group so it is obvious
    // what a preset is about to overwrite.
    int CountInGroup(const std::string& group) const;

    std::vector<PresetInfo> ListPresets() const;

    bool SavePreset(const std::string& name, const std::vector<std::string>& groups);

    // Applies a preset. Only groups recorded in the file are touched; anything
    // else keeps its current value.
    bool LoadPreset(const std::string& name);

    bool DeletePreset(const std::string& name);

private:
    enum class Type
    {
        Bool,
        Int,
        Float
    };

    struct Binding
    {
        std::string key;
        Type        type;
        void*       target;
    };

    static std::string GroupOf(const std::string& key);
    static std::string SanitizePresetName(const std::string& name);

    std::string PresetDirectory() const;
    std::string PresetPath(const std::string& sanitizedName) const;

    void WriteValue(const Binding& binding, std::ostream& out) const;
    void ApplyValue(const Binding& binding, const std::string& text) const;

    std::string m_modName;
    std::unordered_map<std::string, std::string> m_values;
    std::vector<Binding> m_bindings;
    bool m_dirty = false;
};
