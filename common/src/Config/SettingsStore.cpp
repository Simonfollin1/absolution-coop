#include "Config/SettingsStore.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <sstream>
#include <stdexcept>

std::string SettingsStore::GetFilePath() const
{
    const std::string directory = "mods/" + m_modName;

    std::error_code errorCode;
    std::filesystem::create_directories(directory, errorCode);

    return directory + "/settings.ini";
}

void SettingsStore::Load()
{
    m_values.clear();

    std::ifstream file(GetFilePath());

    if (!file)
    {
        return;
    }

    std::string line;

    while (std::getline(file, line))
    {
        if (line.empty() || line.front() == ';' || line.front() == '#' || line.front() == '[')
        {
            continue;
        }

        const size_t separator = line.find('=');

        if (separator == std::string::npos)
        {
            continue;
        }

        m_values[line.substr(0, separator)] = line.substr(separator + 1);
    }
}

void SettingsStore::WriteValue(const Binding& binding, std::ostream& out) const
{
    switch (binding.type)
    {
    case Type::Bool:
        out << (*static_cast<bool*>(binding.target) ? '1' : '0');
        break;
    case Type::Int:
        out << *static_cast<int*>(binding.target);
        break;
    case Type::Float:
        out << *static_cast<float*>(binding.target);
        break;
    }
}

void SettingsStore::ApplyValue(const Binding& binding, const std::string& text) const
{
    // A corrupt or hand-edited file must never take the game down.
    try
    {
        switch (binding.type)
        {
        case Type::Bool:
            *static_cast<bool*>(binding.target) = (text == "1" || text == "true");
            break;
        case Type::Int:
            *static_cast<int*>(binding.target) = std::stoi(text);
            break;
        case Type::Float:
            *static_cast<float*>(binding.target) = std::stof(text);
            break;
        }
    }
    catch (const std::exception&)
    {
    }
}

void SettingsStore::Save()
{
    std::ofstream file(GetFilePath());

    if (!file)
    {
        return;
    }

    file << "; Generated settings for " << m_modName
         << ". Edit while the game is closed.\n";
    file << "[Settings]\n";

    for (const Binding& binding : m_bindings)
    {
        file << binding.key << '=';
        WriteValue(binding, file);
        file << '\n';
    }

    m_dirty = false;
}

void SettingsStore::Bind(const char* key, bool& value)
{
    const auto stored = m_values.find(key);

    if (stored != m_values.end())
    {
        value = (stored->second == "1" || stored->second == "true");
    }

    m_bindings.push_back({ key, Type::Bool, &value });
}

void SettingsStore::Bind(const char* key, int& value)
{
    const auto stored = m_values.find(key);

    if (stored != m_values.end())
    {
        // A corrupt settings file must never take the game down.
        try
        {
            value = std::stoi(stored->second);
        }
        catch (const std::exception&)
        {
        }
    }

    m_bindings.push_back({ key, Type::Int, &value });
}

void SettingsStore::Bind(const char* key, float& value)
{
    const auto stored = m_values.find(key);

    if (stored != m_values.end())
    {
        try
        {
            value = std::stof(stored->second);
        }
        catch (const std::exception&)
        {
        }
    }

    m_bindings.push_back({ key, Type::Float, &value });
}

// ---- Presets ----

std::string SettingsStore::GroupOf(const std::string& key)
{
    const size_t dot = key.find('.');

    return dot == std::string::npos ? key : key.substr(0, dot);
}

std::string SettingsStore::SanitizePresetName(const std::string& name)
{
    std::string result;

    for (const char character : name)
    {
        const bool allowed =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '-' || character == '_' || character == ' ';

        if (allowed)
        {
            result += (character == ' ') ? '-' : character;
        }
    }

    return result;
}

std::string SettingsStore::PresetDirectory() const
{
    const std::string directory = "mods/" + m_modName + "/presets";

    std::error_code errorCode;
    std::filesystem::create_directories(directory, errorCode);

    return directory;
}

std::string SettingsStore::PresetPath(const std::string& sanitizedName) const
{
    return PresetDirectory() + "/" + sanitizedName + ".ini";
}

