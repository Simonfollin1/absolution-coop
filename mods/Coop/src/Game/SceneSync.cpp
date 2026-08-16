#include <Windows.h>

#include <algorithm>
#include <cctype>

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
        SSceneParameters& parameters = LevelManager->GetSceneParameters();

        // ZString does not copy. Its const char* constructor sets the top bit
        // of the length - the flag for "not allocated" - and keeps the pointer,
        // and assigning from an unallocated one copies that pointer straight
        // through. The first version of this handed the engine a pointer into
        // a std::string this mod owns and then overwrote that string every time
        // a peer announced a level, so the loader read freed memory. It killed
        // the game on the first press.
        //
        // CopyFrom allocates through the game's own allocator, which is what
        // makes the engine the owner of what it is holding.
        const ZString view(sceneResource.c_str());

        parameters.sSceneResource   = ZString::CopyFrom(view);
        parameters.nCheckpointIndex = checkpointIndex < 0 ? 0 : checkpointIndex;
        parameters.bRestoring       = false;
        parameters.bUseSaveGame     = false;

        Diag::Log("scene: parameters written, asking for the load");

        // An empty streaming state is what a plain transition uses.
        //
        // Must be the game thread. This tears the whole scene down and builds
        // another, and the first version called it from OnDrawUI - inside
        // Present, mid-frame, with the renderer's state set up and ImGui
        // halfway through its own draw. CoopMod defers the request to the frame
        // update for exactly this reason.
        context->CreateScene(ZString(""));

        Diag::Log("scene: load requested");

        return true;
    }
}
