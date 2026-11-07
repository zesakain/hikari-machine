# engine/script/CMakeLists.txt
#
# Embedded sandboxed Lua 5.4 scripting subsystem with Flecs ECS integration.

add_library(lua_static STATIC
    src/lua/onelua.c
)
target_include_directories(lua_static
    SYSTEM PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/lua>
)
set_target_properties(lua_static PROPERTIES
    LINKER_LANGUAGE C
    C_STANDARD 99
)
target_compile_definitions(lua_static PRIVATE
    MAKE_LIB
    LUA_USE_POSIX
)
target_compile_options(lua_static PRIVATE -w)

if(UNIX)
    target_link_libraries(lua_static PRIVATE m)
endif()

add_library(engine_script STATIC
    src/script_engine.cpp
    src/script_components.cpp
)
add_library(hikari::engine_script ALIAS engine_script)

target_include_directories(engine_script
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/lua>
)

target_link_libraries(engine_script
    PUBLIC
        hikari::engine_flags
        hikari::engine_core
        hikari::engine_world
        lua_static
)
