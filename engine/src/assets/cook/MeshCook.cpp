#include <assets/cook/MeshCook.h>

#include <assets/animation/AnimationClipSerializer.h>
#include <assets/skeleton/SkeletonSerializer.h>
#include <assets/static_mesh/MeshSerializer.h>
#include <core/hash/ContentHash.h>
#include <core/logging/LoggingProvider.h>
#include <math/Quat.h>
#include <render/static_mesh/MeshValidation.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <mikktspace.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace
{
    struct CgltfFree
    {
        void operator()(cgltf_data* data) const { cgltf_free(data); }
    };
    using CgltfDataPtr = std::unique_ptr<cgltf_data, CgltfFree>;

    void SetError(std::string* error, std::string message)
    {
        if (error)
            *error = std::move(message);
    }

    // -- Tangent fallbacks ----------------------------------------------------

    Vec4 SnapTangentW(Vec4 tangent)
    {
        // Normalization is the cook's job; the runtime validates w == ±1 and
        // never fixes data.
        tangent.W = tangent.W < 0.0f ? -1.0f : 1.0f;
        return tangent;
    }

    // Deterministic orthonormal basis for UV-less geometry: there is no
    // texture space to be tangent *to*, so any stable perpendicular works.
    Vec4 TangentFromNormal(const Vec3d& normal)
    {
        const Vec3d reference = std::abs(normal.Y) < 0.99f ? Vec3d(0.0f, 1.0f, 0.0f)
                                                           : Vec3d(1.0f, 0.0f, 0.0f);
        Vec3d tangent = reference.Cross(normal);
        const float length = tangent.Magnitude();
        tangent = length > 1e-6f ? tangent / length : Vec3d(1.0f, 0.0f, 0.0f);
        return Vec4(tangent.X, tangent.Y, tangent.Z, 1.0f);
    }

    // -- MikkTSpace adapter ----------------------------------------------------
    //
    // MikkTSpace wants per-corner access and may assign different tangents to
    // corners sharing a vertex, so the section de-indexes first and re-welds
    // exact duplicates afterwards.

    int MikkGetNumFaces(const SMikkTSpaceContext* context)
    {
        const auto* corners = static_cast<const std::vector<StaticMeshVertex>*>(context->m_pUserData);
        return static_cast<int>(corners->size() / 3);
    }

    int MikkGetNumVerticesOfFace(const SMikkTSpaceContext*, int)
    {
        return 3;
    }

    StaticMeshVertex& MikkCorner(const SMikkTSpaceContext* context, int face, int vert)
    {
        auto* corners = static_cast<std::vector<StaticMeshVertex>*>(context->m_pUserData);
        return (*corners)[static_cast<size_t>(face) * 3 + static_cast<size_t>(vert)];
    }

    void MikkGetPosition(const SMikkTSpaceContext* context, float out[], int face, int vert)
    {
        const StaticMeshVertex& corner = MikkCorner(context, face, vert);
        out[0] = corner.Position.X;
        out[1] = corner.Position.Y;
        out[2] = corner.Position.Z;
    }

    void MikkGetNormal(const SMikkTSpaceContext* context, float out[], int face, int vert)
    {
        const StaticMeshVertex& corner = MikkCorner(context, face, vert);
        out[0] = corner.Normal.X;
        out[1] = corner.Normal.Y;
        out[2] = corner.Normal.Z;
    }

    void MikkGetTexCoord(const SMikkTSpaceContext* context, float out[], int face, int vert)
    {
        const StaticMeshVertex& corner = MikkCorner(context, face, vert);
        out[0] = corner.Uv0.X;
        out[1] = corner.Uv0.Y;
    }

    void MikkSetTSpaceBasic(const SMikkTSpaceContext* context,
                            const float tangent[],
                            float sign,
                            int face,
                            int vert)
    {
        StaticMeshVertex& corner = MikkCorner(context, face, vert);
        corner.Tangent = Vec4(tangent[0], tangent[1], tangent[2], sign);
    }

    std::span<const std::byte> VertexBytes(const StaticMeshVertex& vertex)
    {
        return { reinterpret_cast<const std::byte*>(&vertex), sizeof(StaticMeshVertex) };
    }

    // -- glTF primitive reading ------------------------------------------------

    const cgltf_accessor* FindAttribute(const cgltf_primitive& primitive,
                                        cgltf_attribute_type type)
    {
        for (cgltf_size i = 0; i < primitive.attributes_count; ++i)
        {
            const cgltf_attribute& attribute = primitive.attributes[i];
            if (attribute.type == type && attribute.index == 0)
                return attribute.data;
        }
        return nullptr;
    }

    // Converts glTF VEC4 float weights (already normalized to sum ~1) into
    // unorm8 weights summing to exactly 255, with zero-weight slots forced to
    // joint 0. The cook normalizes; the runtime never fixes data (Decision N).
    MeshSkinInfluence MakeInfluence(const uint32_t joints[4], const float weights[4])
    {
        MeshSkinInfluence influence{};

        float sum = weights[0] + weights[1] + weights[2] + weights[3];
        if (sum <= 0.0f)
        {
            // A vertex with no influence binds rigidly to joint 0.
            influence.Weights[0] = 255;
            return influence;
        }

        int quantized[4]{};
        int total = 0;
        for (int slot = 0; slot < 4; ++slot)
        {
            quantized[slot] = static_cast<int>((weights[slot] / sum) * 255.0f + 0.5f);
            total += quantized[slot];
        }
        // Push the rounding remainder onto the largest slot so the sum is 255.
        int largest = 0;
        for (int slot = 1; slot < 4; ++slot)
            if (quantized[slot] > quantized[largest])
                largest = slot;
        quantized[largest] += 255 - total;
        if (quantized[largest] < 0)
            quantized[largest] = 0;

        for (int slot = 0; slot < 4; ++slot)
        {
            influence.Weights[slot] = static_cast<uint8_t>(quantized[slot]);
            // Skin-local joint index; remapped to skeleton-local by the caller.
            influence.Joints[slot] = quantized[slot] > 0
                ? static_cast<uint16_t>(joints[slot]) : uint16_t{ 0 };
        }
        return influence;
    }

    // Reads one primitive's geometry. When `outInfluences` is non-null the
    // primitive is treated as skinned: JOINTS_0/WEIGHTS_0 are read into
    // influences (raw skin-local joints, the caller remaps), and a primitive
    // that would need MikkTSpace tangents is rejected — skinned influences
    // cannot ride the de-index/reweld, so skinned sources must carry tangents
    // (or be UV-less). Re-export is cheap; a silent reweld that desyncs
    // influences is a data-quality lie.
    bool ReadPrimitive(const cgltf_primitive& primitive,
                       std::string_view meshName,
                       size_t primitiveIndex,
                       std::vector<StaticMeshVertex>& vertices,
                       std::vector<uint32_t>& indices,
                       std::string* error,
                       std::vector<MeshSkinInfluence>* outInfluences = nullptr)
    {
        const auto fail = [&](std::string_view why) {
            SetError(error, std::format("mesh '{}' primitive {}: {}", meshName, primitiveIndex, why));
            return false;
        };

        if (primitive.type != cgltf_primitive_type_triangles)
            return fail("only triangle primitives are supported");

        const cgltf_accessor* positions = FindAttribute(primitive, cgltf_attribute_type_position);
        const cgltf_accessor* normals = FindAttribute(primitive, cgltf_attribute_type_normal);
        const cgltf_accessor* uvs = FindAttribute(primitive, cgltf_attribute_type_texcoord);
        const cgltf_accessor* tangents = FindAttribute(primitive, cgltf_attribute_type_tangent);
        const cgltf_accessor* joints = FindAttribute(primitive, cgltf_attribute_type_joints);
        const cgltf_accessor* weights = FindAttribute(primitive, cgltf_attribute_type_weights);

        if (positions == nullptr)
            return fail("missing POSITION attribute");
        if (normals == nullptr)
            return fail("missing NORMAL attribute (re-export with normals)");
        if (normals->count != positions->count
            || (uvs != nullptr && uvs->count != positions->count)
            || (tangents != nullptr && tangents->count != positions->count))
        {
            return fail("attribute counts do not match POSITION count");
        }

        const bool skinned = outInfluences != nullptr;
        if (skinned)
        {
            if (joints == nullptr || weights == nullptr)
                return fail("skinned mesh primitive missing JOINTS_0/WEIGHTS_0");
            if (joints->count != positions->count || weights->count != positions->count)
                return fail("JOINTS_0/WEIGHTS_0 counts do not match POSITION count");
            if (tangents == nullptr && uvs != nullptr)
                return fail("skinned mesh primitive needs authored TANGENT "
                            "(re-export with tangents; cook-side tangents for skinned "
                            "meshes would desync the influence stream)");
        }

        const cgltf_size vertexCount = positions->count;
        vertices.resize(vertexCount);
        if (skinned)
            outInfluences->resize(vertexCount);
        for (cgltf_size i = 0; i < vertexCount; ++i)
        {
            StaticMeshVertex& vertex = vertices[i];

            float position[3]{};
            float normal[3]{};
            if (!cgltf_accessor_read_float(positions, i, position, 3)
                || !cgltf_accessor_read_float(normals, i, normal, 3))
            {
                return fail("could not read vertex attributes");
            }
            vertex.Position = Vec3d(position[0], position[1], position[2]);
            vertex.Normal = Vec3d(normal[0], normal[1], normal[2]);

            if (uvs != nullptr)
            {
                float uv[2]{};
                if (!cgltf_accessor_read_float(uvs, i, uv, 2))
                    return fail("could not read TEXCOORD_0");
                vertex.Uv0 = Vec2d(uv[0], uv[1]);
            }

            if (tangents != nullptr)
            {
                float tangent[4]{};
                if (!cgltf_accessor_read_float(tangents, i, tangent, 4))
                    return fail("could not read TANGENT");
                vertex.Tangent = SnapTangentW(Vec4(tangent[0], tangent[1], tangent[2], tangent[3]));
            }

            if (skinned)
            {
                cgltf_uint jointIndices[4]{};
                float jointWeights[4]{};
                if (!cgltf_accessor_read_uint(joints, i, jointIndices, 4)
                    || !cgltf_accessor_read_float(weights, i, jointWeights, 4))
                {
                    return fail("could not read JOINTS_0/WEIGHTS_0");
                }
                const uint32_t jointU32[4] = { jointIndices[0], jointIndices[1],
                                               jointIndices[2], jointIndices[3] };
                (*outInfluences)[i] = MakeInfluence(jointU32, jointWeights);
            }
        }

        if (primitive.indices != nullptr)
        {
            indices.resize(primitive.indices->count);
            for (cgltf_size i = 0; i < primitive.indices->count; ++i)
            {
                const cgltf_size index = cgltf_accessor_read_index(primitive.indices, i);
                if (index >= vertexCount)
                    return fail("index out of range");
                indices[i] = static_cast<uint32_t>(index);
            }
        }
        else
        {
            indices.resize(vertexCount);
            for (cgltf_size i = 0; i < vertexCount; ++i)
                indices[i] = static_cast<uint32_t>(i);
        }

        if (indices.empty() || indices.size() % 3 != 0)
            return fail("triangle index count must be a nonzero multiple of 3");

        // Decision M: every cooked vertex carries a tangent. Skinned primitives
        // never reach the MikkTSpace branch (rejected above), so influences
        // stay aligned with the un-rewelded vertex order.
        if (tangents == nullptr)
        {
            if (uvs != nullptr)
            {
                if (!GenerateSectionTangents(vertices, indices, error))
                    return false;
            }
            else
            {
                for (StaticMeshVertex& vertex : vertices)
                    vertex.Tangent = TangentFromNormal(vertex.Normal);
            }
        }

        return true;
    }

    std::string CgltfResultMessage(cgltf_result result)
    {
        switch (result)
        {
        case cgltf_result_data_too_short: return "data too short";
        case cgltf_result_unknown_format: return "unknown format";
        case cgltf_result_invalid_json: return "invalid JSON";
        case cgltf_result_invalid_gltf: return "invalid glTF";
        case cgltf_result_out_of_memory: return "out of memory";
        case cgltf_result_legacy_gltf: return "legacy (pre-2.0) glTF";
        default: return "parse error";
        }
    }

    // -- Artifact naming ---------------------------------------------------------

    std::string SanitizeMeshName(std::string_view name)
    {
        std::string sanitized;
        sanitized.reserve(name.size());
        for (const char c : name)
        {
            const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9') || c == '_' || c == '-';
            sanitized.push_back(keep ? c : '_');
        }
        return sanitized;
    }

    // -- Skeletal extraction ---------------------------------------------------

    // glTF matrices are column-major; Sencha's Mat4 is row-major (Mat * Vec).
    Mat4 GltfMat4ToRowMajor(const float m[16])
    {
        Mat4 out;
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                out.Data[row][col] = m[col * 4 + row];
        return out;
    }

    Quat<float> QuatFromRotationColumns(const Vec3d& c0, const Vec3d& c1, const Vec3d& c2)
    {
        // c0/c1/c2 are the normalized basis columns. r[row][col] = c{col}[row].
        const float r00 = c0.X, r10 = c0.Y, r20 = c0.Z;
        const float r01 = c1.X, r11 = c1.Y, r21 = c1.Z;
        const float r02 = c2.X, r12 = c2.Y, r22 = c2.Z;

        const float trace = r00 + r11 + r22;
        Quat<float> q;
        if (trace > 0.0f)
        {
            float s = std::sqrt(trace + 1.0f) * 2.0f;
            q.W = 0.25f * s;
            q.X = (r21 - r12) / s;
            q.Y = (r02 - r20) / s;
            q.Z = (r10 - r01) / s;
        }
        else if (r00 > r11 && r00 > r22)
        {
            float s = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
            q.W = (r21 - r12) / s;
            q.X = 0.25f * s;
            q.Y = (r01 + r10) / s;
            q.Z = (r02 + r20) / s;
        }
        else if (r11 > r22)
        {
            float s = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
            q.W = (r02 - r20) / s;
            q.X = (r01 + r10) / s;
            q.Y = 0.25f * s;
            q.Z = (r12 + r21) / s;
        }
        else
        {
            float s = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
            q.W = (r10 - r01) / s;
            q.X = (r02 + r20) / s;
            q.Y = (r12 + r21) / s;
            q.Z = 0.25f * s;
        }
        return q.Normalized();
    }

    void NodeLocalTrs(const cgltf_node& node, Vec3d& translation, Quat<float>& rotation, Vec3d& scale)
    {
        if (node.has_matrix)
        {
            const float* m = node.matrix; // column-major
            translation = Vec3d(m[12], m[13], m[14]);
            Vec3d c0(m[0], m[1], m[2]);
            Vec3d c1(m[4], m[5], m[6]);
            Vec3d c2(m[8], m[9], m[10]);
            const float sx = static_cast<float>(c0.Magnitude());
            const float sy = static_cast<float>(c1.Magnitude());
            const float sz = static_cast<float>(c2.Magnitude());
            scale = Vec3d(sx, sy, sz);
            const Vec3d n0 = sx > 1e-8f ? c0 / sx : Vec3d(1, 0, 0);
            const Vec3d n1 = sy > 1e-8f ? c1 / sy : Vec3d(0, 1, 0);
            const Vec3d n2 = sz > 1e-8f ? c2 / sz : Vec3d(0, 0, 1);
            rotation = QuatFromRotationColumns(n0, n1, n2);
            return;
        }

        translation = node.has_translation
            ? Vec3d(node.translation[0], node.translation[1], node.translation[2])
            : Vec3d(0, 0, 0);
        rotation = node.has_rotation
            ? Quat<float>(node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3])
                  .Normalized()
            : Quat<float>();
        scale = node.has_scale
            ? Vec3d(node.scale[0], node.scale[1], node.scale[2])
            : Vec3d(1, 1, 1);
    }

    // Builds one skeleton from a glTF skin: joints topologically ordered
    // (parents before children, the format invariant), bind TRS from each
    // joint node, inverse-bind from the skin's IBM accessor (identity when
    // absent). `skinLocalToSkeleton[i]` maps skin.joints[i] to its skeleton
    // index — the remap meshes and animations resolve their joint refs through.
    bool BuildSkeletonFromSkin(const cgltf_data& data,
                               const cgltf_skin& skin,
                               SkeletonData& out,
                               std::vector<uint32_t>& skinLocalToSkeleton,
                               std::string* error)
    {
        const size_t jointCount = skin.joints_count;
        if (jointCount == 0)
            return SetError(error, "skin has no joints"), false;
        if (jointCount > kMaxSkeletonJoints)
            return SetError(error, std::format("skin has {} joints (cap is {})",
                                               jointCount, kMaxSkeletonJoints)),
                   false;

        std::unordered_map<const cgltf_node*, int> localOf;
        for (size_t i = 0; i < jointCount; ++i)
            localOf.emplace(skin.joints[i], static_cast<int>(i));

        std::vector<int> parentLocal(jointCount, -1);
        for (size_t i = 0; i < jointCount; ++i)
        {
            const cgltf_node* parent = skin.joints[i]->parent;
            if (parent != nullptr)
                if (auto it = localOf.find(parent); it != localOf.end())
                    parentLocal[i] = it->second;
        }

        // Topological order: emit a joint once its parent has been emitted.
        std::vector<int> localToSkeleton(jointCount, -1);
        std::vector<int> skeletonToLocal;
        skeletonToLocal.reserve(jointCount);
        bool progress = true;
        while (skeletonToLocal.size() < jointCount && progress)
        {
            progress = false;
            for (size_t i = 0; i < jointCount; ++i)
            {
                if (localToSkeleton[i] != -1)
                    continue;
                const int parent = parentLocal[i];
                if (parent == -1 || localToSkeleton[parent] != -1)
                {
                    localToSkeleton[i] = static_cast<int>(skeletonToLocal.size());
                    skeletonToLocal.push_back(static_cast<int>(i));
                    progress = true;
                }
            }
        }
        if (skeletonToLocal.size() != jointCount)
            return SetError(error, "skin joint hierarchy has a cycle"), false;

        out.Joints.resize(jointCount);
        for (size_t skelIndex = 0; skelIndex < jointCount; ++skelIndex)
        {
            const int localIndex = skeletonToLocal[skelIndex];
            const cgltf_node& jointNode = *skin.joints[localIndex];
            SkeletonJoint& joint = out.Joints[skelIndex];

            joint.Name = jointNode.name != nullptr ? jointNode.name : "";
            joint.ParentIndex = parentLocal[localIndex] == -1
                ? -1 : localToSkeleton[parentLocal[localIndex]];
            NodeLocalTrs(jointNode, joint.BindTranslation, joint.BindRotation, joint.BindScale);

            if (skin.inverse_bind_matrices != nullptr)
            {
                float m[16]{};
                if (!cgltf_accessor_read_float(skin.inverse_bind_matrices,
                                               static_cast<cgltf_size>(localIndex), m, 16))
                    return SetError(error, "could not read inverse bind matrix"), false;
                joint.InverseBind = GltfMat4ToRowMajor(m);
            }
            else
            {
                for (int d = 0; d < 4; ++d)
                    joint.InverseBind.Data[d][d] = 1.0f;
            }
        }

        (void)data;
        skinLocalToSkeleton.assign(localToSkeleton.begin(), localToSkeleton.end());
        return ValidateSkeletonData(out, error);
    }

    int SkinIndexOf(const cgltf_data& data, const cgltf_skin* skin)
    {
        if (skin == nullptr)
            return -1;
        for (cgltf_size i = 0; i < data.skins_count; ++i)
            if (&data.skins[i] == skin)
                return static_cast<int>(i);
        return -1;
    }

    // The distinct skins a mesh is instanced with — a glTF mesh may be reused
    // by several nodes, each potentially with a different skin. Empty means
    // the mesh is never used skinned; more than one is ambiguous (the cook
    // cannot pick a single skeleton for one artifact) and is rejected by the
    // caller rather than silently honoring the first.
    std::vector<int> MeshSkinIndices(const cgltf_data& data, const cgltf_mesh& mesh)
    {
        std::vector<int> skins;
        for (cgltf_size i = 0; i < data.nodes_count; ++i)
        {
            const cgltf_node& node = data.nodes[i];
            if (node.mesh != &mesh || node.skin == nullptr)
                continue;
            const int skin = SkinIndexOf(data, node.skin);
            if (skin >= 0 && std::find(skins.begin(), skins.end(), skin) == skins.end())
                skins.push_back(skin);
        }
        return skins;
    }

    AnimationChannelPath MapChannelPath(cgltf_animation_path_type path, bool& supported)
    {
        supported = true;
        switch (path)
        {
        case cgltf_animation_path_type_translation: return AnimationChannelPath::Translation;
        case cgltf_animation_path_type_rotation:    return AnimationChannelPath::Rotation;
        case cgltf_animation_path_type_scale:       return AnimationChannelPath::Scale;
        default: supported = false; return AnimationChannelPath::Translation; // weights, etc.
        }
    }
} // namespace

