/// @file script_engine.cpp
/// @brief Sandboxed Lua 5.4 scripting engine implementation.

#include "script/script_engine.h"

#include "core/assert.h"
#include "core/log.h"
#include "world/world.h"

// Third-party Lua C headers from vendored source
extern "C" {
#include "lua/lauxlib.h"
#include "lua/lua.h"
#include "lua/lualib.h"
}

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

namespace eng::script {

namespace {

constexpr const char* kVec3Meta = "Hikari.Vec3";
constexpr const char* kEntityMeta = "Hikari.Entity";
constexpr const char* kEngineRegistryKey = "Hikari.ScriptEngine";

struct ScriptAllocState {
    size_t currentBytes{0};
    size_t maxBytes{0};
};

void* scriptAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    auto* state = static_cast<ScriptAllocState*>(ud);
    if (nsize == 0) {
        if (state != nullptr) {
            state->currentBytes =
                (state->currentBytes >= osize) ? (state->currentBytes - osize) : 0;
        }
        std::free(ptr);
        return nullptr;
    }

    if (state != nullptr && state->maxBytes > 0) {
        size_t diff = (nsize > osize) ? (nsize - osize) : 0;
        if (state->currentBytes + diff > state->maxBytes) {
            // Budget exceeded
            return nullptr;
        }
        state->currentBytes += diff;
    }

    void* newPtr = std::realloc(ptr, nsize);
    if (newPtr == nullptr && state != nullptr && nsize > osize) {
        state->currentBytes -= (nsize - osize);
    }
    return newPtr;
}

ScriptEngine* getEngineFromLua(lua_State* L) {
    lua_pushstring(L, kEngineRegistryKey);
    lua_rawget(L, LUA_REGISTRYINDEX);
    auto* engine = static_cast<ScriptEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return engine;
}

// -----------------------------------------------------------------------------
// Lua Logging Bridge
// -----------------------------------------------------------------------------
int luaPrint(lua_State* L) {
    int n = lua_gettop(L);
    std::string out;
    for (int i = 1; i <= n; ++i) {
        if (i > 1) {
            out += "  ";
        }
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        if (s != nullptr) {
            out.append(s, len);
        }
        lua_pop(L, 1); // pop result of luaL_tolstring
    }
    ENG_LOG_INFO("Script", out);
    return 0;
}

// -----------------------------------------------------------------------------
// Vec3 Lua Bindings
// -----------------------------------------------------------------------------
Vec3* checkVec3(lua_State* L, int index) {
    return static_cast<Vec3*>(luaL_checkudata(L, index, kVec3Meta));
}

int vec3New(lua_State* L) {
    float x = static_cast<float>(luaL_optnumber(L, 1, 0.0));
    float y = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    float z = static_cast<float>(luaL_optnumber(L, 3, 0.0));

    auto* ud = static_cast<Vec3*>(lua_newuserdatauv(L, sizeof(Vec3), 0));
    new (ud) Vec3(x, y, z);
    luaL_setmetatable(L, kVec3Meta);
    return 1;
}

int vec3Index(lua_State* L) {
    auto* v = checkVec3(L, 1);
    const char* key = luaL_checkstring(L, 2);

    if (std::strcmp(key, "x") == 0) {
        lua_pushnumber(L, static_cast<lua_Number>(v->x));
        return 1;
    }
    if (std::strcmp(key, "y") == 0) {
        lua_pushnumber(L, static_cast<lua_Number>(v->y));
        return 1;
    }
    if (std::strcmp(key, "z") == 0) {
        lua_pushnumber(L, static_cast<lua_Number>(v->z));
        return 1;
    }

    // Methods
    luaL_getmetatable(L, kVec3Meta);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    return 1;
}

int vec3NewIndex(lua_State* L) {
    auto* v = checkVec3(L, 1);
    const char* key = luaL_checkstring(L, 2);
    float val = static_cast<float>(luaL_checknumber(L, 3));

    if (std::strcmp(key, "x") == 0) {
        v->x = val;
    } else if (std::strcmp(key, "y") == 0) {
        v->y = val;
    } else if (std::strcmp(key, "z") == 0) {
        v->z = val;
    } else {
        return luaL_error(L, "Invalid member '%s' on Vec3", key);
    }
    return 0;
}

int vec3Add(lua_State* L) {
    auto* a = checkVec3(L, 1);
    auto* b = checkVec3(L, 2);
    auto* ud = static_cast<Vec3*>(lua_newuserdatauv(L, sizeof(Vec3), 0));
    new (ud) Vec3(*a + *b);
    luaL_setmetatable(L, kVec3Meta);
    return 1;
}

int vec3Sub(lua_State* L) {
    auto* a = checkVec3(L, 1);
    auto* b = checkVec3(L, 2);
    auto* ud = static_cast<Vec3*>(lua_newuserdatauv(L, sizeof(Vec3), 0));
    new (ud) Vec3(*a - *b);
    luaL_setmetatable(L, kVec3Meta);
    return 1;
}

int vec3Mul(lua_State* L) {
    if (lua_isnumber(L, 1)) {
        float s = static_cast<float>(lua_tonumber(L, 1));
        auto* v = checkVec3(L, 2);
        auto* ud = static_cast<Vec3*>(lua_newuserdatauv(L, sizeof(Vec3), 0));
        new (ud) Vec3(*v * s);
        luaL_setmetatable(L, kVec3Meta);
        return 1;
    }
    if (lua_isnumber(L, 2)) {
        auto* v = checkVec3(L, 1);
        float s = static_cast<float>(lua_tonumber(L, 2));
        auto* ud = static_cast<Vec3*>(lua_newuserdatauv(L, sizeof(Vec3), 0));
        new (ud) Vec3(*v * s);
        luaL_setmetatable(L, kVec3Meta);
        return 1;
    }
    auto* a = checkVec3(L, 1);
    auto* b = checkVec3(L, 2);
    auto* ud = static_cast<Vec3*>(lua_newuserdatauv(L, sizeof(Vec3), 0));
    new (ud) Vec3(*a * *b);
    luaL_setmetatable(L, kVec3Meta);
    return 1;
}

int vec3Length(lua_State* L) {
    auto* v = checkVec3(L, 1);
    lua_pushnumber(L, static_cast<lua_Number>(v->length()));
    return 1;
}

int vec3Normalize(lua_State* L) {
    auto* v = checkVec3(L, 1);
    auto* ud = static_cast<Vec3*>(lua_newuserdatauv(L, sizeof(Vec3), 0));
    new (ud) Vec3(v->normalized());
    luaL_setmetatable(L, kVec3Meta);
    return 1;
}

int vec3ToString(lua_State* L) {
    auto* v = checkVec3(L, 1);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Vec3(%.3f, %.3f, %.3f)", static_cast<double>(v->x),
                  static_cast<double>(v->y), static_cast<double>(v->z));
    lua_pushstring(L, buf);
    return 1;
}

