#include "tools/editor/widgets/MaterialInspector.h"

#include "asset/types/MaterialAsset.h"
#include "asset/database/AssetDatabase.h"
#include "asset/exporter/MaterialAssetExporter.h"
#include "asset/format/MaterialAssetFormat.h"
#include "asset/importer/AssetImportPipeline.h"
#include "core/filesystem/FileSystem.h"
#include "render/material/Material.h"
#include "render/material/MaterialManager.h"
#include "render/shader/Shader.h"
#include "render/shader/ShaderManager.h"
#include "tools/editor/widgets/EditorWidgets.h"

#include "imgui.h"

#include <cstdio>
#include <optional>

namespace engine::editor {
namespace {

constexpr float kSaveDebounceSeconds = 0.5F;

[[nodiscard]] std::string propertyLabel(const ShaderPropertyDesc& property) {
    return property.displayName.empty() ? property.name : property.displayName;
}

} // namespace

void MaterialInspector::draw(const Ref<Material>& material) {
    Material* data = material.get();
    if (data == nullptr) {
        ImGui::TextDisabled("Invalid material handle");
        return;
    }

    // Switching targets flushes the previous target's pending edit so the
    // debounce window can never swallow the last change of a material the
    // widget has already moved away from.
    if (pendingSavePath_.valid() && !(pendingSavePath_ == data->assetPath())) {
        const VirtualPath stale = pendingSavePath_;
        pendingSavePath_ = {};
        idleSeconds_ = 0.0F;
        saveNow(stale);
    }

    // The handle index scopes every control id to this instance, so the
    // component view and the asset view can both embed the widget (and a
    // detached clone never collides with its source asset).
    ImGui::PushID(static_cast<int>(material->resourceId().index()));
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!data->isAssetBacked())
            ImGui::TextDisabled("Runtime-only (detached) - edits do not write back");

        drawIdentity(*data);
        drawShaderCombo(material, *data);
        drawRenderQueue(*data);
        drawProperties(*data);
        drawKeywords(*data);

        if (!status_.empty()) {
            ImGui::TextDisabled("%s", status_.c_str());
            status_.clear();
        }
    }
    ImGui::PopID();

    // Debounce lives outside the header so a collapsed editor still saves.
    if (pendingSavePath_.valid()) {
        idleSeconds_ += ImGui::GetIO().DeltaTime;
        if (idleSeconds_ >= kSaveDebounceSeconds) {
            const VirtualPath path = pendingSavePath_;
            pendingSavePath_ = {};
            idleSeconds_ = 0.0F;
            saveNow(path);
        }
    }
}

void MaterialInspector::flushPendingSave() {
    if (!pendingSavePath_.valid())
        return;
    const VirtualPath path = pendingSavePath_;
    pendingSavePath_ = {};
    idleSeconds_ = 0.0F;
    saveNow(path);
}

void MaterialInspector::drawIdentity(Material& data) {
    char buffer[128]{};
    std::snprintf(buffer, sizeof(buffer), "%s", data.name.c_str());
    if (ImGui::InputText("Name", buffer, sizeof(buffer))) {
        data.name = buffer; // Display-only: no uniform upload, no markChanged.
        queueSave(data);
    }
}

void MaterialInspector::drawShaderCombo(const Ref<Material>& material, Material& data) {
    const VirtualPath current = data.shader().assetPath();
    VirtualPath chosen;
    if (assetCombo("Shader", collectAssets(AssetType::Shader), current, chosen) &&
        !(chosen == current)) {
        // setShader rebuilds the uniform block and keeps values of
        // compatible properties (rebuildForShader with preserveValues). The
        // chosen != current guard keeps re-picking the current shader a no-op.
        MATERIAL_RESOURCE_MANAGER.setShader(material, chosen);
        queueSave(data);
    }
}

void MaterialInspector::drawRenderQueue(Material& data) {
    bool hasOverride = data.renderQueueOverride().has_value();
    if (ImGui::Checkbox("Override Render Queue", &hasOverride)) {
        // Turning the override on seeds it with the currently effective queue;
        // turning it off falls back to the shader default.
        data.setRenderQueue(hasOverride ? std::optional<int>{data.renderQueue} : std::nullopt);
        queueSave(data);
    }
    int queue = data.renderQueue;
    if (ImGui::InputInt("Render Queue", &queue, 0, 0)) {
        data.setRenderQueue(queue); // Any direct edit asserts an override.
        queueSave(data);
    }
}

