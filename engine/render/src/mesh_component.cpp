/// @file mesh_component.cpp
/// @brief Registration of render-related Flecs ECS components.

#include "render/mesh_component.h"

namespace eng::render {

void registerRenderComponents(world::World& world) {
    auto& ecs = world.getRawWorld();
    ecs.component<MeshComponent>("MeshComponent");
}

} // namespace eng::render
