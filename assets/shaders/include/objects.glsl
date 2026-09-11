// Shared object-data declarations for vertex shaders.
// Set 0 binding 1 holds one transform row per visible render object.
// Set 0 binding 2 is the per-pass instance table: batches draw N instances in
// one call and this table maps each gl_InstanceIndex to an object row.
layout(std430, set = 0, binding = 1) readonly buffer ObjectBuffer
{
    mat4 transforms[];
} Objects;

layout(std430, set = 0, binding = 2) readonly buffer InstanceTableBuffer
{
    uint objectIndices[];
} InstanceIndices;
