#pragma once

#include "asset/base/Asset.h"

#include <vector>

namespace engine {

// DefaultImporter 的透传资产：源文件字节原样进入 Artifact，不做任何转换。
// 供编辑器/打包管线按 GUID 稳定引用任意资产（音效、配置、文本等）。
class GenericAsset final : public Asset {
public:
    [[nodiscard]] AssetType type() const override { return AssetType::Generic; }

    std::vector<std::byte> data;

    [[nodiscard]] bool transfer(Transfer& archive) override;
};

} // namespace engine
