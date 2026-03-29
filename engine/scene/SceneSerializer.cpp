#include "engine/scene/SceneSerializer.h"

#include <unordered_map>

#include "engine/rendering/EcsComponents.h"
#include "engine/scene/HierarchyComponents.h"
#include "engine/scene/NameComponent.h"
#include "engine/scene/SceneGraph.h"

namespace engine::scene
{

// ---------------------------------------------------------------------------
// registerComponent
// ---------------------------------------------------------------------------

void SceneSerializer::registerComponent(const char* name, SerializeFn serialize,
                                        DeserializeFn deserialize)
{
    handlers_.push_back({name, std::move(serialize), std::move(deserialize)});
}

// ---------------------------------------------------------------------------
// Built-in component handlers
// ---------------------------------------------------------------------------

void SceneSerializer::registerEngineComponents()
{
    // ----- Transform -----
    registerComponent(
        "Transform",
        [](ecs::EntityID e, const ecs::Registry& reg, const rendering::RenderResources&,
           io::JsonWriter& w)
        {
            const auto* tc = reg.get<rendering::TransformComponent>(e);
            if (!tc)
                return;
            w.key("Transform");
            w.startObject();
            w.key("position");
            w.writeVec3(tc->position);
            w.key("rotation");
            w.writeQuat(tc->rotation);
            w.key("scale");
            w.writeVec3(tc->scale);
            w.endObject();
        },
        [](ecs::EntityID e, ecs::Registry& reg, rendering::RenderResources&, assets::AssetManager&,
           io::JsonValue val)
        {
            rendering::TransformComponent tc{};
            tc.position = val.hasMember("position") ? val["position"].getVec3() : math::Vec3(0.0f);
            tc.rotation = val.hasMember("rotation") ? val["rotation"].getQuat()
                                                    : math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
            tc.scale = val.hasMember("scale") ? val["scale"].getVec3() : math::Vec3(1.0f);
            tc.flags = 1;  // dirty
            reg.emplace<rendering::TransformComponent>(e, tc);
        });

    // ----- Camera -----
    registerComponent(
        "Camera",
        [](ecs::EntityID e, const ecs::Registry& reg, const rendering::RenderResources&,
           io::JsonWriter& w)
        {
            const auto* cc = reg.get<rendering::CameraComponent>(e);
            if (!cc)
                return;
            w.key("Camera");
            w.startObject();
            w.key("fovY");
            w.writeFloat(cc->fovY);
            w.key("nearPlane");
            w.writeFloat(cc->nearPlane);
            w.key("farPlane");
            w.writeFloat(cc->farPlane);
            w.key("type");
            w.writeString(cc->type == rendering::ProjectionType::Perspective ? "Perspective"
                                                                             : "Orthographic");
            w.key("viewLayer");
            w.writeUint(cc->viewLayer);
            w.endObject();
        },
        [](ecs::EntityID e, ecs::Registry& reg, rendering::RenderResources&, assets::AssetManager&,
           io::JsonValue val)
        {
            rendering::CameraComponent cc{};
            cc.fovY = val["fovY"].getFloat(60.0f);
            cc.nearPlane = val["nearPlane"].getFloat(0.1f);
            cc.farPlane = val["farPlane"].getFloat(1000.0f);

            const char* typeStr = val["type"].getString("Perspective");
            if (std::strcmp(typeStr, "Orthographic") == 0)
                cc.type = rendering::ProjectionType::Orthographic;
            else
                cc.type = rendering::ProjectionType::Perspective;

            cc.viewLayer = static_cast<uint8_t>(val["viewLayer"].getUint(0));
            cc.aspectRatio = 0.0f;  // set by the rendering system
            reg.emplace<rendering::CameraComponent>(e, cc);
        });

    // ----- DirectionalLight -----
    registerComponent(
        "DirectionalLight",
        [](ecs::EntityID e, const ecs::Registry& reg, const rendering::RenderResources&,
           io::JsonWriter& w)
        {
            const auto* dl = reg.get<rendering::DirectionalLightComponent>(e);
            if (!dl)
                return;
            w.key("DirectionalLight");
            w.startObject();
            w.key("direction");
            w.writeVec3(dl->direction);
            w.key("color");
            w.writeVec3(dl->color);
            w.key("intensity");
            w.writeFloat(dl->intensity);
            w.key("castShadows");
            w.writeBool((dl->flags & 1u) != 0);
            w.endObject();
        },
        [](ecs::EntityID e, ecs::Registry& reg, rendering::RenderResources&, assets::AssetManager&,
           io::JsonValue val)
        {
            rendering::DirectionalLightComponent dl{};
            dl.direction = val.hasMember("direction") ? val["direction"].getVec3()
                                                      : math::Vec3(0.0f, -1.0f, 0.0f);
            dl.color = val.hasMember("color") ? val["color"].getVec3() : math::Vec3(1.0f);
            dl.intensity = val["intensity"].getFloat(1.0f);
            dl.flags = val["castShadows"].getBool(false) ? 1u : 0u;
            reg.emplace<rendering::DirectionalLightComponent>(e, dl);
        });

    // ----- PointLight -----
    registerComponent(
        "PointLight",
        [](ecs::EntityID e, const ecs::Registry& reg, const rendering::RenderResources&,
           io::JsonWriter& w)
        {
            const auto* pl = reg.get<rendering::PointLightComponent>(e);
            if (!pl)
                return;
            w.key("PointLight");
            w.startObject();
            w.key("color");
            w.writeVec3(pl->color);
            w.key("intensity");
            w.writeFloat(pl->intensity);
            w.key("radius");
            w.writeFloat(pl->radius);
            w.endObject();
        },
        [](ecs::EntityID e, ecs::Registry& reg, rendering::RenderResources&, assets::AssetManager&,
           io::JsonValue val)
        {
            rendering::PointLightComponent pl{};
            pl.color = val.hasMember("color") ? val["color"].getVec3() : math::Vec3(1.0f);
            pl.intensity = val["intensity"].getFloat(1.0f);
            pl.radius = val["radius"].getFloat(10.0f);
            reg.emplace<rendering::PointLightComponent>(e, pl);
        });

    // ----- SpotLight -----
    registerComponent(
        "SpotLight",
        [](ecs::EntityID e, const ecs::Registry& reg, const rendering::RenderResources&,
           io::JsonWriter& w)
        {
            const auto* sl = reg.get<rendering::SpotLightComponent>(e);
            if (!sl)
                return;
            w.key("SpotLight");
            w.startObject();
            w.key("direction");
            w.writeVec3(sl->direction);
            w.key("color");
            w.writeVec3(sl->color);
            w.key("intensity");
            w.writeFloat(sl->intensity);
            w.key("cosInnerAngle");
            w.writeFloat(sl->cosInnerAngle);
            w.key("cosOuterAngle");
            w.writeFloat(sl->cosOuterAngle);
            w.key("radius");
            w.writeFloat(sl->radius);
            w.endObject();
        },
        [](ecs::EntityID e, ecs::Registry& reg, rendering::RenderResources&, assets::AssetManager&,
           io::JsonValue val)
        {
            rendering::SpotLightComponent sl{};
            sl.direction = val.hasMember("direction") ? val["direction"].getVec3()
                                                      : math::Vec3(0.0f, -1.0f, 0.0f);
            sl.color = val.hasMember("color") ? val["color"].getVec3() : math::Vec3(1.0f);
            sl.intensity = val["intensity"].getFloat(1.0f);
            sl.cosInnerAngle = val["cosInnerAngle"].getFloat(0.9f);
            sl.cosOuterAngle = val["cosOuterAngle"].getFloat(0.8f);
            sl.radius = val["radius"].getFloat(10.0f);
            reg.emplace<rendering::SpotLightComponent>(e, sl);
        });
}

// ---------------------------------------------------------------------------
// saveScene
// ---------------------------------------------------------------------------

bool SceneSerializer::saveScene(const ecs::Registry& reg,
                                const rendering::RenderResources& resources, const char* filepath)
{
    io::JsonWriter writer(true);

    // Assign scene-local IDs to all live entities.
    std::unordered_map<ecs::EntityID, uint32_t> entityToLocalId;
    std::vector<ecs::EntityID> entities;
    uint32_t nextLocalId = 0;

    reg.forEachEntity(
        [&](ecs::EntityID e)
        {
            entityToLocalId[e] = nextLocalId++;
            entities.push_back(e);
        });

    writer.startObject();
    writer.key("version");
    writer.writeInt(1);

    writer.key("entities");
    writer.startArray();

    for (auto e : entities)
    {
        writer.startObject();

        // id
        writer.key("id");
        writer.writeUint(entityToLocalId[e]);

        // name (optional)
        const auto* nameComp = reg.get<NameComponent>(e);
        if (nameComp && !nameComp->name.empty())
        {
            writer.key("name");
            writer.writeString(nameComp->name.c_str());
        }

        // parent (optional)
        ecs::EntityID parent = getParent(reg, e);
        if (parent != ecs::INVALID_ENTITY)
        {
            auto parentIt = entityToLocalId.find(parent);
            if (parentIt != entityToLocalId.end())
            {
                writer.key("parent");
                writer.writeUint(parentIt->second);
            }
        }

        // components
        writer.key("components");
        writer.startObject();
        for (const auto& handler : handlers_)
        {
            handler.serialize(e, reg, resources, writer);
        }
        writer.endObject();

        writer.endObject();
    }

    writer.endArray();
    writer.endObject();

    return writer.writeToFile(filepath);
}

// ---------------------------------------------------------------------------
// loadScene
// ---------------------------------------------------------------------------

bool SceneSerializer::loadScene(const char* filepath, ecs::Registry& reg,
                                rendering::RenderResources& resources,
                                assets::AssetManager& assetManager)
{
    io::JsonDocument doc;
    if (!doc.parseFile(filepath))
        return false;

    auto root = doc.root();
    if (!root.isObject())
        return false;

    // Version check
    int version = root["version"].getInt(0);
    if (version < 1)
        return false;

    auto entitiesArr = root["entities"];
    if (!entitiesArr.isArray())
        return false;

    // Build handler lookup by name.
    std::unordered_map<std::string, const ComponentHandler*> handlerMap;
    for (const auto& handler : handlers_)
    {
        handlerMap[handler.name] = &handler;
    }

    // First pass: create entities and deserialize components.
    // Store scene-local-id -> runtime EntityID mapping.
    std::unordered_map<uint32_t, ecs::EntityID> localIdToEntity;

    struct EntityEntry
    {
        uint32_t localId;
        int32_t parentLocalId;  // -1 = no parent
        ecs::EntityID runtimeId;
    };
    std::vector<EntityEntry> entries;

    std::size_t count = entitiesArr.arraySize();
    for (std::size_t i = 0; i < count; ++i)
    {
        auto entityJson = entitiesArr[i];
        if (!entityJson.isObject())
            continue;

        uint32_t localId = entityJson["id"].getUint(static_cast<uint32_t>(i));
        ecs::EntityID e = reg.createEntity();
        localIdToEntity[localId] = e;

        // Name (optional)
        if (entityJson.hasMember("name") && entityJson["name"].isString())
        {
            NameComponent nc;
            nc.name = entityJson["name"].getString();
            reg.emplace<NameComponent>(e, std::move(nc));
        }

        // Parent (deferred to second pass)
        int32_t parentLocalId = -1;
        if (entityJson.hasMember("parent") && !entityJson["parent"].isNull())
        {
            parentLocalId = static_cast<int32_t>(entityJson["parent"].getUint(0));
        }

        entries.push_back({localId, parentLocalId, e});

        // Deserialize components
        auto comps = entityJson["components"];
        if (comps.isObject())
        {
            for (auto member : comps)
            {
                const char* compName = member.memberName();
                auto handlerIt = handlerMap.find(compName);
                if (handlerIt != handlerMap.end())
                {
                    handlerIt->second->deserialize(e, reg, resources, assetManager, member);
                }
                // Unknown components are silently ignored.
            }
        }
    }

    // Second pass: rebuild hierarchy.
    for (const auto& entry : entries)
    {
        if (entry.parentLocalId >= 0)
        {
            auto parentIt = localIdToEntity.find(static_cast<uint32_t>(entry.parentLocalId));
            if (parentIt != localIdToEntity.end())
            {
                setParent(reg, entry.runtimeId, parentIt->second);
            }
        }
    }

    return true;
}

}  // namespace engine::scene
