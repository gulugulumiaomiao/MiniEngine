#include "tools/editor/EditorApplication.h"
#include "runtime/engine/Engine.h"
#include "rhi/vulkan/VulkanFactory.h"

#include <filesystem>

int main(int, char** argv) {
    const std::filesystem::path executable =
        std::filesystem::absolute(std::filesystem::path{argv[0]}).lexically_normal();
    // The editor config lives under editor/config/ next to the executable and is read
    // and written as a plain physical file: window preference plus the recent-project
    // registry. There is no editor-config:// mount; the whole file is owned by
    // EditorConfig::load/save.
    const std::filesystem::path editorConfigPath = executable.parent_path() / "editor" / "config" / "editor.json";
    engine::editor::EditorApplication application{editorConfigPath};
    const engine::rhi::vulkan::VulkanFactory contextFactory;
    return ENGINE.run(application, contextFactory, executable.parent_path() / "engine.json");
}
