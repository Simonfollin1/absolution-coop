#include <algorithm>
#include <cctype>

#include "Game/SceneTable.h"

namespace Coop::Game
{
    namespace
    {
        // Read out of assets/HashMap.txt, which ships with the SDK: 28 MB of
        // "<runtime id> <resource uri>", one line each, covering every resource
        // in the game. Twenty-one levels times three resources is sixty-three
        // lines of it, so the table is here rather than the file - the mod
        // stays self-contained and nothing is hashed at runtime.
        //
        // The labels are the level codes, not mission names. The game's own
        // names for these are not in the hash table and guessing them would put
        // a wrong title in front of the player with nothing to correct it.
        //
        // Benchmark is deliberately absent: it has a header library and no
        // factory or blueprint, so it cannot be built this way, and nobody
        // wants to follow anybody into it.
        constexpr SceneResources kScenes[] = {
            { "assembly:/scenes/l01/l01_main.entity",   "l01",  0x800007A7A45DBE71ull, 0x800001F893D61C2Aull, 0x00985D5C975B2115ull },
            { "assembly:/scenes/l01b/l01b_main.entity", "l01b", 0x8000055450379EBEull, 0x80000046ADEDD102ull, 0x004CBE19B33DF40Aull },
            { "assembly:/scenes/l02b/l02b_main.entity", "l02b", 0x80000A11D0691C83ull, 0x80000178D3E69262ull, 0x002F0AD0C98D109Aull },
            { "assembly:/scenes/l02c/l02c_main.entity", "l02c", 0x80000C7ECE5B2712ull, 0x8000026ABA2C5E17ull, 0x0008A27EC0762074ull },
            { "assembly:/scenes/l02d/l02d_main.entity", "l02d", 0x80000C95C8296388ull, 0x8000019BB5E18D4Full, 0x00F8E3CE59912BF1ull },
            { "assembly:/scenes/l03/l03_main.entity",   "l03",  0x8000077E6B95BEEDull, 0x800001A6E27E23E3ull, 0x003771CD8261AFF2ull },
            { "assembly:/scenes/l04b/l04b_main.entity", "l04b", 0x8000053FF51D7907ull, 0x800001BE0F220E01ull, 0x00E21D15A5B539A8ull },
            { "assembly:/scenes/l04c/l04c_main.entity", "l04c", 0x800006AB979EBA64ull, 0x800002582F915882ull, 0x0091732BFB3C0D96ull },
            { "assembly:/scenes/l04d/l04d_main.entity", "l04d", 0x800008760C5BF6C3ull, 0x8000012022F2A50Eull, 0x00E0FE5802A8911Dull },
            { "assembly:/scenes/l04e/l04e_main.entity", "l04e", 0x800002021BA1A9CBull, 0x800000766E70B9C7ull, 0x00182FF19AA446CDull },
            { "assembly:/scenes/l05a/l05a_main.entity", "l05a", 0x8000094CD3285297ull, 0x800001B9F197F330ull, 0x00DF4949C7C16E36ull },
            { "assembly:/scenes/l05b/l05b_main.entity", "l05b", 0x80000934793D5071ull, 0x800002089575BFC1ull, 0x00F43AFAFE50B03Eull },
            { "assembly:/scenes/l05c/l05c_main.entity", "l05c", 0x8000063880AF864Bull, 0x800000DB7DCA8652ull, 0x0086D9C416176354ull },
            { "assembly:/scenes/l06a/l06a_main.entity", "l06a", 0x8000072A63433197ull, 0x800001B00505BF68ull, 0x007C3E5B7493A7BFull },
            { "assembly:/scenes/l06d/l06d_main.entity", "l06d", 0x800009846094DB5Cull, 0x8000016125865949ull, 0x00B44D03C2803551ull },
            { "assembly:/scenes/l07a/l07a_main.entity", "l07a", 0x80000B1588B2C681ull, 0x800001E41D095F1Cull, 0x00DEDC25B719727Cull },
            { "assembly:/scenes/l08a/l08a_main.entity", "l08a", 0x800003C1A1F0FD63ull, 0x80000136451A4784ull, 0x003D7019546BEC03ull },
            { "assembly:/scenes/l08b/l08b_main.entity", "l08b", 0x80000A6BC7DB2865ull, 0x800001C8934CDA7Dull, 0x006AFCFC20CD72B9ull },
            { "assembly:/scenes/l09/l09_main.entity",   "l09",  0x8000043163639D28ull, 0x800000A32556606Aull, 0x00FB505947C037CBull },
            { "assembly:/scenes/l10/l10_main.entity",   "l10",  0x80000486B92DD006ull, 0x800000EAF01B0749ull, 0x00230277AD52CFBDull },
            { "assembly:/scenes/menu/menu_main.entity", "menu", 0x8000007CD7025963ull, 0x80000013B7D677CCull, 0x0028F873F20C3E6Bull },
        };

        std::string Lowered(const std::string& text)
        {
            std::string out = text;

            std::transform(out.begin(), out.end(), out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            return out;
        }
    }

    const SceneResources* FindSceneResources(const std::string& scenePath)
    {
        if (scenePath.empty())
        {
            return nullptr;
        }

        // HMA.ini spells the menu with capitals and the engine reports it in
        // lower case, so the same level arrives written two ways depending on
        // who is asked. Compare with neither.
        const std::string wanted = Lowered(scenePath);

        for (const SceneResources& entry : kScenes)
        {
            if (wanted == entry.scene)
            {
                return &entry;
            }
        }

        return nullptr;
    }

    const SceneResources* AllScenes(size_t& countOut)
    {
        countOut = sizeof(kScenes) / sizeof(kScenes[0]);

        return kScenes;
    }
}
