#include "tools/editor/widgets/MaterialInspector.h"

#include "asset/database/AssetDatabase.h"
#include "asset/format/MaterialAssetFormat.h"
#include "core/filesystem/FileSystem.h"
#include "core/filesystem/FileWatcher.h"
#include "asset/types/MaterialAsset.h"
#include "render/material/Material.h"
#include "render/material/MaterialManager.h"
#include "scene/components/MaterialComponent.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "TestAssetEnvironment.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {
using namespace engine;
using namespace engine::editor;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition);                           \
            return false;                                                                          \
        }                                                                                          \
    } while (false)

constexpr std::string_view kVertexShader = "#version 450\nvoid main(){gl_Position=vec4(0);}\n";
constexpr std::string_view kFragmentShader =
    "#version 450\nlayout(location=0) out vec4 c;void main(){c=vec4(1);}\n";

// Metallic 先于 BaseColor 声明，UI 里 slider 排在颜色控件之前：扫描点击在触到
// ColorEdit4（点击可能弹 picker）之前先命中 Metallic。RECEIVE_SHADOWS 供 keyword
// 用例使用。
constexpr std::string_view kFixtureShaderJson = R"json({
  "$schemaVersion": 1,
  "name": "FixtureShader",
  "properties": [
    { "name": "Metallic", "type": "Range", "default": 0.0, "range": [0.0, 1.0] },
    { "name": "BaseColor", "type": "Color", "default": [1, 1, 1, 1] }
  ],
  "subShaders": [{
    "passes": [{
      "name": "Forward",
      "features": ["RECEIVE_SHADOWS"],
      "program": { "vertex": "panel.vert", "frag": "panel.frag" }
    }]
  }]
})json";

// 同属性集合的第二个 shader：切换时兼容属性保留，写回时 shader 引用更新。
constexpr std::string_view kFixture2ShaderJson = R"json({
  "$schemaVersion": 1,
  "name": "FixtureShader2",
  "properties": [
    { "name": "Metallic", "type": "Range", "default": 0.0, "range": [0.0, 1.0] },
    { "name": "BaseColor", "type": "Color", "default": [1, 1, 1, 1] }
  ],
  "subShaders": [{
    "passes": [{
      "name": "Forward",
      "features": ["RECEIVE_SHADOWS"],
      "program": { "vertex": "panel.vert", "frag": "panel.frag" }
    }]
  }]
})json";

constexpr std::string_view kWarmMaterialJson = R"json({
  "$schemaVersion": 1,
  "name": "Warm",
  "shader": "assets://shaders/fixture.shader.json",
  "properties": {
    "BaseColor": [1.0, 0.5, 0.2, 1.0]
  }
})json";

constexpr std::string_view kCoolMaterialJson = R"json({
  "$schemaVersion": 1,
  "name": "Cool",
  "shader": "assets://shaders/fixture.shader.json",
  "properties": {
    "BaseColor": [0.2, 0.5, 1.0, 1.0]
  }
})json";

const VirtualPath kWarmMaterialPath{"assets://materials/warm.material.json"};
const VirtualPath kCoolMaterialPath{"assets://materials/cool.material.json"};

[[nodiscard]] std::shared_ptr<MaterialAsset> readMaterialFromDisk(const VirtualPath& path) {
    const std::optional<std::string> source = FILE_SYSTEM.readText(path);
    if (!source)
        return nullptr;
    return format::parseMaterialAsset(path, *source, ASSET_DATABASE);
}

// 事件注入驱动 MaterialInspector。物理写入 fixture 后 initializeAssetEnvironment
// 一次性导入（先写后挂载，保证 AssetDatabase 有 GUID 记录），FileWatcher 停用以
// 隔离异步重导入。
struct Harness {
    MaterialInspector inspector;
    std::filesystem::path root;
    bool valid{};
    Ref<Material> current;

