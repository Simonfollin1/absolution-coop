#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The engine's own configuration variables, enumerated at runtime.
//
// ZConfigCommand is a singly linked list rooted at a static, and every entry
// carries a name, a type and a value. The SDK exposes the whole thing -
// First(), Next(), Name(), GetType(), ZConfigFloat::GetVal(),
// ZConfigInt::GetVal() and ExecuteCommand() - so this needs no hook, no
// offset, and nothing that could break on a different build.
//
// Worth having for its own sake. A disassembly of the shipping binary shows
// names like HealthRegenPerSecond, NPC_MaxCharacterStrafeBlendSpeed,
// morpheme_DisableAnimation, ai_BehaviorTreeEvaluationsPerFrame and
// HM5DisguiseLODDissolveMin, which is a large tuning surface that nothing in
// this ecosystem currently exposes to anybody.
//
// For co-op specifically it answers one question directly: whether the damage
// the player receives can be scaled to zero through configuration instead of
// through a god-mode address that is Steam-only and that the SDK's own two
// mods disagree about.
namespace Coop::Game
{
    enum class ConfigVarType : uint8_t
    {
        Float   = 0,
        Int     = 1,
        String  = 2,
        Unknown = 3,
    };

    struct ConfigVar
    {
        std::string   name;
        ConfigVarType type = ConfigVarType::Unknown;

        // Only meaningful for Float and Int respectively.
        float floatValue = 0.f;
        int   intValue   = 0;

        std::string ValueText() const;
    };

    class ConfigVars
    {
    public:
        // Walks the chain. Cheap enough to call on demand, not cheap enough to
        // call every frame.
        bool Refresh();

        // Re-reads the values of what Refresh() already found, without walking
        // the list again.
        void RefreshValues();

        const std::vector<ConfigVar>& All() const { return m_vars; }

        // Case-insensitive substring match on the name.
        std::vector<const ConfigVar*> Find(const std::string& needle) const;

        const ConfigVar* Get(const std::string& name) const;

        // Hands the value to the engine's own command dispatcher, which is
        // what the console and HMA.ini's ConsoleCmd lines use.
        static bool Set(const std::string& name, const std::string& value);

        const std::string& Diagnostic() const { return m_diagnostic; }
        bool               Walked() const { return m_walked; }

    private:
        std::vector<ConfigVar> m_vars;
        std::string            m_diagnostic;
        bool                   m_walked = false;
    };
}
