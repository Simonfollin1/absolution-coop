#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
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

#include "Game/BuildInfo.h"
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

        // ZLevelManager's transition window.
        //
        // The dev build's ZLevelManager, mined from its PDBs, declares in
        // order: SSceneParameters m_SceneTransitionData; bool
        // m_bInSceneTransition; TResourcePtr<SPackageListData>
        // m_pLoadingPackageList; ZString m_PreloadedSceneID. The retail SDK
        // hides everything after the parameters in PAD(0x10) - and the pad is
        // exactly bool+padding (4) + pointer (4) + ZString (8). So the flag
        // the engine's main loop watches is one byte at +0x34.
        //
        // ZLevelManager::SwitchToScene - the function the menu itself goes
        // through - does three things: writes m_SceneTransitionData, sets this
        // flag, and warms a package list. The first version of this file wrote
        // the parameters and then tried to run the load by hand; the flag is
        // what actually hands the job to the engine, loading screen and all.
        constexpr uintptr_t kInSceneTransitionOffset = 0x34;
        constexpr size_t    kTransitionWindowBytes   = 0x10;

        bool ReadTransitionWindow(const void* levelManager, uint8_t* out16)
        {
            __try
            {
                const uint8_t* window =
                    reinterpret_cast<const uint8_t*>(levelManager) + kInSceneTransitionOffset;

                for (size_t i = 0; i < kTransitionWindowBytes; ++i)
                {
                    out16[i] = window[i];
                }

                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool WriteTransitionFlag(void* levelManager, uint8_t value, uint8_t* previousOut)
        {
            __try
            {
                uint8_t* flag =
                    reinterpret_cast<uint8_t*>(levelManager) + kInSceneTransitionOffset;

                *previousOut = *flag;
                *flag        = value;

                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        // The scene context's vtable, verified rather than trusted.
        //
        // Two of its slots have addresses every shipped mod hooks: ClearScene
        // at base+0x265A80 and CreateScene(const ZString&) at base+0x4479E0.
        // Finding those two in the live table anchors every neighbour by
        // counting declarations - which sidesteps both the SDK header being
        // wrong and MSVC's habit of laying adjacent overloads out in reverse.
        constexpr uintptr_t kClearSceneRva  = 0x265A80;
        constexpr uintptr_t kCreateSceneRva = 0x4479E0;
        constexpr size_t    kVtableScan     = 32;

        struct VtableMap
        {
            void** table        = nullptr;
            int    clearScene   = -1;   // anchor
            int    createScene  = -1;   // anchor, the ZString overload
            bool   valid        = false;
        };

        bool ReadVtable(const void* object, uintptr_t moduleBase, VtableMap* out)
        {
            __try
            {
                void** table = *reinterpret_cast<void** const*>(object);

                out->table = table;

                for (size_t i = 0; i < kVtableScan; ++i)
                {
                    const uintptr_t entry = reinterpret_cast<uintptr_t>(table[i]);

                    if (entry == moduleBase + kClearSceneRva)  out->clearScene  = static_cast<int>(i);
                    if (entry == moduleBase + kCreateSceneRva) out->createScene = static_cast<int>(i);
                }

                out->valid = out->clearScene >= 0 && out->createScene >= 0;

                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool ReadVtableEntry(void** table, size_t index, uintptr_t* out)
        {
            __try
            {
                *out = reinterpret_cast<uintptr_t>(table[index]);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        void DumpVtable(const VtableMap& map)
        {
            Diag::Log("scene: vtable anchors - ClearScene at slot %d, CreateScene(ZString) at slot %d",
                      map.clearScene, map.createScene);

            for (size_t i = 0; i < kVtableScan; ++i)
            {
                uintptr_t entry = 0;

                if (ReadVtableEntry(map.table, i, &entry))
                {
                    Diag::Log("scene:   vtbl[%02u] = HMA.exe+0x%06X%s",
                              static_cast<unsigned>(i),
                              static_cast<unsigned>(entry - BaseAddress),
                              static_cast<int>(i) == map.clearScene  ? "  <- ClearScene" :
                              static_cast<int>(i) == map.createScene ? "  <- CreateScene(ZString)" : "");
                }
            }
        }

        // Slot positions counted from ClearScene in the interface declaration:
        //   +1 PrepareNewScene   +4 ResetScene        +5 SetSceneResources
        //   +16 StartEntities    +20 SetSceneInitParameters
        // CreateScene(ZString) is not counted - its own anchor covers it, so
        // the overload-order question never matters.
        constexpr int kRelPrepareNewScene        = 1;
        constexpr int kRelSetSceneResources      = 5;
        constexpr int kRelStartEntities          = 16;
        constexpr int kRelSetSceneInitParameters = 20;

        // One guarded pipeline step. POD, because it rides through RunGuarded;
        // the C++ argument objects are built inside the callee, so a fault in
        // any step is a logged failure rather than a dead game - the same rule
        // every other engine touch in this file follows.
        enum FallbackStep
        {
            kStepClearScene = 0,
            kStepPrepareNewScene,
            kStepSetSceneInitParameters,
            kStepSetSceneResources,
            kStepCreateScene,
            kStepStartEntities,
        };

        struct FallbackStepContext
        {
            void*         context   = nullptr;
            void*         function  = nullptr;
            int           step      = 0;
            const char*   scene     = nullptr;
            ZResourcePtr* factory   = nullptr;
            ZResourcePtr* blueprint = nullptr;
            ZResourcePtr* headerLib = nullptr;
        };

        void __cdecl DoFallbackStep(void* raw)
        {
            auto* call = static_cast<FallbackStepContext*>(raw);

            switch (call->step)
            {
            case kStepClearScene:
                reinterpret_cast<void(__thiscall*)(void*, bool)>(call->function)(
                    call->context, true);
                break;

            case kStepPrepareNewScene:
            case kStepStartEntities:
                reinterpret_cast<void(__thiscall*)(void*)>(call->function)(call->context);
                break;

            case kStepSetSceneInitParameters:
            {
                SSceneInitParameters initParameters;

                const ZString scenePath(call->scene);
                const ZString streaming("");

                initParameters.m_SceneResource  = ZString::CopyFrom(scenePath);
                initParameters.m_StreamingState = ZString::CopyFrom(streaming);

                reinterpret_cast<void(__thiscall*)(void*, const SSceneInitParameters&)>(
                    call->function)(call->context, initParameters);

                break;
            }

            case kStepSetSceneResources:
                reinterpret_cast<void(__thiscall*)(void*, TResourcePtr<IEntityFactory>,
                                                   TResourcePtr<IEntityBlueprintFactory>,
                                                   ZResourcePtr)>(call->function)(
                    call->context,
                    TResourcePtr<IEntityFactory>(*call->factory),
                    TResourcePtr<IEntityBlueprintFactory>(*call->blueprint),
                    *call->headerLib);
                break;

            case kStepCreateScene:
                reinterpret_cast<void(__thiscall*)(void*, const ZString&)>(call->function)(
                    call->context, ZString(""));
                break;
            }
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
            bool  mountReady = false; // the library set finished streaming
            bool  fetched = false;   // the three stubs have been requested
            bool  ready   = false;
            bool  fellBack = false;  // the manual pipeline has been tried
            bool  committed = false; // the engine has been handed the load
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

            if (!g_pending)
            {
                return;
            }

            // The stubs hold references into the library's package set, so the
            // bookkeeping - and its three ZResourcePtrs - is destroyed before
            // the library is touched, never after.
            ZDynamicResourceLibrary* library    = g_pending->library;
            const bool               committed  = g_pending->committed;
            const bool               mountReady = g_pending->mountReady;

            g_pending->library = nullptr;

            delete g_pending;
            g_pending = nullptr;

            if (!library)
            {
                return;
            }

            if (committed)
            {
                // The engine was asked to build out of this set, and a load
                // that looks like a failure from here can still be an arrival
                // the watchdog failed to notice. Once committed, the mount is
                // never freed on suspicion: it is parked exactly as a
                // confirmed arrival parks it, and the next load replaces it.
                FreeMount(g_keptMounted, "the previous level's mount");

                g_keptMounted = library;

                return;
            }

            if (mountReady)
            {
                FreeMount(library, "the in-flight mount");

                return;
            }

            // Still streaming. The engine's loader keeps a callback registered
            // on a loading library, and the SDK's destructor cannot deregister
            // the engine's own delegate - so freeing it here leaves the loader
            // a pointer into freed memory. Left standing on purpose.
            Diag::Log("scene: leaving a half-streamed mount resident rather "
                      "than freeing it under the loader");
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

            pending.mountReady = true;

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
        // The stage check matters: pending stays alive after Commit so the
        // fallback has the resources, and without it the caller would commit
        // again every frame.
        return g_pending && g_pending->ready && g_stage.load() == Stage::Ready;
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

        // The transition record the engine's own SwitchToScene fills in: the
        // path, the mode, the checkpoint. Everything downstream reads it - the
        // loader, the HUD, the checkpoint manager - so all of it is written,
        // not just the name.
        //
        // ZString does not copy. Its const char* constructor sets the top bit
        // of the length, the flag for "not allocated", and keeps the pointer;
        // assigning from an unallocated one passes that pointer straight
        // through. CopyFrom allocates through the game's own allocator, which
        // is what makes the engine the owner of what it is holding.
        SSceneParameters& parameters = LevelManager->GetSceneParameters();

        const ZString view(pending.entry->scene);

        parameters.sSceneResource   = ZString::CopyFrom(view);
        parameters.eGameMode        = eCGM_STORYMODE;
        parameters.nCheckpointIndex = pending.checkpoint;
        parameters.bRestoring       = false;
        parameters.bUseSaveGame     = false;
        parameters.bGameCompleted   = false;

        // The tokens still hold whatever the player's last menu-driven start
        // put in them - a bonus weapon, a bonus outfit, a named checkpoint
        // that may not exist in the destination. This is a plain story-mode
        // start, so all three say so.
        parameters.BonusWeapon.m_iValue        = 0;
        parameters.BonusWeapon.m_bValid        = false;
        parameters.BonusOutfit.m_iValue        = 0;
        parameters.BonusOutfit.m_bValid        = false;
        parameters.sStartCheckpointID.m_iValue = 0;
        parameters.sStartCheckpointID.m_bValid = false;

        Diag::Log("scene: parameters written - %s at checkpoint %d, storymode",
                  pending.entry->scene, pending.checkpoint);

        {
            static bool dumped = false;

            if (!dumped)
            {
                dumped = true;

                VtableMap map;

                if (ReadVtable(context, BaseAddress, &map))
                {
                    DumpVtable(map);
                }
            }
        }

        // Then the flag. This is the whole handover: the engine's main loop
        // sees it, runs its own teardown, loading screen, resource resolution
        // and scene build - the exact pipeline a mission start from the menu
        // uses. Nothing here calls the scene context at all any more; the
        // first version did, skipped half the pipeline's steps because nothing
        // shipped documents them, and built nothing.
        uint8_t previous = 0xFF;

        if (!WriteTransitionFlag(LevelManager, 1, &previous))
        {
            error = "could not reach the transition flag";

            Finish(Stage::Failed);

            return false;
        }

        Diag::Log("scene: transition flag set at +0x%02X (was %u) - the engine "
                  "owns the load from here",
                  static_cast<unsigned>(kInSceneTransitionOffset), previous);

        // Everything stays alive: the mount because the engine is about to
        // resolve out of it, the three stubs because the fallback needs them
        // if the engine never picks the flag up. Released on arrival.
        pending.committed = true;

        g_stage = Stage::Committed;

        return true;
    }

    void SceneSync::ConfirmArrived()
    {
        if (!g_pending)
        {
            return;
        }

        Diag::Log("scene: arrival confirmed - releasing the in-flight bookkeeping");

        // Finish parks the mount - the load was committed, so the new world is
        // building out of it - and Idle is what hands the button back. Leaving
        // the stage at Committed here is what once disabled Go there for the
        // rest of the session after the first successful follow.
        Finish(Stage::Idle);
    }

    bool SceneSync::TryFallback(std::string& error)
    {
        error.clear();

        if (!g_pending || !g_pending->ready || g_pending->fellBack)
        {
            error = "nothing in flight to fall back with";
            return false;
        }

        if (!Hitman5Module)
        {
            error = "the game module is not up";
            return false;
        }

        ZEntitySceneContext* context = Hitman5Module->GetSceneContext();

        if (!context)
        {
            error = "there is no scene context";
            return false;
        }

        Pending& pending = *g_pending;

        pending.fellBack = true;

        // The engine ignored the flag, so take it back before doing anything
        // by hand - two drivers is worse than one.
        AbortEngineTransition();

        // The full pipeline this time, in the order ZEngineAppCommon::LoadScene
        // runs it per the dev build's symbols: teardown, prepare, the raw
        // strings, the resolved resources, the scene, the start. The first
        // manual attempt skipped prepare, the strings and the start - the
        // three steps nothing shipped documents - and built nothing.
        //
        // Every call goes through a vtable slot verified against the two
        // anchor addresses the other mods hook, so a wrong header lays out a
        // log line instead of a wrong call.
        VtableMap map;

        if (!ReadVtable(context, BaseAddress, &map) || !map.valid)
        {
            error = "the scene context's vtable does not contain the known anchors";

            Diag::Log("scene: fallback refused - ClearScene/CreateScene not found in the vtable");

            return false;
        }

        DumpVtable(map);

        // The counted slots have to fit inside the table that was actually
        // scanned; an anchor that deep means the layout is not the one the
        // counts were made against.
        if (map.clearScene + kRelSetSceneInitParameters >= static_cast<int>(kVtableScan))
        {
            error = "the anchor sits too deep in the vtable for the counted slots";

            Diag::Log("scene: fallback refused - ClearScene at slot %d leaves "
                      "no room for +%d", map.clearScene, kRelSetSceneInitParameters);

            return false;
        }

        // The whole pipeline, in the order ZEngineAppCommon::LoadScene runs it
        // per the dev build's symbols. Each step is logged before it runs, so
        // a fault names the call that did it, and each call goes through the
        // same guard as every other engine touch in this file.
        struct Step
        {
            int         step;
            int         slotIndex;
            const char* name;
        };

        const Step steps[] = {
            { kStepClearScene,             map.clearScene,                                 "ClearScene(true)" },
            { kStepPrepareNewScene,        map.clearScene + kRelPrepareNewScene,           "PrepareNewScene" },
            { kStepSetSceneInitParameters, map.clearScene + kRelSetSceneInitParameters,    "SetSceneInitParameters" },
            { kStepSetSceneResources,      map.clearScene + kRelSetSceneResources,         "SetSceneResources" },
            { kStepCreateScene,            map.createScene,                                "CreateScene(\"\")" },
            { kStepStartEntities,          map.clearScene + kRelStartEntities,             "StartEntities" },
        };

        // Every target has to be code inside HMA.exe before anything is
        // called. Checked up front, so a wrong slot refuses the whole attempt
        // instead of failing three steps into a teardown.
        const BuildInfo& build = BuildInfo::Get();

        for (const Step& step : steps)
        {
            uintptr_t target = 0;

            if (!ReadVtableEntry(map.table, static_cast<size_t>(step.slotIndex), &target))
            {
                error = "the vtable could not be read back";
                return false;
            }

            if (build.SizeOfImage() != 0 &&
                !build.Contains(reinterpret_cast<void*>(target)))
            {
                error = "a counted slot points outside HMA.exe";

                Diag::Log("scene: fallback refused - %s (slot %d) points at "
                          "0x%08X, outside the game", step.name, step.slotIndex,
                          static_cast<unsigned>(target));

                return false;
            }
        }

        for (const Step& step : steps)
        {
            Diag::Log("scene: fallback - %s", step.name);

            FallbackStepContext call;

            call.context   = context;
            call.function  = map.table[step.slotIndex];
            call.step      = step.step;
            call.scene     = pending.entry->scene;
            call.factory   = &pending.factory.Get();
            call.blueprint = &pending.blueprint.Get();
            call.headerLib = &pending.headerLib.Get();

            if (!RunGuarded(&DoFallbackStep, &call))
            {
                error = std::string(step.name) + " faulted - the manual pipeline stopped there";

                Diag::Log("scene: fallback - %s faulted, stopping", step.name);

                return false;
            }
        }

        Diag::Log("scene: fallback complete - watching for the chapter to change");

        return true;
    }

    bool SceneSync::EngineMidTransition()
    {
        if (!LevelManager)
        {
            return false;
        }

        uint8_t window[kTransitionWindowBytes] = {};

        if (!ReadTransitionWindow(LevelManager, window))
        {
            return false;
        }

        return window[0] != 0;
    }

    void SceneSync::AbortEngineTransition()
    {
        if (!LevelManager)
        {
            return;
        }

        uint8_t previous = 0xFF;

        if (WriteTransitionFlag(LevelManager, 0, &previous))
        {
            Diag::Log("scene: transition flag cleared (was %u)", previous);
        }
    }

    void SceneSync::TraceTransitionWindow()
    {
        if (!LevelManager)
        {
            return;
        }

        static uint8_t lastWindow[kTransitionWindowBytes] = {};
        static bool    haveWindow = false;

        uint8_t window[kTransitionWindowBytes] = {};

        if (!ReadTransitionWindow(LevelManager, window))
        {
            return;
        }

        if (haveWindow && std::memcmp(window, lastWindow, sizeof(window)) == 0)
        {
            return;
        }

        // Logged on every change so the game's OWN transitions confirm the
        // reading: start a mission from the menu and the byte at +0x34 should
        // go 0 -> 1 -> 0 around the loading screen. If it does not, the flag
        // is not where the dev build says, and the log will show it before
        // anybody wonders why Go there did nothing.
        Diag::Log("scene: transition window +34=%02X%02X%02X%02X pkglist=%02X%02X%02X%02X "
                  "preload=%02X%02X%02X%02X.%02X%02X%02X%02X",
                  window[0], window[1], window[2], window[3],
                  window[4], window[5], window[6], window[7],
                  window[8], window[9], window[10], window[11],
                  window[12], window[13], window[14], window[15]);

        std::memcpy(lastWindow, window, sizeof(lastWindow));
        haveWindow = true;
    }
}
