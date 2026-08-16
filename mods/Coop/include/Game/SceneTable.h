#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace Coop::Game
{
    // The three resources a level is made of.
    //
    // ZEntitySceneContext::SetSceneResources takes exactly three arguments and
    // this is what they are. Every one of them is a separate resource with its
    // own runtime ID, and none of them is the .entity path itself - that path
    // is a name for the level, not something the engine can load. Which is why
    // writing it into SSceneParameters and asking for a scene did nothing: the
    // parameters are what the game tells itself it is doing, and the resources
    // are what it actually loads.
    //
    // The URIs follow a fixed shape, so the whole set could be computed from
    // the scene name if the mod could hash a URI the way the engine does:
    //
    //     factory     [<scene>].pc_entitytemplate
    //     blueprint   [<scene>].pc_entityblueprint
    //     header lib  [[assembly:/common/pc.layoutconfig].pc_layoutdef](<scene>).pc_headerlib
    //
    // It cannot, and does not need to. There are twenty-one levels and they do
    // not change. The IDs below are the game's own, read out of the hash table
    // that ships with the SDK, so no hashing happens at runtime and nothing can
    // drift.
    struct SceneResources
    {
        const char* scene     = nullptr;
        const char* label     = nullptr;
        uint64_t    factory   = 0;
        uint64_t    blueprint = 0;
        uint64_t    headerLib = 0;
    };

    // Case-insensitive. Null when the path is not one of the game's levels,
    // which is the honest answer for a modded or misspelled scene: better to
    // refuse than to tear the world down and build nothing.
    const SceneResources* FindSceneResources(const std::string& scenePath);

    // Every level the mod knows how to load, in the order the game runs them.
    const SceneResources* AllScenes(size_t& countOut);
}
