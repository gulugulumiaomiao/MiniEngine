#include "render/mesh/MeshPrimitive.h"

#include "core/serialization/Transfer.h"

namespace engine {

bool PlaneGeometry::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("size", size) &&
           archive.transfer("segments_x", segmentsX) && archive.transfer("segments_z", segmentsZ) &&
           archive.endObject();
}

bool BoxGeometry::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("size", size) &&
           archive.transfer("segments_x", segmentsX) && archive.transfer("segments_y", segmentsY) &&
           archive.transfer("segments_z", segmentsZ) && archive.endObject();
}

bool UvSphereGeometry::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("radius", radius) &&
           archive.transfer("longitude_segments", longitudeSegments) &&
           archive.transfer("latitude_segments", latitudeSegments) && archive.endObject();
}

bool CylinderGeometry::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("bottom_radius", bottomRadius) &&
           archive.transfer("top_radius", topRadius) && archive.transfer("height", height) &&
           archive.transfer("radial_segments", radialSegments) &&
           archive.transfer("height_segments", heightSegments) &&
           archive.transfer("cap_bottom", capBottom) && archive.transfer("cap_top", capTop) &&
           archive.endObject();
}

MeshPrimitiveType MeshPrimitive::type() const {
    return static_cast<MeshPrimitiveType>(value.index() + 1U);
}

bool MeshPrimitive::transfer(Transfer& archive) {
    if (!archive.beginObject({}))
        return false;
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
               [&archive](auto& geometry) { return archive.transfer("parameters", geometry); },
               value) &&
           archive.endObject();
}

bool MeshPrimitivePart::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("primitive", primitive) &&
           archive.transfer("translation", translation) && archive.transfer("rotation", rotation) &&
           archive.transfer("scale", scale) && archive.transfer("material_slot", materialSlot) &&
           archive.endObject();
}

} // namespace engine
