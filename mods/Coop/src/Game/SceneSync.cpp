#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>

#include <Glacier/ZLevelManager.h>
#include <Glacier/Module/ZHitman5Module.h>
#include <Glacier/Scene/ZEntitySceneContext.h>
#include <Glacier/Resource/ZResourceManager.h>
#include <Glacier/Resource/ZResourcePtr.h>
#include <Glacier/Resource/ZRuntimeResourceID.h>
#include <Glacier/Entity/IEntityFactory.h>
#include <Glacier/Entity/IEntityBlueprintFactory.h>
#include <Glacier/Templates/TResourcePtr.h>
#include <Global.h>

#include "Game/SceneSync.h"
#include "Game/SceneTable.h"
#include "Diag/Diag.h"

namespace Coop::Game
{
    namespace
    {
        // Everything here reads or writes engine structures the mod does not
        // own, so every one is guarded. POD only in the guarded functions:
        // __try cannot share a function with anything that unwinds.
        bool CopySceneName(const void* levelManager, char* out, size_t capacity)
        {
            __try
            {
                const auto* manager = static_cast<const ZLevelManager*>(levelManager);

                const ZString& scene = manager->GetSceneParameters().sSceneResource;

                const char* text = scene.ToCString();

                if (!text)
                {
                    out[0] = '\0';
                    return false;
                }

                size_t i = 0;

                for (; i + 1 < capacity && text[i] != '\0'; ++i)
                {
                    out[i] = text[i];
                }

                out[i] = '\0';

                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                out[0] = '\0';
                return false;
            }
        }

