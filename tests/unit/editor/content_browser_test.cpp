/// @file content_browser_test.cpp
/// @brief Comprehensive unit tests for Content Browser headless controller and asset management.

#include "editor/content_browser_controller.h"
#include "framework/scene_serializer.h"
#include "world/world.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace eng;
using namespace eng::editor;

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << '\n'; \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)

namespace {

struct SandboxRAII {
    std::filesystem::path dir;

    SandboxRAII() {
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        dir = std::filesystem::temp_directory_path() / ("hikari_cb_test_" + std::to_string(now));
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
    }

    ~SandboxRAII() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

} // namespace

static void testNavigationAndHistory() {
    SandboxRAII sandbox;
    std::filesystem::path root = sandbox.dir / "assets";
    std::filesystem::create_directories(root / "models" / "characters");
    std::filesystem::create_directories(root / "textures");
    std::filesystem::create_directories(root / "audio");

    ContentBrowserController controller(root);
    TEST_CHECK(controller.getRootDirectory() == root);
    TEST_CHECK(controller.getCurrentDirectory() == root);
    TEST_CHECK(!controller.canNavigateBack());
    TEST_CHECK(!controller.canNavigateForward());
    TEST_CHECK(!controller.canNavigateUp());

    // 1. Navigate to subdirectory
    bool navOk = controller.navigateTo(root / "models");
    TEST_CHECK(navOk);
    TEST_CHECK(controller.getCurrentDirectory() == root / "models");
    TEST_CHECK(controller.canNavigateBack());
    TEST_CHECK(controller.canNavigateUp());
    TEST_CHECK(!controller.canNavigateForward());

    // 2. Navigate nested
    navOk = controller.navigateTo(root / "models" / "characters");
    TEST_CHECK(navOk);
    TEST_CHECK(controller.getCurrentDirectory() == root / "models" / "characters");

    // 3. Navigate Back
    bool backOk = controller.navigateBack();
    TEST_CHECK(backOk);
    TEST_CHECK(controller.getCurrentDirectory() == root / "models");
    TEST_CHECK(controller.canNavigateForward());

    // 4. Navigate Forward
    bool fwdOk = controller.navigateForward();
    TEST_CHECK(fwdOk);
    TEST_CHECK(controller.getCurrentDirectory() == root / "models" / "characters");

    // 5. Navigate Up
    bool upOk = controller.navigateUp();
    TEST_CHECK(upOk);
    TEST_CHECK(controller.getCurrentDirectory() == root / "models");

    upOk = controller.navigateUp();
    TEST_CHECK(upOk);
    TEST_CHECK(controller.getCurrentDirectory() == root);
    TEST_CHECK(!controller.canNavigateUp());

    // 6. Breadcrumbs
    controller.navigateTo(root / "models" / "characters");
    auto breadcrumbs = controller.getBreadcrumbs();
    TEST_CHECK(breadcrumbs.size() == 3);
    TEST_CHECK(breadcrumbs[0].name == "assets");
    TEST_CHECK(breadcrumbs[1].name == "models");
    TEST_CHECK(breadcrumbs[2].name == "characters");

    // 7. Prevent Path Traversal Outside Root
    std::filesystem::path outside = sandbox.dir; // parent of root
    bool escapeOk = controller.navigateTo(outside);
    TEST_CHECK(!escapeOk);
    TEST_CHECK(controller.getCurrentDirectory() == root / "models" / "characters");

    std::cout << "[PASS] testNavigationAndHistory\n";
}

static void testFolderAndFileManagement() {
    SandboxRAII sandbox;
    std::filesystem::path root = sandbox.dir / "assets";
    std::filesystem::create_directories(root);

    ContentBrowserController controller(root);

    // 1. Create Folder
    auto folderRes = controller.createFolder("Meshes");
    TEST_CHECK(folderRes.hasValue());
    TEST_CHECK(std::filesystem::is_directory(folderRes.value()));

    // Reject duplicate folder
    auto dupFolderRes = controller.createFolder("Meshes");
    TEST_CHECK(!dupFolderRes.hasValue());

    // Reject illegal characters
    auto invalidFolderRes = controller.createFolder("Invalid:Folder?");
    TEST_CHECK(!invalidFolderRes.hasValue());

    // 2. Create Files with Templates
    auto emptyRes = controller.createFile("plain", ContentFileTemplate::EmptyFile);
    TEST_CHECK(emptyRes.hasValue());
    TEST_CHECK(std::filesystem::exists(emptyRes.value()));

    auto textRes = controller.createFile("notes", ContentFileTemplate::TextFile);
    TEST_CHECK(textRes.hasValue());
    TEST_CHECK(textRes.value().extension() == ".txt");
    TEST_CHECK(std::filesystem::exists(textRes.value()));

    auto luaRes = controller.createFile("player_controller", ContentFileTemplate::LuaScript);
    TEST_CHECK(luaRes.hasValue());
    TEST_CHECK(luaRes.value().extension() == ".lua");
    TEST_CHECK(std::filesystem::exists(luaRes.value()));
    // Verify boilerplate script content
    std::ifstream luaIn(luaRes.value());
    std::string luaContent((std::istreambuf_iterator<char>(luaIn)),
                           std::istreambuf_iterator<char>());
    TEST_CHECK(luaContent.find("on_update") != std::string::npos);

    // 3. Create Hikari Scene Template and verify SceneSerializer compatibility
    auto sceneRes = controller.createFile("MainLevel", ContentFileTemplate::HikariScene);
    TEST_CHECK(sceneRes.hasValue());
    TEST_CHECK(sceneRes.value().extension() == ".hikari");
    TEST_CHECK(std::filesystem::exists(sceneRes.value()));

    // CRITICAL REQUIREMENT: Verify valid minimal scene parses with SceneSerializer
    world::World testWorld;
    auto loadRes =
        framework::SceneSerializer::deserializeFromFile(testWorld, sceneRes.value().string(), true);
    TEST_CHECK(loadRes.hasValue());

    // 4. Duplicate File
    auto dupFileRes = controller.duplicateFile(textRes.value());
    TEST_CHECK(dupFileRes.hasValue());
    TEST_CHECK(std::filesystem::exists(dupFileRes.value()));
    TEST_CHECK(dupFileRes.value().filename() == "notes_Copy.txt");

    auto dupFile2Res = controller.duplicateFile(textRes.value());
    TEST_CHECK(dupFile2Res.hasValue());
    TEST_CHECK(dupFile2Res.value().filename() == "notes_Copy_1.txt");

    // 5. Rename File
    auto renameRes = controller.renameItem(emptyRes.value(), "renamed_plain.dat");
    TEST_CHECK(renameRes.hasValue());
    TEST_CHECK(!std::filesystem::exists(emptyRes.value()));
    TEST_CHECK(std::filesystem::exists(renameRes.value()));

    // 6. Rename Folder
    auto renameFolderRes = controller.renameItem(folderRes.value(), "StaticMeshes");
    TEST_CHECK(renameFolderRes.hasValue());
    TEST_CHECK(!std::filesystem::exists(folderRes.value()));
    TEST_CHECK(std::filesystem::exists(renameFolderRes.value()));

    // 7. Delete File
    auto delFileRes = controller.deleteItem(renameRes.value());
    TEST_CHECK(delFileRes.hasValue());
    TEST_CHECK(!std::filesystem::exists(renameRes.value()));

    // 8. Delete Non-Empty Directory Safeguard
    controller.navigateTo(renameFolderRes.value());
    controller.createFile("dummy.txt", ContentFileTemplate::TextFile);
    controller.navigateUp();

    // Deleting non-empty directory without recursive flag MUST fail
    auto nonRecDelete = controller.deleteItem(renameFolderRes.value(), false);
    TEST_CHECK(!nonRecDelete.hasValue());
    TEST_CHECK(std::filesystem::exists(renameFolderRes.value()));

    // Deleting non-empty directory with recursive=true succeeds
    auto recDelete = controller.deleteItem(renameFolderRes.value(), true);
    TEST_CHECK(recDelete.hasValue());
    TEST_CHECK(!std::filesystem::exists(renameFolderRes.value()));

    // 9. Root Deletion Prevention
    auto rootDelete = controller.deleteItem(root, true);
    TEST_CHECK(!rootDelete.hasValue());
    TEST_CHECK(std::filesystem::exists(root));

    // 10. Selection State Handling on Deletion
    auto tempFile = controller.createFile("to_delete.txt", ContentFileTemplate::TextFile);
    TEST_CHECK(tempFile.hasValue());
    TEST_CHECK(controller.getSelectedItem() != nullptr);
    TEST_CHECK(controller.getSelectedItem()->name == "to_delete.txt");

    auto delRes = controller.deleteItem(tempFile.value());
    TEST_CHECK(delRes.hasValue());
    controller.setSelectedItemIndex(-1);
    TEST_CHECK(controller.getSelectedItem() == nullptr);
    TEST_CHECK(controller.getSelectedItemIndex() == -1);

    std::cout << "[PASS] testFolderAndFileManagement\n";
}

static void testSortingAndFiltering() {
    SandboxRAII sandbox;
    std::filesystem::path root = sandbox.dir / "assets";
    std::filesystem::create_directories(root);

    ContentBrowserController controller(root);
    controller.createFolder("FolderB");
    controller.createFolder("FolderA");
    controller.createFile("model_hero.glb", ContentFileTemplate::EmptyFile);
    controller.createFile("texture_diffuse.png", ContentFileTemplate::EmptyFile);
    controller.createFile("script_ai.lua", ContentFileTemplate::LuaScript);

    // 1. Default Sort (Directories first, Name ascending)
    const auto& items = controller.getItems();
    TEST_CHECK(items.size() == 5);
    TEST_CHECK(items[0].name == "FolderA");
    TEST_CHECK(items[1].name == "FolderB");
    TEST_CHECK(items[0].isDirectory);
    TEST_CHECK(items[1].isDirectory);

    // 2. Name Descending
    controller.setSort(ContentSortField::Name, ContentSortDirection::Descending);
    const auto& descItems = controller.getItems();
    TEST_CHECK(descItems[0].name == "FolderB");
    TEST_CHECK(descItems[1].name == "FolderA");

    // 3. Search Filter
    controller.setSearchFilter("hero");
    TEST_CHECK(controller.getItems().size() == 1);
    TEST_CHECK(controller.getItems()[0].name == "model_hero.glb");

    controller.setSearchFilter("");
    TEST_CHECK(controller.getItems().size() == 5);

    // 4. Category Filter
    controller.setFilterCategory(ContentFilterCategory::Meshes);
    TEST_CHECK(controller.getItems().size() == 3); // 2 folders + 1 mesh
    bool hasMesh = false;
    for (const auto& it : controller.getItems()) {
        if (!it.isDirectory) {
            TEST_CHECK(it.type == ContentItemType::Mesh);
            hasMesh = true;
        }
    }
    TEST_CHECK(hasMesh);

    controller.setFilterCategory(ContentFilterCategory::Scripts);
    bool hasScript = false;
    for (const auto& it : controller.getItems()) {
        if (!it.isDirectory) {
            TEST_CHECK(it.type == ContentItemType::Script);
            hasScript = true;
        }
    }
    TEST_CHECK(hasScript);

    std::cout << "[PASS] testSortingAndFiltering\n";
}

static void testAssetFormatBadgesAndMetadata() {
    SandboxRAII sandbox;
    std::filesystem::path root = sandbox.dir / "assets";
    std::filesystem::create_directories(root);

    ContentBrowserController controller(root);
    controller.createFile("mesh.obj", ContentFileTemplate::EmptyFile);
    controller.createFile("scene.gltf", ContentFileTemplate::EmptyFile);
    controller.createFile("model.glb", ContentFileTemplate::EmptyFile);

    controller.refresh();
    const auto& items = controller.getItems();
    TEST_CHECK(items.size() == 3);

    for (const auto& it : items) {
        TEST_CHECK(it.isMesh);
        if (it.extension == ".obj") {
            TEST_CHECK(std::string(it.typeBadge) == "[OBJ]");
        } else if (it.extension == ".gltf") {
            TEST_CHECK(std::string(it.typeBadge) == "[GLTF]");
        } else if (it.extension == ".glb") {
            TEST_CHECK(std::string(it.typeBadge) == "[GLB]");
        }
    }

    std::cout << "[PASS] testAssetFormatBadgesAndMetadata\n";
}

static void testAssetImportPipeline() {
    SandboxRAII sandbox;
    std::filesystem::path root = sandbox.dir / "assets";
    std::filesystem::path externalDir = sandbox.dir / "external_downloads";
    std::filesystem::create_directories(root);
    std::filesystem::create_directories(externalDir);

    // 1. Supported extension validation
    TEST_CHECK(ContentBrowserController::isSupportedImportExtension(".obj"));
    TEST_CHECK(ContentBrowserController::isSupportedImportExtension("OBJ"));
    TEST_CHECK(ContentBrowserController::isSupportedImportExtension(".gltf"));
    TEST_CHECK(ContentBrowserController::isSupportedImportExtension(".glb"));
    TEST_CHECK(ContentBrowserController::isSupportedImportExtension(".png"));
    TEST_CHECK(ContentBrowserController::isSupportedImportExtension(".wav"));
    TEST_CHECK(ContentBrowserController::isSupportedImportExtension(".lua"));
    TEST_CHECK(ContentBrowserController::isSupportedImportExtension(".hikari"));
    TEST_CHECK(ContentBrowserController::isSupportedImportExtension(".mtl"));
    TEST_CHECK(!ContentBrowserController::isSupportedImportExtension(".exe"));
    TEST_CHECK(!ContentBrowserController::isSupportedImportExtension(".dll"));
    TEST_CHECK(!ContentBrowserController::isSupportedImportExtension(""));

    // 2. Create valid Wavefront OBJ in external directory
    std::filesystem::path validExternalObj = externalDir / "custom_pyramid.obj";
    {
        std::ofstream out(validExternalObj);
        out << "# Valid Wavefront OBJ\n"
            << "v 0.0 1.0 0.0\n"
            << "v -1.0 0.0 1.0\n"
            << "v 1.0 0.0 1.0\n"
            << "v 0.0 0.0 -1.0\n"
            << "f 1 2 3\n"
            << "f 1 3 4\n"
            << "f 1 4 2\n"
            << "f 2 4 3\n";
    }

    ContentBrowserController controller(root);

    // 3. Import valid OBJ
    auto importRes = controller.importFile(validExternalObj);
    TEST_CHECK(importRes.hasValue());
    std::filesystem::path importedPath = importRes.value();
    TEST_CHECK(std::filesystem::exists(importedPath));
    TEST_CHECK(importedPath.filename() == "custom_pyramid.obj");
    TEST_CHECK(importedPath.parent_path() == root);

    // Verify immediately enumerated and selected
    const auto* selItem = controller.getSelectedItem();
    TEST_CHECK(selItem != nullptr);
    TEST_CHECK(selItem->name == "custom_pyramid.obj");
    TEST_CHECK(selItem->isMesh);
    TEST_CHECK(std::string(selItem->typeBadge) == "[OBJ]");

    // 4. Import into subfolder
    auto folderRes = controller.createFolder("ImportedMeshes");
    TEST_CHECK(folderRes.hasValue());
    TEST_CHECK(controller.navigateTo(folderRes.value()));

    auto subImportRes = controller.importFile(validExternalObj);
    TEST_CHECK(subImportRes.hasValue());
    TEST_CHECK(subImportRes.value().parent_path() == folderRes.value());
    TEST_CHECK(controller.getItems().size() == 1);
    TEST_CHECK(controller.getItems()[0].name == "custom_pyramid.obj");

    // 5. Batch import multiple files
    std::filesystem::path textureFile = externalDir / "diffuse.png";
    {
        std::ofstream out(textureFile, std::ios::binary);
        out << "fake_png_data";
    }
    std::filesystem::path materialFile = externalDir / "custom.mtl";
    {
        std::ofstream out(materialFile);
        out << "newmtl Material1\n";
    }

    auto batchRes = controller.importFiles({textureFile, materialFile});
    TEST_CHECK(batchRes.hasValue());
    TEST_CHECK(batchRes.value().size() == 2);
    TEST_CHECK(std::filesystem::exists(folderRes.value() / "diffuse.png"));
    TEST_CHECK(std::filesystem::exists(folderRes.value() / "custom.mtl"));
    TEST_CHECK(controller.getItems().size() == 3);

    // 6. Graceful Error Handling: Non-existent file
    auto nonExistentRes = controller.importFile(externalDir / "does_not_exist.obj");
    TEST_CHECK(!nonExistentRes.hasValue());

    // 7. Graceful Error Handling: Unsupported extension
    std::filesystem::path badExe = externalDir / "malicious.exe";
    {
        std::ofstream out(badExe);
        out << "binary";
    }
    auto badExtRes = controller.importFile(badExe);
    TEST_CHECK(!badExtRes.hasValue());

    // 8. Graceful Error Handling: Corrupted / invalid OBJ (no valid vertex geometry)
    std::filesystem::path corruptedObj = externalDir / "corrupted.obj";
    {
        std::ofstream out(corruptedObj);
        out << "# Corrupted OBJ file\n"
            << "invalid line without any vertices\n"
            << "foo bar baz\n";
    }
    auto corruptRes = controller.importFile(corruptedObj);
    TEST_CHECK(!corruptRes.hasValue());

    // 8b. Graceful Error Handling: Corrupted / invalid glTF
    std::filesystem::path corruptedGltf = externalDir / "corrupted.gltf";
    {
        std::ofstream out(corruptedGltf);
        out << "{\n  \"not_valid_gltf\": true\n}\n";
    }
    auto corruptGltfRes = controller.importFile(corruptedGltf);
    TEST_CHECK(!corruptGltfRes.hasValue());

    // 9. Graceful Error Handling: Importing file already at destination
    auto selfImportRes = controller.importFile(folderRes.value() / "custom_pyramid.obj");
    TEST_CHECK(!selfImportRes.hasValue());

    std::cout << "[PASS] testAssetImportPipeline\n";
}

static void testFolderDeletionSecurity() {
    SandboxRAII sandbox;
    std::filesystem::path root = sandbox.dir / "assets";
    std::filesystem::path external = sandbox.dir / "outside_project";
    std::filesystem::create_directories(root);
    std::filesystem::create_directories(external);

    ContentBrowserController controller(root);

    // 1. Deleting empty folder succeeds
    auto emptyFolderRes = controller.createFolder("EmptyFolder");
    TEST_CHECK(emptyFolderRes.hasValue());
    TEST_CHECK(std::filesystem::exists(emptyFolderRes.value()));
    auto delEmptyRes = controller.deleteItem(emptyFolderRes.value(), false);
    TEST_CHECK(delEmptyRes.hasValue());
    TEST_CHECK(!std::filesystem::exists(emptyFolderRes.value()));

    // 2. Deleting non-empty folder without recursive flag fails
    auto parentFolderRes = controller.createFolder("ParentFolder");
    TEST_CHECK(parentFolderRes.hasValue());
    controller.navigateTo(parentFolderRes.value());
    controller.createFolder("NestedChild");
    controller.createFile("data.txt", ContentFileTemplate::TextFile);
    controller.navigateUp();

    auto delNonEmptyNoRec = controller.deleteItem(parentFolderRes.value(), false);
    TEST_CHECK(!delNonEmptyNoRec.hasValue());
    TEST_CHECK(std::filesystem::exists(parentFolderRes.value()));
    TEST_CHECK(std::filesystem::exists(parentFolderRes.value() / "NestedChild"));
    TEST_CHECK(std::filesystem::exists(parentFolderRes.value() / "data.txt"));

    // 3. Deleting non-empty folder with recursive flag succeeds
    auto delNonEmptyRec = controller.deleteItem(parentFolderRes.value(), true);
    TEST_CHECK(delNonEmptyRec.hasValue());
    TEST_CHECK(!std::filesystem::exists(parentFolderRes.value()));

    // 4. Root deletion prevention
    auto delRootRes = controller.deleteItem(root, true);
    TEST_CHECK(!delRootRes.hasValue());
    TEST_CHECK(std::filesystem::exists(root));

    // 5. Path traversal / escape prevention
    auto traversalPath = root / ".." / "outside_project";
    auto delTraversalRes = controller.deleteItem(traversalPath, true);
    TEST_CHECK(!delTraversalRes.hasValue());
    TEST_CHECK(std::filesystem::exists(external));

    // 6. Absolute path outside root
    auto delOutsideRes = controller.deleteItem(external, true);
    TEST_CHECK(!delOutsideRes.hasValue());
    TEST_CHECK(std::filesystem::exists(external));

    // 7. Symlink escape protection (if symlinks supported on platform)
    std::error_code symEc;
    std::filesystem::path symlinkPath = root / "external_symlink";
    std::filesystem::create_directory_symlink(external, symlinkPath, symEc);
    if (!symEc && std::filesystem::exists(symlinkPath)) {
        // Attempting to delete symlink pointing outside root
        auto delSymRes = controller.deleteItem(symlinkPath, true);
        TEST_CHECK(!delSymRes.hasValue());
        TEST_CHECK(std::filesystem::exists(external));
        std::filesystem::remove(symlinkPath, symEc);
    }

    // 8. Navigating inside folder, deleting it, retreats current directory to parent
    auto workFolderRes = controller.createFolder("WorkFolder");
    TEST_CHECK(workFolderRes.hasValue());
    controller.navigateTo(workFolderRes.value());
    TEST_CHECK(controller.getCurrentDirectory() == workFolderRes.value());
    auto delCurDirRes = controller.deleteItem(workFolderRes.value(), false);
    TEST_CHECK(delCurDirRes.hasValue());
    TEST_CHECK(controller.getCurrentDirectory() == root);

    std::cout << "[PASS] testFolderDeletionSecurity\n";
}

int main() {
    std::cout << "Running Content Browser unit tests...\n";
    testNavigationAndHistory();
    testFolderAndFileManagement();
    testSortingAndFiltering();
    testAssetFormatBadgesAndMetadata();
    testAssetImportPipeline();
    testFolderDeletionSecurity();
    std::cout << "All Content Browser tests passed successfully!\n";
    return 0;
}
