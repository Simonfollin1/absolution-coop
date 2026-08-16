#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <format>

#include <Glacier/Configuration/ZConfigCommand.h>
#include <Glacier/Configuration/ZConfigFloat.h>
#include <Glacier/Configuration/ZConfigInt.h>

#include "Game/ConfigVars.h"

namespace Coop::Game
{
    namespace
    {
        // The list is walked from a static root, and a corrupt or mid-teardown
        // link would otherwise take the frame with it. The engine has a few
        // thousand of these at most.
        constexpr size_t kMaxWalk = 8192;

        // Windows never maps the first 64 KB, so anything below this cannot be
        // an object. The same check the SDK's own list walkers need, and one of
        // its bundled mods forgets.
        constexpr uintptr_t kLowestPlausibleAddress = 0x10000;

        constexpr size_t kNameBufferSize = 128;

        bool Plausible(const void* pointer)
        {
            return reinterpret_cast<uintptr_t>(pointer) >= kLowestPlausibleAddress;
        }

        // MSVC will not compile __try in a function that owns anything with a
        // destructor (C2712), and ZConfigCommand::Name() returns a ZString by
        // value. So the two are split: this half touches the C++ objects, the
        // half below guards the call. The restriction is per-function, so a
        // guarded function may call an unguarded one freely.
        bool CopyName(ZConfigCommand* command, char* buffer, size_t size)
        {
            const ZString name = command->Name();
            const char*   text = name.ToCString();

            if (!text)
            {
                return false;
            }

            strncpy_s(buffer, size, text, _TRUNCATE);

            return true;
        }

        void ExecuteCommandUnguarded(const char* name, const char* value)
        {
            ZConfigCommand::ExecuteCommand(ZString(name), value);
        }

        struct RawEntry
        {
            char  name[kNameBufferSize];
            int   type;
            float floatValue;
            int   intValue;

            ZConfigCommand* next;
        };

        // Everything here is a plain type, which is what makes the __try legal.
        bool ReadEntry(ZConfigCommand* command, RawEntry* entry)
        {
            __try
            {
                entry->name[0]    = '\0';
                entry->type       = static_cast<int>(ConfigVarType::Unknown);
                entry->floatValue = 0.f;
                entry->intValue   = 0;
                entry->next       = nullptr;

                if (!CopyName(command, entry->name, kNameBufferSize))
                {
                    return false;
                }

                entry->type = static_cast<int>(command->GetType());

                if (entry->type == static_cast<int>(ConfigVarType::Float))
                {
                    entry->floatValue = static_cast<ZConfigFloat*>(command)->GetVal();
                }
                else if (entry->type == static_cast<int>(ConfigVarType::Int))
                {
                    entry->intValue = static_cast<ZConfigInt*>(command)->GetVal();
                }

                entry->next = command->Next();

                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool ReadFirst(ZConfigCommand** out)
        {
            __try
            {
                *out = ZConfigCommand::First();
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool ExecuteGuarded(const char* name, const char* value)
        {
            __try
            {
                ExecuteCommandUnguarded(name, value);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool ContainsNoCase(const std::string& haystack, const std::string& needle)
        {
            if (needle.empty())
            {
                return true;
            }

            const auto found = std::search(
                haystack.begin(), haystack.end(),
                needle.begin(), needle.end(),
                [](char a, char b)
                {
                    return std::tolower(static_cast<unsigned char>(a)) ==
                           std::tolower(static_cast<unsigned char>(b));
                });

            return found != haystack.end();
        }
    }

    std::string ConfigVar::ValueText() const
    {
        switch (type)
        {
        case ConfigVarType::Float:  return std::format("{:.4g}", floatValue);
        case ConfigVarType::Int:    return std::format("{}", intValue);
        case ConfigVarType::String: return "(string)";
        default:                    return "(unknown)";
        }
    }

    bool ConfigVars::Refresh()
    {
        m_vars.clear();
        m_walked     = false;
        m_diagnostic.clear();

        ZConfigCommand* command = nullptr;

        if (!ReadFirst(&command))
        {
            m_diagnostic = "ZConfigCommand::First() faulted - the engine is "
                           "probably not initialised yet";
            return false;
        }

        if (!Plausible(command))
        {
            m_diagnostic = "the configuration list is empty or not built yet";
            return false;
        }

        size_t walked = 0;
        bool   faulted = false;

        while (Plausible(command) && walked < kMaxWalk)
        {
            ++walked;

            RawEntry raw{};

            if (!ReadEntry(command, &raw))
            {
                faulted = true;
                break;
            }

            if (raw.name[0] != '\0')
            {
                ConfigVar entry;

                entry.name       = raw.name;
                entry.type       = static_cast<ConfigVarType>(raw.type);
                entry.floatValue = raw.floatValue;
                entry.intValue   = raw.intValue;

                m_vars.push_back(std::move(entry));
            }

            command = raw.next;
        }

        m_walked = true;

        if (faulted)
        {
            m_diagnostic = std::format(
                "walk faulted after {} entries, kept {}", walked, m_vars.size());
        }
        else if (walked >= kMaxWalk)
        {
            m_diagnostic = std::format(
                "stopped at the {} entry cap - the list may be circular", kMaxWalk);
        }
        else
        {
            m_diagnostic = std::format("{} configuration variables", m_vars.size());
        }

        std::sort(m_vars.begin(), m_vars.end(),
            [](const ConfigVar& a, const ConfigVar& b) { return a.name < b.name; });

        return !m_vars.empty();
    }

    void ConfigVars::RefreshValues()
    {
        // Deliberately a full re-walk. Pointers into the list are never kept,
        // because a stored one is exactly the kind of thing that outlives what
        // it points at.
        Refresh();
    }

    std::vector<const ConfigVar*> ConfigVars::Find(const std::string& needle) const
    {
        std::vector<const ConfigVar*> result;

        for (const ConfigVar& entry : m_vars)
        {
            if (ContainsNoCase(entry.name, needle))
            {
                result.push_back(&entry);
            }
        }

        return result;
    }

    const ConfigVar* ConfigVars::Get(const std::string& name) const
    {
        for (const ConfigVar& entry : m_vars)
        {
            if (entry.name == name)
            {
                return &entry;
            }
        }

        return nullptr;
    }

    bool ConfigVars::Set(const std::string& name, const std::string& value)
    {
        if (name.empty())
        {
            return false;
        }

        // The same dispatcher the console and HMA.ini's ConsoleCmd lines use.
        // It returns nothing, so true here means "did not fault", not "the
        // variable exists" - read the value back to know that.
        return ExecuteGuarded(name.c_str(), value.c_str());
    }
}
