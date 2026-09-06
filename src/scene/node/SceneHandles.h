#pragma once

#include "core/base/Handle.h"

namespace engine {

struct SceneHandleTag;
struct NodeHandleTag;

using SceneHandle = Handle<SceneHandleTag>;
using NodeHandle = Handle<NodeHandleTag>;

} // namespace engine
