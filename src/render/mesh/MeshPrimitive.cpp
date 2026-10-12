#include "render/mesh/MeshPrimitive.h"
namespace engine {
MeshPrimitiveType MeshPrimitive::type() const {
    return static_cast<MeshPrimitiveType>(value.index() + 1U);
}
} // namespace engine