bool GenerateSectionTangents(std::vector<StaticMeshVertex>& vertices,
                             std::vector<uint32_t>& indices,
                             std::string* error)
{
    if (indices.empty() || indices.size() % 3 != 0)
    {
        SetError(error, "tangent generation requires a nonzero multiple of 3 indices");
        return false;
    }

    std::vector<StaticMeshVertex> corners;
    corners.reserve(indices.size());
    for (const uint32_t index : indices)
    {
        if (index >= vertices.size())
        {
            SetError(error, "tangent generation: index out of range");
            return false;
        }
        corners.push_back(vertices[index]);
    }

    SMikkTSpaceInterface mikkInterface{};
    mikkInterface.m_getNumFaces = MikkGetNumFaces;
    mikkInterface.m_getNumVerticesOfFace = MikkGetNumVerticesOfFace;
    mikkInterface.m_getPosition = MikkGetPosition;
    mikkInterface.m_getNormal = MikkGetNormal;
    mikkInterface.m_getTexCoord = MikkGetTexCoord;
    mikkInterface.m_setTSpaceBasic = MikkSetTSpaceBasic;

    SMikkTSpaceContext context{};
    context.m_pInterface = &mikkInterface;
    context.m_pUserData = &corners;

    if (genTangSpaceDefault(&context) == 0)
    {
        SetError(error, "MikkTSpace tangent generation failed");
        return false;
    }

    // Re-weld exact duplicates so the de-index doesn't triple the vertex
    // count where corners agree.
    std::vector<StaticMeshVertex> welded;
    std::vector<uint32_t> weldedIndices;
    weldedIndices.reserve(corners.size());
    std::unordered_map<uint64_t, std::vector<uint32_t>> candidatesByHash;

    for (const StaticMeshVertex& corner : corners)
    {
        const uint64_t hash = HashBytes64(VertexBytes(corner));
        std::vector<uint32_t>& candidates = candidatesByHash[hash];

        uint32_t found = UINT32_MAX;
        for (const uint32_t candidate : candidates)
        {
            if (std::memcmp(&welded[candidate], &corner, sizeof(StaticMeshVertex)) == 0)
            {
                found = candidate;
                break;
            }
        }
        if (found == UINT32_MAX)
        {
            found = static_cast<uint32_t>(welded.size());
            welded.push_back(corner);
            candidates.push_back(found);
        }
        weldedIndices.push_back(found);
    }

    vertices = std::move(welded);
    indices = std::move(weldedIndices);
    return true;
}

