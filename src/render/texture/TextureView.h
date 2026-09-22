#pragma once

#include "render/texture/Sampler.h"
#include "rhi/api/TextureView.h"

namespace engine {

// Lightweight, non-owning view of an RHI Texture. Its lifetime must not exceed the Texture.
class TextureView final {
public:
    TextureView() = default;
    TextureView(rhi::TextureHandle texture, rhi::TextureViewHandle view, rhi::TextureViewDesc desc)
        : texture_(texture), view_(view), desc_(desc) {}

    [[nodiscard]] rhi::TextureHandle textureHandle() const { return texture_; }
    [[nodiscard]] rhi::TextureViewHandle rhiHandle() const { return view_; }
    [[nodiscard]] const rhi::TextureViewDesc& desc() const { return desc_; }
    [[nodiscard]] explicit operator bool() const { return static_cast<bool>(view_); }
    [[nodiscard]] bool operator==(const TextureView&) const = default;

private:
    rhi::TextureHandle texture_;
    rhi::TextureViewHandle view_;
    rhi::TextureViewDesc desc_;
};

// Canonical engine-level shader texture binding. Texture itself is never bound directly.
struct TextureBinding {
    TextureView view;
    Sampler sampler;

    [[nodiscard]] rhi::TextureBinding toRhi() const {
        return {view.rhiHandle(), sampler.rhiHandle()};
    }
    [[nodiscard]] explicit operator bool() const {
        return static_cast<bool>(view) && static_cast<bool>(sampler);
    }
    [[nodiscard]] bool operator==(const TextureBinding&) const = default;
};

} // namespace engine
