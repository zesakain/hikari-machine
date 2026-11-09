/// @file hierarchy_systems.h
/// @brief Hierarchical transform propagation systems for ECS entity trees.
///
/// Purpose:
/// Evaluates and propagates world-space transformations across entity parent-child
/// hierarchies. Computes WorldTransformComponent from local TransformComponent trees,
/// respecting dirty invalidation flags and matrix composition conventions.
///
/// Ownership and Lifetime:
/// Systems are owned by the World under SystemGroup::Continuous and registered in Flecs pipelines.
///
/// Thread-safety:
/// Executed on the main thread during world simulation progress. Non-mutating queries on
/// resulting WorldTransformComponent are safe across worker jobs.
///
/// Performance:
/// Breadth-first topological traversal with zero dynamic heap allocation in the hot path.

#pragma once

namespace eng::world {

class World;

/// @brief Registers the hierarchical transform propagation system in the World under
/// SystemGroup::Continuous.
void registerHierarchySystems(World& world);

/// @brief Directly evaluates and updates WorldTransformComponent for all entities in the World.
///
/// Safe to invoke on demand during Edit Mode, post-scene loading, or headless test scenarios
/// without stepping the full continuous simulation pipeline.
void updateHierarchyTransforms(World& world);

} // namespace eng::world
