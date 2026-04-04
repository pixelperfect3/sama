#include "engine/scene/SceneManager.h"

#include <algorithm>
#include <cstdio>

#include "engine/assets/AssetManager.h"
#include "engine/core/Engine.h"
#include "engine/ecs/Registry.h"
#include "engine/scene/SceneSerializer.h"

namespace engine::scene
{

SceneManager::SceneManager(ecs::Registry& registry, core::Engine& engine,
                           assets::AssetManager& assets)
    : registry_(registry), engine_(engine), assets_(assets)
{
}

SceneManager::~SceneManager() = default;

SceneHandle SceneManager::loadScene(const char* filepath)
{
    // Track how many entities exist before loading so we can identify new ones.
    std::vector<ecs::EntityID> entitiesBefore;
    registry_.forEachEntity([&](ecs::EntityID e) { entitiesBefore.push_back(e); });

    SceneSerializer serializer;
    serializer.registerEngineComponents();

    if (!serializer.loadScene(filepath, registry_, engine_.resources(), assets_))
    {
        fprintf(stderr, "SceneManager: failed to load scene '%s'\n", filepath);
        return INVALID_SCENE_HANDLE;
    }

    // Determine which entities were created by the scene load.
    registry_.forEachEntity(
        [&](ecs::EntityID e)
        {
            bool existed =
                std::find(entitiesBefore.begin(), entitiesBefore.end(), e) != entitiesBefore.end();
            if (!existed)
            {
                sceneEntities_.push_back(e);
            }
        });

    activeScenePath_ = filepath;
    activeScene_ = nextSceneId_++;

    if (onSceneLoaded_)
    {
        onSceneLoaded_(activeScene_);
    }

    return activeScene_;
}

void SceneManager::unloadScene()
{
    for (ecs::EntityID e : sceneEntities_)
    {
        if (isPersistent(e))
            continue;
        if (registry_.isValid(e))
        {
            registry_.destroyEntity(e);
        }
    }
    sceneEntities_.clear();
    activeScene_ = INVALID_SCENE_HANDLE;
}

SceneHandle SceneManager::reloadScene()
{
    std::string path = activeScenePath_;
    if (path.empty())
        return INVALID_SCENE_HANDLE;

    unloadScene();
    return loadScene(path.c_str());
}

void SceneManager::markPersistent(ecs::EntityID entity)
{
    if (!isPersistent(entity))
    {
        persistentEntities_.push_back(entity);
    }
}

bool SceneManager::isPersistent(ecs::EntityID entity) const
{
    return std::find(persistentEntities_.begin(), persistentEntities_.end(), entity) !=
           persistentEntities_.end();
}

void SceneManager::setOnSceneLoaded(SceneLoadedCallback cb)
{
    onSceneLoaded_ = std::move(cb);
}

}  // namespace engine::scene
