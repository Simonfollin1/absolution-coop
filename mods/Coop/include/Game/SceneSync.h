#pragma once

#include <string>

namespace Coop::Game
{
    // Following somebody into a mission.
    //
    // The first session with two machines connected got as far as both players
    // being in a session and no further: the host started a mission and the
    // other player stayed in the menu, listed as "elsewhere", with nothing he
    // could do about it. Nothing in the mod replicated *which* level anybody
    // was in, only where they were standing inside one - and the progression
    // rules deliberately refuse to pull anybody across a chapter boundary,
    // which is exactly what starting a mission is.
    //
    // The engine makes this simpler than it sounds. ZLevelManager hands out its
    // SSceneParameters by non-const reference, and the first field is the scene
    // resource - the path the game itself hands to the loader on every
    // transition. ZHitman5Module::GetSceneContext gives the loader. So the
    // whole of "come to where I am" is: read the host's scene name, put it in
    // ours, and ask for it.
    //
    // Not automatic. A level load throws away everything the player was doing,
    // and doing that to somebody because a packet arrived is not a decision the
    // mod gets to make on its own - the panel offers it and the player takes it.
    class SceneSync
    {
    public:
        // The scene this game is in, from the level manager. Empty when it
        // cannot be read.
        static std::string CurrentScene();

        // Whether a scene name is a mission rather than the front end. The
        // menu is a scene like any other, and somebody sitting in it is not a
        // place to offer anyone a ride to.
        static bool IsMissionScene(const std::string& scene);

        // Which checkpoint the parameters were last loaded with, or -1.
        static int CurrentCheckpoint();

        // Loads a scene by resource name, at a checkpoint. Returns false with a
        // reason when it could not even be attempted.
        //
        // This is the game's own transition path, the one the SDK's free camera
        // already hooks - not a teleport and not a trick. Everything the player
        // had in the old level is gone afterwards, which is what starting a
        // mission means.
        static bool LoadScene(const std::string& sceneResource, int checkpointIndex,
                              std::string& error);

        // Writes the scene name and nothing else.
        //
        // Needed because asking for a load changes what the game says about
        // itself whether or not anything loads, and something has to be able to
        // put that back. A game claiming to be in a level it never went to is
        // worse than one that admits the attempt failed - it takes the button
        // to try again away with it.
        static bool SetSceneName(const std::string& sceneResource);
    };
}
