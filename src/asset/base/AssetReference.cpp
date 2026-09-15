#include "asset/base/AssetReference.h"

#include <utility>

namespace engine {

AssetReference::AssetReference(AssetId guid) : value_(std::move(guid)) {}

AssetReference::AssetReference(VirtualPath path) : value_(std::move(path)) {}

AssetReference::AssetReference(std::string_view text) {
    // GUID references are authoritative: an invalid guid:// string is rejected.
    constexpr std::string_view kGuidPrefix{"guid://"};
    if (text.size() >= kGuidPrefix.size() && text.substr(0, kGuidPrefix.size()) == kGuidPrefix) {
        if (const auto parsed = AssetId::parse(text.substr(kGuidPrefix.size()))) {
            value_ = *parsed;
        }
        return;
    }
    VirtualPath path{text};
    if (path.valid()) {
        value_ = std::move(path);
    }
}

bool AssetReference::valid() const {
    return isGuid() || isPath();
}

bool AssetReference::isGuid() const {
    return std::holds_alternative<AssetId>(value_);
}

bool AssetReference::isPath() const {
    return std::holds_alternative<VirtualPath>(value_);
}

AssetId AssetReference::guid() const {
    return isGuid() ? std::get<AssetId>(value_) : AssetId{};
}

const VirtualPath& AssetReference::path() const {
    static const VirtualPath kEmpty;
    return isPath() ? std::get<VirtualPath>(value_) : kEmpty;
}

std::optional<VirtualPath> AssetReference::resolve(const GuidResolver& resolver) const {
    if (isGuid()) {
        return resolver.findPath(guid());
    }
    if (isPath()) {
        return path();
    }
    return std::nullopt;
}

std::string AssetReference::toString() const {
    if (isGuid()) {
        return std::string{"guid://"} + guid().toString();
    }
    if (isPath()) {
        return path().string();
    }
    return {};
}

} // namespace engine