// -----------------------------------------------------------------------------
// Entity Lua Bindings
// -----------------------------------------------------------------------------
struct EntityUserData {
    uint64_t id;
};

EntityUserData* checkEntity(lua_State* L, int index) {
    return static_cast<EntityUserData*>(luaL_checkudata(L, index, kEntityMeta));
}

int entityId(lua_State* L) {
    auto* e = checkEntity(L, 1);
    lua_pushinteger(L, static_cast<lua_Integer>(e->id));
    return 1;
}

int entityIsValid(lua_State* L) {
    auto* e = checkEntity(L, 1);
    lua_pushboolean(L, e->id != world::EntityHandle::kInvalidId ? 1 : 0);
    return 1;
}

int entityIsAlive(lua_State* L) {
    auto* e = checkEntity(L, 1);
    ScriptEngine* engine = getEngineFromLua(L);
    if (engine == nullptr || engine->boundWorld() == nullptr) {
        lua_pushboolean(L, 0);
        return 1;
    }
    bool alive = engine->boundWorld()->isAlive(world::EntityHandle{e->id});
    lua_pushboolean(L, alive ? 1 : 0);
    return 1;
}

int entityToString(lua_State* L) {
    auto* e = checkEntity(L, 1);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Entity(%llu)", static_cast<unsigned long long>(e->id));
    lua_pushstring(L, buf);
    return 1;
}

// Helper to push ScriptValue to Lua
void pushScriptValue(lua_State* L, const ScriptValue& val) {
    std::visit(
        [L](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, Nil>) {
                lua_pushnil(L);
            } else if constexpr (std::is_same_v<T, bool>) {
                lua_pushboolean(L, arg ? 1 : 0);
            } else if constexpr (std::is_same_v<T, double>) {
                lua_pushnumber(L, static_cast<lua_Number>(arg));
            } else if constexpr (std::is_same_v<T, std::string>) {
                lua_pushlstring(L, arg.data(), arg.size());
            } else if constexpr (std::is_same_v<T, world::EntityHandle>) {
                auto* ud =
                    static_cast<EntityUserData*>(lua_newuserdatauv(L, sizeof(EntityUserData), 0));
                ud->id = arg.id;
                luaL_setmetatable(L, kEntityMeta);
            } else if constexpr (std::is_same_v<T, Vec3>) {
                auto* ud = static_cast<Vec3*>(lua_newuserdatauv(L, sizeof(Vec3), 0));
                new (ud) Vec3(arg);
                luaL_setmetatable(L, kVec3Meta);
            }
        },
        val);
}

