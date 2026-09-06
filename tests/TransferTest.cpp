#include "core/serialization/BinaryTransfer.h"
#include "core/serialization/JsonTransfer.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace transfer_test {

struct Sample {
    bool enabled{true};
    std::int32_t count{-7};
    std::uint64_t mask{0xfedcba9876543210ULL};
    float weight{2.5F};
    double distance{1234.5};
    std::string name{"Sample"};
    engine::VirtualPath path{"asset://data/sample.bin"};
    engine::math::Vec2 uv{0.25F, 0.75F};
    engine::math::Vec3 position{1.0F, 2.0F, 3.0F};
    engine::math::Vec4 color{0.1F, 0.2F, 0.3F, 1.0F};
    engine::math::Mat33 basis{1.0F};
    engine::math::Mat44 matrix{1.0F};
    engine::math::Quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
    std::vector<std::uint32_t> values{3, 5, 8};
    std::vector<std::byte> bytes{std::byte{1}, std::byte{2}, std::byte{255}};
    std::optional<std::int32_t> optional{42};
    std::variant<float, std::string> variant{std::string{"text"}};

    [[nodiscard]] bool transfer(engine::Transfer& archive);
};

bool Sample::transfer(engine::Transfer& archive) {
    return archive.transfer("enabled", enabled) &&
           archive.transfer("count", count) &&
           archive.transfer("mask", mask) &&
           archive.transfer("weight", weight) &&
           archive.transfer("distance", distance) &&
           archive.transfer("name", name) && archive.transfer("path", path) &&
           archive.transfer("uv", uv) &&
           archive.transfer("position", position) &&
           archive.transfer("color", color) &&
           archive.transfer("basis", basis) &&
           archive.transfer("matrix", matrix) &&
           archive.transfer("rotation", rotation) &&
           archive.transfer("values", values) &&
           archive.transfer("bytes", bytes) &&
           archive.transfer("optional", optional) &&
           archive.transfer("variant", variant);
}

bool equal(const Sample& left, const Sample& right) {
    return left.enabled == right.enabled && left.count == right.count &&
           left.mask == right.mask && left.weight == right.weight &&
           left.distance == right.distance && left.name == right.name &&
           left.path == right.path && left.uv == right.uv &&
           left.position == right.position && left.color == right.color &&
           left.basis == right.basis && left.matrix == right.matrix &&
           left.rotation == right.rotation && left.values == right.values &&
           left.bytes == right.bytes && left.optional == right.optional &&
           left.variant == right.variant;
}

} // namespace transfer_test

int main() {
    using namespace engine;
    using transfer_test::Sample;

    Sample source;
    BinaryWriter binaryWriter;
    if (!binaryWriter.transfer("root", source) || !binaryWriter.valid()) return 1;
    const std::vector<std::byte> binary = binaryWriter.takeBytes();
    Sample binaryResult;
    BinaryReader binaryReader{binary};
    if (!binaryReader.transfer("root", binaryResult) ||
        !binaryReader.finished() ||
        !transfer_test::equal(source, binaryResult)) return 2;

    JsonWriter jsonWriter;
    if (!jsonWriter.transfer("root", source) || !jsonWriter.valid()) return 3;
    const std::string json = jsonWriter.toString();
    Sample jsonResult;
    JsonReader jsonReader{json};
    if (!jsonReader.transfer("root", jsonResult) || !jsonReader.valid() ||
        !transfer_test::equal(source, jsonResult)) return 4;

    JsonReader malformed{"{not-json"};
    Sample ignored;
    if (malformed.valid() || malformed.transfer("root", ignored)) return 5;

    std::vector<std::byte> truncated = binary;
    truncated.pop_back();
    BinaryReader truncatedReader{truncated};
    if (truncatedReader.transfer("root", ignored) || truncatedReader.valid()) {
        return 6;
    }
    return 0;
}
