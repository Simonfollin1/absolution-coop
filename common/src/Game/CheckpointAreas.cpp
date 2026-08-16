#include "Game/CheckpointAreas.h"

#include <cstdio>

namespace
{
    using CheckpointAreas::Area;
    using CheckpointAreas::Flag;

    // Ordered exactly like the autosplitter's if/else chain, so the more
    // specific entries (those with flag conditions) are matched first.
    constexpr Area AREAS[] = {
        {  0, 1, Flag::Any,       Flag::Any,      Flag::Any,       "C0_Garden",              "Garden",                    0 },
        {  0, 2, Flag::Any,       Flag::Any,      Flag::Any,       "C0_Greenhouse",          "Greenhouse",                0 },
        {  0, 3, Flag::Any,       Flag::Any,      Flag::Any,       "C0_Cliffside",           "Cliffside",                 0 },
        {  0, 4, Flag::Forbidden, Flag::Any,      Flag::Any,       "C0_Mansion",             "Mansion",                   0 },
        {  0, 4, Flag::Required,  Flag::Any,      Flag::Any,       "C0_Kottidor",            "Kott Idor",                 0 },

        {  1, 0, Flag::Required,  Flag::Any,      Flag::Any,       "C1_Chinatown",           "Chinatown",                 1 },

        {  2, 0, Flag::Any,       Flag::Required, Flag::Any,       "C2_TerminusBottom",      "Terminus - Bottom",         2 },
        {  2, 3, Flag::Required,  Flag::Any,      Flag::Any,       "C2_TerminusUpper",       "Terminus - Upper",          2 },

        {  3, 1, Flag::Any,       Flag::Any,      Flag::Any,       "C3_BurningHotel",        "Burning Hotel",             3 },
        {  3, 2, Flag::Any,       Flag::Any,      Flag::Any,       "C3_Libary",              "Library",                   3 },
        {  3, 4, Flag::Any,       Flag::Any,      Flag::Any,       "C3_PigeonCoop",          "Pigeon Coop",               3 },
        {  3, 5, Flag::Any,       Flag::Any,      Flag::Any,       "C3_ShangriLa",           "Shangri-La",                3 },
        {  3, 6, Flag::Required,  Flag::Any,      Flag::Any,       "C3_Trainstation",        "Train Station",             3 },

        {  4, 1, Flag::Any,       Flag::Any,      Flag::Any,       "C4_Countryard",          "Courtyard",                 4 },
        {  4, 2, Flag::Any,       Flag::Any,      Flag::Any,       "C4_VixenClub",           "Vixen Club",                4 },
        {  4, 3, Flag::Any,       Flag::Any,      Flag::Any,       "C4_DressingRooms",       "Dressing Rooms",            4 },
        {  4, 4, Flag::Any,       Flag::Any,      Flag::Any,       "C4_DerelictBuilding",    "Derelict Building",         4 },
        {  4, 5, Flag::Any,       Flag::Any,      Flag::Any,       "C4_Conveniencestore",    "Convenience Store",         4 },
        {  4, 6, Flag::Any,       Flag::Any,      Flag::Any,       "C4_LoadingArea",         "Loading Area",              4 },
        {  4, 7, Flag::Required,  Flag::Any,      Flag::Any,       "C4_ChineseNewYear",      "Chinese New Year",          4 },

        {  6, 1, Flag::Any,       Flag::Any,      Flag::Any,       "C6_Victoria",            "Victoria",                  6 },
        {  6, 2, Flag::Any,       Flag::Any,      Flag::Any,       "C6_Orphanage",           "Orphanage",                 6 },
        {  6, 3, Flag::Required,  Flag::Any,      Flag::Any,       "C6_CentralHeating",      "Central Heating",           6 },

        {  7, 0, Flag::Required,  Flag::Any,      Flag::Any,       "C7_BallsOfFire",         "Balls of Fire",             7 },

        {  8, 0, Flag::Required,  Flag::Any,      Flag::Any,       "C8_GunShop",             "Gun Shop",                  8 },

        {  9, 1, Flag::Any,       Flag::Any,      Flag::Any,       "C9_StreetsOfHope",       "Streets of Hope",           9 },
        {  9, 2, Flag::Required,  Flag::Any,      Flag::Any,       "C9_Barbershop",          "Barbershop",                9 },

        { 10, 0, Flag::Required,  Flag::Any,      Flag::Any,       "C10_TheDesert",          "The Desert",               10 },

        { 11, 2, Flag::Any,       Flag::Any,      Flag::Any,       "C11_DeadEnd",            "Dead End",                 11 },
        { 11, 3, Flag::Any,       Flag::Any,      Flag::Any,       "C11_OldMill",            "Old Mill",                 11 },
        { 11, 4, Flag::Forbidden, Flag::Any,      Flag::Any,       "C11_Descent",            "Descent",                  11 },
        { 11, 4, Flag::Required,  Flag::Any,      Flag::Any,       "C11_FactoryCompound",    "Factory Compound",         11 },

        { 12, 1, Flag::Any,       Flag::Any,      Flag::Any,       "C12_TestFacility",       "Test Facility",            12 },
        { 12, 2, Flag::Forbidden, Flag::Any,      Flag::Any,       "C12_Decontamination",    "Decontamination",          12 },
        { 12, 2, Flag::Required,  Flag::Any,      Flag::Any,       "C12_RnD",                "R&D",                      12 },

        { 13, 1, Flag::Forbidden, Flag::Any,      Flag::Any,       "C13_PatriotsHangar",     "Patriots Hangar",          13 },
        { 13, 1, Flag::Required,  Flag::Any,      Flag::Any,       "C13_Arena",              "Arena",                    13 },

        { 14, 1, Flag::Any,       Flag::Any,      Flag::Any,       "C14_Parking",            "Parking",                  14 },
        { 14, 2, Flag::Any,       Flag::Any,      Flag::Any,       "C14_Reception",          "Reception",                14 },
        { 14, 3, Flag::Required,  Flag::Any,      Flag::Any,       "C14_Cornfield",          "Cornfield",                14 },

        { 17, 1, Flag::Any,       Flag::Any,      Flag::Any,       "C17_Courthouse",         "Courthouse",               17 },
        { 17, 2, Flag::Any,       Flag::Any,      Flag::Any,       "C17_HoldingCells",       "Holding Cells",            17 },
        { 17, 3, Flag::Required,  Flag::Any,      Flag::Any,       "C17_Prision",            "Prison",                   17 },

        { 18, 1, Flag::Any,       Flag::Any,      Flag::Any,       "C18_CountyJail",         "County Jail",              18 },
        { 18, 2, Flag::Any,       Flag::Any,      Flag::Any,       "C18_Outgunned",          "Outgunned",                18 },
        { 18, 3, Flag::Any,       Flag::Any,      Flag::Any,       "C18_Burn",               "Burn",                     18 },
        { 18, 5, Flag::Required,  Flag::Any,      Flag::Any,       "C18_HopeFiar",           "Hope Fair",                18 },

        { 21, 0, Flag::Required,  Flag::Any,      Flag::Any,       "C21_TailorShop",         "Tailor Shop",              21 },

        { 22, 1, Flag::Any,       Flag::Any,      Flag::Any,       "C22_BlackwaterPark",     "Blackwater Park",          22 },
        { 22, 2, Flag::Required,  Flag::Any,      Flag::Any,       "C22_Penthouse",          "Penthouse",                22 },

        { 24, 0, Flag::Required,  Flag::Any,      Flag::Any,       "C24_BlackwaterRoof",     "Blackwater Roof",          24 },

        { 25, 1, Flag::Any,       Flag::Any,      Flag::Any,       "C25_CementaryEntrence",  "Cemetery Entrance",        25 },
        { 25, 2, Flag::Any,       Flag::Any,      Flag::Forbidden, "C25_BurnwoodFamilyTomb", "Burnwood Family Tomb",     25 },
        { 25, 2, Flag::Any,       Flag::Any,      Flag::Required,  "C25_Crematorium",        "Crematorium",              25 },
    };

