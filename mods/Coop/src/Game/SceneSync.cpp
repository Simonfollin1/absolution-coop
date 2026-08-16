#include <Windows.h>

#include <Glacier/ZLevelManager.h>
#include <Glacier/Module/ZHitman5Module.h>
#include <Glacier/Scene/ZEntitySceneContext.h>
#include <Global.h>

#include "Game/SceneSync.h"
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

    bool SceneSync::LoadScene(const std::string& sceneResource, int checkpointIndex,
                              std::string& error)
    {
        error.clear();

        if (sceneResource.empty())
        {
            error = "no scene to go to - the host has not started anything yet";
            return false;
        }

        if (!LevelManager)
        {
            error = "the level manager is not up yet";
            return false;
        }

        if (!Hitman5Module)
        {
            error = "the game module is not up yet";
            return false;
        }

        ZEntitySceneContext* context = Hitman5Module->GetSceneContext();

        if (!context)
        {
            error = "there is no scene context to load through";
            return false;
        }

        Diag::Log("scene: going to %s at checkpoint %d", sceneResource.c_str(), checkpointIndex);

        // The parameters the loader reads on its way in. Writing them and then
        // asking for the scene is what the game does to itself on every
        // transition; this is the same path, with the same values, aimed at the
        // level somebody else is already in.
        //
        // Deliberately not guarded by __try: ZString assignment allocates, and
        // a structured exception around something that unwinds will not
        // compile. If the level manager pointer were bad we would already have
        // faulted reading the scene name above.
        SSceneParameters& parameters = LevelManager->GetSceneParameters();

        parameters.sSceneResource   = ZString(sceneResource.c_str());
        parameters.nCheckpointIndex = checkpointIndex < 0 ? 0 : checkpointIndex;
        parameters.bRestoring       = false;
        parameters.bUseSaveGame     = false;

        // An empty streaming state is what a plain transition uses.
        context->CreateScene(ZString(""));

        Diag::Log("scene: load requested");

        return true;
    }
}
