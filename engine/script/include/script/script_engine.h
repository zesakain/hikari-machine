/// @file script_engine.h
/// @brief Sandboxed Lua 5.4 scripting engine with instruction watchdog and ECS/math bindings.
///
/// Purpose:
/// Manages the embedded Lua 5.4 runtime state, enforces security sandboxing (stripping OS,
/// filesystem, and arbitrary module loading), provides execution instruction budgeting to prevent
/// infinite loops, and bridges C++ math and ECS entities into Lua.
///
/// Ownership and Lifetime:
/// Owned by the simulation runtime, application, or Framework subsystem. Outlives individual
/// script components. RAII lifecycle via initialize() and shutdown().
///
/// Thread-safety:
/// Not thread-safe for concurrent script invocation. Script evaluation must be pinned to the
/// simulation thread or worker job owning the script task.
///
/// Performance:
/// Hot path script calls do not heap allocate outside the bounded Lua allocator.
/// Garbage collection is stepped incrementally per frame.
///
/// Header self-containment:
/// Does NOT include <lua.h> or any third-party header. Uses an opaque pointer to the Lua VM.

#pragma once

#include "core/result.h"
#include "script/script_types.h"

#include <span>
#include <string>
#include <string_view>

// Forward declaration of internal Lua VM state
struct lua_State;

namespace eng::world {
class World;
} // namespace eng::world

namespace eng::script {

/// @class ScriptEngine
/// @brief Embedded Lua 5.4 script engine with sandboxing, watchdog limits, and ECS bindings.
class ScriptEngine {
public:
    ScriptEngine();
    ~ScriptEngine();

    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    ScriptEngine(ScriptEngine&& other) noexcept;
    ScriptEngine& operator=(ScriptEngine&& other) noexcept;

    /// Initializes the Lua runtime, creates the sandbox environment, and registers engine bindings.
    ///
    /// Preconditions : Not previously initialized or already shut down.
    /// Postconditions: Safe standard libraries loaded (base, table, string, math, utf8).
    ///                 Dangerous libraries removed (os, io, package, debug).
    ///                 Math and entity bindings registered in the global environment.
    /// Thread-safety : Simulation / main thread only.
    [[nodiscard]] Result<void, ScriptError> initialize(const ScriptConfig& config = {});

    /// Gracefully tears down the Lua state and releases all script allocations.
    void shutdown();

    /// Checks whether the engine has been successfully initialized.
    [[nodiscard]] bool isInitialized() const noexcept;

    /// Compiles and executes an arbitrary string chunk in the sandbox.
    ///
    /// @param script Source code to execute.
    /// @param chunkName Diagnostic identifier used in error stack traces.
    /// @return The result value of the evaluated chunk, or a ScriptError.
    [[nodiscard]] Result<ScriptValue, ScriptError>
    executeString(std::string_view script, std::string_view chunkName = "chunk");

    /// Loads and defines a named script chunk without immediate return value extraction.
    [[nodiscard]] Result<void, ScriptError> loadScript(std::string_view scriptName,
                                                       std::string_view scriptCode);

    /// Calls a global Lua function by name with typed arguments.
    ///
    /// @param funcName Name of the global function to invoke.
    /// @param args Span of arguments converted to Lua types.
    /// @return The return value of the function call, or a ScriptError.
    [[nodiscard]] Result<ScriptValue, ScriptError>
    callFunction(std::string_view funcName, std::span<const ScriptValue> args = {});

    /// Calls an entity-specific script hook (e.g., `on_update(entity, dt)` or `scriptName_update`).
    [[nodiscard]] Result<void, ScriptError>
    callEntityUpdate(std::string_view funcName, world::EntityHandle entity, float deltaTime);

    /// Sets a global variable in the Lua environment.
    [[nodiscard]] Result<void, ScriptError> setGlobal(std::string_view name,
                                                      const ScriptValue& value);

    /// Retrieves a global variable from the Lua environment.
    [[nodiscard]] Result<ScriptValue, ScriptError> getGlobal(std::string_view name);

    /// Performs an incremental garbage collection step with a defined work budget.
    void stepGarbageCollector(int32_t stepBudget = 100);

    /// Sets the instruction limit watchdog threshold for runaway script protection.
    void setInstructionLimit(uint32_t limit);

    /// Binds the authoritative Shared World for entity transform and lifecycle queries.
    void bindWorld(world::World* world);

    /// Unbinds the simulation world.
    void unbindWorld();

    /// Returns the currently bound simulation World, or nullptr if none.
    [[nodiscard]] world::World* boundWorld() const noexcept { return boundWorld_; }

    /// Returns the most recent diagnostic error message recorded by the runtime.
    [[nodiscard]] std::string_view lastErrorMessage() const noexcept { return lastErrorMessage_; }

private:
    lua_State* luaState_{nullptr};
    world::World* boundWorld_{nullptr};
    ScriptConfig config_{};
    std::string lastErrorMessage_{};
    uint32_t instructionsExecuted_{0};
    bool budgetExceeded_{false};

    void setupSandbox();
    void registerMathBindings();
    void registerWorldBindings();
    void registerLoggingBindings();

    static void instructionHook(lua_State* L, void* ar);
};

} // namespace eng::script
