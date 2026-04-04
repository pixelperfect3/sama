#pragma once

namespace engine::core
{
class Engine;
}  // namespace engine::core

namespace engine::ecs
{
class Registry;
}  // namespace engine::ecs

namespace engine::game
{

// ---------------------------------------------------------------------------
// IGame -- interface for game-specific logic.
//
// The engine calls these methods at well-defined points in the frame.
// All methods have default empty implementations so games only override
// what they need.
// ---------------------------------------------------------------------------

class IGame
{
public:
    virtual ~IGame() = default;

    // Called once after Engine::init() completes.
    // Create entities, load scenes, register custom systems, set up game state.
    virtual void onInit(core::Engine& engine, ecs::Registry& registry) {}

    // Called 0-N times per frame at a fixed timestep (default 1/60s).
    // Use for physics-rate gameplay: movement, AI ticks, physics forces.
    virtual void onFixedUpdate(core::Engine& engine, ecs::Registry& registry, float fixedDt) {}

    // Called once per frame with the variable (rendering) delta time.
    // Use for input response, camera, animation blending, ImGui panels.
    virtual void onUpdate(core::Engine& engine, ecs::Registry& registry, float dt) {}

    // Called after engine systems (transform, cull) but before endFrame.
    // Use for custom render passes, HUD overlays, debug visualization.
    virtual void onRender(core::Engine& engine) {}

    // Called once before Engine::shutdown().
    // Release assets, destroy physics bodies, clean up game state.
    virtual void onShutdown(core::Engine& engine, ecs::Registry& registry) {}
};

}  // namespace engine::game
