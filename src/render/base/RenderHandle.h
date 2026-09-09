#pragma once

#include "core/base/Handle.h"

#include <cstdint>

namespace engine {

struct MeshHandleTag;
struct MaterialHandleTag;
struct ShaderHandleTag;
struct TextureHandleTag;

using MeshHandle = Handle<MeshHandleTag>;
using MaterialHandle = Handle<MaterialHandleTag>;
using ShaderHandle = Handle<ShaderHandleTag>;
using TextureHandle = Handle<TextureHandleTag>;

} // namespace engine