    constexpr unsigned int AREA_COUNT = sizeof(AREAS) / sizeof(AREAS[0]);

    bool FlagMatches(Flag flag, bool value)
    {
        switch (flag)
        {
        case Flag::Required:  return value;
        case Flag::Forbidden: return !value;
        case Flag::Any:
        default:              return true;
        }
    }
}

const CheckpointAreas::Area* CheckpointAreas::Resolve(const State& state)
{
    // Two passes: prefer an entry that constrains at least one flag, so the
    // specific variants win over the catch-all with the same level/section.
    for (int pass = 0; pass < 2; ++pass)
    {
        const bool wantSpecific = (pass == 0);

        for (unsigned int i = 0; i < AREA_COUNT; ++i)
        {
            const Area& area = AREAS[i];

            if (area.level != state.level || area.section != state.section)
            {
                continue;
            }

            const bool isSpecific =
                area.resultScreen     != Flag::Any ||
                area.terminusElevator != Flag::Any ||
                area.finaleExplosion  != Flag::Any;

            if (isSpecific != wantSpecific)
            {
                continue;
            }

            if (FlagMatches(area.resultScreen,     state.resultScreen) &&
                FlagMatches(area.terminusElevator, state.terminusElevator) &&
                FlagMatches(area.finaleExplosion,  state.finaleExplosion))
            {
                return &area;
            }
        }
    }

    return nullptr;
}

const char* CheckpointAreas::ResolveName(const State& state, char* fallbackBuffer, unsigned int bufferSize)
{
    const Area* area = Resolve(state);

    if (area)
    {
        return area->name;
    }

    std::snprintf(fallbackBuffer, bufferSize, "Level %d / Section %d", state.level, state.section);

    return fallbackBuffer;
}

const CheckpointAreas::Area* CheckpointAreas::GetAll(unsigned int& countOut)
{
    countOut = AREA_COUNT;

    return AREAS;
}
