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
    // was in, only where they were standing inside one.
    //
    // The first attempt at fixing that wrote the host's scene path into
    // SSceneParameters and called CreateScene, on the reasoning that this is
    // what the game does to itself on every transition. It returned cleanly and
    // nothing happened, twice, on two machines.
    //
    // It was never going to. SSceneParameters is what the game tells itself it
    // is doing - the name of the level, the checkpoint, whether this is a
    // restore. It is not an input to the loader. The loader takes three
    // resources, through SetSceneResources, and CreateScene only instantiates
    // whatever is already there. With no resources set there was nothing to
    // instantiate, so CreateScene did the only correct thing available to it.
    //
    // SceneTable has the resources. What is left is the waiting: they stream in
    // asynchronously, so asking for them and building the scene in the same
    // frame gets three null pointers. Hence a load that spans frames.
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

        // Hands the resources to the scene context and asks for the scene.
        // After this returns, nothing that was in the old world is valid.
        static bool Commit(std::string& error);

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
