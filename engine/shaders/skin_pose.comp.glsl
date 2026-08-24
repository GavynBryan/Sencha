#version 450

// Poses one skinned instance's vertices: rest geometry in model space plus a
// joint palette in, posed model-space geometry out, consumed by every
// geometry pass exactly as static vertices (pipeline Decision N: pre-skin).
//
// Vertices are read and written as raw words so the 52-byte StaticMeshVertex
// layout stays asserted in exactly one place (StaticMeshFormat.h): position
// words 0-2, normal 3-5, uv 6-7, tangent 8-11, packed lightmap uv 12. An
// influence is 3 words (MeshSkinInfluence, 12 bytes): joints u16x4 in words
// 0-1, weights u8x4 in word 2.
//
// Nothing here normalizes: at bind pose the palette is identity and this
// dispatch must reproduce the rest vertices bit-exactly (the skinned_rest
// golden is the gate), and the vertex stage normalizes normal and tangent
// anyway. The normal transforms by the blended upper 3x3, which assumes
// uniform joint scale; a cofactor upgrade is the recorded fix if
// non-uniform bind scales ever appear.

layout(local_size_x = 64) in;

layout(std430, set = 0, binding = 0) readonly buffer RestVertices
{
    uint restWords[];
};
layout(std430, set = 0, binding = 1) readonly buffer Influences
{
    uint influenceWords[];
};
layout(std430, set = 0, binding = 2) readonly buffer Palette
{
    mat4 joints[];
};
layout(std430, set = 0, binding = 3) writeonly buffer PosedVertices
{
    uint posedWords[];
};

layout(push_constant) uniform Push
{
    uint VertexCount;
} push;

const uint kWordsPerVertex = 13u;
const uint kWordsPerInfluence = 3u;

void main()
{
    uint vertex = gl_GlobalInvocationID.x;
    if (vertex >= push.VertexCount)
        return;

    uint base = vertex * kWordsPerVertex;
    vec3 position = vec3(uintBitsToFloat(restWords[base + 0u]),
                         uintBitsToFloat(restWords[base + 1u]),
                         uintBitsToFloat(restWords[base + 2u]));
    vec3 normal = vec3(uintBitsToFloat(restWords[base + 3u]),
                       uintBitsToFloat(restWords[base + 4u]),
                       uintBitsToFloat(restWords[base + 5u]));
    vec3 tangent = vec3(uintBitsToFloat(restWords[base + 8u]),
                        uintBitsToFloat(restWords[base + 9u]),
                        uintBitsToFloat(restWords[base + 10u]));

    uint influenceBase = vertex * kWordsPerInfluence;
    uint jointsLow = influenceWords[influenceBase + 0u];
    uint jointsHigh = influenceWords[influenceBase + 1u];
    uint weightsPacked = influenceWords[influenceBase + 2u];
    uvec4 jointIndex = uvec4(jointsLow & 0xFFFFu, jointsLow >> 16,
                             jointsHigh & 0xFFFFu, jointsHigh >> 16);
    vec4 weight = vec4(float(weightsPacked & 0xFFu),
                       float((weightsPacked >> 8) & 0xFFu),
                       float((weightsPacked >> 16) & 0xFFu),
                       float(weightsPacked >> 24));
    float total = weight.x + weight.y + weight.z + weight.w;

    // A zero-weight vertex (unskinned by authoring) passes through unposed.
    if (total > 0.0)
    {
        weight /= total;
        mat4 blended = joints[jointIndex.x] * weight.x
                     + joints[jointIndex.y] * weight.y
                     + joints[jointIndex.z] * weight.z
                     + joints[jointIndex.w] * weight.w;
        position = (blended * vec4(position, 1.0)).xyz;
        mat3 rotation = mat3(blended);
        normal = rotation * normal;
        tangent = rotation * tangent;
    }

    posedWords[base + 0u] = floatBitsToUint(position.x);
    posedWords[base + 1u] = floatBitsToUint(position.y);
    posedWords[base + 2u] = floatBitsToUint(position.z);
    posedWords[base + 3u] = floatBitsToUint(normal.x);
    posedWords[base + 4u] = floatBitsToUint(normal.y);
    posedWords[base + 5u] = floatBitsToUint(normal.z);
    posedWords[base + 6u] = restWords[base + 6u];
    posedWords[base + 7u] = restWords[base + 7u];
    posedWords[base + 8u] = floatBitsToUint(tangent.x);
    posedWords[base + 9u] = floatBitsToUint(tangent.y);
    posedWords[base + 10u] = floatBitsToUint(tangent.z);
    posedWords[base + 11u] = restWords[base + 11u];
    posedWords[base + 12u] = restWords[base + 12u];
}
