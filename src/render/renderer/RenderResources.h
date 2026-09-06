#pragma once

#include "core/base/Handle.h"

namespace engine {

struct MeshHandleTag;
struct MaterialHandleTag;
struct ShaderHandleTag;

using MeshHandle = Handle<MeshHandleTag>;
using MaterialHandle = Handle<MaterialHandleTag>;
using ShaderHandle = Handle<ShaderHandleTag>;

} // namespace engine
