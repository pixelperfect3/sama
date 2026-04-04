#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "engine/ecs/Entity.h"

namespace engine::core
{
class Engine;
}  // namespace engine::core

namespace engine::ecs
{
class Registry;
}  // namespace engine::ecs

namespace engine::assets
{
class AssetManager;
}  // namespace engine::assets

namespace engine::scene
{

using SceneHandle = uint32_t;
inline constexpr SceneHandle INVALID_SCENE_HANDLE = 0;

// ---------------------------------------------------------------------------
// SceneManager -- manages scene lifecycle (load, unload, reload).
//
// Wraps SceneSerializer and adds the concept of "the current scene" plus
// persistent entities that survive scene transitions.
// ---------------------------------------------------------------------------

class SceneManager
{
public:
    SceneManager(ecs::Registry& registry, core::Engine& engine, assets::AssetManager& assets);
    ~SceneManager();

    // Load a scene file. Entities are created in the registry.
    // Returns a handle to the loaded scene.
    SceneHandle loadScene(const char* filepath);

    // Unload the active scene: destroy all non-persistent entities,
    // release scene-owned assets.
    void unloadScene();

    // Convenience: unload current + reload same file. Useful for dev iteration.
    SceneHandle reloadScene();

    // The currently active scene.
    [[nodiscard]] SceneHandle activeScene() const
    {
        return activeScene_;
    }

    // Mark an entity as persistent -- it survives scene transitions.
    void markPersistent(ecs::EntityID entity);

    // Check if an entity is persistent.
    [[nodiscard]] bool isPersistent(ecs::EntityID entity) const;

    // Register a callback invoked after a scene finishes loading.
    using SceneLoadedCallback = std::function<void(SceneHandle)>;
    void setOnSceneLoaded(SceneLoadedCallback cb);

private:
    ecs::Registry& registry_;
    core::Engine& engine_;
    assets::AssetManager& assets_;

    SceneHandle activeScene_ = INVALID_SCENE_HANDLE;
    uint32_t nextSceneId_ = 1;

    std::string activeScenePath_;
    std::vector<ecs::EntityID> sceneEntities_;
    std::vector<ecs::EntityID> persistentEntities_;

    SceneLoadedCallback onSceneLoaded_;
};

}  // namespace engine::scene