bool ImportGltfMeshes(std::span<const std::byte> bytes,
                      std::vector<ImportedGltfMesh>& out,
                      std::string* error)
{
    out.clear();

    cgltf_options options{};
    cgltf_data* rawData = nullptr;
    cgltf_result result = cgltf_parse(&options, bytes.data(), bytes.size(), &rawData);
    if (result != cgltf_result_success)
    {
        SetError(error, "glTF parse failed: " + CgltfResultMessage(result));
        return false;
    }
    CgltfDataPtr data(rawData);

    // No base path: a source must be self-contained (.glb or data: URIs) so
    // the cooked cache's single-source-hash staleness stays honest.
    result = cgltf_load_buffers(&options, data.get(), nullptr);
    if (result != cgltf_result_success)
    {
        SetError(error,
                 result == cgltf_result_unknown_format
                     ? "external buffer URIs are not supported: export a self-contained "
                       ".glb (or embed buffers as data: URIs)"
                     : "glTF buffer load failed: " + CgltfResultMessage(result));
        return false;
    }

    if (data->meshes_count == 0)
    {
        SetError(error, "source contains no meshes");
        return false;
    }

    for (cgltf_size meshIndex = 0; meshIndex < data->meshes_count; ++meshIndex)
    {
        const cgltf_mesh& gltfMesh = data->meshes[meshIndex];
        const std::string meshName = gltfMesh.name != nullptr ? gltfMesh.name : "";
        const std::string_view nameForErrors =
            meshName.empty() ? std::string_view("<unnamed>") : std::string_view(meshName);

        if (gltfMesh.primitives_count == 0)
        {
            SetError(error, std::format("mesh '{}' has no primitives", nameForErrors));
            return false;
        }

        ImportedGltfMesh imported;
        imported.Name = meshName;

        for (cgltf_size primitiveIndex = 0; primitiveIndex < gltfMesh.primitives_count; ++primitiveIndex)
        {
            std::vector<StaticMeshVertex> vertices;
            std::vector<uint32_t> indices;
            if (!ReadPrimitive(gltfMesh.primitives[primitiveIndex], nameForErrors,
                               primitiveIndex, vertices, indices, error))
            {
                return false;
            }

            MeshGeometry& mesh = imported.Geometry;
            const uint32_t vertexBase = static_cast<uint32_t>(mesh.Vertices.size());

            StaticMeshSection section;
            section.IndexOffset = static_cast<uint32_t>(mesh.Indices.size());
            section.IndexCount = static_cast<uint32_t>(indices.size());
            section.VertexOffset = vertexBase;
            section.VertexCount = static_cast<uint32_t>(vertices.size());
            section.MaterialSlot = static_cast<uint32_t>(primitiveIndex);
            mesh.Sections.push_back(section);

            mesh.Vertices.insert(mesh.Vertices.end(), vertices.begin(), vertices.end());
            mesh.Indices.reserve(mesh.Indices.size() + indices.size());
            for (const uint32_t index : indices)
                mesh.Indices.push_back(vertexBase + index);
        }

        RecomputeMeshBounds(imported.Geometry);
        const MeshValidationResult validation = ValidateMeshGeometry(imported.Geometry);
        if (!validation.IsValid())
        {
            std::string joined;
            for (const MeshValidationError& validationError : validation.Errors)
            {
                if (!joined.empty())
                    joined += "; ";
                joined += validationError.Message;
            }
            SetError(error, std::format("mesh '{}' is invalid: {}", nameForErrors, joined));
            return false;
        }

        out.push_back(std::move(imported));
    }

    return true;
}

