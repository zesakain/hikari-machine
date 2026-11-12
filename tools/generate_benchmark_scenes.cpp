/// @file generate_benchmark_scenes.cpp
/// @brief Generator tool synthesizing persistent .hikari benchmark scene files.
///
/// Purpose:
/// Imports Khronos glTF benchmark assets (Sponza, FlightHelmet, DamagedHelmet),
/// attaches directional sunlight with shadow mapping, primary framed camera entities,
/// ACES tone mapping and bloom post-processing settings, and writes self-contained
/// portable .hikari scene files into assets/benchmark-scenes/.

#include "core/log.h"
#include "framework/scene_serializer.h"
#include "render/asset_manager.h"
#include "render/camera_component.h"
#include "render/gltf_scene_importer.h"
#include "render/light_components.h"
#include "render/post_process_types.h"
#include "world/hierarchy_systems.h"
#include "world/world.h"

#include <filesystem>
#include <iostream>

using namespace eng;

static bool generateSponza(const std::filesystem::path& outDir) {
    const std::string gltfPath = "test-assets/benchmark/sponza/Sponza.gltf";
    if (!std::filesystem::exists(gltfPath)) {
        std::cerr << "Sponza glTF source not found: " << gltfPath << '\n';
        return false;
    }

    world::World world;
    render::AssetManager assetMgr;
    auto importRes = render::GltfSceneImporter::importScene(gltfPath, world, &assetMgr);
    if (!importRes.success) {
        std::cerr << "Failed to import Sponza: " << importRes.errorMessage << '\n';
        return false;
    }

    // Directional Sunlight with shadow mapping
    auto sun = world.createEntity("Sunlight");
    render::DirectionalLightComponent dl{};
    dl.direction = Vec3(0.4F, 0.85F, 0.3F).normalized();
    dl.color = Vec3(1.0F, 0.96F, 0.90F);
    dl.intensity = 3.5F;
    dl.castShadows = true;
    dl.shadowBias = 0.0025F;
    dl.enabled = true;
    world.setComponent(sun, dl);

    // Primary Overview Camera looking down the atrium colonnade
    auto cam = world.createEntity("DefaultBenchmarkCamera");
    world.setComponent(cam, world::TransformComponent{Transform(Vec3(0.0F, 2.0F, 0.0F))});
    render::CameraComponent cc{};
    cc.fovDegrees = 65.0F;
    cc.nearPlane = 0.1F;
    cc.farPlane = 2000.0F;
    cc.isPrimary = true;
    world.setComponent(cam, cc);

    world::updateHierarchyTransforms(world);

    // Post-processing: ACES fitted tone mapping + exposure 1.0 + bloom
    render::PostProcessSettings pp{};
    pp.enabled = true;
    pp.exposure = 1.0F;
    pp.toneMapOp = render::ToneMapOperator::AcesFitted;
    pp.bloomEnabled = true;
    pp.bloomThreshold = 1.0F;
    pp.bloomIntensity = 0.04F;
    pp.bloomKnee = 0.5F;
    pp.reinhardWhitePoint = 4.0F;

    framework::SceneMetadata meta{
        .name = "Sponza Benchmark",
        .generator = "Hikari Engine 10G Benchmark Certification",
        .timestamp = 20261112,
    };

    const Vec3 camPos(0.0F, 2.0F, 0.0F);
    const Vec3 camTarget(0.0F, 2.0F, -6.0F);

    const std::filesystem::path dest = outDir / "Sponza_Benchmark.hikari";
    auto serRes =
        framework::SceneSerializer::serializeToFile(world, dest, meta, {}, pp, camPos, camTarget);
    if (!serRes.hasValue()) {
        std::cerr << "Failed to serialize Sponza benchmark: " << serRes.error().message << '\n';
        return false;
    }

    std::cout << "[SUCCESS] Wrote " << dest.string() << " (" << importRes.nodeCount + 2
              << " entities)\n";
    return true;
}

