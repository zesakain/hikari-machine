/// @file scene.h
/// @brief Persistent scene descriptor, metadata, and stable entity identifier primitives.
///
/// Purpose:
/// Defines the container model for Hikari scenes, including format versioning, authoring metadata,
/// and stable cross-session entity identification.
///
/// Ownership and Lifetime:
/// Scene and SceneMetadata are value types. Zero dynamic allocations in the hot path.
///
/// Thread Safety:
/// Scene value types can be read concurrently. Mutation requires external synchronization.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace eng::framework {

/// @struct StableIdComponent
/// @brief Persistent 64-bit unique entity identifier surviving scene serialization cycles.
struct StableIdComponent {
    uint64_t id{0};
};

/// @struct SceneMetadata
/// @brief Descriptive header metadata embedded into the .hikari scene container.
struct SceneMetadata {
    std::string name{"Untitled"};
    std::string generator{"Hikari Engine 0.1.0"};
    uint64_t timestamp{0};
};

/// @class Scene
/// @brief Authoring-time scene metadata and configuration.
class Scene {
public:
    static constexpr uint32_t kCurrentFormatVersion = 1;

    Scene() = default;
    explicit Scene(std::string_view name);

    [[nodiscard]] std::string_view getName() const noexcept { return metadata_.name; }
    void setName(std::string_view name) { metadata_.name = std::string(name); }

    [[nodiscard]] const SceneMetadata& getMetadata() const noexcept { return metadata_; }
    void setMetadata(const SceneMetadata& metadata) { metadata_ = metadata; }

    [[nodiscard]] uint32_t getFormatVersion() const noexcept { return formatVersion_; }
    void setFormatVersion(uint32_t version) noexcept { formatVersion_ = version; }

private:
    uint32_t formatVersion_{kCurrentFormatVersion};
    SceneMetadata metadata_{};
};

} // namespace eng::framework