bool ImportGltfScene(std::span<const std::byte> bytes, ImportedGltfScene& out, std::string* error)
{
    out = {};

    cgltf_options options{};
    cgltf_data* rawData = nullptr;
    cgltf_result result = cgltf_parse(&options, bytes.data(), bytes.size(), &rawData);
    if (result != cgltf_result_success)
    {
        SetError(error, "glTF parse failed: " + CgltfResultMessage(result));
        return false;
    }
    CgltfDataPtr data(rawData);

    result = cgltf_load_buffers(&options, data.get(), nullptr);
    if (result != cgltf_result_success)
    {
        SetError(error,
                 result == cgltf_result_unknown_format
                     ? "external buffer URIs are not supported: export a self-contained "
                       ".glb (or embed buffers as data: URIs)"
                     : "glTF buffer load failed: " + CgltfResultMessage(result));
        return false;
    }

    if (data->meshes_count == 0)
    {
        SetError(error, "source contains no meshes");
        return false;
    }

    // Skeletons, one per skin. Keep each skin's skin-local → skeleton remap,
    // and a node → (skin, skeleton-joint) lookup for animation channels.
    std::vector<std::vector<uint32_t>> skinRemaps(data->skins_count);
    std::unordered_map<const cgltf_node*, std::pair<int, uint32_t>> jointLookup;
    for (cgltf_size skinIndex = 0; skinIndex < data->skins_count; ++skinIndex)
    {
        const cgltf_skin& skin = data->skins[skinIndex];
        ImportedSkeleton skeleton;
        skeleton.Name = skin.name != nullptr ? skin.name : "";
        if (!BuildSkeletonFromSkin(*data, skin, skeleton.Data, skinRemaps[skinIndex], error))
            return false;

        for (cgltf_size j = 0; j < skin.joints_count; ++j)
        {
            const uint32_t skeletonJoint = skinRemaps[skinIndex][j];
            jointLookup.try_emplace(skin.joints[j],
                                    std::pair<int, uint32_t>{ static_cast<int>(skinIndex), skeletonJoint });
        }
        out.Skeletons.push_back(std::move(skeleton));
    }

    // Meshes — skinned ones carry an influence stream with skeleton-local
    // joints; SkeletonPath is left for the importer to assign.
    for (cgltf_size meshIndex = 0; meshIndex < data->meshes_count; ++meshIndex)
    {
        const cgltf_mesh& gltfMesh = data->meshes[meshIndex];
        const std::string meshName = gltfMesh.name != nullptr ? gltfMesh.name : "";
        const std::string_view nameForErrors =
            meshName.empty() ? std::string_view("<unnamed>") : std::string_view(meshName);

        if (gltfMesh.primitives_count == 0)
        {
            SetError(error, std::format("mesh '{}' has no primitives", nameForErrors));
            return false;
        }

        const std::vector<int> meshSkins = MeshSkinIndices(*data, gltfMesh);
        if (meshSkins.size() > 1)
        {
            SetError(error, std::format(
                "mesh '{}' is instanced with {} different skins; the cook will not silently "
                "pick one — split it into one mesh per skin or re-export",
                nameForErrors, meshSkins.size()));
            return false;
        }
        const int skinIndex = meshSkins.empty() ? -1 : meshSkins.front();
        const bool skinned = skinIndex >= 0;

        ImportedGltfMesh imported;
        imported.Name = meshName;
        imported.SkinIndex = skinIndex;

        std::vector<MeshSkinInfluence> meshInfluences;
        for (cgltf_size primitiveIndex = 0; primitiveIndex < gltfMesh.primitives_count; ++primitiveIndex)
        {
            std::vector<StaticMeshVertex> vertices;
            std::vector<uint32_t> indices;
            std::vector<MeshSkinInfluence> influences;
            if (!ReadPrimitive(gltfMesh.primitives[primitiveIndex], nameForErrors, primitiveIndex,
                               vertices, indices, error, skinned ? &influences : nullptr))
            {
                return false;
            }

            MeshGeometry& mesh = imported.Geometry;
            const uint32_t vertexBase = static_cast<uint32_t>(mesh.Vertices.size());

            StaticMeshSection section;
            section.IndexOffset = static_cast<uint32_t>(mesh.Indices.size());
            section.IndexCount = static_cast<uint32_t>(indices.size());
            section.VertexOffset = vertexBase;
            section.VertexCount = static_cast<uint32_t>(vertices.size());
            section.MaterialSlot = static_cast<uint32_t>(primitiveIndex);
            mesh.Sections.push_back(section);

            mesh.Vertices.insert(mesh.Vertices.end(), vertices.begin(), vertices.end());
            mesh.Indices.reserve(mesh.Indices.size() + indices.size());
            for (const uint32_t index : indices)
                mesh.Indices.push_back(vertexBase + index);

            if (skinned)
                meshInfluences.insert(meshInfluences.end(), influences.begin(), influences.end());
        }

        if (skinned)
        {
            const std::vector<uint32_t>& remap = skinRemaps[skinIndex];
            for (MeshSkinInfluence& influence : meshInfluences)
            {
                for (int slot = 0; slot < 4; ++slot)
                {
                    if (influence.Weights[slot] == 0)
                    {
                        influence.Joints[slot] = 0;
                        continue;
                    }
                    if (influence.Joints[slot] >= remap.size())
                    {
                        SetError(error, std::format("mesh '{}' references joint {} outside skin",
                                                    nameForErrors, influence.Joints[slot]));
                        return false;
                    }
                    influence.Joints[slot] = static_cast<uint16_t>(remap[influence.Joints[slot]]);
                }
            }

            MeshSkinning skinning;
            skinning.JointCount = static_cast<uint32_t>(out.Skeletons[skinIndex].Data.Joints.size());
            skinning.Influences = std::move(meshInfluences);
            imported.Skinning = std::move(skinning);
        }

        RecomputeMeshBounds(imported.Geometry);

        // Validate geometry now; the skinning invariants (which need the
        // skeleton's assigned artifact path) are validated by the serializer
        // once the importer fills SkeletonPath.
        const MeshValidationResult validation = ValidateMeshGeometry(imported.Geometry);
        if (!validation.IsValid())
        {
            std::string joined;
            for (const MeshValidationError& validationError : validation.Errors)
            {
                if (!joined.empty())
                    joined += "; ";
                joined += validationError.Message;
            }
            SetError(error, std::format("mesh '{}' is invalid: {}", nameForErrors, joined));
            return false;
        }

        out.Meshes.push_back(std::move(imported));
    }

    // Animations — each becomes one clip on the single skeleton its channels
    // pose. A clip whose channels span more than one skin (a multi-character
    // export) is ambiguous and rejected rather than silently truncated;
    // channels targeting non-joint nodes are node animation, out of scope here.
    for (cgltf_size animIndex = 0; animIndex < data->animations_count; ++animIndex)
    {
        const cgltf_animation& animation = data->animations[animIndex];
        const std::string_view animName =
            animation.name != nullptr ? std::string_view(animation.name) : std::string_view("<unnamed>");

        std::vector<int> animSkins;
        for (cgltf_size c = 0; c < animation.channels_count; ++c)
        {
            const cgltf_node* target = animation.channels[c].target_node;
            if (target == nullptr)
                continue;
            if (auto it = jointLookup.find(target); it != jointLookup.end())
                if (std::find(animSkins.begin(), animSkins.end(), it->second.first) == animSkins.end())
                    animSkins.push_back(it->second.first);
        }
        if (animSkins.empty())
            continue; // not a skeletal animation (node/morph animation); out of scope
        if (animSkins.size() > 1)
        {
            SetError(error, std::format(
                "animation '{}' targets joints in {} different skins; the cook will not "
                "silently drop tracks — split it into one clip per skeleton",
                animName, animSkins.size()));
            return false;
        }
        const int animSkin = animSkins.front();

        ImportedAnimation imported;
        imported.Name = animation.name != nullptr ? animation.name : "";
        imported.SkinIndex = animSkin;
        AnimationClipData& clip = imported.Data;

        for (cgltf_size c = 0; c < animation.channels_count; ++c)
        {
            const cgltf_animation_channel& channel = animation.channels[c];
            const cgltf_node* target = channel.target_node;
            if (target == nullptr)
                continue;
            auto it = jointLookup.find(target);
            if (it == jointLookup.end() || it->second.first != animSkin)
                continue; // targets another skin or a non-joint node

            bool supportedPath = false;
            const AnimationChannelPath path = MapChannelPath(channel.target_path, supportedPath);
            if (!supportedPath)
                continue; // morph-target weights, etc.

            const cgltf_animation_sampler* sampler = channel.sampler;
            if (sampler == nullptr || sampler->input == nullptr || sampler->output == nullptr)
                continue;
            if (sampler->interpolation == cgltf_interpolation_type_cubic_spline)
            {
                SetError(error, "cubic spline animation interpolation is not supported "
                                "(re-export with linear or step keys)");
                return false;
            }

            AnimationJointTrack track;
            track.JointIndex = it->second.second;
            track.Path = path;
            track.Interpolation = sampler->interpolation == cgltf_interpolation_type_step
                ? AnimationInterpolation::Step : AnimationInterpolation::Linear;

            const cgltf_size keyCount = sampler->input->count;
            const uint32_t components = AnimationChannelComponentCount(path);
            if (sampler->output->count != keyCount)
            {
                SetError(error, "animation sampler input/output counts disagree");
                return false;
            }

            track.TimesSeconds.resize(keyCount);
            track.Values.resize(static_cast<size_t>(keyCount) * components);
            for (cgltf_size k = 0; k < keyCount; ++k)
            {
                if (!cgltf_accessor_read_float(sampler->input, k, &track.TimesSeconds[k], 1))
                {
                    SetError(error, "could not read animation key time");
                    return false;
                }
                float value[4]{};
                if (!cgltf_accessor_read_float(sampler->output, k, value, components))
                {
                    SetError(error, "could not read animation key value");
                    return false;
                }
                if (path == AnimationChannelPath::Rotation)
                {
                    // Normalize so the unit-quaternion invariant holds exactly.
                    const float len = std::sqrt(value[0] * value[0] + value[1] * value[1]
                                                + value[2] * value[2] + value[3] * value[3]);
                    if (len > 1e-8f)
                        for (float& component : value)
                            component /= len;
                }
                for (uint32_t component = 0; component < components; ++component)
                    track.Values[static_cast<size_t>(k) * components + component] = value[component];

                clip.DurationSeconds = std::max(clip.DurationSeconds, track.TimesSeconds[k]);
            }

            clip.Tracks.push_back(std::move(track));
        }

        if (clip.Tracks.empty())
            continue;
        out.Animations.push_back(std::move(imported));
    }

    return true;
}

