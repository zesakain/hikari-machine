/// @file script_components.h
/// @brief Flecs ECS components and execution systems for entity scripting.
///
/// Purpose:
/// Binds sandboxed Lua scripts to entities in the shared simulation World.
/// Executes script callbacks (`on_update` or `<scriptName>_update`) according to
/// SystemGroup gating and execution budgets.
///
/// Ownership and Lifetime:
/// Value types stored within Flecs archetypes. Trivially copyable without heap allocation.
///
/// Thread-safety:
/// Script component evaluation occurs during the continuous or mode-gated simulation phases.

#pragma once

#include "world/system_group.h"
#include "world/world.h"

#include <cstdint>

namespace eng::script {

class ScriptEngine;

/// @struct ScriptComponent
/// @brief Flecs component associating an entity with a named Lua script routine.
struct ScriptComponent {
    /// Name of the script function or module to execute (e.g., "patrol", "turret_ai").
    char scriptName[64]{0};

    /// Whether this entity's script is actively ticked.
    bool enabled{true};

    /// Target update interval in seconds (0.0f = update every frame).
    float updateInterval{0.0f};

    /// Accumulated delta time since last script invocation.
    float timeSinceLastUpdate{0.0f};
};

/// Registers the ScriptComponent type and reflection in the Flecs World.
void registerScriptComponents(world::World& world);

/// Registers continuous or mode-gated script update systems in the Flecs World.
void registerScriptSystems(world::World& world, ScriptEngine& scriptEngine,
                           world::SystemGroup group = world::SystemGroup::Continuous);

} // namespace eng::script