// Helper to pull ScriptValue from Lua stack
ScriptValue toScriptValue(lua_State* L, int index) {
    int type = lua_type(L, index);
    switch (type) {
    case LUA_TNIL:
        return Nil{};
    case LUA_TBOOLEAN:
        return lua_toboolean(L, index) != 0;
    case LUA_TNUMBER:
        return static_cast<double>(lua_tonumber(L, index));
    case LUA_TSTRING: {
        size_t len = 0;
        const char* str = lua_tolstring(L, index, &len);
        return std::string(str, len);
    }
    case LUA_TUSERDATA: {
        void* p = luaL_testudata(L, index, kVec3Meta);
        if (p != nullptr) {
            return *static_cast<Vec3*>(p);
        }
        p = luaL_testudata(L, index, kEntityMeta);
        if (p != nullptr) {
            return world::EntityHandle{static_cast<EntityUserData*>(p)->id};
        }
        return Nil{};
    }
    default:
        return Nil{};
    }
}

} // namespace

// -----------------------------------------------------------------------------
// ScriptEngine Implementation
// -----------------------------------------------------------------------------
ScriptEngine::ScriptEngine() = default;

ScriptEngine::~ScriptEngine() {
    shutdown();
}

ScriptEngine::ScriptEngine(ScriptEngine&& other) noexcept
    : luaState_(other.luaState_), boundWorld_(other.boundWorld_), config_(other.config_),
      lastErrorMessage_(std::move(other.lastErrorMessage_)),
      instructionsExecuted_(other.instructionsExecuted_), budgetExceeded_(other.budgetExceeded_) {
    other.luaState_ = nullptr;
    other.boundWorld_ = nullptr;
}

ScriptEngine& ScriptEngine::operator=(ScriptEngine&& other) noexcept {
    if (this != &other) {
        shutdown();
        luaState_ = other.luaState_;
        boundWorld_ = other.boundWorld_;
        config_ = other.config_;
        lastErrorMessage_ = std::move(other.lastErrorMessage_);
        instructionsExecuted_ = other.instructionsExecuted_;
        budgetExceeded_ = other.budgetExceeded_;

        other.luaState_ = nullptr;
        other.boundWorld_ = nullptr;
    }
    return *this;
}

void ScriptEngine::instructionHook(lua_State* L, void* /*ar*/) {
    auto* engine = getEngineFromLua(L);
    if (engine == nullptr) {
        return;
    }

    engine->instructionsExecuted_ += 1000;
    if (engine->config_.instructionLimit > 0 &&
        engine->instructionsExecuted_ >= engine->config_.instructionLimit) {
        engine->budgetExceeded_ = true;
        luaL_error(L, "Script instruction budget exceeded (watchdog timeout)");
    }
}

Result<void, ScriptError> ScriptEngine::initialize(const ScriptConfig& config) {
    if (luaState_ != nullptr) {
        return ScriptError::InvalidState;
    }

    config_ = config;

    // Create state with bounded allocator
    auto* allocState = new ScriptAllocState{0, config_.memoryBudgetBytes};
    luaState_ = lua_newstate(scriptAlloc, allocState);
    if (luaState_ == nullptr) {
        delete allocState;
        lastErrorMessage_ = "Failed to create Lua VM state (allocation failed)";
        return ScriptError::OutOfMemory;
    }

    // Store engine pointer in registry
    lua_pushstring(luaState_, kEngineRegistryKey);
    lua_pushlightuserdata(luaState_, this);
    lua_rawset(luaState_, LUA_REGISTRYINDEX);

    setupSandbox();
    registerMathBindings();
    registerWorldBindings();
    registerLoggingBindings();

    // Configure instruction watchdog
    if (config_.instructionLimit > 0) {
        lua_sethook(luaState_, reinterpret_cast<lua_Hook>(instructionHook), LUA_MASKCOUNT, 1000);
    }

    ENG_LOG_FORMAT(LogLevel::Info, "Script",
                   "Lua 5.4 scripting engine initialized successfully (budget: %zu bytes)",
                   config_.memoryBudgetBytes);

    return Result<void, ScriptError>::success();
}

