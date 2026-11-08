/// @file world.cpp
/// @brief Unified simulation World implementation.

#include "world/world.h"

namespace eng::world {

World::World() {
    groupActive_[static_cast<size_t>(SystemGroup::Continuous)] = true;
    groupActive_[static_cast<size_t>(SystemGroup::StateA)] = true;
    groupActive_[static_cast<size_t>(SystemGroup::StateB)] = false;
}

World::~World() = default;

World::World(World&& other) noexcept
    : ecs_(std::move(other.ecs_)), groupActive_(other.groupActive_),
      groupSystems_(std::move(other.groupSystems_)) {}

World& World::operator=(World&& other) noexcept {
    if (this != &other) {
        ecs_ = std::move(other.ecs_);
        groupActive_ = other.groupActive_;
        groupSystems_ = std::move(other.groupSystems_);
    }
    return *this;
}

EntityHandle World::createEntity() {
    const flecs::entity e = ecs_.entity();
    return EntityHandle(e.id());
}

EntityHandle World::createEntity(std::string_view name) {
    // Small stack buffer to ensure null-terminated string for Flecs name
    std::array<char, 256> buf{};
    const size_t copyLen = std::min(name.size(), buf.size() - 1);
    for (size_t i = 0; i < copyLen; ++i) {
        buf[i] = name[i];
    }
    buf[copyLen] = '\0';

    const flecs::entity e = ecs_.entity(buf.data());
    return EntityHandle(e.id());
}

void World::destroyEntity(EntityHandle entity) {
    if (isAlive(entity)) {
        ecs_.entity(entity.id).destruct();
    }
}

bool World::isAlive(EntityHandle entity) const {
    if (!entity.isValid()) {
        return false;
    }
    return ecs_.is_alive(entity.id);
}

std::string_view World::getEntityName(EntityHandle entity) const {
    if (!isAlive(entity)) {
        return "";
    }
    const char* name = ecs_.entity(entity.id).name().c_str();
    return (name != nullptr) ? std::string_view(name) : std::string_view("");
}

void World::setEntityName(EntityHandle entity, std::string_view name) {
    if (!isAlive(entity)) {
        return;
    }
    std::array<char, 256> buf{};
    const size_t copyLen = std::min(name.size(), buf.size() - 1);
    for (size_t i = 0; i < copyLen; ++i) {
        buf[i] = name[i];
    }
    buf[copyLen] = '\0';
    ecs_.entity(entity.id).set_name(buf.data());
}

void World::registerSystem(SystemGroup group, flecs::system system) {
    const auto idx = static_cast<size_t>(group);
    ENG_ASSERT(idx < groupSystems_.size(), "Invalid SystemGroup index");
    if (groupActive_[idx]) {
        system.enable();
    } else {
        system.disable();
    }
    groupSystems_[idx].push_back(system);
}

void World::setSystemGroupActive(SystemGroup group, bool active) {
    const auto idx = static_cast<size_t>(group);
    ENG_ASSERT(idx < groupSystems_.size(), "Invalid SystemGroup index");
    groupActive_[idx] = active;
    for (auto& sys : groupSystems_[idx]) {
        if (active) {
            sys.enable();
        } else {
            sys.disable();
        }
    }
}

bool World::isSystemGroupActive(SystemGroup group) const noexcept {
    const auto idx = static_cast<size_t>(group);
    return idx < groupActive_.size() && groupActive_[idx];
}

void World::progress(float deltaSeconds) {
    ecs_.progress(deltaSeconds);
}

} // namespace eng::world