    Harness() {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {400, 600};
        io.DeltaTime = 1.0F / 60.0F;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        root = std::filesystem::temp_directory_path() /
               ("MiniEngineMaterialInspectorTest-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        const std::filesystem::path assets = root / "assets";
        const auto write = [&assets](const char* relative, std::string_view content) {
            const std::filesystem::path target = assets / relative;
            std::error_code ignored;
            std::filesystem::create_directories(target.parent_path(), ignored);
            std::ofstream stream(target, std::ios::binary);
            stream << std::string{content};
            return stream.good();
        };
        valid = write("shaders/panel.vert", kVertexShader) &&
                write("shaders/panel.frag", kFragmentShader) &&
                write("shaders/fixture.shader.json", kFixtureShaderJson) &&
                write("shaders/fixture2.shader.json", kFixture2ShaderJson) &&
                write("materials/warm.material.json", kWarmMaterialJson) &&
                write("materials/cool.material.json", kCoolMaterialJson) &&
                test::initializeAssetEnvironment(assets);
        if (!valid)
            return;
        FILE_WATCHER.stop();
    }
    ~Harness() {
        FILE_WATCHER.stop();
        test::shutdownAssetEnvironment();
        ImGui::DestroyContext();
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    void frame() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({400, 600});
        ImGui::Begin("MaterialInspectorTest", nullptr, ImGuiWindowFlags_NoCollapse);
        inspector.draw(current);
        ImGui::End();
        ImGui::Render();
    }
    void settle() {
        frame();
        frame();
    }
    void advance(int frames = 22) {
        for (int i = 0; i < frames; ++i)
            frame();
    }
    void move(ImVec2 position) {
        ImGui::GetIO().AddMousePosEvent(position.x, position.y);
        frame();
    }
    void button(int id, bool down) {
        ImGui::GetIO().AddMouseButtonEvent(id, down);
        frame();
    }
    // press 落在目标上、release 移到窗口右下空白：Combo 只展开不选中、Checkbox 与
    // 按钮不触发（它们由 release 激活）、Slider 在 press 时跳值并随拖动 clamp 到
    // 轨道右端（1.0）——副作用可控且编辑值可预测。
    void pressClick(ImVec2 position) {
        move(position);
        button(0, true);
        move({380.0F, 590.0F});
        button(0, false);
        settle();
    }
    // 一帧超长 DeltaTime 让防抖分支立即把 pending 编辑写盘。
    void pumpDebounce() {
        ImGui::GetIO().DeltaTime = 0.6F;
        frame();
        ImGui::GetIO().DeltaTime = 1.0F / 60.0F;
        settle();
    }
    [[nodiscard]] ImVec2 content(float x, float y) const {
        const ImGuiWindow* window = ImGui::FindWindowByName("MaterialInspectorTest");
        return {window->DC.CursorStartPos.x + x, window->DC.CursorStartPos.y + y};
    }
    // 从上向下扫描 pressClick，直到谓词成真（控件没有稳定行号可寻址）。
    template <typename Predicate>
    bool scan(Predicate predicate, float maxY = 220.0F) {
        for (float y = 0.0F; y < maxY; y += 6.0F) {
            pressClick(content(100.0F, y));
            if (predicate())
                return true;
        }
        return false;
    }
};

// UI 编辑直达运行时实例（MaterialComponent 引用者同一对象、当帧同步）并防抖写回
// .material.json 源文件。
bool editsUpdateInstanceAndWriteBack() {
    Harness ui;
    CHECK(ui.valid);
    const Ref<Material> handle = MATERIAL_RESOURCE_MANAGER.load(kWarmMaterialPath);
    CHECK(static_cast<bool>(handle));
    MaterialComponent component;
    component.setMaterial(0, handle);
    ui.current = handle;
    ui.settle();

    Material* material = handle.get();
    CHECK(material != nullptr);
    CHECK(material->getFloat("Metallic") == 0.0F);
    // 引用者持有的是管理器里的同一实例（单句柄语义）。
    CHECK(component.material(0).get() == material);

    CHECK(ui.scan([handle] {
        return handle->getFloat("Metallic") != 0.0F;
    }));

    material = handle.get();
    CHECK(material->getFloat("Metallic") == 1.0F);
    CHECK(material->dirty());
    CHECK(component.material(0)->getFloat("Metallic") == 1.0F);

    ui.pumpDebounce();
    const auto onDisk = readMaterialFromDisk(kWarmMaterialPath);
    CHECK(onDisk != nullptr);
    const float* const metallic = std::get_if<float>(&onDisk->properties.at("Metallic"));
    CHECK(metallic != nullptr);
    CHECK(*metallic == 1.0F);
    return true;
}

// 写回 + 同步重导入幂等：句柄不变、运行时值不回退、空闲帧不再产生写动作（值稳定）。
bool writeBackIsIdempotentThroughReimport() {
    Harness ui;
    CHECK(ui.valid);
    const Ref<Material> handle = MATERIAL_RESOURCE_MANAGER.load(kWarmMaterialPath);
    CHECK(static_cast<bool>(handle));
    ui.current = handle;
    ui.settle();

    CHECK(ui.scan([handle] {
        return handle->getFloat("Metallic") != 0.0F;
    }));
    const Material* material = handle.get();
    const float edited = material->getFloat("Metallic");

    ui.pumpDebounce();
    material = handle.get();
    CHECK(material != nullptr);
    CHECK(material->getFloat("Metallic") == edited);

    ui.advance(35);
    CHECK(handle.get() == material);
    CHECK(material->getFloat("Metallic") == edited);
    return true;
}

// 切换 shader 保留兼容属性；随后的写回把新 shader 引用落进源文件。
bool shaderSwitchPreservesCompatibleValues() {
    Harness ui;
    CHECK(ui.valid);
    const Ref<Material> handle = MATERIAL_RESOURCE_MANAGER.load(kWarmMaterialPath);
    CHECK(static_cast<bool>(handle));
    ui.current = handle;
    ui.settle();

    const math::Vec4 baseColor = handle->getVec4("BaseColor");
    MATERIAL_RESOURCE_MANAGER.setShader(handle, VirtualPath{"assets://shaders/fixture2.shader.json"});
    ui.settle();

    Material* material = handle.get();
    CHECK(material->shader().assetPath() == VirtualPath{"assets://shaders/fixture2.shader.json"});
    CHECK(material->getVec4("BaseColor") == baseColor);

    CHECK(ui.scan([handle] {
        return handle->getFloat("Metallic") != 0.0F;
    }));
    ui.pumpDebounce();

    const auto onDisk = readMaterialFromDisk(kWarmMaterialPath);
    CHECK(onDisk != nullptr);
    CHECK(onDisk->shader == VirtualPath{"assets://shaders/fixture2.shader.json"});
    CHECK(std::get_if<float>(&onDisk->properties.at("Metallic")) != nullptr);
    return true;
}

// renderQueue override 与 keywords 的写回往返：设置后显式写出，清除后恢复源文件
// 的省略语义（无 renderQueue / keywords 字段）。
bool keywordAndRenderQueueOverride() {
    Harness ui;
    CHECK(ui.valid);
    const Ref<Material> handle = MATERIAL_RESOURCE_MANAGER.load(kWarmMaterialPath);
    CHECK(static_cast<bool>(handle));
    ui.current = handle;
    ui.settle();

    Material* material = handle.get();
    material->setKeywordEnabled("RECEIVE_SHADOWS", true);
    material->setRenderQueue(3100);

    CHECK(ui.scan([handle] {
        return handle->getFloat("Metallic") != 0.0F;
    }));
    ui.pumpDebounce();
    auto onDisk = readMaterialFromDisk(kWarmMaterialPath);
    CHECK(onDisk != nullptr);
    CHECK(onDisk->renderQueue.has_value());
    CHECK(*onDisk->renderQueue == 3100);
    CHECK(onDisk->keywords == std::vector<std::string>{"RECEIVE_SHADOWS"});

    material = handle.get();
    material->setFloat("Metallic", 0.9F); // 让下一次点击产生编辑事件
    material->setKeywordEnabled("RECEIVE_SHADOWS", false);
    material->setRenderQueue(std::nullopt);
    CHECK(ui.scan([handle] {
        return handle->getFloat("Metallic") == 1.0F;
    }));
    ui.pumpDebounce();
    onDisk = readMaterialFromDisk(kWarmMaterialPath);
    CHECK(onDisk != nullptr);
    CHECK(!onDisk->renderQueue.has_value());
    CHECK(onDisk->keywords.empty());
    return true;
}

// detached 克隆实例照常编辑（运行时生效），但绝不写回源文件。
bool detachedMaterialEditsWithoutWriteBack() {
    Harness ui;
    CHECK(ui.valid);
    const Ref<Material> source = MATERIAL_RESOURCE_MANAGER.load(kWarmMaterialPath);
    CHECK(static_cast<bool>(source));
    const Ref<Material> cloned = MATERIAL_RESOURCE_MANAGER.clone(source);
    CHECK(static_cast<bool>(cloned));
    ui.current = cloned;
    ui.settle();

    const Material* cloneData = cloned.get();
    CHECK(cloneData != nullptr);
    CHECK(!cloneData->isAssetBacked());

    CHECK(ui.scan([cloned] {
        return cloned->getFloat("Metallic") != 0.0F;
    }));
    CHECK(cloned->getFloat("Metallic") == 1.0F);

    ui.pumpDebounce();
    const auto onDisk = readMaterialFromDisk(kWarmMaterialPath);
    CHECK(onDisk != nullptr);
    CHECK(onDisk->properties.find("Metallic") == onDisk->properties.end());
    return true;
}

// 编辑 A 后立即切到 B：draw 入口把 A 的 pending 编辑立即落盘，防抖窗口不丢改动。
bool targetSwitchFlushesPendingSave() {
    Harness ui;
    CHECK(ui.valid);
    const Ref<Material> warm = MATERIAL_RESOURCE_MANAGER.load(kWarmMaterialPath);
    const Ref<Material> cool = MATERIAL_RESOURCE_MANAGER.load(kCoolMaterialPath);
    CHECK(static_cast<bool>(warm));
    CHECK(static_cast<bool>(cool));

    ui.current = warm;
    ui.settle();
    CHECK(ui.scan([warm] {
        return warm->getFloat("Metallic") != 0.0F;
    }));

    ui.current = cool; // 不等防抖直接切目标
    ui.settle();

    const auto warmOnDisk = readMaterialFromDisk(kWarmMaterialPath);
    CHECK(warmOnDisk != nullptr);
    const float* const metallic = std::get_if<float>(&warmOnDisk->properties.at("Metallic"));
    CHECK(metallic != nullptr);
    CHECK(*metallic == 1.0F);

    const auto coolOnDisk = readMaterialFromDisk(kCoolMaterialPath);
    CHECK(coolOnDisk != nullptr);
    CHECK(coolOnDisk->properties.find("Metallic") == coolOnDisk->properties.end());
    return true;
}

} // namespace

int main() {
    return editsUpdateInstanceAndWriteBack() && writeBackIsIdempotentThroughReimport() &&
                   shaderSwitchPreservesCompatibleValues() && keywordAndRenderQueueOverride() &&
                   detachedMaterialEditsWithoutWriteBack() && targetSwitchFlushesPendingSave()
               ? 0
               : 1;
}