void ScriptEngine::shutdown() {
    if (luaState_ != nullptr) {
        void* ud = nullptr;
        lua_Alloc alloc = lua_getallocf(luaState_, &ud);
        (void)alloc;

        lua_close(luaState_);
        luaState_ = nullptr;

        if (ud != nullptr) {
            delete static_cast<ScriptAllocState*>(ud);
        }

        boundWorld_ = nullptr;
        lastErrorMessage_.clear();
        ENG_LOG_INFO("Script", "Lua scripting engine shut down");
    }
}

bool ScriptEngine::isInitialized() const noexcept {
    return luaState_ != nullptr;
}

void ScriptEngine::setupSandbox() {
    ENG_ASSERT(luaState_ != nullptr, "Lua state must be valid during setupSandbox");

    // Load safe standard libraries
    static const luaL_Reg safeLibs[] = {{LUA_GNAME, luaopen_base},
                                        {LUA_LOADLIBNAME, nullptr}, // package stripped
                                        {LUA_COLIBNAME, luaopen_coroutine},
                                        {LUA_TABLIBNAME, luaopen_table},
                                        {LUA_IOLIBNAME, nullptr}, // io stripped
                                        {LUA_OSLIBNAME, nullptr}, // os stripped
                                        {LUA_STRLIBNAME, luaopen_string},
                                        {LUA_MATHLIBNAME, luaopen_math},
                                        {LUA_UTF8LIBNAME, luaopen_utf8},
                                        {LUA_DBLIBNAME, nullptr}, // debug stripped
                                        {nullptr, nullptr}};

    for (const luaL_Reg* lib = safeLibs; lib->func != nullptr; ++lib) {
        luaL_requiref(luaState_, lib->name, lib->func, 1);
        lua_pop(luaState_, 1);
    }

    // Explicitly purge hazardous base globals
    static const char* dangerousGlobals[] = {"dofile", "loadfile", "load", "collectgarbage",
                                             nullptr};
    for (const char** g = dangerousGlobals; *g != nullptr; ++g) {
        lua_pushnil(luaState_);
        lua_setglobal(luaState_, *g);
    }
}

void ScriptEngine::registerLoggingBindings() {
    ENG_ASSERT(luaState_ != nullptr, "Lua state must be valid");
    lua_pushcfunction(luaState_, luaPrint);
    lua_setglobal(luaState_, "print");
}

void ScriptEngine::registerMathBindings() {
    ENG_ASSERT(luaState_ != nullptr, "Lua state must be valid");

    // Create Vec3 metatable
    if (luaL_newmetatable(luaState_, kVec3Meta)) {
        lua_pushstring(luaState_, "__index");
        lua_pushcfunction(luaState_, vec3Index);
        lua_rawset(luaState_, -3);

        lua_pushstring(luaState_, "__newindex");
        lua_pushcfunction(luaState_, vec3NewIndex);
        lua_rawset(luaState_, -3);

        lua_pushstring(luaState_, "__add");
        lua_pushcfunction(luaState_, vec3Add);
        lua_rawset(luaState_, -3);

        lua_pushstring(luaState_, "__sub");
        lua_pushcfunction(luaState_, vec3Sub);
        lua_rawset(luaState_, -3);

        lua_pushstring(luaState_, "__mul");
        lua_pushcfunction(luaState_, vec3Mul);
        lua_rawset(luaState_, -3);

        lua_pushstring(luaState_, "__tostring");
        lua_pushcfunction(luaState_, vec3ToString);
        lua_rawset(luaState_, -3);

        // Methods on metatable
        lua_pushstring(luaState_, "length");
        lua_pushcfunction(luaState_, vec3Length);
        lua_rawset(luaState_, -3);

        lua_pushstring(luaState_, "normalize");
        lua_pushcfunction(luaState_, vec3Normalize);
        lua_rawset(luaState_, -3);
    }
    lua_pop(luaState_, 1); // pop metatable

    // Global Vec3 constructor table/function
    lua_pushcfunction(luaState_, vec3New);
    lua_setglobal(luaState_, "Vec3");
}

void ScriptEngine::registerWorldBindings() {
    ENG_ASSERT(luaState_ != nullptr, "Lua state must be valid");

    if (luaL_newmetatable(luaState_, kEntityMeta)) {
        lua_pushstring(luaState_, "__index");
        lua_newtable(luaState_); // methods table

        lua_pushstring(luaState_, "id");
        lua_pushcfunction(luaState_, entityId);
        lua_rawset(luaState_, -3);

        lua_pushstring(luaState_, "is_valid");
        lua_pushcfunction(luaState_, entityIsValid);
        lua_rawset(luaState_, -3);

        lua_pushstring(luaState_, "is_alive");
        lua_pushcfunction(luaState_, entityIsAlive);
        lua_rawset(luaState_, -3);

        lua_rawset(luaState_, -3); // set __index table

        lua_pushstring(luaState_, "__tostring");
        lua_pushcfunction(luaState_, entityToString);
        lua_rawset(luaState_, -3);
    }
    lua_pop(luaState_, 1);
}

