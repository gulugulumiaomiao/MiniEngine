#include "runtime/config/EngineConfig.h"
#include "runtime/config/ProjectConfig.h"
#include "runtime/engine/Engine.h"
#if defined(MINI_EDITOR)
#include "tools/editor/EditorConfig.h"
#endif
#include "core/serialization/JsonTransfer.h"
#include <concepts>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

namespace {

template <typename T>
concept HasEditorConfig = requires(T& engine, const std::filesystem::path& path) {
    engine.editorConfig();
    engine.loadEditorConfig(path);
    engine.saveEditorConfig();
};

#if defined(MINI_EDITOR)
static_assert(HasEditorConfig<engine::Engine>);
#else
static_assert(!HasEditorConfig<engine::Engine>);
#endif

std::filesystem::path makeTemporaryRoot() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
#if defined(MINI_EDITOR)
        "MiniEngineProjectConfigEditorTest";
#else
        "MiniEngineProjectConfigTest";
#endif
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    return root;
}

bool writeText(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    if (!stream)
        return false;
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return static_cast<bool>(stream);
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

template <typename T> bool checkConfigScopes(T source) {
    using namespace engine;
    static_assert(std::derived_from<T, Transferable>);
    JsonWriter root;
    if (!source.transfer(root))
        return false;
    const auto expected = nlohmann::json::parse(root.toString());
    if (!expected.is_object() || expected.contains(""))
        return false;
    JsonWriter nested;
    std::uint32_t before = 17, after = 29;
    if (!nested.beginObject({}) || !nested.transfer("before", before) ||
        !nested.transfer("config", source) || !nested.transfer("after", after) ||
        !nested.endObject())
        return false;
    const auto tree = nlohmann::json::parse(nested.toString());
    if (tree != nlohmann::json{{"before", 17}, {"config", expected}, {"after", 29}})
        return false;
    T decoded;
    JsonReader directReader{root.toString()};
    if (!decoded.transfer(directReader) || !directReader.valid())
        return false;
    JsonWriter decodedRoot;
    if (!decoded.transfer(decodedRoot) || nlohmann::json::parse(decodedRoot.toString()) != expected)
        return false;
    JsonReader nestedReader{nested.toString()};
    if (!nestedReader.beginObject({}) || !nestedReader.transfer("config", decoded) ||
        !nestedReader.transfer("after", after) || !nestedReader.endObject() || after != 29)
        return false;
    JsonWriter decodedNested;
    if (!decoded.transfer(decodedNested) ||
        nlohmann::json::parse(decodedNested.toString()) != expected)
        return false;
    for (const char* invalid : {"null", "[]", "42"}) {
        JsonReader reader{invalid};
        if (decoded.transfer(reader) || reader.valid())
            return false;
    }
    return true;
}

bool testConfigScopes([[maybe_unused]] const std::filesystem::path& root) {
    using namespace engine;
    ProjectConfig project;
    project.name = "Scope";
    if (!checkConfigScopes(project))
        return false;
    project.window = WindowConfig{1920, 1080, false};
    if (!checkConfigScopes(project))
        return false;
    JsonWriter writer;
    if (!project.transfer(writer))
        return false;
    const auto projectTree = nlohmann::json::parse(writer.toString());
    if (projectTree.at("window") !=
        nlohmann::json{{"width", 1920}, {"height", 1080}, {"vsync", false}})
        return false;
#if defined(MINI_EDITOR)
    EditorConfig editor = EditorConfig::createDefault();
    editor.registry.setLastScene(root, "assets://scenes/example.scene.json");
    engine::editor::RecentProjectEntry entry{root, "Scope"};
    if (!checkConfigScopes(editor) || !checkConfigScopes(*editor.window) ||
        !checkConfigScopes(editor.registry) || !checkConfigScopes(entry))
        return false;
    editor.window.reset();
    if (!checkConfigScopes(editor))
        return false;
    const auto editorPath = root / "editor.json";
    if (!editor.save(editorPath))
        return false;
    const auto loaded = EditorConfig::load(editorPath);
    if (!loaded || loaded->window ||
        loaded->registry.lastScene(root) != "assets://scenes/example.scene.json")
        return false;
    // 旧格式省略 window 时仍能读取后面的 registry，且不遗留归档错误。
    JsonReader minimal{R"({"schema_version":1,"registry":{"entries":[],"last_scenes":[]}})"};
    return editor.transfer(minimal) && minimal.valid() && !editor.window && editor.registry.empty();
#else
    return true;
#endif
}

} // namespace

