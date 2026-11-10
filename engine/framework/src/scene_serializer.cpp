/// @file scene_serializer.cpp
/// @brief High-reliability transactional serializer and deserializer implementation for .hikari
/// files.

#include "framework/scene_serializer.h"

#include "core/json.h"
#include "core/log.h"
#include "physics/physics_components.h"
#include "render/mesh_component.h"
#include "script/script_components.h"
#include "world/components.h"
#include "world/hierarchy_systems.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace eng::framework {

namespace {

// In-memory staged entity representation for validation before mutating World
struct StagedTransform {
    Vec3 position{Vec3::zero()};
    Quat rotation{Quat::identity()};
    Vec3 scale{Vec3::one()};
    bool present{false};
};

struct StagedRigidBody {
    physics::MotionType motionType{physics::MotionType::Dynamic};
    uint16_t collisionLayer{physics::CollisionLayer::kMoving};
    bool present{false};
};

struct StagedCollider {
    physics::ColliderDesc desc{};
    bool present{false};
};

struct StagedScript {
    std::string scriptName;
    bool enabled{true};
    float updateInterval{0.0F};
    bool present{false};
};

struct StagedMesh {
    std::string assetPath;
    uint32_t submeshIndex{0};
    bool visible{true};
    bool castShadows{true};
    bool present{false};
};

struct StagedEntity {
    uint64_t stableId{0};
    uint64_t parentStableId{0};
    std::string name;
    StagedTransform transform{};
    StagedRigidBody rigidBody{};
    StagedCollider collider{};
    StagedScript script{};
    StagedMesh mesh{};
};

const char* motionTypeToString(physics::MotionType type) noexcept {
    switch (type) {
    case physics::MotionType::Static:
        return "Static";
    case physics::MotionType::Kinematic:
        return "Kinematic";
    case physics::MotionType::Dynamic:
        return "Dynamic";
    default:
        return "Dynamic";
    }
}

std::optional<physics::MotionType> stringToMotionType(std::string_view str) noexcept {
    if (str == "Static" || str == "static") {
        return physics::MotionType::Static;
    }
    if (str == "Kinematic" || str == "kinematic") {
        return physics::MotionType::Kinematic;
    }
    if (str == "Dynamic" || str == "dynamic") {
        return physics::MotionType::Dynamic;
    }
    return std::nullopt;
}

const char* colliderTypeToString(physics::ColliderType type) noexcept {
    switch (type) {
    case physics::ColliderType::Box:
        return "Box";
    case physics::ColliderType::Sphere:
        return "Sphere";
    case physics::ColliderType::Capsule:
        return "Capsule";
    default:
        return "Box";
    }
}

std::optional<physics::ColliderType> stringToColliderType(std::string_view str) noexcept {
    if (str == "Box" || str == "box") {
        return physics::ColliderType::Box;
    }
    if (str == "Sphere" || str == "sphere") {
        return physics::ColliderType::Sphere;
    }
    if (str == "Capsule" || str == "capsule") {
        return physics::ColliderType::Capsule;
    }
    return std::nullopt;
}

core::JsonValue vec3ToJson(const Vec3& v) {
    core::JsonValue arr = core::JsonValue::makeArray();
    arr.pushBack(v.x);
    arr.pushBack(v.y);
    arr.pushBack(v.z);
    return arr;
}

core::JsonValue quatToJson(const Quat& q) {
    core::JsonValue arr = core::JsonValue::makeArray();
    arr.pushBack(q.x);
    arr.pushBack(q.y);
    arr.pushBack(q.z);
    arr.pushBack(q.w);
    return arr;
}

std::optional<Vec3> jsonToVec3(const core::JsonValue& val) {
    if (!val.isArray() || val.size() < 3) {
        return std::nullopt;
    }
    return Vec3(val[0].asFloat(), val[1].asFloat(), val[2].asFloat());
}

std::optional<Quat> jsonToQuat(const core::JsonValue& val) {
    if (!val.isArray() || val.size() < 4) {
        return std::nullopt;
    }
    float x = val[0].asFloat();
    float y = val[1].asFloat();
    float z = val[2].asFloat();
    float w = val[3].asFloat();
    float lenSq = x * x + y * y + z * z + w * w;
    if (lenSq < 1e-8F) {
        return Quat::identity();
    }
    return Quat(x, y, z, w).normalized();
}

} // namespace

