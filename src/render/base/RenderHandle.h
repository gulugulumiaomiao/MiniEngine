#pragma once

#include "core/base/Handle.h"

#include <cstdint>

namespace engine {

struct MeshHandleTag;
struct MaterialHandleTag;
struct ShaderHandleTag;
struct TextureHandleTag;
struct RenderTargetHandleTag;

using MeshHandle = Handle<MeshHandleTag>;
using MaterialHandle = Handle<MaterialHandleTag>;
using ShaderHandle = Handle<ShaderHandleTag>;
using TextureHandle = Handle<TextureHandleTag>;
using RenderTargetHandle = Handle<RenderTargetHandleTag>;

} // namespace engine
