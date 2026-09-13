#include "tools/editor/EditorApplication.h"
#include "runtime/engine/Engine.h"
#include "rhi/vulkan/VulkanFactory.h"

int main() {
    // editor.json / imgui.ini / engine.json 全部由 EditorApplication 与 Engine::run
    // 从进程当前工作目录下的固定位置解析，main 不再定位或传入任何路径。
    engine::editor::EditorApplication application;
    const engine::rhi::vulkan::VulkanFactory contextFactory;
    return ENGINE.run(application, contextFactory);
}
