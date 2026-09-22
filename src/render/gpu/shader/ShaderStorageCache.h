#pragma once

#include "render/gpu/common/GpuCacheBase.h"
#include "render/gpu/shader/ShaderStorageEntry.h"
#include "render/shader/ShaderCompilePipeline.h"

namespace engine {

class ShaderStorageCache final : public GpuCacheBase<CompiledShaderId, ShaderStorageEntry> {};

} // namespace engine
