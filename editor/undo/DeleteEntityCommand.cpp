#include "editor/undo/DeleteEntityCommand.h"

#include "editor/EditorState.h"
#include "engine/ecs/Registry.h"
#include "engine/scene/NameComponent.h"

using namespace engine::ecs;
using namespace engine::rendering;

namespace engine::editor
{

DeleteEntityCommand::DeleteEntityCommand(Registry& registry, EditorState& state, EntityID entity)
    : registry_(registry), state_(state), entity_(entity)
{
    // Capture all component data at construction time (before execute).
    auto* tc = registry_.get<TransformComponent>(entity_);
    if (tc)
    {
        hadTransform_ = true;
        savedTransform_ = *tc;
    }

    auto* wt = registry_.get<WorldTransformComponent>(entity_);
    if (wt)
    {
        hadWorldTransform_ = true;
        savedWorldTransform_ = *wt;
    }

    auto* mc = registry_.get<MeshComponent>(entity_);
    if (mc)
    {
        hadMesh_ = true;
        savedMesh_ = *mc;
    }

    auto* mat = registry_.get<MaterialComponent>(entity_);
    if (mat)
    {
        hadMaterial_ = true;
        savedMaterial_ = *mat;
    }

    hadVisible_ = registry_.has<VisibleTag>(entity_);

    auto* name = registry_.get<engine::scene::NameComponent>(entity_);
    if (name)
    {
        hadName_ = true;
        savedName_ = name->name;
    }

    auto* dl = registry_.get<DirectionalLightComponent>(entity_);
    if (dl)
    {
        hadDirLight_ = true;
        savedDirLight_ = *dl;
    }

    auto* pl = registry_.get<PointLightComponent>(entity_);
    if (pl)
    {
        hadPointLight_ = true;
        savedPointLight_ = *pl;
    }
}

void DeleteEntityCommand::execute()
{
    if (state_.isSelected(entity_))
    {
        state_.clearSelection();
    }
    if (registry_.isValid(entity_))
    {
        registry_.destroyEntity(entity_);
    }
}

void DeleteEntityCommand::undo()
{
    // Re-create the entity (gets a new ID since the old one is destroyed).
    entity_ = registry_.createEntity();

    if (hadTransform_)
    {
        registry_.emplace<TransformComponent>(entity_, savedTransform_);
    }
    if (hadWorldTransform_)
    {
        registry_.emplace<WorldTransformComponent>(entity_, savedWorldTransform_);
    }
    if (hadMesh_)
    {
        registry_.emplace<MeshComponent>(entity_, savedMesh_);
    }
    if (hadMaterial_)
    {
        registry_.emplace<MaterialComponent>(entity_, savedMaterial_);
    }
    if (hadVisible_)
    {
        registry_.emplace<VisibleTag>(entity_);
    }
    if (hadName_)
    {
        registry_.emplace<engine::scene::NameComponent>(entity_,
                                                        engine::scene::NameComponent{savedName_});
    }
    if (hadDirLight_)
    {
        registry_.emplace<DirectionalLightComponent>(entity_, savedDirLight_);
    }
    if (hadPointLight_)
    {
        registry_.emplace<PointLightComponent>(entity_, savedPointLight_);
    }

    state_.select(entity_);
}

const char* DeleteEntityCommand::description() const
{
    return "Delete Entity";
}

}  // namespace engine::editor