int main() {
    using namespace engine;

    const std::filesystem::path root = makeTemporaryRoot();
    if (!testConfigScopes(root))
        return 24;
    // 两种引擎都必须具备可链接的项目 API；未初始化时不能破坏现有场景。
    Scene* initialScene = &ENGINE.scene();
    if (ENGINE.isProjectOpen() || !ENGINE.projectConfig().name.empty() || ENGINE.openProject(root))
        return 25;
    ENGINE.closeProject();
    if (ENGINE.isProjectOpen() || &ENGINE.scene() != initialScene)
        return 26;
    const std::filesystem::path configPath = projectConfigPath(root); std::printf("ConfigPath: %s\n", configPath.string().c_str()); std::fflush(stdout); if (configPath != root / "project.json") { return 1; }
    if (isProjectDirectory(root)) { return 2; }

    // --- save then load must round trip every field ---
    ProjectConfig source;
    source.name = "Sample Project";
    source.window = WindowConfig{1600, 900, false};
    source.render.pipeline = "MiniForward";
    std::string error; if (!source.save(configPath, error) || !error.empty()) { std::printf("FAIL: save failed, error=%s\n", error.c_str()); std::fflush(stdout); return 3; }
    if (!isProjectDirectory(root)) { return 4; } const std::optional<ProjectConfig> loaded = ProjectConfig::load(configPath, error);
        std::printf("Load returned: %s, error=%s\n", loaded ? "success" : "failed", error.c_str());
        std::fflush(stdout);
    if (!loaded || loaded->schemaVersion != 1 || loaded->name != source.name ||
        !loaded->window.has_value() || loaded->window->width != 1600 ||
        loaded->window->height != 900 || loaded->window->vsync != false ||
        loaded->render.pipeline != "MiniForward") { return 5;
    }
    // The scene to open is a convention plus editor state, never part of the project
    // configuration. if (readText(configPath).find("default_scene") != std::string::npos) { return 6; }

    // The serialized shape must stay human friendly: a window override is a direct
    // object, never the has_value/value wrapper the generic optional transfer emits.
    if (readText(configPath).find("has_value") != std::string::npos) { return 21; }

    // --- a project.json style window override is a direct object ---
    const std::filesystem::path overridePath = root / "override.json"; if (!writeText(overridePath, R"json({
  "schema_version": 1,
  "name": "Override",
  "window": {"width": 1920, "height": 1080, "vsync": false},
  "render": {"pipeline": "MiniForward"}
})json")) { return 22; }
    const std::optional<ProjectConfig> overridden = ProjectConfig::load(overridePath, error);
    if (!overridden || !overridden->window.has_value() ||
        overridden->window->width != 1920 || overridden->window->height != 1080 ||
        overridden->window->vsync || overridden->render.pipeline != "MiniForward" ||
        overridden->render.pipelineCachePath.string() != "shader-cache://pipeline_cache.bin") {
        return 23;
    }

    // --- a configuration without the optional window field is still complete ---
    const std::filesystem::path minimalPath = root / "minimal.json"; if (!writeText(minimalPath, R"json({
  "schema_version": 1,
  "name": "Minimal",
  "render": {"pipeline": "MiniForward"}
})json")) { return 7; }
    // Debug: check if file exists and read it back
    if (std::filesystem::exists(minimalPath)) {
        std::ifstream ifs(minimalPath);
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        std::printf("File content: %s\n", content.c_str()); std::fflush(stdout);
    } else {
    }
    const std::optional<ProjectConfig> minimal = ProjectConfig::load(minimalPath, error);
    if (!minimal) { std::printf("FAIL 8: minimal not loaded, error=%s\n", error.c_str()); std::fflush(stdout); return 8; }
    std::printf("Minimal loaded, name=%s\n", minimal->name.c_str()); std::fflush(stdout);
    if (minimal->name != "Minimal") { return 8; }
    std::printf("Name OK, has_window=%s\n", minimal->window.has_value() ? "yes" : "no"); std::fflush(stdout);
    if (minimal->window.has_value()) { return 8; }

    // --- validation rejections ---
    const auto rejects = [](const ProjectConfig& config) {
        std::string reason;
        return !config.validate(reason) && !reason.empty();
    };
    ProjectConfig badVersion = source;
    badVersion.schemaVersion = 2;
    ProjectConfig emptyName = source;
    emptyName.name.clear();
    ProjectConfig separatorName = source;
    separatorName.name = "nested/name";
    ProjectConfig zeroWidth = source;
    zeroWidth.window = WindowConfig{0, 720, true};
    ProjectConfig zeroHeight = source;
    zeroHeight.window = WindowConfig{1280, 0, true};
    ProjectConfig emptyPipeline = source;
    emptyPipeline.render.pipeline.clear();
    if (!rejects(badVersion) || !rejects(emptyName) || !rejects(separatorName) ||
        !rejects(zeroWidth) || !rejects(zeroHeight) || !rejects(emptyPipeline)) {
        return 9;
    }
    // An invalid configuration must not reach the disk either.
    if (emptyName.save(root / "rejected.json", error) ||
        std::filesystem::exists(root / "rejected.json")) {
        return 10;
    }

    // --- malformed and missing files ---
    const std::filesystem::path brokenPath = root / "broken.json";
    if (!writeText(brokenPath, R"json({"schema_version": 1, "name": "NoRender"})json"))
        return 11;
    if (ProjectConfig::load(brokenPath, error) || error.empty())
        return 12;
    if (ProjectConfig::load(root / "absent.json", error))
        return 13;
    const std::filesystem::path unsupportedPath = root / "unsupported.json";
    if (!writeText(unsupportedPath, R"json({
  "schema_version": 7,
  "name": "FromTheFuture",
  "render": {"pipeline": "MiniForward"}
})json")) {
        return 14;
    }
    if (ProjectConfig::load(unsupportedPath, error))
        return 15;

    // --- the derived mount table is fixed and fully project scoped ---
    const std::vector<ProjectMount> mounts = projectMounts(root);
    if (mounts.size() != 4)
        return 16;
    const std::filesystem::path normalized = std::filesystem::absolute(root).lexically_normal();
    if (mounts[0].scheme != "assets" || mounts[0].directory != normalized / "assets" ||
        mounts[0].readOnly || mounts[1].scheme != "library" ||
        mounts[1].directory != normalized / "library" || mounts[1].readOnly ||
        mounts[2].scheme != "shader-cache" ||
        mounts[2].directory != normalized / "generated-shaders/runtime" || mounts[2].readOnly ||
        mounts[3].scheme != "shader-bin" ||
        mounts[3].directory != normalized / "generated-shaders/compiled" || !mounts[3].readOnly) {
        return 17;
    }
    for (const ProjectMount& mount : mounts) {
        if (!isProjectMountScheme(mount.scheme))
            return 18;
    }
    // The engine never exposes a scheme for its built-in content; schemes like "builtin"
    // must never leak into a project's mount table.
    if (isProjectMountScheme("builtin"))
        return 19;

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    return 0;
}
