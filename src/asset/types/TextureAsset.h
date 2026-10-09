#pragma once
#include "asset/base/Asset.h"
#include "render/texture/Texture.h"
namespace engine {
struct SamplerAssetDesc : SamplerDesc, Transferable {
    SamplerAssetDesc() = default;
    explicit SamplerAssetDesc(const SamplerDesc& value) : SamplerDesc(value) {}
    bool transfer(Transfer& archive) override;
};
struct TextureAssetDesc : TextureDesc, Transferable {
    using TextureDesc::TextureDesc;
    TextureAssetDesc() = default;
    TextureAssetDesc& operator=(const TextureDesc& value) {
        static_cast<TextureDesc&>(*this) = value;
        return *this;
    }
    bool transfer(Transfer& archive) override;
};
class TextureAsset final : public Asset {
public:
    ~TextureAsset() override;
    [[nodiscard]] AssetType type() const override { return AssetType::Texture; }

    TextureAssetDesc desc;
    std::vector<std::uint8_t> pixels;

    [[nodiscard]] bool transfer(Transfer& archive) override;

    /// 唯一运行时实例：首次创建并登记，之后复用同一实例（asset 就地重传会推送到它）。
    [[nodiscard]] Ref<Texture> instantiate();
    /// 克隆一个脱离 asset 的独立运行时纹理（每次新建，不随 asset 变化）。
    [[nodiscard]] Ref<Texture> clone();

private:
    friend class Texture;

    /// 把当前 pixels 重新上传给唯一实例（存在时）。就地重传（热重载）后调用。
    void syncInstance();

    Texture* instance_{}; // 裸观察者指针；~Texture 反注册置空
};

Ref<Texture> resolveTextureReference(std::string_view reference);
} // namespace engine