Result<ScriptValue, ScriptError> ScriptEngine::executeString(std::string_view script,
                                                             std::string_view chunkName) {
    if (luaState_ == nullptr) {
        return ScriptError::InvalidState;
    }

    instructionsExecuted_ = 0;
    budgetExceeded_ = false;

    std::string chunkStr = "@";
    chunkStr.append(chunkName);

    int loadResult = luaL_loadbuffer(luaState_, script.data(), script.size(), chunkStr.c_str());
    if (loadResult != LUA_OK) {
        lastErrorMessage_ = lua_tostring(luaState_, -1);
        lua_pop(luaState_, 1);
        return ScriptError::SyntaxError;
    }

    int pcallResult = lua_pcall(luaState_, 0, 1, 0);
    if (pcallResult != LUA_OK) {
        lastErrorMessage_ = lua_tostring(luaState_, -1);
        lua_pop(luaState_, 1);

        if (budgetExceeded_) {
            return ScriptError::BudgetExceeded;
        }
        return ScriptError::RuntimeError;
    }

    ScriptValue ret = toScriptValue(luaState_, -1);
    lua_pop(luaState_, 1);
    return ret;
}

Result<void, ScriptError> ScriptEngine::loadScript(std::string_view scriptName,
                                                   std::string_view scriptCode) {
    auto res = executeString(scriptCode, scriptName);
    if (!res.hasValue()) {
        return res.error();
    }
    return Result<void, ScriptError>::success();
}

Result<ScriptValue, ScriptError> ScriptEngine::callFunction(std::string_view funcName,
                                                            std::span<const ScriptValue> args) {
    if (luaState_ == nullptr) {
        return ScriptError::InvalidState;
    }

    instructionsExecuted_ = 0;
    budgetExceeded_ = false;

    std::string nameStr(funcName);
    int type = lua_getglobal(luaState_, nameStr.c_str());
    if (type != LUA_TFUNCTION) {
        lua_pop(luaState_, 1);
        lastErrorMessage_ = "Global function '" + nameStr + "' not found or not callable";
        return ScriptError::NotFound;
    }

    for (const auto& arg : args) {
        pushScriptValue(luaState_, arg);
    }

    int pcallResult = lua_pcall(luaState_, static_cast<int>(args.size()), 1, 0);
    if (pcallResult != LUA_OK) {
        lastErrorMessage_ = lua_tostring(luaState_, -1);
        lua_pop(luaState_, 1);

        if (budgetExceeded_) {
            return ScriptError::BudgetExceeded;
        }
        return ScriptError::RuntimeError;
    }

    ScriptValue ret = toScriptValue(luaState_, -1);
    lua_pop(luaState_, 1);
    return ret;
}

Result<void, ScriptError> ScriptEngine::callEntityUpdate(std::string_view funcName,
                                                         world::EntityHandle entity,
                                                         float deltaTime) {
    ScriptValue args[2] = {ScriptValue(entity), ScriptValue(static_cast<double>(deltaTime))};
    auto res = callFunction(funcName, args);
    if (!res.hasValue()) {
        return res.error();
    }
    return Result<void, ScriptError>::success();
}

Result<void, ScriptError> ScriptEngine::setGlobal(std::string_view name, const ScriptValue& value) {
    if (luaState_ == nullptr) {
        return ScriptError::InvalidState;
    }
    pushScriptValue(luaState_, value);
    std::string nameStr(name);
    lua_setglobal(luaState_, nameStr.c_str());
    return Result<void, ScriptError>::success();
}

Result<ScriptValue, ScriptError> ScriptEngine::getGlobal(std::string_view name) {
    if (luaState_ == nullptr) {
        return ScriptError::InvalidState;
    }
    std::string nameStr(name);
    lua_getglobal(luaState_, nameStr.c_str());
    ScriptValue val = toScriptValue(luaState_, -1);
    lua_pop(luaState_, 1);
    return val;
}

void ScriptEngine::stepGarbageCollector(int32_t stepBudget) {
    if (luaState_ != nullptr) {
        lua_gc(luaState_, LUA_GCSTEP, stepBudget);
    }
}

void ScriptEngine::setInstructionLimit(uint32_t limit) {
    config_.instructionLimit = limit;
}

void ScriptEngine::bindWorld(world::World* world) {
    boundWorld_ = world;
}

void ScriptEngine::unbindWorld() {
    boundWorld_ = nullptr;
}

} // namespace eng::script
