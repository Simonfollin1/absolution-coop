#pragma once

#include <string>

namespace Coop::Game
{
    // Following somebody into a mission.
    //
    // Three attempts got here. The first wrote the scene path into
    // SSceneParameters and called CreateScene - which instantiates resources
    // that were never set, so it correctly did nothing. The second requested
    // the level's factory and blueprint cold, before the level's package set
    // was mounted, and the engine never came back from the lookup: both
    // machines' logs end between two lines.
    //
    // The dev build's symbols settled how the game itself does it. The menu
    // calls ZLevelManager::SwitchToScene, which writes SSceneParameters, sets
    // m_bInSceneTransition, and warms a package list - and then the engine's
    // own main loop notices the flag and runs everything: loading screen,
    // teardown, resource resolution, scene build, entity start.
    //
    // So this does the same three things. Begin mounts the level's libraries
    // (the engine's own runtime-mount machinery, so everything is resident
    // before anything irreversible happens), Update watches the mount, and
    // Commit writes the parameters and sets the flag. The engine does the
    // rest, exactly as if the menu had asked.
    class SceneSync
    {
    public:
        enum class Stage
        {
            Idle,        // nothing asked for
            Streaming,   // the three resources are on their way
            Ready,       // all three are in memory, waiting to be committed
            Committed,   // the scene has been asked for
            Failed,
        };

        // The scene this game is in, from the level manager. Empty when it
        // cannot be read.
        static std::string CurrentScene();

        // Whether a scene name is a mission rather than the front end. The
        // menu is a scene like any other, and somebody sitting in it is not a
        // place to offer anyone a ride to.
        static bool IsMissionScene(const std::string& scene);

        // Which checkpoint the parameters were last loaded with, or -1.
        static int CurrentCheckpoint();

        // Starts a load: looks the level up and asks the resource manager for
        // its three resources. Returns false with a reason when the level is
        // not one the mod has IDs for, or when the engine is not up.
        //
        // Nothing is torn down here. This only puts requests in.
        static bool Begin(const std::string& sceneResource, int checkpointIndex,
                          std::string& error);

        // Game thread, once a frame. Watches the three resources and moves the
        // stage on when they are all in - or gives up on them.
        static void Update(float deltaSeconds);

        // True once all three are in memory. The caller commits, rather than
        // this doing it by itself, because everything the mod is holding that
        // points into the world - the patched vtable on the player, the
        // spectator camera, the actor list - has to be let go of first, and
        // only the caller knows about those.
        static bool IsReadyToCommit();

        // Starts the engine's own transition. After this returns, the engine
        // owns everything that happens next - loading screen included.
        static bool Commit(std::string& error);

        // Clears the transition flag this mod set, for when the watchdog
        // decides nothing is going to happen.
        static void AbortEngineTransition();

        // Watches the level manager's transition window - the flag and the
        // fields beside it - and logs every change. Called once a frame. This
        // is how the +0x34 reading gets confirmed against the game's own
        // transitions before anything is bet on it.
        static void TraceTransitionWindow();

        // Throws away a load in progress without touching the world.
        static void Cancel();

        // Both are read from the render thread while the game thread runs the
        // load, so what crosses is an enum and a pointer to a string literal.
        // Why a load failed goes back through Begin's and Commit's error
        // parameter instead, on the thread that asked.
        static Stage       CurrentStage();
        static const char* StatusLine();

        // Writes the scene name and nothing else.
        //
        // Needed because a load writes the destination into SSceneParameters
        // before anything else happens, so a load that fails leaves the game
        // claiming to be somewhere it never went. A game that admits the
        // attempt failed is better than one that believes it arrived - the
        // second takes the button to try again away with it.
        static bool SetSceneName(const std::string& sceneResource);
    };
}
