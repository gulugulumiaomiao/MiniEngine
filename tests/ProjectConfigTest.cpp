#include "runtime/config/EngineConfig.h"
#include "runtime/config/ProjectConfig.h"
#include "runtime/engine/Engine.h"
#if defined(MINI_EDITOR)
#include "tools/editor/EditorConfig.h"
#endif
#include "core/serialization/JsonTransfer.h"
#include "core/filesystem/FileSystem.h"
#include "runtime/window/Window.h"
#include "rhi/vulkan/VulkanFactory.h"
#include "scene/scene/SceneAsset.h"
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
    EngineConfig engineConfig = EngineConfig::createDefault();
    if (engineConfig.window.name != "Mini Engine" ||
        !checkConfigScopes(engineConfig) || !checkConfigScopes(engineConfig.render))
        return false;
    // 缓存路径不再序列化；旧配置中的该字段仍可忽略并正常读取。
    for (const char* json : {
             R"({"pipeline":"MiniForward"})",
             R"({"pipeline":"MiniForward","pipeline_cache_path":"custom://cache.bin"})"}) {
        RenderConfig render;
        JsonReader reader{json};
        JsonWriter encoded;
        if (!render.transfer(reader) || !reader.valid() || !render.transfer(encoded) ||
            nlohmann::json::parse(encoded.toString()) !=
                nlohmann::json{{"pipeline", "MiniForward"}})
            return false;
    }
    WindowConfig named{800, 600, false, "Custom Window"};
    WindowConfig other = named;
    other.name = "Another Window";
    if (named == other || !checkConfigScopes(named))
        return false;
    WindowConfig legacy;
    JsonReader legacyReader{R"({"width":800,"height":600,"vsync":true})"};
    if (!legacy.transfer(legacyReader) || !legacyReader.valid() || legacy.name != "Mini Engine")
        return false;
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
        nlohmann::json{{"width", 1920}, {"height", 1080}, {"vsync", false}, {"name", "Mini Engine"}})
        return false;
#if defined(MINI_EDITOR)
    EditorConfig editor = EditorConfig::createDefault();
    if (!editor.window || editor.window->name != "Mini Editor")
        return false;
    JsonReader legacyEditor{R"({"width":800,"height":600,"vsync":true,"x":-1,"y":-1,"maximized":false})"};
    EditorConfig::WindowPreference preference;
    if (!preference.transfer(legacyEditor) || !legacyEditor.valid() || preference.name != "Mini Editor")
        return false;
    editor.window->name = "Custom Editor";
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

// 使用真实 Win32 窗口和 Vulkan 上下文验证标题、切换和退出时的配置落盘。
class WindowSession final : public engine::Application {
public:
    explicit WindowSession(std::filesystem::path root, bool projects = false)
        : root_(std::move(root)), projects_(projects) {}
    bool passed{true};

private:
    bool title(std::wstring_view expected) {
        wchar_t text[256]{};
        GetWindowTextW(ENGINE.window().nativeHandle(), text, 256);
        return text == expected;
    }
    void check(bool result) {
        if (!result) {
            std::fprintf(stderr, "窗口生命周期验证失败，步骤 %d\n", step_);
            passed = false;
            ENGINE.requestQuit();
        }
    }
    void onStart() override {
#if defined(MINI_EDITOR)
        check(title(L"Mini Editor") && ENGINE.windowConfig().name == "Mini Editor");
#else
        check(title(L"Mini Engine") && ENGINE.windowConfig().name == "Mini Engine");
#endif
        if (!projects_)
            ENGINE.requestQuit();
    }
    void onUpdate(float) override {
#if defined(MINI_EDITOR)
        using namespace engine;
        const auto a = root_ / "a";
        const auto b = root_ / "b";
        std::string error;
        switch (step_++) {
        case 0: {
            const HWND previous = ENGINE.window().nativeHandle();
            check(ENGINE.openProject(a));
            if (!passed) return;
            check(title(L"Mini Editor: 测试项目甲") && ENGINE.window().nativeHandle() == previous);
            // 改尺寸后切换，确认保存的是项目 A 的配置，不是 B 的。
            RECT rectangle{0, 0, 520, 360};
            AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE);
            SetWindowPos(ENGINE.window().nativeHandle(), nullptr, 0, 0,
                         rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
                         SWP_NOMOVE | SWP_NOZORDER);
            break;
        }
        case 1: {
            check(ENGINE.openProject(b));
            if (!passed) return;
            check(title(L"Mini Editor: Project B"));
            const auto saved = ProjectConfig::load(projectConfigPath(a), error);
            check(saved && saved->window && saved->window->name == "Mini Editor: 测试项目甲" &&
                  saved->window->width == 520 && saved->window->height == 360);
            break;
        }
        case 2: {
            ENGINE.closeProject();
            check(!ENGINE.isProjectOpen() && title(L"Mini Editor"));
            check(ENGINE.windowConfig().width == 480 && ENGINE.windowConfig().height == 320);
            check(!FILE_SYSTEM.resolvePhysicalPath(VirtualPath{"assets://scenes/main.scene.json"}));
            const auto saved = ProjectConfig::load(projectConfigPath(b), error);
            check(saved && saved->window && saved->window->name == "Mini Editor: Project B");
            break;
        }
        case 3:
            // 失败后必须仍有可绘制的启动界面，而不是残留旧标题或空 Renderer。
            check(!ENGINE.openProject(root_ / "missing") && title(L"Mini Editor"));
            break;
        case 4:
            check(ENGINE.openProject(a));
            if (!passed) return;
            check(title(L"Mini Editor: 测试项目甲"));
            ShowWindow(ENGINE.window().nativeHandle(), SW_MINIMIZE);
            ENGINE.requestQuit();
            break;
        default:
            check(false);
        }
#endif
    }
    void onStop() override {
#if defined(MINI_EDITOR)
        // shutdown 必须保存 onStop 后的最终编辑器状态。
        ENGINE.editorConfig().registry.setLastScene(root_, "assets://scenes/final.scene.json");
#endif
    }
    std::filesystem::path root_;
    bool projects_{};
    int step_{};
};

