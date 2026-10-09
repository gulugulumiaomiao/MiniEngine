#include "asset/types/MaterialAssetData.h"
#include "core/serialization/Transfer.h"
#include <algorithm>
namespace engine {
namespace {
constexpr std::uint32_t kMaterialAssetMagic = 0x4c54414dU;
constexpr std::uint16_t kMaterialAssetVersion = 2;

struct MaterialPropertyEntry : public Transferable {
    std::string name;
    MaterialAssetValue value;

    MaterialPropertyEntry() = default;
    MaterialPropertyEntry(std::string name, MaterialAssetValue value)
        : name(std::move(name)), value(std::move(value)) {}

    [[nodiscard]] bool transfer(Transfer& archive) override {
        return archive.beginObject({}) && archive.transfer("name", name) &&
               archive.transfer("value", value) && archive.endObject();
    }
};

bool transferMaterialPayload(Transfer& archive, MaterialAssetData& value) {
    std::uint32_t magic = kMaterialAssetMagic;
    std::uint16_t version = kMaterialAssetVersion;
    if (!archive.transfer("magic", magic) || magic != kMaterialAssetMagic ||
        !archive.transfer("version", version) || version != kMaterialAssetVersion ||
        !archive.transfer("name", value.name) || !archive.transfer("shader", value.shader)) {
        return false;
    }

    std::vector<MaterialPropertyEntry> entries;
    if (archive.writing()) {
        entries.reserve(value.properties.size());
        for (const auto& [name, property] : value.properties) {
            entries.push_back({name, property});
        }
        std::ranges::sort(entries, {}, &MaterialPropertyEntry::name);
    }
    if (!archive.transfer("properties", entries))
        return false;
    if (archive.reading()) {
        value.properties.clear();
        for (MaterialPropertyEntry& entry : entries) {
            if (!value.properties.emplace(std::move(entry.name), std::move(entry.value)).second) {
                return false;
            }
        }
    }
    return archive.transfer("keywords", value.keywords) &&
           archive.transfer("render_queue", value.renderQueue);
}

} // namespace

bool MaterialAssetData::transfer(Transfer& archive) {
    MaterialAssetData decoded;
    auto& target = archive.reading() ? decoded : *this;
    if (!archive.beginObject({}) || !transferMaterialPayload(archive, target) ||
        !archive.endObject())
        return false;
    if (archive.reading())
        *this = std::move(decoded);
    return true;
}

} // namespace engine