        bool ReadCheckpointIndex(const void* levelManager, int& indexOut)
        {
            __try
            {
                const auto* manager = static_cast<const ZLevelManager*>(levelManager);

                indexOut = manager->GetSceneParameters().nCheckpointIndex;

                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        // A load in flight.
        //
        // Heap-allocated and deliberately never destroyed implicitly: it holds
        // three ZResourcePtrs, and ~ZResourcePtr calls Release on a stub inside
        // the engine. At static teardown the engine may already be gone, and a
        // leak of three pointers as the process exits is cheaper than a crash
        // on the way out with nothing in the log to explain it.
        struct Pending
        {
            // Taken by const reference and copied in the initialiser list, on
            // purpose. ZResourcePtr declares a copy constructor and a
            // destructor and no assignment operator, so the compiler generates
            // one that copies the stub pointer and adds no reference - and then
            // the source releases on its way out. Assigning these instead of
            // constructing them would leave this holding three pointers it does
            // not own and releasing three references it never took.
            Pending(const char* scenePath, const char* sceneLabel, int checkpointIndex,
                    const ZResourcePtr& sceneFactory,
                    const ZResourcePtr& sceneBlueprint,
                    const ZResourcePtr& sceneHeaderLib)
                : scene(scenePath)
                , label(sceneLabel)
                , checkpoint(checkpointIndex)
                , factory(sceneFactory)
                , blueprint(sceneBlueprint)
                , headerLib(sceneHeaderLib)
            {
            }

            std::string  scene;
            std::string  label;
            int          checkpoint = 0;

            ZResourcePtr factory;
            ZResourcePtr blueprint;
            ZResourcePtr headerLib;

            float waited = 0.f;
            bool  ready  = false;
        };

        Pending* g_pending = nullptr;

        // Read from the render thread, where the panel draws the button, and
        // written from the game thread. An enum is the whole of what crosses:
        // the reasons a load failed go back through Begin and Commit's error
        // parameter, so nothing here has to hand a std::string between threads.
        std::atomic<SceneSync::Stage> g_stage{ SceneSync::Stage::Idle };

        // Long enough for a disc to spin up and short enough that a player who
        // is going nowhere finds out.
        constexpr float kStreamTimeoutSeconds = 45.f;

        void Finish(SceneSync::Stage stage)
        {
            g_stage = stage;

            delete g_pending;
            g_pending = nullptr;
        }

        const char* Describe(const ZResourcePtr& resource)
        {
            if (!resource.Exists())  return "no stub";
            if (resource.Failed())   return "failed";
            if (resource.IsReady())  return "ready";

            return "streaming";
        }
    }

    bool SceneSync::IsMissionScene(const std::string& scene)
    {
        if (scene.empty())
        {
            return false;
        }

        // HMA.ini boots into assembly:/scenes/Menu/Menu_Main.entity, and every
        // front-end scene lives under the same folder. A player sitting in the
        // menu is not somewhere anybody should be offered a ride to - without
        // this, the one still in the menu announces it, and the one playing
        // gets a button that would drop them out of their own mission.
        std::string lowered = scene;

        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        return lowered.find("/menu") == std::string::npos
            && lowered.find("menu_") == std::string::npos;
    }

    std::string SceneSync::CurrentScene()
    {
        if (!LevelManager)
        {
            return {};
        }

        char name[512] = {};

        if (!CopySceneName(LevelManager, name, sizeof(name)))
        {
            return {};
        }

        return name;
    }

    int SceneSync::CurrentCheckpoint()
    {
        if (!LevelManager)
        {
            return -1;
        }

        int index = -1;

        return ReadCheckpointIndex(LevelManager, index) ? index : -1;
    }

    bool SceneSync::SetSceneName(const std::string& sceneResource)
    {
        if (!LevelManager || sceneResource.empty())
        {
            return false;
        }

        const ZString view(sceneResource.c_str());

        LevelManager->GetSceneParameters().sSceneResource = ZString::CopyFrom(view);

        return true;
    }

    SceneSync::Stage SceneSync::CurrentStage()
    {
        return g_stage.load();
    }

    const char* SceneSync::StatusLine()
    {
        switch (g_stage.load())
        {
            case Stage::Streaming: return "loading their level...";
            case Stage::Ready:     return "building it now";
            case Stage::Committed: return "asked the engine for it";
            case Stage::Failed:    return "it did not load";
            default:               return "";
        }
    }

    void SceneSync::Cancel()
    {
        if (g_pending)
        {
            Diag::Log("scene: cancelled the load of %s", g_pending->scene.c_str());
        }

        Finish(Stage::Idle);
    }

    bool SceneSync::Begin(const std::string& sceneResource, int checkpointIndex,
                          std::string& error)
    {
        error.clear();

        if (sceneResource.empty())
        {
            error = "no scene to go to - the host has not started anything yet";
            return false;
        }

        if (!ResourceManager)
        {
            error = "the resource manager is not up yet";
            return false;
        }

        if (!Hitman5Module || !Hitman5Module->GetSceneContext())
        {
            error = "there is no scene context to load through";
            return false;
        }

        const SceneResources* entry = FindSceneResources(sceneResource);

        if (!entry)
        {
            error = "the mod has no resource IDs for " + sceneResource;

            Diag::Log("scene: %s is not in the table - refusing to tear the world down for it",
                      sceneResource.c_str());

            return false;
        }

        // Anything already in flight loses. The player pressed the button
        // again, or a different peer's level arrived; either way the newer
        // answer is the one they want.
        Cancel();

        Diag::Log("scene: %s (%s) checkpoint %d - asking for its three resources",
                  entry->scene, entry->label, checkpointIndex);
        Diag::Log("scene:   factory   %016llX", entry->factory);
        Diag::Log("scene:   blueprint %016llX", entry->blueprint);
        Diag::Log("scene:   headerlib %016llX", entry->headerLib);

        // Priority zero is what the game's own transitions use. Anything else
        // would put this ahead of work the engine considers more urgent, which
        // is not a judgement a mod should be making.
        //
        // Initialised from the returned value rather than assigned, so the
        // reference the engine handed back is the one these hold.
        const ZResourcePtr factory   = ResourceManager->GetResourcePtr(ZRuntimeResourceID(entry->factory), 0);
        const ZResourcePtr blueprint = ResourceManager->GetResourcePtr(ZRuntimeResourceID(entry->blueprint), 0);
        const ZResourcePtr headerLib = ResourceManager->GetResourcePtr(ZRuntimeResourceID(entry->headerLib), 0);

        Diag::Log("scene: stubs - factory %s, blueprint %s, headerlib %s",
                  Describe(factory), Describe(blueprint), Describe(headerLib));

        if (!factory.Exists() || !blueprint.Exists() || !headerLib.Exists())
        {
            error = "the game does not recognise one of that level's resources";

            g_stage = Stage::Failed;

            return false;
        }

        g_pending = new Pending(entry->scene, entry->label,
                                checkpointIndex < 0 ? 0 : checkpointIndex,
                                factory, blueprint, headerLib);

        g_stage = Stage::Streaming;

        return true;
    }

    void SceneSync::Update(float deltaSeconds)
    {
        if (!g_pending || g_pending->ready)
        {
            return;
        }

        Pending& pending = *g_pending;

        pending.waited += deltaSeconds;

        if (pending.factory.Failed() || pending.blueprint.Failed() || pending.headerLib.Failed())
        {
            Diag::Log("scene: a resource failed to load - factory %s, blueprint %s, headerlib %s",
                      Describe(pending.factory), Describe(pending.blueprint),
                      Describe(pending.headerLib));

            Finish(Stage::Failed);

            return;
        }

        if (pending.factory.IsReady() && pending.blueprint.IsReady() && pending.headerLib.IsReady())
        {
            Diag::Log("scene: all three resources are in after %.1f s", pending.waited);

            pending.ready = true;

            g_stage = Stage::Ready;

            return;
        }

        if (pending.waited >= kStreamTimeoutSeconds)
        {
            Diag::Log("scene: gave up after %.0f s - factory %s, blueprint %s, headerlib %s",
                      pending.waited, Describe(pending.factory), Describe(pending.blueprint),
                      Describe(pending.headerLib));

            Finish(Stage::Failed);
        }
    }

    bool SceneSync::IsReadyToCommit()
    {
        return g_pending && g_pending->ready;
    }

    bool SceneSync::Commit(std::string& error)
    {
        error.clear();

        if (!IsReadyToCommit())
        {
            error = "nothing is ready to load";
            return false;
        }

        Pending& pending = *g_pending;

        if (!LevelManager || !Hitman5Module)
        {
            error = "the engine went away while the level was loading";

            Finish(Stage::Failed);

            return false;
        }

        ZEntitySceneContext* context = Hitman5Module->GetSceneContext();

        if (!context)
        {
            error = "there is no scene context to load through";

            Finish(Stage::Failed);

            return false;
        }

        // What the game tells itself it is doing. Not an input to the loader,
        // but everything downstream reads it - the HUD, the checkpoint manager,
        // and this mod's own idea of where it is - so it has to agree with what
        // is about to be built.
        //
        // ZString does not copy. Its const char* constructor sets the top bit
        // of the length, the flag for "not allocated", and keeps the pointer;
        // assigning from an unallocated one passes that pointer straight
        // through. The first version handed the engine a pointer into a
        // std::string this mod overwrites every three seconds. CopyFrom
        // allocates through the game's own allocator, which is what makes the
        // engine the owner of what it is holding.
        SSceneParameters& parameters = LevelManager->GetSceneParameters();

        const ZString view(pending.scene.c_str());

        parameters.sSceneResource   = ZString::CopyFrom(view);
        parameters.nCheckpointIndex = pending.checkpoint;
        parameters.bRestoring       = false;
        parameters.bUseSaveGame     = false;

        Diag::Log("scene: parameters written - %s at checkpoint %d",
                  pending.scene.c_str(), pending.checkpoint);

        // From here on the old world is going away, and every step is logged
        // before it runs rather than after. If the game dies inside one of
        // these, the last line in the log is the name of the call that did it -
        // which is the only thing a crash on the other side of the internet can
        // usefully tell us.
        Diag::Log("scene: ClearScene(true)");

        context->ClearScene(true);

        Diag::Log("scene: SetSceneResources");

        context->SetSceneResources(TResourcePtr<IEntityFactory>(pending.factory),
                                   TResourcePtr<IEntityBlueprintFactory>(pending.blueprint),
                                   pending.headerLib);

        // An empty streaming state is what a plain transition uses.
        Diag::Log("scene: CreateScene");

        context->CreateScene(ZString(""));

        // StartEntities is deliberately not called. The scene streams in over
        // several frames and the engine starts it when it is whole; calling it
        // here would start a world that is half built. If the level comes up
        // and nothing in it moves, this is the line to revisit.
        Diag::Log("scene: asked for %s (%s)", pending.scene.c_str(), pending.label.c_str());

        Finish(Stage::Committed);

        return true;
    }
}
