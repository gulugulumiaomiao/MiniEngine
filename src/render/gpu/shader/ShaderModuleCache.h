#pragma once

#include "render/gpu/common/GpuCacheBase.h"
#include "render/gpu/shader/ShaderModuleGpuResource.h"
#include "render/shader/ShaderCompilePipeline.h"

namespace engine {

class ShaderModuleCache final : public GpuCacheBase<CompiledShaderId, ShaderModuleGpuResource> {};

} // namespace engine