std::vector<std::string_view> GltfMeshImporter::SourceExtensions() const
{
    return { ".glb", ".gltf" };
}

ImportResult GltfMeshImporter::Import(const ImportInput& input, ICookOutputWriter& output)
{
    ImportedGltfScene scene;
    std::string error;
    if (!ImportGltfScene(input.Bytes, scene, &error))
        return ImportResult{ .Error = "gltf import: " + error };

    const std::string source(input.SourceRelPath);
    const std::string virtualPrefix = "asset://" + source;
    const std::string fileBase = ".cooked/" + source;

    // A single static mesh keeps the source's virtual path; everything else
    // takes a '#'-suffixed artifact name ('#' can't appear in scanned paths,
    // so cooked names never collide with real files).
    const bool singleStaticMesh = scene.Meshes.size() == 1
        && scene.Meshes[0].SkinIndex < 0 && scene.Skeletons.empty();

    std::unordered_set<std::string> usedNames;
    const auto uniqueName = [&usedNames](std::string base, size_t ordinal) {
        if (base.empty())
            base = std::format("{}", ordinal);
        std::string name = base;
        while (!usedNames.insert(name).second)
            name += std::format("_{}", ordinal);
        return name;
    };

    // Skeleton artifact paths, indexed by skin, so meshes and clips can
    // reference them.
    std::vector<std::string> skeletonPaths(scene.Skeletons.size());

    ImportResult result;
    LoggingProvider silentLogging; // importers report, never log.

    // -- Skeletons --
    for (size_t i = 0; i < scene.Skeletons.size(); ++i)
    {
        const std::string name = "skel:" + SanitizeMeshName(scene.Skeletons[i].Name);
        const std::string unique = uniqueName(name, i);

        std::vector<std::byte> bytes;
        if (!WriteSskelToBytes(scene.Skeletons[i].Data, bytes, &error))
            return ImportResult{ .Error = "gltf import: .sskel serialization failed: " + error };

        CookedArtifact artifact;
        artifact.Path = virtualPrefix + "#" + unique;
        artifact.FileRelPath = fileBase + "." + unique + ".sskel";
        artifact.Type = AssetType::Skeleton;
        skeletonPaths[i] = artifact.Path;

        if (!output.WriteBytes(artifact.FileRelPath, bytes))
            return ImportResult{ .Error = "gltf import: artifact write failed for '" + artifact.FileRelPath + "'" };
        result.Artifacts.push_back(std::move(artifact));
    }

    // -- Meshes --
    // A skinned mesh emits a `.skmesh` (AssetType::SkinnedMesh) referencing
    // its skeleton; a static mesh emits a `.smesh` (AssetType::StaticMesh).
    // The kind is path-level — the extension and asset type distinguish them
    // without reading the payload.
    MeshSerializer serializer(silentLogging);
    for (size_t meshIndex = 0; meshIndex < scene.Meshes.size(); ++meshIndex)
    {
        ImportedGltfMesh& mesh = scene.Meshes[meshIndex];
        const bool skinned = mesh.SkinIndex >= 0 && mesh.Skinning.has_value();

        std::vector<std::byte> meshBytes;
        if (skinned)
        {
            mesh.Skinning->SkeletonPath = skeletonPaths[mesh.SkinIndex];
            SkinnedMeshData skinnedData{ std::move(mesh.Geometry), std::move(*mesh.Skinning) };
            if (!serializer.WriteSkinnedToBytes(skinnedData, meshBytes))
                return ImportResult{ .Error = std::format(
                    "gltf import: .skmesh serialization failed for mesh {}", meshIndex) };
        }
        else if (!serializer.WriteToBytes(mesh.Geometry, meshBytes))
        {
            return ImportResult{ .Error = std::format(
                "gltf import: .smesh serialization failed for mesh {}", meshIndex) };
        }

        const std::string_view extension = skinned ? ".skmesh" : ".smesh";
        CookedArtifact artifact;
        if (singleStaticMesh)
        {
            artifact.Path = virtualPrefix;
            artifact.FileRelPath = fileBase + std::string(extension);
        }
        else
        {
            const std::string unique = uniqueName(SanitizeMeshName(mesh.Name), meshIndex);
            artifact.Path = virtualPrefix + "#" + unique;
            artifact.FileRelPath = fileBase + "." + unique + std::string(extension);
        }
        artifact.Type = skinned ? AssetType::SkinnedMesh : AssetType::StaticMesh;

        if (!output.WriteBytes(artifact.FileRelPath, meshBytes))
            return ImportResult{ .Error = "gltf import: artifact write failed for '" + artifact.FileRelPath + "'" };
        result.Artifacts.push_back(std::move(artifact));
    }

    // -- Animations --
    for (size_t animIndex = 0; animIndex < scene.Animations.size(); ++animIndex)
    {
        ImportedAnimation& animation = scene.Animations[animIndex];
        animation.Data.SkeletonPath = skeletonPaths[animation.SkinIndex];

        std::vector<std::byte> bytes;
        if (!WriteSanimToBytes(animation.Data, bytes, &error))
            return ImportResult{ .Error = "gltf import: .sanim serialization failed: " + error };

        const std::string unique = uniqueName("anim:" + SanitizeMeshName(animation.Name), animIndex);
        CookedArtifact artifact;
        artifact.Path = virtualPrefix + "#" + unique;
        artifact.FileRelPath = fileBase + "." + unique + ".sanim";
        artifact.Type = AssetType::AnimationClip;

        if (!output.WriteBytes(artifact.FileRelPath, bytes))
            return ImportResult{ .Error = "gltf import: artifact write failed for '" + artifact.FileRelPath + "'" };
        result.Artifacts.push_back(std::move(artifact));
    }

    return result;
}
