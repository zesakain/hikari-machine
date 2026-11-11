/// @file mesh_component.cpp
/// @brief Registration of render-related Flecs ECS components.

#include "render/mesh_component.h"

#include "render/light_components.h"

namespace eng::render {

void registerRenderComponents(world::World& world) {
    auto& ecs = world.getRawWorld();
    ecs.component<MeshComponent>("MeshComponent");
    ecs.component<DirectionalLightComponent>("DirectionalLightComponent");
    ecs.component<PointLightComponent>("PointLightComponent");
    ecs.component<SpotLightComponent>("SpotLightComponent");
}

} // namespace eng::render