Result<std::string> SceneSerializer::serializeToString(world::World& world,
                                                       const SceneMetadata& metadata,
                                                       const SerializationOptions& options) {
    using namespace eng::core;

    JsonValue root = JsonValue::makeObject();
    root.set("formatVersion", static_cast<int>(kSupportedFormatVersion));

    JsonValue metaObj = JsonValue::makeObject();
    metaObj.set("name", metadata.name.empty() ? "Untitled" : metadata.name);
    metaObj.set("generator", metadata.generator.empty() ? "Hikari Engine" : metadata.generator);
    metaObj.set("timestamp", metadata.timestamp);
    root.set("metadata", std::move(metaObj));

    auto entities = world.getAllEntities();
    JsonValue entityArray = JsonValue::makeArray();

    // Determine highest assigned stable ID to mint IDs for entities without StableIdComponent
    uint64_t maxId = 0;
    for (auto h : entities) {
        if (const auto* s = world.getComponent<StableIdComponent>(h)) {
            if (s->id > maxId) {
                maxId = s->id;
            }
        }
    }

    uint64_t nextAvailableId = maxId + 1;

    for (auto h : entities) {
        if (!world.isAlive(h)) {
            continue;
        }

        uint64_t stableId = 0;
        if (const auto* s = world.getComponent<StableIdComponent>(h)) {
            stableId = s->id;
        }
        if (stableId == 0) {
            stableId = nextAvailableId++;
            world.setComponent(h, StableIdComponent{stableId});
        }

        JsonValue entObj = JsonValue::makeObject();
        entObj.set("id", stableId);

        std::string_view name = world.getEntityName(h);
        if (!name.empty()) {
            entObj.set("name", name);
        }

        world::EntityHandle parent = world.getParent(h);
        if (parent.isValid() && world.isAlive(parent)) {
            uint64_t parentStableId = 0;
            if (const auto* ps = world.getComponent<StableIdComponent>(parent)) {
                parentStableId = ps->id;
            }
            if (parentStableId == 0) {
                parentStableId = nextAvailableId++;
                world.setComponent(parent, StableIdComponent{parentStableId});
            }
            entObj.set("parent", parentStableId);
        }

        JsonValue compsObj = JsonValue::makeObject();

        // 1. Transform / TransformComponent / PhysicsTransformComponent
        if (const auto* tc = world.getComponent<world::TransformComponent>(h)) {
            JsonValue tObj = JsonValue::makeObject();
            tObj.set("position", vec3ToJson(tc->transform.position));
            tObj.set("rotation", quatToJson(tc->transform.rotation));
            tObj.set("scale", vec3ToJson(tc->transform.scale));
            compsObj.set("Transform", std::move(tObj));
        } else if (const auto* pt = world.getComponent<physics::PhysicsTransformComponent>(h)) {
            JsonValue tObj = JsonValue::makeObject();
            tObj.set("position", vec3ToJson(pt->transform.position));
            tObj.set("rotation", quatToJson(pt->transform.rotation));
            tObj.set("scale", vec3ToJson(pt->transform.scale));
            compsObj.set("Transform", std::move(tObj));
        }

        // 2. RigidBodyComponent
        if (const auto* rb = world.getComponent<physics::RigidBodyComponent>(h)) {
            JsonValue rbObj = JsonValue::makeObject();
            rbObj.set("motionType", motionTypeToString(rb->motionType));
            rbObj.set("collisionLayer", static_cast<int>(rb->collisionLayer));
            compsObj.set("RigidBody", std::move(rbObj));
        }

        // 3. ColliderComponent
        if (const auto* col = world.getComponent<physics::ColliderComponent>(h)) {
            JsonValue colObj = JsonValue::makeObject();
            colObj.set("type", colliderTypeToString(col->desc.type));
            colObj.set("halfExtents", vec3ToJson(col->desc.halfExtents));
            colObj.set("radius", col->desc.radius);
            colObj.set("halfHeight", col->desc.halfHeight);
            colObj.set("offset", vec3ToJson(col->desc.offset));
            colObj.set("rotation", quatToJson(col->desc.rotation));
            compsObj.set("Collider", std::move(colObj));
        }

        // 4. ScriptComponent
        if (const auto* sc = world.getComponent<script::ScriptComponent>(h)) {
            JsonValue scObj = JsonValue::makeObject();
            scObj.set("scriptName", std::string(sc->scriptName));
            scObj.set("enabled", sc->enabled);
            scObj.set("updateInterval", sc->updateInterval);
            compsObj.set("Script", std::move(scObj));
        }

        // 5. MeshComponent
        if (const auto* mc = world.getComponent<render::MeshComponent>(h)) {
            JsonValue mcObj = JsonValue::makeObject();
            mcObj.set("assetPath", mc->assetPath);
            mcObj.set("submeshIndex", static_cast<int>(mc->submeshIndex));
            mcObj.set("visible", mc->visible);
            mcObj.set("castShadows", mc->castShadows);
            compsObj.set("Mesh", std::move(mcObj));
        }

        entObj.set("components", std::move(compsObj));
        entityArray.pushBack(std::move(entObj));
    }

    root.set("entities", std::move(entityArray));

    return Result<std::string>::success(
        root.serialize(options.prettyPrint ? options.indentSpaces : 0));
}