static bool generateFlightHelmet(const std::filesystem::path& outDir) {
    const std::string gltfPath = "test-assets/benchmark/flight_helmet/FlightHelmet.gltf";
    if (!std::filesystem::exists(gltfPath)) {
        std::cerr << "FlightHelmet glTF source not found: " << gltfPath << '\n';
        return false;
    }

    world::World world;
    render::AssetManager assetMgr;
    auto importRes = render::GltfSceneImporter::importScene(gltfPath, world, &assetMgr);
    if (!importRes.success) {
        std::cerr << "Failed to import FlightHelmet: " << importRes.errorMessage << '\n';
        return false;
    }

    // Directional Key Light
    auto sun = world.createEntity("KeyLight");
    render::DirectionalLightComponent dl{};
    dl.direction = Vec3(0.3F, 0.8F, 0.5F).normalized();
    dl.color = Vec3(1.0F, 1.0F, 1.0F);
    dl.intensity = 3.0F;
    dl.castShadows = true;
    dl.shadowBias = 0.0015F;
    dl.enabled = true;
    world.setComponent(sun, dl);

    // Primary Framed Camera
    auto cam = world.createEntity("DefaultBenchmarkCamera");
    world.setComponent(cam, world::TransformComponent{Transform(Vec3(0.0F, 0.35F, 0.85F))});
    render::CameraComponent cc{};
    cc.fovDegrees = 60.0F;
    cc.nearPlane = 0.05F;
    cc.farPlane = 500.0F;
    cc.isPrimary = true;
    world.setComponent(cam, cc);

    world::updateHierarchyTransforms(world);

    // Post-processing
    render::PostProcessSettings pp{};
    pp.enabled = true;
    pp.exposure = 1.0F;
    pp.toneMapOp = render::ToneMapOperator::AcesFitted;
    pp.bloomEnabled = true;
    pp.bloomThreshold = 1.0F;
    pp.bloomIntensity = 0.04F;
    pp.bloomKnee = 0.5F;

    framework::SceneMetadata meta{
        .name = "Flight Helmet Benchmark",
        .generator = "Hikari Engine 10G Benchmark Certification",
        .timestamp = 20261112,
    };

    const Vec3 camPos(0.0F, 0.35F, 0.85F);
    const Vec3 camTarget(0.0F, 0.2F, 0.0F);

    const std::filesystem::path dest = outDir / "FlightHelmet_Benchmark.hikari";
    auto serRes =
        framework::SceneSerializer::serializeToFile(world, dest, meta, {}, pp, camPos, camTarget);
    if (!serRes.hasValue()) {
        std::cerr << "Failed to serialize FlightHelmet benchmark: " << serRes.error().message
                  << '\n';
        return false;
    }

    std::cout << "[SUCCESS] Wrote " << dest.string() << " (" << importRes.nodeCount + 2
              << " entities)\n";
    return true;
}

static bool generateDamagedHelmet(const std::filesystem::path& outDir) {
    const std::string glbPath = "test-assets/DamagedHelmet.glb";
    if (!std::filesystem::exists(glbPath)) {
        std::cerr << "DamagedHelmet GLB source not found: " << glbPath << '\n';
        return false;
    }

    world::World world;
    render::AssetManager assetMgr;
    auto importRes = render::GltfSceneImporter::importScene(glbPath, world, &assetMgr);
    if (!importRes.success) {
        std::cerr << "Failed to import DamagedHelmet: " << importRes.errorMessage << '\n';
        return false;
    }

    // Directional Sunlight
    auto sun = world.createEntity("Sunlight");
    render::DirectionalLightComponent dl{};
    dl.direction = Vec3(0.4F, 0.7F, 0.5F).normalized();
    dl.color = Vec3(1.0F, 0.98F, 0.95F);
    dl.intensity = 3.5F;
    dl.castShadows = true;
    dl.shadowBias = 0.002F;
    dl.enabled = true;
    world.setComponent(sun, dl);

    // Primary Framed Camera
    auto cam = world.createEntity("DefaultBenchmarkCamera");
    world.setComponent(cam, world::TransformComponent{Transform(Vec3(0.0F, 0.0F, 2.5F))});
    render::CameraComponent cc{};
    cc.fovDegrees = 60.0F;
    cc.nearPlane = 0.05F;
    cc.farPlane = 500.0F;
    cc.isPrimary = true;
    world.setComponent(cam, cc);

    world::updateHierarchyTransforms(world);

    // Post-processing
    render::PostProcessSettings pp{};
    pp.enabled = true;
    pp.exposure = 1.0F;
    pp.toneMapOp = render::ToneMapOperator::AcesFitted;
    pp.bloomEnabled = true;
    pp.bloomThreshold = 1.0F;
    pp.bloomIntensity = 0.04F;
    pp.bloomKnee = 0.5F;

    framework::SceneMetadata meta{
        .name = "Damaged Helmet Benchmark",
        .generator = "Hikari Engine 10G Benchmark Certification",
        .timestamp = 20261112,
    };

    const Vec3 camPos(0.0F, 0.0F, 2.5F);
    const Vec3 camTarget(0.0F, 0.0F, 0.0F);

    const std::filesystem::path dest = outDir / "DamagedHelmet_Benchmark.hikari";
    auto serRes =
        framework::SceneSerializer::serializeToFile(world, dest, meta, {}, pp, camPos, camTarget);
    if (!serRes.hasValue()) {
        std::cerr << "Failed to serialize DamagedHelmet benchmark: " << serRes.error().message
                  << '\n';
        return false;
    }

    std::cout << "[SUCCESS] Wrote " << dest.string() << " (" << importRes.nodeCount + 2
              << " entities)\n";
    return true;
}

int main(int argc, char** argv) {
    std::filesystem::path outDir = "assets/benchmark-scenes";
    if (argc > 1) {
        outDir = argv[1];
    }

    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    std::cout << "Generating persistent benchmark scenes into: " << outDir.string() << '\n';

    bool ok = true;
    ok &= generateSponza(outDir);
    ok &= generateFlightHelmet(outDir);
    ok &= generateDamagedHelmet(outDir);

    if (!ok) {
        std::cerr << "One or more benchmark scenes failed to generate.\n";
        return 1;
    }

    std::cout << "All benchmark scenes successfully generated.\n";
    return 0;
}
