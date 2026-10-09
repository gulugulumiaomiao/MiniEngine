#pragma once
#include "core/serialization/Transferable.h"
#include "core/filesystem/VirtualPath.h"
#include "core/math/Math.h"
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
namespace engine {
using MaterialAssetValue =
    std::variant<float, bool, math::Vec2, math::Vec3, math::Vec4, std::string, math::Mat44>;
struct MaterialAssetData final : Transferable {
    std::string name;
    VirtualPath shader;
    std::unordered_map<std::string, MaterialAssetValue> properties;
    std::vector<std::string> keywords;
    std::optional<int> renderQueue;
    bool transfer(Transfer& archive) override;
};
} // namespace engine
