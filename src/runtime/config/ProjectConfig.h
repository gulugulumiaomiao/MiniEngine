#pragma once

// 项目配置与目录约定由普通引擎和编辑器共用，不依赖编辑器宏。

#include "runtime/config/EngineConfig.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

class Transfer;

// A project is a directory holding project.json plus the fixed layout below. The mount
// table is derived from that layout instead of being configurable, so a project can
// never declare a scheme the engine does not manage.
inline constexpr std::string_view kProjectConfigFileName = "project.json";
inline constexpr std::string_view kProjectAssetsDirectory = "assets";
inline constexpr std::string_view kProjectLibraryDirectory = "library";
inline constexpr std::string_view kProjectShaderCacheDirectory = "generated-shaders/runtime";
inline constexpr std::string_view kProjectShaderBinaryDirectory = "generated-shaders/compiled";
// Pipeline cache file, created (empty) as part of the project template so the project
// is self-contained: the Vulkan device reads shader-cache://pipeline_cache.bin on open
// and replaces the placeholder with real bytes once pipelines exist.
inline constexpr std::string_view kProjectPipelineCacheFileName = "pipeline_cache.bin";
// Scene the editor opens when a project has no remembered scene. The built-in default
// Scene ships inside every project's assets/ (see seedSampleContentIntoProject), so it
// is referenced through the project's own assets:// mount like any user asset.
inline constexpr std::string_view kProjectMainScenePath = "assets://scenes/main.scene.json";
inline constexpr std::string_view kBuiltinDefaultScenePath = "assets://scenes/default.scene.json";

// 项目可选的窗口覆盖配置；缺省时使用引擎默认值及编辑器偏好（仅编辑器）。
// 与 engine.json 复用 WindowConfig，直接序列化为对象，缺少字段表示不覆盖。
struct ProjectConfig : public Transferable {
    std::uint32_t schemaVersion{1};
    std::string name;
    std::optional<WindowConfig> window;
    // RenderConfig reuses the engine's render settings, including the optional cache
    // path (pipeline_cache_path). When a project omits it, the shader-cache default is
    // used: the engine reads and writes that cache only while the project is open, so
    // the editor never touches a pipeline cache without a project.
    RenderConfig render;

    bool transfer(Transfer& archive) override;
    [[nodiscard]] bool validate(std::string& error) const;
    [[nodiscard]] static std::optional<ProjectConfig> load(const std::filesystem::path& path,
                                                           std::string& error);
    [[nodiscard]] bool save(const std::filesystem::path& path, std::string& error) const;
};

// One entry of the derived mount table.
struct ProjectMount {
    std::string scheme;
    std::filesystem::path directory;
    bool readOnly{};
};

[[nodiscard]] std::filesystem::path projectConfigPath(const std::filesystem::path& projectRoot);
// Absolute mounts for projectRoot, in mount order. The directories are not required to
// exist yet; openProject checks that separately so it can report a precise reason.
[[nodiscard]] std::vector<ProjectMount> projectMounts(const std::filesystem::path& projectRoot);
// True when projectRoot contains a readable project.json.
[[nodiscard]] bool isProjectDirectory(const std::filesystem::path& projectRoot);

// Returns true when a project has any Scene under assets://scenes. New projects are
// seeded with the built-in default Scene (seedSampleContentIntoProject), so in practice
// the call is a no-op; it fails honestly when the project has no Scene left, which is a
// legitimate state since sample content belongs to the project and may be deleted.
// Existing scenes are never touched, which keeps the call idempotent. Requires assets://
// to be mounted.
[[nodiscard]] bool ensureProjectMainScene(std::string& error);

// Scene to open for the active project: preferred when it still exists, otherwise the
// main scene, otherwise the alphabetically first scene under assets://scenes. Returns an
// invalid path when the project holds no scene at all.
[[nodiscard]] VirtualPath selectProjectScene(const VirtualPath& preferred);

} // namespace engine