Result<void> SceneSerializer::serializeToFile(world::World& world,
                                              const std::filesystem::path& path,
                                              const SceneMetadata& metadata,
                                              const SerializationOptions& options) {
    if (path.empty()) {
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "Empty scene file path"));
    }

    // 1. Serialize into memory buffer first
    auto strRes = serializeToString(world, metadata, options);
    if (!strRes.hasValue()) {
        return Result<void>::failure(strRes.error());
    }

    // 2. Ensure target parent directory exists
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "Failed to create directory for scene save"));
        }
    }

    // 3. Atomic write: write to temporary file, flush, then rename
    std::filesystem::path tmpPath = path;
    tmpPath += ".tmp";

    {
        std::ofstream file(tmpPath, std::ios::out | std::ios::trunc);
        if (!file.is_open()) {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "Failed to open temporary file for writing"));
        }
        file.write(strRes.value().data(), static_cast<std::streamsize>(strRes.value().size()));
        file.flush();
        if (!file.good()) {
            file.close();
            std::filesystem::remove(tmpPath);
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "I/O error during scene write"));
        }
    }

    // 4. Atomically rename temporary file to target path
    std::error_code renameEc;
    std::filesystem::rename(tmpPath, path, renameEc);
    if (renameEc) {
        // Fallback: remove target then rename
        std::filesystem::remove(path, renameEc);
        std::filesystem::rename(tmpPath, path, renameEc);
        if (renameEc) {
            std::filesystem::remove(tmpPath);
            return Result<void>::failure(Error(ErrorCode::InvalidArgument,
                                               "Failed to replace target scene file atomically"));
        }
    }

    ENG_LOG_INFO("Scene", "Scene successfully serialized to file");
    return Result<void>::success();
}

