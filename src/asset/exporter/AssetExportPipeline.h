#pragma once

#include "asset/exporter/AssetExporterRegistry.h"
#include "core/base/Singleton.h"

#include <string>

namespace engine {

class Asset;

// 写回管线（AssetImportPipeline 的单例形态对称物）：按 AssetType 路由到注册的
// exporter 执行源文件写回。由 AssetManager 的 Development 分支挂载；Packaged
// 模式只读，不初始化。写回不触发重导入——FileWatcher/调用方负责后续级联。
class AssetExportPipeline final : public Singleton<AssetExportPipeline> {
public:
    [[nodiscard]] bool initialize();
    void shutdown();

    // 通用入口：按 asset.type() 路由；error 接收失败原因。
    [[nodiscard]] bool exportAsset(const Asset& asset,
                                   const VirtualPath& targetPath,
                                   std::string& error);
    // Material 便捷入口：运行时对象先经 exportMaterialToAsset 提取再路由。
    [[nodiscard]] bool saveMaterial(const class Material& material,
                                    const VirtualPath& targetPath,
                                    std::string& error);

    [[nodiscard]] bool initialized() const { return initialized_; }

private:
    friend class Singleton<AssetExportPipeline>;
    AssetExportPipeline() = default;

    AssetExporterRegistry registry_;
    bool initialized_{};
};

} // namespace engine

#define ASSET_EXPORT_PIPELINE (::engine::AssetExportPipeline::instance())
