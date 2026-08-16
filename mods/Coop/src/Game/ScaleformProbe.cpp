#include <Windows.h>

#include <cstring>
#include <format>

#include <Glacier/UI/ZHUDManager.h>
#include <Glacier/UI/IScaleformPlayer.h>
#include <Glacier/UI/GFxValue.h>
#include <Global.h>

#include "Game/ScaleformProbe.h"
#include "Diag/Diag.h"

namespace Coop::Game
{
    namespace
    {
        // GFxValue as the SDK declares it. Every member is private and the only
        // public accessor is GetBool, so reading a number means reading the
        // union at its declared offset - which is not a guess, the layout is
        // right there in the header:
        //
        //   ObjectInterface* pObjectInterface   +0x00
        //   ValueType        Type               +0x04
        //   ValueUnion       Value              +0x08
        struct ValueMirror
        {
            void*    objectInterface;
            uint32_t type;

            union
            {
                float       number;
                bool        boolean;
                const char* string;
                void*       data;
            } value;
        };

        static_assert(sizeof(ValueMirror) == sizeof(GFxValue),
                      "GFxValue is not laid out the way this mirror assumes");

        const char* TypeName(uint32_t type)
        {
            switch (type)
            {
            case GFxValue::VT_Undefined:      return "undefined";
            case GFxValue::VT_Null:           return "null";
            case GFxValue::VT_Boolean:        return "bool";
            case GFxValue::VT_Number:         return "number";
            case GFxValue::VT_String:         return "string";
            case GFxValue::VT_StringW:        return "wstring";
            case GFxValue::VT_Object:         return "object";
            case GFxValue::VT_Array:          return "array";
            case GFxValue::VT_DisplayObject:  return "displayobject";
            default:                          return "?";
            }
        }

        // POD only: __try cannot share a function with anything that unwinds.
        // Everything here is a mirror struct and a pointer.
        bool DescribeGuarded(const void* value, uint32_t& typeOut, float& numberOut,
                             char* textOut, size_t textCapacity)
        {
            __try
            {
                ValueMirror mirror{};
                memcpy(&mirror, value, sizeof(mirror));

                typeOut   = mirror.type;
                numberOut = mirror.value.number;
                textOut[0] = '\0';

                if (mirror.type == GFxValue::VT_String && mirror.value.string)
                {
                    size_t i = 0;

                    for (; i + 1 < textCapacity && mirror.value.string[i] != '\0'; ++i)
                    {
                        textOut[i] = mirror.value.string[i];
                    }

                    textOut[i] = '\0';
                }

                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool GetMemberGuarded(IScaleformPlayer* player, const char* name, GFxValue* out)
        {
            __try
            {
                return player->GetMember(name, out);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool GetChildGuarded(const GFxValue* parent, const char* name, GFxValue* out)
        {
            __try
            {
                return parent->GetMember(name, out);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        // Top-level names, from the string sweep of the shipping binary. The
        // health ring is the one that matters; the rest are here because a name
        // that resolves tells us the walk works at all, and a HUD we can read
        // is a HUD we can eventually write.
        const char* const kRootMembers[] = {
            "g_mcHealthBar",
            "g_mcHealth",
            "g_mcRadar",
            "g_mcMiniMap",
            "g_mcHud",
            "g_mcInstinct",
            "g_mcFocusBar",
            "g_mcAmmo",
            "g_mcNotoriety",
            "healthBar",
            "health",
        };

        // And the properties worth having on whichever of those exists. The
        // first four are ActionScript's own and exist on every display object,
        // so one of them resolving proves the walk; the rest are guesses at
        // what the ring is driven by.
        const char* const kProperties[] = {
            "_visible",
            "_alpha",
            "_xscale",
            "_yscale",
            "_currentframe",
            "_totalframes",
            "value",
            "health",
            "percent",
        };

        ScaleformMember Describe(const std::string& path, const GFxValue& value)
        {
            ScaleformMember member;

            member.path  = path;
            member.found = true;

            uint32_t type   = 0;
            float    number = 0.f;
            char     text[128] = {};

            if (!DescribeGuarded(&value, type, number, text, sizeof(text)))
            {
                member.type  = "unreadable";
                member.found = false;

                return member;
            }

            member.type = TypeName(type);

            switch (type)
            {
            case GFxValue::VT_Number:
                member.value = std::format("{:.3f}", number);
                break;

            case GFxValue::VT_Boolean:
                member.value = number != 0.f ? "true" : "false";
                break;

            case GFxValue::VT_String:
                member.value = text;
                break;

            default:
                break;
            }

            return member;
        }
    }

    std::vector<ScaleformMember> ProbeHud()
    {
        std::vector<ScaleformMember> found;

        if (!HUDManager)
        {
            found.push_back({ "(HUD manager)", "not available", "", false });
            return found;
        }

        IScaleformPlayer* hud = HUDManager->GetHUD();

        if (!hud)
        {
            found.push_back({ "(HUD)", "GetHUD returned null - not in a level?", "", false });
            return found;
        }

        GFxValue root;

        if (!GetMemberGuarded(hud, "_root", &root))
        {
            found.push_back({ "_root", "GetMember failed", "", false });
            return found;
        }

        found.push_back(Describe("_root", root));

        for (const char* name : kRootMembers)
        {
            GFxValue child;

            if (!GetChildGuarded(&root, name, &child))
            {
                continue;
            }

            const std::string path = std::string("_root.") + name;

            found.push_back(Describe(path, child));

            // Only worth descending into something that has members.
            uint32_t type   = 0;
            float    number = 0.f;
            char     text[128] = {};

            if (!DescribeGuarded(&child, type, number, text, sizeof(text)))
            {
                continue;
            }

            if (type != GFxValue::VT_Object && type != GFxValue::VT_DisplayObject)
            {
                continue;
            }

            for (const char* property : kProperties)
            {
                GFxValue leaf;

                if (GetChildGuarded(&child, property, &leaf))
                {
                    found.push_back(Describe(path + "." + property, leaf));
                }
            }
        }

        return found;
    }

    void LogHudProbe()
    {
        const std::vector<ScaleformMember> members = ProbeHud();

        Diag::Log("HUD probe: %zu names resolved", members.size());

        for (const ScaleformMember& member : members)
        {
            Diag::Log("  %-32s %-14s %s",
                      member.path.c_str(), member.type.c_str(), member.value.c_str());
        }

        if (members.size() <= 1)
        {
            Diag::Log("  nothing under _root resolved - either the names are wrong or "
                      "GetMember does not reach the HUD's timeline from here");
        }
    }
}