bool testWindowSession(const std::filesystem::path& root) {
    using namespace engine;
    const auto previousDirectory = std::filesystem::current_path();
    const auto session = root / "window-session";
    std::filesystem::create_directories(session);
    EngineConfig config = EngineConfig::createDefault();
    config.workingDirectory = session.string();
    config.window = WindowConfig{480, 320, false};
    std::string error;
    if (!config.save(session / "engine.json", error))
        return false;
#if defined(MINI_EDITOR)
    EditorConfig editor = EditorConfig::createDefault();
    editor.window->width = 480;
    editor.window->height = 320;
    editor.window->vsync = false;
    const auto editorPath = session / "editor.json";
    if (!editor.save(editorPath))
        return false;
    ENGINE.loadEditorConfig(editorPath);
    for (const char* directory : {"a", "b"}) {
        const auto projectRoot = session / directory;
        for (const auto& mount : projectMounts(projectRoot))
            std::filesystem::create_directories(mount.directory);
        std::filesystem::create_directories(projectRoot / "assets/scenes");
        SceneAsset scene;
        JsonWriter writer;
        if (!scene.transfer(writer) ||
            !writeText(projectRoot / "assets/scenes/main.scene.json", writer.toString()))
            return false;
        ProjectConfig project;
        project.name = directory[0] == 'a' ? "测试项目甲" : "Project B";
        project.window = directory[0] == 'a' ? WindowConfig{480, 320, false} : WindowConfig{640, 400, false};
        if (!project.save(projectConfigPath(projectRoot), error))
            return false;
    }
    WindowSession app{session, true};
#else
    for (const auto& mount : projectMounts(session))
        std::filesystem::create_directories(mount.directory);
    WindowSession app{session};
#endif
    const rhi::vulkan::VulkanFactory factory;
    const int result = ENGINE.run(app, factory, session / "engine.json");
    std::filesystem::current_path(previousDirectory);
    if (result != 0 || !app.passed)
        return false;
#if defined(MINI_EDITOR)
    const auto saved = ProjectConfig::load(projectConfigPath(session / "a"), error);
    const auto savedEditor = EditorConfig::load(editorPath);
    if (!saved || !saved->window || saved->window->name != "Mini Editor: 测试项目甲" ||
        saved->window->width != 520 || saved->window->height != 360 || !savedEditor ||
        !savedEditor->window || savedEditor->window->name != "Mini Editor" ||
        savedEditor->window->width != 480 || savedEditor->window->height != 320 ||
        savedEditor->registry.lastScene(session) != "assets://scenes/final.scene.json")
        return false;
    ENGINE.loadEditorConfig(editorPath);
    WindowSession reopened{session};
    const int reopenedResult = ENGINE.run(reopened, factory, session / "engine.json");
    std::filesystem::current_path(previousDirectory);
    if (reopenedResult != 0 || !reopened.passed)
        return false;
#endif
    return true;
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
        overridden->window->vsync || overridden->render.pipeline != "MiniForward") {
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

    if (!testWindowSession(root))
        return 27;
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    return 0;
}
