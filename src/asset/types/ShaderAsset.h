#pragma once

#include "asset/base/Asset.h"
#include "core/base/Ref.h"
#include "render/shader/Shader.h"

#include <string>
#include <vector>

namespace engine {

class ShaderAsset final : public Asset {
public:
    ~ShaderAsset() override;
    [[nodiscard]] AssetType type() const override { return AssetType::Shader; }

    std::string name;
    std::vector<ShaderPropertyDesc> properties;
    std::vector<ShaderPropertyDesc> globalProperties;
    std::vector<SubShaderDesc> subShaders;

    [[nodiscard]] const ShaderPropertyDesc* findProperty(const std::string& name) const;
    [[nodiscard]] const ShaderPropertyDesc* findGlobalProperty(const std::string& name) const;

    /// 唯一运行时实例：首次创建并登记，之后复用同一实例（asset 就地重传经 syncInstance 推送到它）。
    [[nodiscard]] Ref<Shader> instantiate();
    /// 克隆一个脱离 asset 的独立运行时 Shader（每次新建，不随 asset 变化）。
    [[nodiscard]] Ref<Shader> clone();

    [[nodiscard]] bool transfer(Transfer& archive) override;

private:
    friend class Shader;

    /// 把当前内容重建到唯一实例（存在时）。就地重传（热重载）后由 transfer 读分支调用。
    void syncInstance();

    Shader* instance_{}; // 裸观察者指针；~Shader 反注册置空（asset 由 AssetManager 强缓存常驻）
};

} // namespace engine