Result<DeserializationResult> SceneSerializer::deserializeFromString(world::World& world,
                                                                     std::string_view jsonText,
                                                                     bool clearWorldFirst) {
    using namespace eng::core;

    if (jsonText.empty()) {
        return Result<DeserializationResult>::failure(
            Error(ErrorCode::InvalidArgument, "Empty scene text supplied for deserialization"));
    }

    // 1. Parse JSON document
    auto parseRes = JsonValue::parse(jsonText);
    if (!parseRes.hasValue()) {
        return Result<DeserializationResult>::failure(
            Error(ErrorCode::InvalidArgument, "JSON parse error in scene file"));
    }

    const auto& root = parseRes.value();
    if (!root.isObject()) {
        return Result<DeserializationResult>::failure(
            Error(ErrorCode::InvalidArgument, "Root JSON value is not an object"));
    }

    // 2. Validate format version
    int version = root.getInt("formatVersion", 0);
    if (version == 0 || version > static_cast<int>(kSupportedFormatVersion)) {
        return Result<DeserializationResult>::failure(
            Error(ErrorCode::InvalidArgument, "Unsupported scene formatVersion in .hikari file"));
    }

    // 3. Validate entities array
    const auto* entityArray = root.getArray("entities");
    if (entityArray == nullptr) {
        return Result<DeserializationResult>::failure(
            Error(ErrorCode::InvalidArgument, "Missing or invalid 'entities' array in scene file"));
    }

    // 4. Staging and pre-validation (Transactional guarantee: World is not touched if any check
    // fails)
    std::vector<StagedEntity> stagedEntities;
    stagedEntities.reserve(entityArray->size());
    std::unordered_set<uint64_t> seenIds;
    std::vector<std::string> warnings;

    for (size_t i = 0; i < entityArray->size(); ++i) {
        const auto& entVal = (*entityArray)[i];
        if (!entVal.isObject()) {
            return Result<DeserializationResult>::failure(Error(
                ErrorCode::InvalidArgument, "Entity entry in 'entities' array is not an object"));
        }

        uint64_t stableId = entVal.getUInt64("id", 0);
        if (stableId == 0) {
            return Result<DeserializationResult>::failure(Error(
                ErrorCode::InvalidArgument, "Entity is missing required valid 'id' identifier"));
        }

        if (seenIds.contains(stableId)) {
            return Result<DeserializationResult>::failure(
                Error(ErrorCode::InvalidArgument, "Duplicate stable entity ID detected in scene"));
        }
        seenIds.insert(stableId);

        StagedEntity staged{};
        staged.stableId = stableId;
        staged.parentStableId = entVal.getUInt64("parent", 0);
        staged.name = std::string(entVal.getString("name", ""));

        const auto* compsObj = entVal.getObject("components");
        if (compsObj != nullptr) {
            for (const auto& compPair : compsObj->asObject()) {
                const std::string& compName = compPair.first;
                const JsonValue& compData = compPair.second;

                if (compName == "Transform") {
                    staged.transform.present = true;
                    if (auto pos = jsonToVec3(*compData.find("position"))) {
                        staged.transform.position = *pos;
                    }
                    if (auto rot = jsonToQuat(*compData.find("rotation"))) {
                        staged.transform.rotation = *rot;
                    }
                    if (auto scl = jsonToVec3(*compData.find("scale"))) {
                        if (scl->x < 0.0001F || scl->y < 0.0001F || scl->z < 0.0001F) {
                            return Result<DeserializationResult>::failure(
                                Error(ErrorCode::InvalidArgument,
                                      "Transform scale must be strictly positive"));
                        }
                        staged.transform.scale = *scl;
                    }
                } else if (compName == "RigidBody") {
                    staged.rigidBody.present = true;
                    std::string_view mtStr = compData.getString("motionType", "Dynamic");
                    auto mtOpt = stringToMotionType(mtStr);
                    if (!mtOpt.has_value()) {
                        return Result<DeserializationResult>::failure(
                            Error(ErrorCode::InvalidArgument, "Invalid RigidBody motionType"));
                    }
                    staged.rigidBody.motionType = *mtOpt;
                    int layer = compData.getInt("collisionLayer", physics::CollisionLayer::kMoving);
                    if (layer < 0 || layer >= physics::CollisionLayer::kCount) {
                        return Result<DeserializationResult>::failure(Error(
                            ErrorCode::InvalidArgument, "RigidBody collisionLayer out of bounds"));
                    }
                    staged.rigidBody.collisionLayer = static_cast<uint16_t>(layer);
                } else if (compName == "Collider") {
                    staged.collider.present = true;
                    std::string_view typeStr = compData.getString("type", "Box");
                    auto typeOpt = stringToColliderType(typeStr);
                    if (!typeOpt.has_value()) {
                        return Result<DeserializationResult>::failure(
                            Error(ErrorCode::InvalidArgument, "Invalid Collider primitive type"));
                    }
                    staged.collider.desc.type = *typeOpt;
                    if (auto ext = jsonToVec3(*compData.find("halfExtents"))) {
                        staged.collider.desc.halfExtents = *ext;
                    }
                    staged.collider.desc.radius = compData.getFloat("radius", 0.5F);
                    staged.collider.desc.halfHeight = compData.getFloat("halfHeight", 0.5F);
                    if (auto off = jsonToVec3(*compData.find("offset"))) {
                        staged.collider.desc.offset = *off;
                    }
                    if (auto rot = jsonToQuat(*compData.find("rotation"))) {
                        staged.collider.desc.rotation = *rot;
                    }
                } else if (compName == "Script") {
                    staged.script.present = true;
                    staged.script.scriptName = std::string(compData.getString("scriptName", ""));
                    staged.script.enabled = compData.getBool("enabled", true);
                    staged.script.updateInterval = compData.getFloat("updateInterval", 0.0F);
                } else if (compName == "Mesh") {
                    staged.mesh.present = true;
                    staged.mesh.assetPath = std::string(compData.getString("assetPath", ""));
                    staged.mesh.submeshIndex =
                        static_cast<uint32_t>(compData.getInt("submeshIndex", 0));
                    staged.mesh.visible = compData.getBool("visible", true);
                    staged.mesh.castShadows = compData.getBool("castShadows", true);
                } else {
                    warnings.push_back("Unknown component '" + compName + "' skipped on entity " +
                                       std::to_string(stableId));
                }
            }
        }

        stagedEntities.push_back(std::move(staged));
    }

    // 5. Commit transaction into World (guaranteed zero validation failures from here)
    if (clearWorldFirst) {
        world.clearUserEntities();
    }

    std::unordered_map<uint64_t, world::EntityHandle> stableIdToHandle;
    stableIdToHandle.reserve(stagedEntities.size());

    for (const auto& staged : stagedEntities) {
        world::EntityHandle h =
            staged.name.empty() ? world.createEntity() : world.createEntity(staged.name);
        world.setComponent(h, StableIdComponent{staged.stableId});
        stableIdToHandle[staged.stableId] = h;

        if (staged.transform.present) {
            Transform tr(staged.transform.position, staged.transform.rotation,
                         staged.transform.scale);
            world.setComponent(h, world::TransformComponent{tr});
            world.setComponent(h, physics::PhysicsTransformComponent{.transform = tr});
        }

        if (staged.rigidBody.present) {
            world.setComponent(h, physics::RigidBodyComponent{
                                      .bodyHandle = {},
                                      .motionType = staged.rigidBody.motionType,
                                      .collisionLayer = staged.rigidBody.collisionLayer,
                                  });
        }

        if (staged.collider.present) {
            world.setComponent(h, physics::ColliderComponent{
                                      .desc = staged.collider.desc,
                                  });
        }

        if (staged.script.present) {
            script::ScriptComponent sc{};
            const size_t len = std::min(staged.script.scriptName.size(), sizeof(sc.scriptName) - 1);
            for (size_t k = 0; k < len; ++k) {
                sc.scriptName[k] = staged.script.scriptName[k];
            }
            sc.scriptName[len] = '\0';
            sc.enabled = staged.script.enabled;
            sc.updateInterval = staged.script.updateInterval;
            sc.timeSinceLastUpdate = 0.0F;
            world.setComponent(h, sc);
        }

        if (staged.mesh.present) {
            render::MeshComponent mc{};
            mc.assetPath = staged.mesh.assetPath;
            mc.meshAsset = render::makeAssetIdFromPath(mc.assetPath);
            mc.submeshIndex = staged.mesh.submeshIndex;
            mc.visible = staged.mesh.visible;
            mc.castShadows = staged.mesh.castShadows;
            world.setComponent(h, mc);
        }
    }

    // 6. Establish parent-child hierarchy
    for (const auto& staged : stagedEntities) {
        if (staged.parentStableId != 0) {
            auto it = stableIdToHandle.find(staged.parentStableId);
            if (it != stableIdToHandle.end()) {
                world::EntityHandle childHandle = stableIdToHandle[staged.stableId];
                world.setParent(childHandle, it->second, false);
            }
        }
    }

    // 7. Propagate hierarchy transforms to initialize WorldTransformComponent
    world::updateHierarchyTransforms(world);

    ENG_LOG_INFO("Scene", "Deserialization completed successfully");

    return Result<DeserializationResult>::success(DeserializationResult{
        .entitiesLoaded = stagedEntities.size(),
        .warnings = std::move(warnings),
    });
}

Result<DeserializationResult>
SceneSerializer::deserializeFromFile(world::World& world, const std::filesystem::path& path,
                                     bool clearWorldFirst) {
    if (path.empty() || !std::filesystem::exists(path)) {
        return Result<DeserializationResult>::failure(
            Error(ErrorCode::InvalidArgument, "Scene file does not exist on disk"));
    }

    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return Result<DeserializationResult>::failure(
            Error(ErrorCode::InvalidArgument, "Failed to open scene file for reading"));
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    return deserializeFromString(world, content, clearWorldFirst);
}

} // namespace eng::framework
