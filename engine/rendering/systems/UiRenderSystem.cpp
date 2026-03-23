#include "engine/rendering/systems/UiRenderSystem.h"

#include <bgfx/bgfx.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/ViewIds.h"
#include "engine/rendering/components/SpriteComponent.h"

namespace engine::rendering
{

void UiRenderSystem::update(ecs::Registry& reg, const RenderResources& res,
                            bgfx::ProgramHandle spriteProgram, bgfx::UniformHandle s_texture,
                            uint16_t screenWidth, uint16_t screenHeight)
{
    // 1. Configure view: no clear (UI renders on top of 3D scene).
    bgfx::setViewClear(kViewUi, BGFX_CLEAR_NONE);
    bgfx::setViewRect(kViewUi, 0, 0, screenWidth, screenHeight);

    // 2. Orthographic projection — top-left origin, NDC depth [0, 1].
    const float w = static_cast<float>(screenWidth);
    const float h = static_cast<float>(screenHeight);
    const glm::mat4 ortho = glm::ortho(0.f, w, h, 0.f, -1.f, 1.f);
    bgfx::setViewTransform(kViewUi, nullptr, glm::value_ptr(ortho));

    // 3. Collect sprites.
    batcher_.begin();

    auto spriteView = reg.view<SpriteComponent, WorldTransformComponent>();
    spriteView.each([&](ecs::EntityID /*entity*/, SpriteComponent& sprite,
                        WorldTransformComponent& wtc) { batcher_.addSprite(wtc.matrix, sprite); });

    // 4. Submit batches to kViewUi.
    bgfx::Encoder* enc = bgfx::begin();
    if (enc)
    {
        batcher_.flush(enc, spriteProgram, s_texture, res);
        bgfx::end(enc);
    }
}

}  // namespace engine::rendering
