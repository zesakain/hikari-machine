/// @file scene.cpp
/// @brief Scene descriptor implementation.

#include "framework/scene.h"

namespace eng::framework {

Scene::Scene(std::string_view name) {
    metadata_.name = std::string(name);
}

} // namespace eng::framework
