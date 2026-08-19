#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <new>

#include <Glacier/ZLevelManager.h>
#include <Glacier/Module/ZHitman5Module.h>
#include <Glacier/Scene/ZEntitySceneContext.h>
#include <Glacier/Resource/ZResourceManager.h>
#include <Glacier/Resource/ZResourcePtr.h>
#include <Glacier/Resource/ZRuntimeResourceID.h>
#include <Glacier/Resource/ZDynamicResourceLibrary.h>
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

        // The general guard the loading below runs under.
        //
        // The first version called the resource manager bare, and asking it
        // for a level's factory before that level's libraries were mounted
        // killed the game between two log lines - twice, on two machines, with
        // no crash report either time. Every engine call on this path now goes
        // through here: a fault becomes a logged failure and a working game.
        //
        // The called function may construct C++ objects (this function must
        // not - C2712). A fault mid-construction leaks whatever was alive,
        // which is the accepted price of not dying.
        using GuardedFn = void(__cdecl*)(void*);

        bool RunGuarded(GuardedFn fn, void* context)
        {
            __try
            {
                fn(context);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        // ---- the guarded engine touches, one small function each ----------

        struct MountContext
        {
            uint64_t                 headerLibId = 0;
            ZDynamicResourceLibrary* library     = nullptr;
        };

        void __cdecl DoMount(void* context)
        {
            auto* mount = static_cast<MountContext*>(context);

            // Delayed entities, zero instances: this is a mount, not a spawn.
            // The scene context builds the level; this only makes the level's
            // packages resident so the scene context has something to build
            // from.
            mount->library = new ZDynamicResourceLibrary(
                ZRuntimeResourceID(mount->headerLibId), true, 0);
        }

        struct PollContext
        {
            ZDynamicResourceLibrary* library = nullptr;
            bool                     ready   = false;
            bool                     failed  = false;
        };

        void __cdecl DoPoll(void* context)
        {
            auto* poll = static_cast<PollContext*>(context);

            // IsReady() answers true for a failed library too, so failed is
            // checked first and ready means actually ready.
            poll->failed = poll->library->IsFailed();
            poll->ready  = !poll->failed && poll->library->IsReady();
        }

        struct FreeContext
        {
            ZDynamicResourceLibrary* library = nullptr;
        };

        void __cdecl DoFree(void* context)
        {
            delete static_cast<FreeContext*>(context)->library;
        }

        struct FetchContext
        {
            uint64_t id      = 0;
            void*    storage = nullptr;   // sizeof(ZResourcePtr) of raw space
        };

        void __cdecl DoFetch(void* context)
        {
            auto* fetch = static_cast<FetchContext*>(context);

            // Placement copy-construction, not assignment. ZResourcePtr has a
            // copy constructor and a destructor and no assignment operator, so
            // the generated assignment copies the stub pointer without taking
            // a reference - and then the temporary releases on its way out.
            new (fetch->storage) ZResourcePtr(
                ResourceManager->GetResourcePtr(ZRuntimeResourceID(fetch->id), 0));
        }

        // A ZResourcePtr fetched under guard. Holds raw storage so nothing is
        // constructed unless the engine call actually completed.
        class FetchedPtr
        {
        public:
            FetchedPtr() = default;

            FetchedPtr(const FetchedPtr&)            = delete;
            FetchedPtr& operator=(const FetchedPtr&) = delete;

            ~FetchedPtr()
            {
                if (m_constructed)
                {
                    Get().~ZResourcePtr();
                }
            }

            bool Fetch(uint64_t id, const char* what)
            {
                Diag::Log("scene: requesting the %s (%016llX)", what, id);

                FetchContext context;
                context.id      = id;
                context.storage = m_storage;

                m_constructed = RunGuarded(&DoFetch, &context);

                if (!m_constructed)
                {
                    Diag::Log("scene: requesting the %s faulted - backing out", what);
                }

                return m_constructed;
            }

            ZResourcePtr& Get()
            {
                return *reinterpret_cast<ZResourcePtr*>(m_storage);
            }

        private:
            alignas(void*) unsigned char m_storage[sizeof(ZResourcePtr)]{};
            bool m_constructed = false;
        };

        const char* Describe(ZResourcePtr& resource)
        {
            if (!resource.Exists())  return "no stub";
            if (resource.Failed())   return "failed";
            if (resource.IsReady())  return "ready";

            return "streaming";
        }

        // A load in flight.
        struct Pending
        {
            Pending(const SceneResources* sceneEntry, int checkpointIndex)
                : entry(sceneEntry)
                , checkpoint(checkpointIndex)
            {
            }

            const SceneResources* entry      = nullptr;  // static table
            int                   checkpoint = 0;

            // The mount. Owned here until Commit hands it to g_keptMounted.
            ZDynamicResourceLibrary* library = nullptr;

            FetchedPtr factory;
            FetchedPtr blueprint;
            FetchedPtr headerLib;

            float waited  = 0.f;
            bool  fetched = false;   // the three stubs have been requested
            bool  ready   = false;
        };

        Pending* g_pending = nullptr;

        // The mount belonging to the scene that was last committed. The scene
        // holds its own references through SetSceneResources, but releasing
        // the library set while the level it backs is still streaming in is
        // not a risk worth taking to save a little bookkeeping - so it lives
        // until the next load replaces it.
        ZDynamicResourceLibrary* g_keptMounted = nullptr;

        // Read from the render thread, where the panel draws the button, and
        // written from the game thread. An enum is the whole of what crosses.
        std::atomic<SceneSync::Stage> g_stage{ SceneSync::Stage::Idle };

        // A level is hundreds of megabytes and the mount is most of the wait.
        constexpr float kStreamTimeoutSeconds = 90.f;

        void FreeMount(ZDynamicResourceLibrary*& library, const char* what)
        {
            if (!library)
            {
                return;
            }

            FreeContext context;
            context.library = library;
            library         = nullptr;

            if (!RunGuarded(&DoFree, &context))
            {
                Diag::Log("scene: releasing %s faulted - leaking it instead", what);
            }
        }

        void Finish(SceneSync::Stage stage)
        {
            g_stage = stage;

            if (g_pending)
            {
                FreeMount(g_pending->library, "the in-flight mount");

                delete g_pending;
                g_pending = nullptr;
            }
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
            Diag::Log("scene: cancelled the load of %s", g_pending->entry->scene);
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

        Diag::Log("scene: %s (%s) checkpoint %d", entry->scene, entry->label, checkpointIndex);

        // The order is the whole fix. The factory and blueprint are library
        // resources - the top bit of their IDs says so - and they live inside
        // the level's own package set, which is not mounted while the player
        // is somewhere else. The first version asked for them cold, and the
        // engine died looking up a library that was not resident: the log
        // ended between two lines, twice, on two machines.
        //
        // So the header library is mounted first, through the same machinery
        // the SDK's own mods use to spawn things from other levels' packages
        // at runtime. Only when the whole set is resident are the scene's
        // resources worth asking for.
        Diag::Log("scene: mounting the level's libraries via %016llX", entry->headerLib);

        MountContext mount;
        mount.headerLibId = entry->headerLib;

        if (!RunGuarded(&DoMount, &mount) || !mount.library)
        {
            FreeMount(mount.library, "the failed mount");

            error = "the engine faulted mounting the level's libraries - the log has the last step";

            g_stage = Stage::Failed;

            return false;
        }

        g_pending = new Pending(entry, checkpointIndex < 0 ? 0 : checkpointIndex);

        g_pending->library = mount.library;

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

        // Phase one: the mount. The engine streams the library set in on its
        // own threads while the game keeps running; this only watches.
        if (!pending.fetched)
        {
            PollContext poll;
            poll.library = pending.library;

            if (!RunGuarded(&DoPoll, &poll))
            {
                Diag::Log("scene: polling the mount faulted");
                Finish(Stage::Failed);

                return;
            }

            if (poll.failed)
            {
                Diag::Log("scene: the library set failed to load after %.1f s", pending.waited);
                Finish(Stage::Failed);

                return;
            }

            if (!poll.ready)
            {
                if (pending.waited >= kStreamTimeoutSeconds)
                {
                    Diag::Log("scene: gave up on the mount after %.0f s", pending.waited);
                    Finish(Stage::Failed);
                }

                return;
            }

            Diag::Log("scene: library set resident after %.1f s - requesting the scene's resources",
                      pending.waited);

            if (!pending.factory.Fetch(pending.entry->factory, "factory") ||
                !pending.blueprint.Fetch(pending.entry->blueprint, "blueprint") ||
                !pending.headerLib.Fetch(pending.entry->headerLib, "header library"))
            {
                Finish(Stage::Failed);

                return;
            }

            Diag::Log("scene: stubs - factory %s, blueprint %s, headerlib %s",
                      Describe(pending.factory.Get()), Describe(pending.blueprint.Get()),
                      Describe(pending.headerLib.Get()));

            if (!pending.factory.Get().Exists() ||
                !pending.blueprint.Get().Exists() ||
                !pending.headerLib.Get().Exists())
            {
                Diag::Log("scene: the game does not recognise one of the level's resources");
                Finish(Stage::Failed);

                return;
            }

            pending.fetched = true;

            return;
        }

        // Phase two: the three stubs themselves. With the set resident these
        // are usually ready immediately; this covers the gap if they are not.
        if (pending.factory.Get().Failed() ||
            pending.blueprint.Get().Failed() ||
            pending.headerLib.Get().Failed())
        {
            Diag::Log("scene: a resource failed - factory %s, blueprint %s, headerlib %s",
                      Describe(pending.factory.Get()), Describe(pending.blueprint.Get()),
                      Describe(pending.headerLib.Get()));

            Finish(Stage::Failed);

            return;
        }

        if (pending.factory.Get().IsReady() &&
            pending.blueprint.Get().IsReady() &&
            pending.headerLib.Get().IsReady())
        {
            Diag::Log("scene: everything is in after %.1f s", pending.waited);

            pending.ready = true;

            g_stage = Stage::Ready;

            return;
        }

        if (pending.waited >= kStreamTimeoutSeconds)
        {
            Diag::Log("scene: gave up after %.0f s - factory %s, blueprint %s, headerlib %s",
                      pending.waited, Describe(pending.factory.Get()),
                      Describe(pending.blueprint.Get()), Describe(pending.headerLib.Get()));

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
        // through. CopyFrom allocates through the game's own allocator, which
        // is what makes the engine the owner of what it is holding.
        SSceneParameters& parameters = LevelManager->GetSceneParameters();

        const ZString view(pending.entry->scene);

        parameters.sSceneResource   = ZString::CopyFrom(view);
        parameters.nCheckpointIndex = pending.checkpoint;
        parameters.bRestoring       = false;
        parameters.bUseSaveGame     = false;

        Diag::Log("scene: parameters written - %s at checkpoint %d",
                  pending.entry->scene, pending.checkpoint);

        // The mount kept from the previous transition, if any, has done its
        // job - that world is about to be torn down.
        FreeMount(g_keptMounted, "the previous level's mount");

        // From here on the old world is going away, and every step is logged
        // before it runs rather than after. If the game dies inside one of
        // these, the last line in the log is the name of the call that did it.
        Diag::Log("scene: ClearScene(true)");

        context->ClearScene(true);

        Diag::Log("scene: SetSceneResources");

        context->SetSceneResources(TResourcePtr<IEntityFactory>(pending.factory.Get()),
                                   TResourcePtr<IEntityBlueprintFactory>(pending.blueprint.Get()),
                                   pending.headerLib.Get());

        // An empty streaming state is what a plain transition uses.
        Diag::Log("scene: CreateScene");

        context->CreateScene(ZString(""));

        // StartEntities is deliberately not called. The scene streams in over
        // several frames and the engine starts it when it is whole; calling it
        // here would start a world that is half built. If the level comes up
        // and nothing in it moves, this is the line to revisit.
        Diag::Log("scene: asked for %s (%s)", pending.entry->scene, pending.entry->label);

        // The new scene builds out of the libraries this mount holds resident,
        // so it outlives the Pending and is only released when the transition
        // after this one replaces it.
        g_keptMounted   = pending.library;
        pending.library = nullptr;

        Finish(Stage::Committed);

        return true;
    }
}
