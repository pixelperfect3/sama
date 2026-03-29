#pragma once

#include <functional>
#include <string>
#include <vector>

#include "engine/assets/AssetManager.h"
#include "engine/ecs/Registry.h"
#include "engine/io/Json.h"
#include "engine/rendering/RenderResources.h"

namespace engine::scene
{

// ---------------------------------------------------------------------------
// SceneSerializer — extensible scene save/load via registered component
// handlers.
//
// Usage:
//   SceneSerializer serializer;
//   serializer.registerEngineComponents();
//   serializer.saveScene(reg, resources, "scene.json");
//   serializer.loadScene("scene.json", reg, resources, assetManager);
// ---------------------------------------------------------------------------

class SceneSerializer
{
public:
    // Callback types for extensible component serialization.
    using SerializeFn = std::function<void(ecs::EntityID, const ecs::Registry&,
                                           const rendering::RenderResources&, io::JsonWriter&)>;
    using DeserializeFn =
        std::function<void(ecs::EntityID, ecs::Registry&, rendering::RenderResources&,
                           assets::AssetManager&, io::JsonValue)>;

    // Register a named component type with its serialize/deserialize functions.
    void registerComponent(const char* name, SerializeFn serialize, DeserializeFn deserialize);

    // Register all built-in engine component types (Transform, Camera,
    // DirectionalLight, PointLight, SpotLight).
    void registerEngineComponents();

    // Save the entire scene to a JSON file.
    bool saveScene(const ecs::Registry& reg, const rendering::RenderResources& resources,
                   const char* filepath);

    // Load a scene from a JSON file.
    bool loadScene(const char* filepath, ecs::Registry& reg, rendering::RenderResources& resources,
                   assets::AssetManager& assetManager);

private:
    struct ComponentHandler
    {
        std::string name;
        SerializeFn serialize;
        DeserializeFn deserialize;
    };

    std::vector<ComponentHandler> handlers_;
};

}  // namespace engine::scene
