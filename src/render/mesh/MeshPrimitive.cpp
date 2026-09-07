#include "render/mesh/MeshPrimitive.h"

#include "core/serialization/Transfer.h"

namespace engine {

bool PlaneGeometry::transfer(Transfer& archive) {
    return archive.transfer("size", size) && archive.transfer("segments_x", segmentsX) &&
           archive.transfer("segments_z", segmentsZ);
}

bool BoxGeometry::transfer(Transfer& archive) {
    return archive.transfer("size", size) && archive.transfer("segments_x", segmentsX) &&
           archive.transfer("segments_y", segmentsY) && archive.transfer("segments_z", segmentsZ);
}

bool UvSphereGeometry::transfer(Transfer& archive) {
    return archive.transfer("radius", radius) &&
           archive.transfer("longitude_segments", longitudeSegments) &&
           archive.transfer("latitude_segments", latitudeSegments);
}

bool CylinderGeometry::transfer(Transfer& archive) {
    return archive.transfer("bottom_radius", bottomRadius) &&
           archive.transfer("top_radius", topRadius) && archive.transfer("height", height) &&
           archive.transfer("radial_segments", radialSegments) &&
           archive.transfer("height_segments", heightSegments) &&
           archive.transfer("cap_bottom", capBottom) && archive.transfer("cap_top", capTop);
}

MeshPrimitiveType MeshPrimitive::type() const {
    return static_cast<MeshPrimitiveType>(value.index() + 1U);
}

bool MeshPrimitive::transfer(Transfer& archive) {
    MeshPrimitiveType primitiveType = type();
    if (!archive.transfer("primitive_type", primitiveType))
        return false;
    if (archive.reading()) {
        switch (primitiveType) {
        case MeshPrimitiveType::Plane: value.emplace<PlaneGeometry>(); break;
        case MeshPrimitiveType::Box: value.emplace<BoxGeometry>(); break;
        case MeshPrimitiveType::UvSphere: value.emplace<UvSphereGeometry>(); break;
        case MeshPrimitiveType::Cylinder: value.emplace<CylinderGeometry>(); break;
        default: return false;
        }
    }
    return std::visit(
        [&archive](auto& geometry) { return archive.transfer("parameters", geometry); }, value);
}

bool MeshPrimitivePart::transfer(Transfer& archive) {
    return archive.transfer("primitive", primitive) &&
           archive.transfer("translation", translation) && archive.transfer("rotation", rotation) &&
           archive.transfer("scale", scale) && archive.transfer("material_slot", materialSlot);
}

} // namespace engine