std::vector<std::string> SettingsStore::GetGroups() const
{
    std::vector<std::string> groups;

    for (const Binding& binding : m_bindings)
    {
        const std::string group = GroupOf(binding.key);

        if (std::find(groups.begin(), groups.end(), group) == groups.end())
        {
            groups.push_back(group);
        }
    }

    std::sort(groups.begin(), groups.end());

    return groups;
}

int SettingsStore::CountInGroup(const std::string& group) const
{
    int count = 0;

    for (const Binding& binding : m_bindings)
    {
        if (GroupOf(binding.key) == group)
        {
            ++count;
        }
    }

    return count;
}

bool SettingsStore::SavePreset(const std::string& name, const std::vector<std::string>& groups)
{
    const std::string sanitized = SanitizePresetName(name);

    if (sanitized.empty() || groups.empty())
    {
        return false;
    }

    std::ofstream file(PresetPath(sanitized));

    if (!file)
    {
        return false;
    }

    // The group list is the contract: loading this preset will touch these
    // parts of the mod and nothing else.
    file << "; preset for " << m_modName << '\n';
    file << "#groups=";

    for (size_t i = 0; i < groups.size(); ++i)
    {
        file << (i ? "," : "") << groups[i];
    }

    file << "\n[Values]\n";

    for (const Binding& binding : m_bindings)
    {
        if (std::find(groups.begin(), groups.end(), GroupOf(binding.key)) == groups.end())
        {
            continue;
        }

        file << binding.key << '=';
        WriteValue(binding, file);
        file << '\n';
    }

    return true;
}

bool SettingsStore::LoadPreset(const std::string& name)
{
    const std::string sanitized = SanitizePresetName(name);

    if (sanitized.empty())
    {
        return false;
    }

    std::ifstream file(PresetPath(sanitized));

    if (!file)
    {
        return false;
    }

    std::unordered_map<std::string, std::string> values;
    std::string line;

    while (std::getline(file, line))
    {
        if (line.empty() || line.front() == ';' || line.front() == '[')
        {
            continue;
        }

        if (line.front() == '#')
        {
            // Group header; the values themselves say which groups they are in,
            // so nothing more is needed here.
            continue;
        }

        const size_t separator = line.find('=');

        if (separator == std::string::npos)
        {
            continue;
        }

        values[line.substr(0, separator)] = line.substr(separator + 1);
    }

    if (values.empty())
    {
        return false;
    }

    for (const Binding& binding : m_bindings)
    {
        const auto stored = values.find(binding.key);

        if (stored != values.end())
        {
            ApplyValue(binding, stored->second);
        }
    }

    m_dirty = true;

    return true;
}

std::vector<SettingsStore::PresetInfo> SettingsStore::ListPresets() const
{
    std::vector<PresetInfo> presets;

    std::error_code errorCode;
    const std::filesystem::path directory(PresetDirectory());

    if (!std::filesystem::exists(directory, errorCode))
    {
        return presets;
    }

    for (const auto& entry : std::filesystem::directory_iterator(directory, errorCode))
    {
        if (entry.path().extension() != ".ini")
        {
            continue;
        }

        PresetInfo info;
        info.name = entry.path().stem().string();

        std::ifstream file(entry.path());
        std::string line;

        while (std::getline(file, line))
        {
            if (line.rfind("#groups=", 0) == 0)
            {
                std::istringstream stream(line.substr(8));
                std::string group;

                while (std::getline(stream, group, ','))
                {
                    if (!group.empty())
                    {
                        info.groups.push_back(group);
                    }
                }
            }
            else if (!line.empty() && line.front() != ';' && line.front() != '[' &&
                     line.find('=') != std::string::npos)
            {
                ++info.valueCount;
            }
        }

        presets.push_back(std::move(info));
    }

    std::sort(presets.begin(), presets.end(),
        [](const PresetInfo& left, const PresetInfo& right) { return left.name < right.name; });

    return presets;
}

bool SettingsStore::DeletePreset(const std::string& name)
{
    const std::string sanitized = SanitizePresetName(name);

    if (sanitized.empty())
    {
        return false;
    }

    std::error_code errorCode;

    return std::filesystem::remove(PresetPath(sanitized), errorCode);
}
