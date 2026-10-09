#include "asset/types/TextureAsset.h"
#include "core/serialization/Transfer.h"
namespace engine {
bool SamplerAssetDesc::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("filter_mode", filterMode) &&
           archive.transfer("address_mode_u", addressModeU) &&
           archive.transfer("address_mode_v", addressModeV) &&
           archive.transfer("max_anisotropy", maxAnisotropy) && archive.endObject();
}

bool TextureAssetDesc::transfer(Transfer& archive) {
    SamplerAssetDesc samplerData{sampler};
    const bool result =
        archive.beginObject({}) && archive.transfer("type", type) &&
        archive.transfer("format", format) && archive.transfer("color_space", colorSpace) &&
        archive.transfer("width", width) && archive.transfer("height", height) &&
        archive.transfer("depth", depth) && archive.transfer("array_layers", arrayLayers) &&
        archive.transfer("mip_count", mipCount) && archive.transfer("sampler", samplerData) &&
        archive.endObject();
    if (result && archive.reading())
        sampler = samplerData;
    return result;
}

} // namespace engine