void MaterialInspector::drawProperties(Material& data) {
    for (const ShaderPropertyDesc& property : data.shader().properties()) {
        ImGui::PushID(property.name.c_str());
        const std::string label = propertyLabel(property);
        bool edited = false;
        switch (property.type) {
        case ShaderPropertyType::Float: {
            float value = data.getFloat(property.name);
            edited = ImGui::DragFloat(label.c_str(), &value, kDragSpeed, 0.0F, 0.0F, "%.3f");
            if (edited)
                data.setFloat(property.name, value);
            break;
        }
        case ShaderPropertyType::Range: {
            const math::Vec2 range = property.range.value_or(math::Vec2{0.0F, 1.0F});
            float value = data.getFloat(property.name);
            edited = ImGui::SliderFloat(label.c_str(), &value, range.x, range.y, "%.3f");
            if (edited)
                data.setFloat(property.name, value);
            break;
        }
        case ShaderPropertyType::Boolean: {
            bool value = data.getBool(property.name);
            edited = ImGui::Checkbox(label.c_str(), &value);
            if (edited)
                data.setBool(property.name, value);
            break;
        }
        case ShaderPropertyType::Vec2: {
            math::Vec2 value = data.getVec2(property.name);
            edited = ImGui::DragFloat2(label.c_str(), &value.x, kDragSpeed, 0.0F, 0.0F, "%.3f");
            if (edited)
                data.setVec2(property.name, value);
            break;
        }
        case ShaderPropertyType::Vec3: {
            math::Vec3 value = data.getVec3(property.name);
            edited = ImGui::DragFloat3(label.c_str(), &value.x, kDragSpeed, 0.0F, 0.0F, "%.3f");
            if (edited)
                data.setVec3(property.name, value);
            break;
        }
        case ShaderPropertyType::Vec4: {
            math::Vec4 value = data.getVec4(property.name);
            edited = ImGui::DragFloat4(label.c_str(), &value.x, kDragSpeed, 0.0F, 0.0F, "%.3f");
            if (edited)
                data.setVec4(property.name, value);
            break;
        }
        case ShaderPropertyType::Color: {
            math::Vec4 value = data.getVec4(property.name);
            edited = ImGui::ColorEdit4(label.c_str(), &value.x);
            if (edited)
                data.setVec4(property.name, value);
            break;
        }
        case ShaderPropertyType::Texture2D: {
            const std::string& current = data.getTexture(property.name);
            const VirtualPath currentPath =
                current.empty() ? VirtualPath{} : VirtualPath{current};
            VirtualPath chosen;
            if (assetCombo(label.c_str(), collectAssets(AssetType::Texture), currentPath, chosen,
                           "(none)")) {
                data.setTexture(property.name, chosen.valid() ? chosen.string() : std::string{});
                queueSave(data);
            }
            break;
        }
        }
        if (edited)
            queueSave(data);
        ImGui::PopID();
    }
}

void MaterialInspector::drawKeywords(Material& data) {
    // The declared set is the union of every pass's features; shaders that
    // declare none skip the section entirely.
    std::vector<std::string> declared;
    for (const SubShader& subShader : data.shader().subShaders()) {
        for (const ShaderPass& pass : subShader.passes()) {
            for (const std::string& keyword : pass.features()) {
                if (std::ranges::find(declared, keyword) == declared.end())
                    declared.push_back(keyword);
            }
        }
    }
    if (declared.empty())
        return;
    if (!ImGui::CollapsingHeader("Keywords"))
        return;
    for (const std::string& keyword : declared) {
        bool enabled = std::ranges::find(data.keywords, keyword) != data.keywords.end();
        if (ImGui::Checkbox(keyword.c_str(), &enabled)) {
            data.setKeywordEnabled(keyword, enabled);
            queueSave(data);
        }
    }
}

void MaterialInspector::queueSave(const Material& data) {
    // Detached clones keep runtime-only edits: no GUID, nothing to write back.
    if (!data.isAssetBacked())
        return;
    pendingSavePath_ = data.assetPath();
    idleSeconds_ = 0.0F;
}

void MaterialInspector::saveNow(const VirtualPath& path) {
    // The file (or its mount) may be gone: a deleted material, a closed
    // project. Writing then would resurrect a deleted file, so skip.
    if (!FILE_SYSTEM.isFile(path)) {
        status_ = "Not saved (file removed): " + path.string();
        return;
    }
    const Ref<Material> material = MATERIAL_RESOURCE_MANAGER.load(path);
    const Material* data = material.get();
    if (data == nullptr || !(data->assetPath() == path)) {
        // load() failed (bad source, unmounted assets://) and fell back to the
        // error material; writing that back would corrupt the asset.
        status_ = "Not saved (instance unavailable): " + path.string();
        return;
    }
    std::string error;
    const auto asset = exportMaterialToAsset(*data, path, error);
    if (!asset) {
        status_ = error;
        return;
    }
    const std::string json = format::writeMaterialAssetJson(*asset, ASSET_DATABASE);
    if (!FILE_SYSTEM.writeTextAtomic(path, json)) {
        status_ = "Cannot write " + path.string();
        return;
    }
    // Synchronous reimport: refreshAsset rebuilds the in-place instance with
    // exactly what was just written (idempotent for the runtime values), and
    // the FileWatcher's async event afterwards loses the reimport decision
    // (hash comparison) instead of overwriting fresher edits.
    if (ASSET_IMPORT_PIPELINE.initialized())
        (void)ASSET_IMPORT_PIPELINE.reimportAsset(path);
}

} // namespace engine::editor
