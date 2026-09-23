#include <assets/cook/MeshCook.h>

#include <anim/AnimationClipSampling.h>
#include <anim/SkinningPalette.h>
#include <assets/cook/CookFingerprint.h>
#include <assets/cook/GltfFrame.h>
#include <assets/cook/MeshImportSettings.h>
#include <assets/animation/AnimationClipSerializer.h>
#include <assets/skeleton/SkeletonSerializer.h>
#include <assets/static_mesh/MeshSerializer.h>
#include <core/hash/ContentHash.h>
#include <core/logging/LoggingProvider.h>
#include <math/Quat.h>
#include <math/geometry/3d/Transform3d.h>
#include <assets/static_mesh/MeshValidation.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <mikktspace.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <unordered_map>

namespace
{
    // The glTF importer's cook version: part of its CookIdentity, so every
    // artifact it produced recooks when this moves.
    constexpr std::uint32_t kGltfMeshCookVersion = 6;

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
                                        cgltf_attribute_type type,
                                        int setIndex = 0)
    {
        for (cgltf_size i = 0; i < primitive.attributes_count; ++i)
        {
            const cgltf_attribute& attribute = primitive.attributes[i];
            if (attribute.type == type && attribute.index == setIndex)
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
        // TEXCOORD_1, when the author supplies one, imports as the lightmap
        // sheet: a [0,1] layout the cook later assigns per-placement atlas
        // rects to. No auto-unwrapping; meshes without it stay unbaked.
        const cgltf_accessor* lightmapUvs =
            FindAttribute(primitive, cgltf_attribute_type_texcoord, 1);
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

            if (lightmapUvs != nullptr)
            {
                float uv[2]{};
                if (!cgltf_accessor_read_float(lightmapUvs, i, uv, 2))
                    return fail("could not read TEXCOORD_1");
                const auto pack = [](float value) {
                    const float clamped =
                        value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
                    return static_cast<std::uint16_t>(clamped * 65535.0f + 0.5f);
                };
                vertex.LightmapU = pack(uv[0]);
                vertex.LightmapV = pack(uv[1]);
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

    std::string NodeLabel(const cgltf_data& data, const cgltf_node& node)
    {
        if (node.name != nullptr && node.name[0] != '\0')
            return std::format("'{}'", node.name);
        return std::format("node {}", cgltf_node_index(&data, &node));
    }

    std::string SkinLabel(const cgltf_data& data, const cgltf_skin& skin)
    {
        if (skin.name != nullptr && skin.name[0] != '\0')
            return std::format("skin '{}'", skin.name);
        return std::format("skin {}", cgltf_skin_index(&data, &skin));
    }

    // Splits an affine matrix into translation, rotation and scale. Fails when
    // the linear part is degenerate, sheared or mirrored: a TRS cannot hold
    // those, and decomposing them anyway is how a skeleton ends up twisted.
    bool DecomposeAffine(const Mat4& m, Transform3f& out)
    {
        const Vec3d c0(m.Data[0][0], m.Data[1][0], m.Data[2][0]);
        const Vec3d c1(m.Data[0][1], m.Data[1][1], m.Data[2][1]);
        const Vec3d c2(m.Data[0][2], m.Data[1][2], m.Data[2][2]);
        const Vec3d scale(c0.Magnitude(), c1.Magnitude(), c2.Magnitude());
        if (scale.X < 1e-8f || scale.Y < 1e-8f || scale.Z < 1e-8f)
            return false;

        const Vec3d n0 = c0 / scale.X;
        const Vec3d n1 = c1 / scale.Y;
        const Vec3d n2 = c2 / scale.Z;
        constexpr float kOrthogonality = 1e-4f;
        if (std::abs(n0.Dot(n1)) > kOrthogonality || std::abs(n0.Dot(n2)) > kOrthogonality
            || std::abs(n1.Dot(n2)) > kOrthogonality || n0.Cross(n1).Dot(n2) < 0.0f)
        {
            return false;
        }

        out.Position = Vec3d(m.Data[0][3], m.Data[1][3], m.Data[2][3]);
        out.Rotation = Quat<float>::FromBasis(n0, n1, n2).Normalized();
        out.Scale = scale;
        return true;
    }

    bool IsUniformScale(const Vec3d& scale)
    {
        const float largest = std::max({ std::abs(scale.X), std::abs(scale.Y), std::abs(scale.Z) });
        const float smallest = std::min({ scale.X, scale.Y, scale.Z });
        return smallest > 0.0f && largest - smallest <= 1e-4f * largest;
    }

    // A node's local transform as TRS. Only a matrix-form node can fail.
    bool NodeLocalTransform(const cgltf_node& node, Transform3f& out)
    {
        if (node.has_matrix)
            return DecomposeAffine(GltfMat4ToRowMajor(node.matrix), out);

        out.Position = node.has_translation
            ? Vec3d(node.translation[0], node.translation[1], node.translation[2])
            : Vec3d(0, 0, 0);
        out.Rotation = node.has_rotation
            ? Quat<float>(node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3])
                  .Normalized()
            : Quat<float>();
        out.Scale = node.has_scale
            ? Vec3d(node.scale[0], node.scale[1], node.scale[2])
            : Vec3d(1, 1, 1);
        return true;
    }

    // Composes a root joint's ancestors, outermost first, into the one
    // transform they place the root in, starting from the engine frame. The
    // result is folded into the root's TRS, which is exact only for a uniform
    // scale without mirroring.
    bool ComposeAncestors(std::span<const Transform3f> ancestors, Transform3f& out)
    {
        Mat4 composed = GltfToEngineMatrix();
        for (const Transform3f& ancestor : ancestors)
            composed = composed * ancestor.ToMat4();
        return DecomposeAffine(composed, out) && IsUniformScale(out.Scale);
    }

    // The ancestor to name when a chain will not compose: the first one that
    // cannot itself be folded, or the innermost.
    size_t IrregularAncestor(std::span<const Transform3f> ancestors)
    {
        for (size_t i = 0; i < ancestors.size(); ++i)
            if (!IsUniformScale(ancestors[i].Scale))
                return i;
        return ancestors.size() - 1;
    }

    // A root joint and the non-joint nodes above it. A glTF skin's inverse
    // bind matrices and skinned vertices are written in scene space, which
    // includes every node above the roots (an exported armature object, for
    // one). The cook folds those nodes, and the turn into the engine frame,
    // into the root joints, at rest and in every clip, so the skeleton's
    // model space is the engine-frame scene space its skinned meshes are
    // baked into.
    struct RootChain
    {
        uint32_t Joint = 0;
        const cgltf_node* Node = nullptr;
        Transform3f RootRest;

        // Outermost first, and their composition at rest.
        std::vector<const cgltf_node*> Ancestors;
        std::vector<Transform3f> AncestorRest;
        Transform3f Frame;
    };

    // Every joint's rest palette entry must be the identity: the inverse bind
    // matrices describe the same pose the nodes do. A source bound in some
    // other pose would draw deformed at rest, so it is refused.
    bool CheckRestPalette(const cgltf_data& data,
                          const cgltf_skin& skin,
                          const SkeletonData& skeleton,
                          std::string* error)
    {
        std::vector<Mat4> model;
        std::vector<Mat4> palette;
        BuildBindModelTransforms(skeleton, model);
        BuildSkinningPalette(skeleton, model, palette);

        float extent = 1.0f;
        for (const Mat4& transform : model)
            for (int row = 0; row < 3; ++row)
                extent = std::max(extent, std::abs(transform.Data[row][3]));

        for (size_t joint = 0; joint < palette.size(); ++joint)
        {
            const Mat4& entry = palette[joint];
            bool identity = true;
            for (int row = 0; row < 3; ++row)
            {
                for (int col = 0; col < 3; ++col)
                    identity = identity
                        && std::abs(entry.Data[row][col] - (row == col ? 1.0f : 0.0f)) <= 1e-3f;
                identity = identity && std::abs(entry.Data[row][3]) <= 1e-3f * extent;
            }
            if (!identity)
            {
                const std::string& name = skeleton.Joints[joint].Name;
                SetError(error, std::format(
                    "{}: joint '{}' is not at its bind pose at rest (its inverse bind matrix "
                    "disagrees with its rest transform). The source was bound in a pose other than "
                    "its rest pose: apply the pose as the rest pose (Blender: Pose > Apply > "
                    "Apply Pose as Rest Pose) and re-export",
                    SkinLabel(data, skin), name.empty() ? std::format("{}", joint) : name));
                return false;
            }
        }
        return true;
    }

    // Builds one skeleton from a glTF skin: joints topologically ordered
    // (parents before children, the format invariant), bind TRS from each
    // joint node with the root joints' ancestors folded in, inverse-bind from
    // the skin's IBM accessor (identity when absent). `skinLocalToSkeleton[i]`
    // maps skin.joints[i] to its skeleton index — the remap meshes and
    // animations resolve their joint refs through. `roots` receives each root
    // joint's ancestor chain, which animation import folds into root tracks.
    bool BuildSkeletonFromSkin(const cgltf_data& data,
                               const cgltf_skin& skin,
                               SkeletonData& out,
                               std::vector<uint32_t>& skinLocalToSkeleton,
                               std::vector<RootChain>& roots,
                               std::string* error)
    {
        const size_t jointCount = skin.joints_count;
        if (jointCount == 0)
            return SetError(error, std::format("{} has no joints", SkinLabel(data, skin))), false;
        if (jointCount > kMaxSkeletonJoints)
            return SetError(error, std::format("{} has {} joints (cap is {})", SkinLabel(data, skin),
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
            return SetError(error, std::format("{} joint hierarchy has a cycle",
                                               SkinLabel(data, skin))), false;

        out.Joints.resize(jointCount);
        roots.clear();
        for (size_t skelIndex = 0; skelIndex < jointCount; ++skelIndex)
        {
            const int localIndex = skeletonToLocal[skelIndex];
            const cgltf_node& jointNode = *skin.joints[localIndex];
            SkeletonJoint& joint = out.Joints[skelIndex];

            // A joint's name is its stable key: what a bone mask and anything
            // else persisted against the skeleton names it by. So it must be
            // there, and be one joint's alone.
            joint.Name = jointNode.name != nullptr ? jointNode.name : "";
            if (joint.Name.empty())
                return SetError(error, std::format("{}: joint {} has no name; a joint is named so masks and "
                                                   "other content can refer to it across re-exports",
                                                   SkinLabel(data, skin), NodeLabel(data, jointNode))), false;
            for (std::size_t earlier = 0; earlier < skelIndex; ++earlier)
                if (out.Joints[earlier].Name == joint.Name)
                    return SetError(error, std::format("{}: two joints are named '{}'; rename one, because a "
                                                       "joint's name is how content refers to it",
                                                       SkinLabel(data, skin), joint.Name)), false;
            joint.ParentIndex = parentLocal[localIndex] == -1
                ? -1 : localToSkeleton[parentLocal[localIndex]];

            Transform3f local;
            if (!NodeLocalTransform(jointNode, local))
                return SetError(error, std::format(
                           "{}: joint {} has a sheared, mirrored or degenerate matrix",
                           SkinLabel(data, skin), NodeLabel(data, jointNode))), false;
            joint.BindTranslation = local.Position;
            joint.BindRotation = local.Rotation;
            joint.BindScale = local.Scale;

            if (skin.inverse_bind_matrices != nullptr)
            {
                float m[16]{};
                if (!cgltf_accessor_read_float(skin.inverse_bind_matrices,
                                               static_cast<cgltf_size>(localIndex), m, 16))
                    return SetError(error, "could not read inverse bind matrix"), false;
                // Turn engine space back into scene space before the source's
                // inverse bind (the turn is its own inverse), so the rest
                // palette stays the identity.
                joint.InverseBind = GltfMat4ToRowMajor(m) * GltfToEngineMatrix();
            }
            else
            {
                joint.InverseBind = GltfToEngineMatrix();
            }

            if (joint.ParentIndex != -1)
                continue;

            RootChain chain;
            chain.Joint = static_cast<uint32_t>(skelIndex);
            chain.Node = &jointNode;
            chain.RootRest = local;
            for (const cgltf_node* ancestor = jointNode.parent; ancestor != nullptr;
                 ancestor = ancestor->parent)
            {
                // A joint above a non-joint node would put an unposable
                // transform inside the hierarchy.
                if (localOf.contains(ancestor))
                    return SetError(error, std::format(
                               "{}: joint {} hangs from joint {} through non-joint node {}; "
                               "make {} a joint or remove it",
                               SkinLabel(data, skin), NodeLabel(data, jointNode),
                               NodeLabel(data, *ancestor), NodeLabel(data, *jointNode.parent),
                               NodeLabel(data, *jointNode.parent))), false;
                chain.Ancestors.push_back(ancestor);
            }
            std::reverse(chain.Ancestors.begin(), chain.Ancestors.end());

            for (const cgltf_node* ancestor : chain.Ancestors)
            {
                Transform3f ancestorLocal;
                if (!NodeLocalTransform(*ancestor, ancestorLocal))
                    return SetError(error, std::format(
                               "{}: node {} above root joint {} has a sheared, mirrored or "
                               "degenerate matrix",
                               SkinLabel(data, skin), NodeLabel(data, *ancestor),
                               NodeLabel(data, jointNode))), false;
                chain.AncestorRest.push_back(ancestorLocal);
            }
            if (!ComposeAncestors(chain.AncestorRest, chain.Frame))
            {
                const std::string culprit =
                    NodeLabel(data, *chain.Ancestors[IrregularAncestor(chain.AncestorRest)]);
                return SetError(error, std::format(
                           "{}: the nodes above root joint {} (at {}) are not a uniform scale, "
                           "rotation and translation, so the skeleton's space cannot be folded "
                           "into its root; apply the scale on {} or make it uniform",
                           SkinLabel(data, skin), NodeLabel(data, jointNode), culprit, culprit)),
                       false;
            }

            const Transform3f folded = chain.Frame * local;
            joint.BindTranslation = folded.Position;
            joint.BindRotation = folded.Rotation.Normalized();
            joint.BindScale = folded.Scale;
            roots.push_back(std::move(chain));
        }

        skinLocalToSkeleton.assign(localToSkeleton.begin(), localToSkeleton.end());
        if (!ValidateSkeletonData(out, error))
            return false;
        return CheckRestPalette(data, skin, out, error);
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

    // Reads one channel's keys into a track posing `jointIndex`. Rotation keys
    // are renormalized so the unit-quaternion invariant holds exactly.
    bool ReadChannelTrack(const cgltf_animation_channel& channel,
                          AnimationChannelPath path,
                          uint32_t jointIndex,
                          std::string_view animName,
                          AnimationJointTrack& track,
                          std::string* error)
    {
        const cgltf_animation_sampler* sampler = channel.sampler;
        if (sampler == nullptr || sampler->input == nullptr || sampler->output == nullptr)
            return SetError(error, std::format("animation '{}' has a channel without a sampler",
                                               animName)), false;
        if (sampler->interpolation == cgltf_interpolation_type_cubic_spline)
            return SetError(error, "cubic spline animation interpolation is not supported "
                                   "(re-export with linear or step keys)"), false;

        track.JointIndex = jointIndex;
        track.Path = path;
        track.Interpolation = sampler->interpolation == cgltf_interpolation_type_step
            ? AnimationInterpolation::Step : AnimationInterpolation::Linear;

        const cgltf_size keyCount = sampler->input->count;
        const uint32_t components = AnimationChannelComponentCount(path);
        if (sampler->output->count != keyCount)
            return SetError(error, "animation sampler input/output counts disagree"), false;

        track.TimesSeconds.resize(keyCount);
        track.Values.resize(static_cast<size_t>(keyCount) * components);
        for (cgltf_size k = 0; k < keyCount; ++k)
        {
            if (!cgltf_accessor_read_float(sampler->input, k, &track.TimesSeconds[k], 1))
                return SetError(error, "could not read animation key time"), false;
            float value[4]{};
            if (!cgltf_accessor_read_float(sampler->output, k, value, components))
                return SetError(error, "could not read animation key value"), false;
            if (path == AnimationChannelPath::Rotation)
            {
                const float len = std::sqrt(value[0] * value[0] + value[1] * value[1]
                                            + value[2] * value[2] + value[3] * value[3]);
                if (len > 1e-8f)
                    for (float& component : value)
                        component /= len;
            }
            for (uint32_t component = 0; component < components; ++component)
                track.Values[static_cast<size_t>(k) * components + component] = value[component];
        }
        return true;
    }

    void AppendKey(AnimationJointTrack& track, std::initializer_list<float> values)
    {
        track.Values.insert(track.Values.end(), values);
    }

    // Folds a root joint's ancestors into its tracks for one clip. With the
    // ancestors at rest their composition is one similarity, which composes
    // with each channel on its own, so the source keys stay as they are. An
    // animated ancestor makes the root's model transform a composition of
    // several interpolated channels, which no single channel reproduces; the
    // root is then resampled at every key time any of those channels has,
    // and a root the source never keyed gets tracks of its own.
    bool ComposeRootTracks(const cgltf_data& data,
                           const RootChain& chain,
                           std::span<const cgltf_animation_channel* const> channels,
                           std::string_view animName,
                           AnimationClipData& clip,
                           std::string* error)
    {
        const uint32_t rootSlot = static_cast<uint32_t>(chain.Ancestors.size());
        std::vector<AnimationJointTrack> sources;
        bool ancestorAnimated = false;
        bool allStep = true;
        for (const cgltf_animation_channel* channel : channels)
        {
            const auto at = std::find(chain.Ancestors.begin(), chain.Ancestors.end(),
                                      channel->target_node);
            const uint32_t slot = at == chain.Ancestors.end()
                ? rootSlot : static_cast<uint32_t>(at - chain.Ancestors.begin());
            bool supported = false;
            const AnimationChannelPath path = MapChannelPath(channel->target_path, supported);

            AnimationJointTrack track;
            if (!ReadChannelTrack(*channel, path, slot, animName, track, error))
                return false;
            ancestorAnimated = ancestorAnimated || slot != rootSlot;
            allStep = allStep && track.Interpolation == AnimationInterpolation::Step;
            sources.push_back(std::move(track));
        }

        if (!ancestorAnimated)
        {
            const Transform3f& frame = chain.Frame;
            for (AnimationJointTrack& track : sources)
            {
                const uint32_t components = AnimationChannelComponentCount(track.Path);
                for (size_t key = 0; key < track.TimesSeconds.size(); ++key)
                {
                    float* v = &track.Values[key * components];
                    switch (track.Path)
                    {
                    case AnimationChannelPath::Translation:
                    {
                        const Vec3d p = frame.TransformPoint(Vec3d(v[0], v[1], v[2]));
                        v[0] = p.X; v[1] = p.Y; v[2] = p.Z;
                        break;
                    }
                    case AnimationChannelPath::Rotation:
                    {
                        const Quat<float> q =
                            (frame.Rotation * Quat<float>(v[0], v[1], v[2], v[3])).Normalized();
                        v[0] = q.X; v[1] = q.Y; v[2] = q.Z; v[3] = q.W;
                        break;
                    }
                    case AnimationChannelPath::Scale:
                        v[0] *= frame.Scale.X; v[1] *= frame.Scale.Y; v[2] *= frame.Scale.Z;
                        break;
                    }
                }
                track.JointIndex = chain.Joint;
                clip.Tracks.push_back(std::move(track));
            }
            return true;
        }

        std::vector<float> times;
        for (const AnimationJointTrack& track : sources)
            times.insert(times.end(), track.TimesSeconds.begin(), track.TimesSeconds.end());
        std::sort(times.begin(), times.end());
        times.erase(std::unique(times.begin(), times.end()), times.end());

        // The chain posed as a skeleton of its own, so each node is sampled by
        // exactly the rules the runtime samples a clip by.
        SkeletonData chainRest;
        chainRest.Joints.resize(rootSlot + 1);
        for (uint32_t slot = 0; slot <= rootSlot; ++slot)
        {
            const Transform3f& rest = slot == rootSlot ? chain.RootRest : chain.AncestorRest[slot];
            chainRest.Joints[slot].BindTranslation = rest.Position;
            chainRest.Joints[slot].BindRotation = rest.Rotation;
            chainRest.Joints[slot].BindScale = rest.Scale;
        }
        AnimationClipData chainClip;
        chainClip.Tracks = std::move(sources);

        const AnimationInterpolation interpolation =
            allStep ? AnimationInterpolation::Step : AnimationInterpolation::Linear;
        const auto makeTrack = [&](AnimationChannelPath path) {
            AnimationJointTrack track;
            track.JointIndex = chain.Joint;
            track.Path = path;
            track.Interpolation = interpolation;
            track.TimesSeconds = times;
            return track;
        };
        AnimationJointTrack translation = makeTrack(AnimationChannelPath::Translation);
        AnimationJointTrack rotation = makeTrack(AnimationChannelPath::Rotation);
        AnimationJointTrack scale = makeTrack(AnimationChannelPath::Scale);

        std::vector<Transform3f> pose;
        for (const float time : times)
        {
            SampleAnimationClip(chainClip, chainRest, time, pose);
            const std::span<const Transform3f> ancestors(pose.data(), rootSlot);
            Transform3f frame;
            if (!ComposeAncestors(ancestors, frame))
            {
                return SetError(error, std::format(
                           "animation '{}' at {}s: the nodes above root joint {} (at {}) are not a "
                           "uniform scale, rotation and translation",
                           animName, time, NodeLabel(data, *chain.Node),
                           NodeLabel(data, *chain.Ancestors[IrregularAncestor(ancestors)]))),
                       false;
            }
            const Transform3f model = frame * pose[rootSlot];
            const Quat<float> q = model.Rotation.Normalized();
            AppendKey(translation, { model.Position.X, model.Position.Y, model.Position.Z });
            AppendKey(rotation, { q.X, q.Y, q.Z, q.W });
            AppendKey(scale, { model.Scale.X, model.Scale.Y, model.Scale.Z });
        }

        clip.Tracks.push_back(std::move(translation));
        clip.Tracks.push_back(std::move(rotation));
        clip.Tracks.push_back(std::move(scale));
        return true;
    }

    // Parse a self-contained glTF and load its buffers. No base path is passed:
    // a source must carry its own data (.glb or data: URIs) so the cooked
    // cache's single-source-hash staleness stays honest.
    bool ParseGltf(std::span<const std::byte> bytes, CgltfDataPtr& out, std::string* error)
    {
        cgltf_options options{};
        cgltf_data* rawData = nullptr;
        cgltf_result result = cgltf_parse(&options, bytes.data(), bytes.size(), &rawData);
        if (result != cgltf_result_success)
        {
            SetError(error, "glTF parse failed: " + CgltfResultMessage(result));
            return false;
        }
        out.reset(rawData);

        result = cgltf_load_buffers(&options, out.get(), nullptr);
        if (result != cgltf_result_success)
        {
            SetError(error,
                     result == cgltf_result_unknown_format
                         ? "external buffer URIs are not supported: export a self-contained "
                           ".glb (or embed buffers as data: URIs)"
                         : "glTF buffer load failed: " + CgltfResultMessage(result));
            return false;
        }

        if (out->meshes_count == 0)
        {
            SetError(error, "source contains no meshes");
            return false;
        }

        return true;
    }

    // Bakes an affine transform into geometry: positions by the matrix,
    // normals by its inverse transpose, tangent directions by its linear part.
    // A mirroring transform also flips tangent handedness and reverses
    // triangle winding, so the bitangent and the front face both survive it.
    void BakeTransform(std::span<StaticMeshVertex> vertices,
                       std::span<uint32_t> indices,
                       const Mat4& transform)
    {
        Mat<3, 3> linear;
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                linear.Data[row][col] = transform.Data[row][col];
        const Mat<3, 3> normalTransform = linear.Inverse().Transposed();
        const bool mirrors = linear.Determinant() < 0.0f;

        // A zero-length direction (exporters emit them for degenerate UVs)
        // stays zero rather than being invented.
        const auto unit = [](const Vec3d& v) {
            return v.Magnitude() > 0.0f ? v.Normalized() : v;
        };
        for (StaticMeshVertex& vertex : vertices)
        {
            const Vec4 p = transform * Vec4(vertex.Position.X, vertex.Position.Y, vertex.Position.Z, 1.0f);
            vertex.Position = Vec3d(p.X, p.Y, p.Z);
            vertex.Normal = unit(normalTransform * vertex.Normal);
            const Vec3d tangent =
                unit(linear * Vec3d(vertex.Tangent.X, vertex.Tangent.Y, vertex.Tangent.Z));
            vertex.Tangent = Vec4(tangent.X, tangent.Y, tangent.Z,
                                  mirrors ? -vertex.Tangent.W : vertex.Tangent.W);
        }
        if (mirrors)
            for (size_t i = 0; i + 2 < indices.size(); i += 3)
                std::swap(indices[i + 1], indices[i + 2]);
    }

    bool ValidateGeometry(const MeshGeometry& geometry, std::string_view nameForErrors, std::string* error)
    {
        // Geometry only. The skinning invariants need the skeleton's assigned
        // artifact path, so the serializer validates those once the importer
        // fills SkeletonPath.
        const MeshValidationResult validation = ValidateMeshGeometry(geometry);
        if (validation.IsValid())
            return true;
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

    // Accumulate one glTF mesh's primitives into a single static geometry, one
    // section per primitive, baked into model space by `toModel`, and validate
    // the result.
    bool ReadMeshGeometry(const cgltf_mesh& gltfMesh,
                          std::string_view nameForErrors,
                          const Mat4& toModel,
                          ImportedGltfMesh& imported,
                          std::string* error)
    {
        if (gltfMesh.primitives_count == 0)
        {
            SetError(error, std::format("mesh '{}' has no primitives", nameForErrors));
            return false;
        }

        for (cgltf_size primitiveIndex = 0; primitiveIndex < gltfMesh.primitives_count;
             ++primitiveIndex)
        {
            std::vector<StaticMeshVertex> vertices;
            std::vector<uint32_t> indices;
            if (!ReadPrimitive(gltfMesh.primitives[primitiveIndex], nameForErrors, primitiveIndex,
                               vertices, indices, error))
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

        // After tangent generation, so MikkTSpace sees the source's own
        // texture-space orientation.
        BakeTransform(imported.Geometry.Vertices, imported.Geometry.Indices, toModel);
        RecomputeMeshBounds(imported.Geometry);
        return ValidateGeometry(imported.Geometry, nameForErrors, error);
    }

    // One skeleton's model: every piece it draws, each baked into the
    // skeleton's model space before it joins, grouped into one section per
    // material in first-appearance order so a material is one draw.
    class SkinnedModelBuilder
    {
    public:
        // Skinned geometry: already in the skin's scene space, so only
        // `toModel` (the engine-frame turn) applies; its influences are
        // skin-local and remapped through `remap`.
        bool AddSkinned(const cgltf_mesh& mesh,
                        std::string_view nameForErrors,
                        const Mat4& toModel,
                        std::span<const uint32_t> remap,
                        std::string* error)
        {
            return AddMesh(mesh, nameForErrors, toModel, remap, 0, error);
        }

        // Geometry parented under a joint: bound entirely to `joint`, which is
        // joints {joint,0,0,0} with weights {255,0,0,0}.
        bool AddRigid(const cgltf_mesh& mesh,
                      std::string_view nameForErrors,
                      const Mat4& toModel,
                      uint32_t joint,
                      std::string* error)
        {
            return AddMesh(mesh, nameForErrors, toModel, {}, joint, error);
        }

        [[nodiscard]] bool Empty() const { return Buckets.empty(); }

        bool Finish(std::string_view modelName,
                    uint32_t jointCount,
                    MeshGeometry& geometry,
                    MeshSkinning& skinning,
                    std::string* error)
        {
            if (Buckets.size() > kMaxMeshSections)
                return SetError(error, std::format(
                           "model '{}' uses {} materials; a model draws at most {} (merge materials)",
                           modelName, Buckets.size(), kMaxMeshSections)), false;

            geometry = {};
            skinning = {};
            skinning.JointCount = jointCount;
            for (size_t slot = 0; slot < Buckets.size(); ++slot)
            {
                Bucket& bucket = Buckets[slot];
                StaticMeshSection section;
                section.IndexOffset = static_cast<uint32_t>(geometry.Indices.size());
                section.IndexCount = static_cast<uint32_t>(bucket.Indices.size());
                section.VertexOffset = static_cast<uint32_t>(geometry.Vertices.size());
                section.VertexCount = static_cast<uint32_t>(bucket.Vertices.size());
                section.MaterialSlot = static_cast<uint32_t>(slot);
                geometry.Sections.push_back(section);

                for (const uint32_t index : bucket.Indices)
                    geometry.Indices.push_back(section.VertexOffset + index);
                geometry.Vertices.insert(geometry.Vertices.end(), bucket.Vertices.begin(),
                                         bucket.Vertices.end());
                skinning.Influences.insert(skinning.Influences.end(), bucket.Influences.begin(),
                                           bucket.Influences.end());
            }
            RecomputeMeshBounds(geometry);
            return ValidateGeometry(geometry, modelName, error);
        }

    private:
        struct Bucket
        {
            const cgltf_material* Material = nullptr;
            std::vector<StaticMeshVertex> Vertices;
            std::vector<uint32_t> Indices;
            std::vector<MeshSkinInfluence> Influences;
        };

        // An empty `remap` means rigid: every vertex bound to `rigidJoint`.
        bool AddMesh(const cgltf_mesh& mesh,
                     std::string_view nameForErrors,
                     const Mat4& toModel,
                     std::span<const uint32_t> remap,
                     uint32_t rigidJoint,
                     std::string* error)
        {
            if (mesh.primitives_count == 0)
                return SetError(error, std::format("mesh '{}' has no primitives", nameForErrors)), false;

            const bool skinned = !remap.empty();
            for (cgltf_size primitiveIndex = 0; primitiveIndex < mesh.primitives_count; ++primitiveIndex)
            {
                const cgltf_primitive& primitive = mesh.primitives[primitiveIndex];
                std::vector<StaticMeshVertex> vertices;
                std::vector<uint32_t> indices;
                std::vector<MeshSkinInfluence> influences;
                if (!ReadPrimitive(primitive, nameForErrors, primitiveIndex, vertices, indices, error,
                                   skinned ? &influences : nullptr))
                    return false;
                BakeTransform(vertices, indices, toModel);

                if (skinned)
                {
                    for (MeshSkinInfluence& influence : influences)
                        for (int slot = 0; slot < 4; ++slot)
                        {
                            if (influence.Weights[slot] == 0)
                                continue;
                            if (influence.Joints[slot] >= remap.size())
                                return SetError(error, std::format(
                                           "mesh '{}' references joint {} outside its skin",
                                           nameForErrors, influence.Joints[slot])), false;
                            influence.Joints[slot] = static_cast<uint16_t>(remap[influence.Joints[slot]]);
                        }
                }
                else
                {
                    MeshSkinInfluence rigid{};
                    rigid.Joints[0] = static_cast<uint16_t>(rigidJoint);
                    rigid.Weights[0] = 255;
                    influences.assign(vertices.size(), rigid);
                }

                Bucket& bucket = BucketFor(primitive.material);
                const uint32_t vertexBase = static_cast<uint32_t>(bucket.Vertices.size());
                bucket.Vertices.insert(bucket.Vertices.end(), vertices.begin(), vertices.end());
                for (const uint32_t index : indices)
                    bucket.Indices.push_back(vertexBase + index);
                bucket.Influences.insert(bucket.Influences.end(), influences.begin(), influences.end());
            }
            return true;
        }

        Bucket& BucketFor(const cgltf_material* material)
        {
            for (Bucket& bucket : Buckets)
                if (bucket.Material == material)
                    return bucket;
            Bucket& bucket = Buckets.emplace_back();
            bucket.Material = material;
            return bucket;
        }

        std::vector<Bucket> Buckets;
    };
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

bool ImportGltfScene(std::span<const std::byte> bytes, ImportedGltfScene& out, std::string* error)
{
    out = {};

    CgltfDataPtr data;
    if (!ParseGltf(bytes, data, error))
        return false;

    // Skeletons, one per skin. Keep each skin's skin-local → skeleton remap,
    // and a node → (skin, skeleton-joint) lookup for animation channels.
    // The nodes above each skin's root joints are recorded too: their
    // animation is the skeleton's, folded into its root tracks.
    std::vector<std::vector<uint32_t>> skinRemaps(data->skins_count);
    std::vector<std::vector<RootChain>> skinRoots(data->skins_count);
    // (skin, skeleton joint) for every joint node; a node may be a joint of
    // several skins.
    std::unordered_map<const cgltf_node*, std::vector<std::pair<int, uint32_t>>> jointLookup;
    std::unordered_map<const cgltf_node*, std::vector<int>> ancestorSkins;
    for (cgltf_size skinIndex = 0; skinIndex < data->skins_count; ++skinIndex)
    {
        const cgltf_skin& skin = data->skins[skinIndex];
        ImportedSkeleton skeleton;
        skeleton.Name = skin.name != nullptr && skin.name[0] != '\0'
            ? std::string(skin.name) : std::format("skin{}", skinIndex);
        skeleton.Origin = std::format("skin {} '{}'", skinIndex, skin.name != nullptr ? skin.name : "");
        if (!BuildSkeletonFromSkin(*data, skin, skeleton.Data, skinRemaps[skinIndex],
                                   skinRoots[skinIndex], error))
            return false;

        for (cgltf_size j = 0; j < skin.joints_count; ++j)
        {
            const uint32_t skeletonJoint = skinRemaps[skinIndex][j];
            jointLookup[skin.joints[j]].emplace_back(static_cast<int>(skinIndex), skeletonJoint);
        }
        for (const RootChain& chain : skinRoots[skinIndex])
            for (const cgltf_node* ancestor : chain.Ancestors)
            {
                std::vector<int>& owners = ancestorSkins[ancestor];
                if (std::find(owners.begin(), owners.end(), static_cast<int>(skinIndex)) == owners.end())
                    owners.push_back(static_cast<int>(skinIndex));
            }
        out.Skeletons.push_back(std::move(skeleton));
    }

    // Every placed mesh, by node in glTF order. A skinned node's geometry
    // joins its skin's model; a node beneath a joint joins that joint's
    // model as a rigid part; anything else is a static mesh. Every piece is
    // baked into its destination's model space before it joins.
    const Mat4 engineFrame = GltfToEngineMatrix();
    std::vector<SkinnedModelBuilder> models(data->skins_count);
    std::vector<std::vector<Mat4>> bindModel(data->skins_count);
    for (cgltf_size skinIndex = 0; skinIndex < data->skins_count; ++skinIndex)
        BuildBindModelTransforms(out.Skeletons[skinIndex].Data, bindModel[skinIndex]);

    // A rigid part is baked at rest, so a node between it and its joint may
    // not move on its own.
    std::unordered_map<const cgltf_node*, std::string_view> animatedNodes;
    for (cgltf_size animIndex = 0; animIndex < data->animations_count; ++animIndex)
    {
        const cgltf_animation& animation = data->animations[animIndex];
        for (cgltf_size c = 0; c < animation.channels_count; ++c)
            if (animation.channels[c].target_node != nullptr)
                animatedNodes.try_emplace(animation.channels[c].target_node,
                                          animation.name != nullptr ? animation.name : "<unnamed>");
    }

    // A mesh placed twice with one skin is the same geometry twice: skinned
    // vertices are in the skin's space whatever node places them. That is
    // the one occurrence the cook folds.
    std::vector<std::pair<const cgltf_mesh*, const cgltf_skin*>> skinnedOccurrences;
    std::vector<bool> meshPlaced(data->meshes_count, false);
    for (cgltf_size nodeIndex = 0; nodeIndex < data->nodes_count; ++nodeIndex)
    {
        const cgltf_node& node = data->nodes[nodeIndex];
        if (node.mesh == nullptr)
            continue;
        meshPlaced[cgltf_mesh_index(data.get(), node.mesh)] = true;
        const std::string_view meshName = node.mesh->name != nullptr
            ? std::string_view(node.mesh->name) : std::string_view("<unnamed>");

        if (node.skin != nullptr)
        {
            const std::pair<const cgltf_mesh*, const cgltf_skin*> occurrence{ node.mesh, node.skin };
            if (std::find(skinnedOccurrences.begin(), skinnedOccurrences.end(), occurrence)
                != skinnedOccurrences.end())
                continue;
            skinnedOccurrences.push_back(occurrence);
            const size_t skinIndex = cgltf_skin_index(data.get(), node.skin);
            if (!models[skinIndex].AddSkinned(*node.mesh, meshName, engineFrame, skinRemaps[skinIndex], error))
                return false;
            continue;
        }

        // The nearest joint at or above the node: a mesh on a joint node is
        // a part of that joint.
        const cgltf_node* joint = &node;
        while (joint != nullptr && !jointLookup.contains(joint))
            joint = joint->parent;
        if (joint != nullptr)
        {
            const std::vector<std::pair<int, uint32_t>>& owners = jointLookup.at(joint);
            if (owners.size() > 1)
                return SetError(error, std::format(
                           "node {} hangs from joint {}, which belongs to {} skins; the cook cannot "
                           "tell which model it is part of",
                           NodeLabel(*data, node), NodeLabel(*data, *joint), owners.size())), false;
            const auto [skinIndex, skeletonJoint] = owners.front();

            // R: the rest transforms from beneath the joint down to the mesh.
            Mat4 fromJoint = Mat4::Identity();
            for (const cgltf_node* link = &node; link != joint; link = link->parent)
            {
                if (auto it = animatedNodes.find(link); it != animatedNodes.end())
                    return SetError(error, std::format(
                               "animation '{}' moves node {}, which carries the rigid part {} on "
                               "joint {}; a part is bound to its joint at rest, so animate the "
                               "joint instead",
                               it->second, NodeLabel(*data, *link), NodeLabel(*data, node),
                               NodeLabel(*data, *joint))), false;
                float local[16]{};
                cgltf_node_transform_local(link, local);
                fromJoint = GltfMat4ToRowMajor(local) * fromJoint;
            }
            const Mat4 toModel = bindModel[skinIndex][skeletonJoint] * fromJoint;
            if (!models[skinIndex].AddRigid(*node.mesh, meshName, toModel, skeletonJoint, error))
                return false;
            continue;
        }

        const std::string nodeName = node.name != nullptr ? node.name : "";
        ImportedGltfMesh imported;
        imported.Name = nodeName.empty() ? std::format("node{}", nodeIndex) : nodeName;
        imported.Origin = std::format("node {} '{}'", nodeIndex, nodeName);
        float world[16]{};
        cgltf_node_transform_world(&node, world);
        if (!ReadMeshGeometry(*node.mesh, meshName, engineFrame * GltfMat4ToRowMajor(world),
                              imported, error))
            return false;
        out.StaticMeshes.push_back(std::move(imported));
    }

    // A mesh no node places is not part of the scene; importing it anyway
    // would mean guessing where it goes.
    for (cgltf_size meshIndex = 0; meshIndex < data->meshes_count; ++meshIndex)
    {
        if (meshPlaced[meshIndex])
            continue;
        const char* name = data->meshes[meshIndex].name;
        SetError(error, std::format(
            "mesh {} '{}' is not placed by any node; the cook imports what the scene places",
            meshIndex, name != nullptr ? name : ""));
        return false;
    }

    for (cgltf_size skinIndex = 0; skinIndex < data->skins_count; ++skinIndex)
    {
        if (models[skinIndex].Empty())
            continue;
        const ImportedSkeleton& skeleton = out.Skeletons[skinIndex];
        ImportedSkinnedModel model;
        model.Name = skeleton.Name;
        model.Origin = skeleton.Origin;
        model.SkinIndex = static_cast<int>(skinIndex);
        if (!models[skinIndex].Finish(skeleton.Name, static_cast<uint32_t>(skeleton.Data.Joints.size()),
                                      model.Geometry, model.Skinning, error))
            return false;
        out.SkinnedModels.push_back(std::move(model));
    }

    // Animations — each becomes one clip on the single skeleton its channels
    // pose, through its joints or through the nodes above its root joints. A
    // clip whose channels span more than one skin (a multi-character export)
    // is ambiguous and rejected rather than silently truncated; channels on
    // any other node are node animation, out of scope here.
    for (cgltf_size animIndex = 0; animIndex < data->animations_count; ++animIndex)
    {
        const cgltf_animation& animation = data->animations[animIndex];
        const std::string_view animName =
            animation.name != nullptr ? std::string_view(animation.name) : std::string_view("<unnamed>");

        std::vector<int> animSkins;
        const auto noteSkin = [&animSkins](int skin) {
            if (std::find(animSkins.begin(), animSkins.end(), skin) == animSkins.end())
                animSkins.push_back(skin);
        };
        for (cgltf_size c = 0; c < animation.channels_count; ++c)
        {
            const cgltf_animation_channel& channel = animation.channels[c];
            bool supportedPath = false;
            MapChannelPath(channel.target_path, supportedPath);
            if (channel.target_node == nullptr || !supportedPath)
                continue;
            if (auto it = jointLookup.find(channel.target_node); it != jointLookup.end())
                for (const auto& [skin, joint] : it->second)
                    noteSkin(skin);
            if (auto it = ancestorSkins.find(channel.target_node); it != ancestorSkins.end())
                for (const int skin : it->second)
                    noteSkin(skin);
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
        const std::vector<RootChain>& roots = skinRoots[animSkin];

        ImportedAnimation imported;
        imported.Name = animation.name != nullptr && animation.name[0] != '\0'
            ? std::string(animation.name) : std::format("animation{}", animIndex);
        imported.Origin = std::format("animation {} '{}'", animIndex,
                                      animation.name != nullptr ? animation.name : "");
        imported.SkinIndex = animSkin;
        AnimationClipData& clip = imported.Data;

        // Tracks on non-root joints are the source's own; a root joint's
        // channels and its ancestors' are gathered and composed together.
        std::vector<std::vector<const cgltf_animation_channel*>> rootChannels(roots.size());
        for (cgltf_size c = 0; c < animation.channels_count; ++c)
        {
            const cgltf_animation_channel& channel = animation.channels[c];
            bool supportedPath = false;
            const AnimationChannelPath path = MapChannelPath(channel.target_path, supportedPath);
            if (channel.target_node == nullptr || !supportedPath)
                continue; // morph-target weights, etc.

            if (auto it = jointLookup.find(channel.target_node); it != jointLookup.end())
            {
                const uint32_t joint = it->second.front().second;
                const auto root = std::find_if(roots.begin(), roots.end(),
                                               [joint](const RootChain& chain) { return chain.Joint == joint; });
                if (root != roots.end())
                {
                    rootChannels[static_cast<size_t>(root - roots.begin())].push_back(&channel);
                    continue;
                }
                AnimationJointTrack track;
                if (!ReadChannelTrack(channel, path, joint, animName, track, error))
                    return false;
                clip.Tracks.push_back(std::move(track));
                continue;
            }

            for (size_t r = 0; r < roots.size(); ++r)
                if (std::find(roots[r].Ancestors.begin(), roots[r].Ancestors.end(),
                              channel.target_node) != roots[r].Ancestors.end())
                    rootChannels[r].push_back(&channel);
        }
        for (size_t r = 0; r < roots.size(); ++r)
            if (!rootChannels[r].empty()
                && !ComposeRootTracks(*data, roots[r], rootChannels[r], animName, clip, error))
                return false;

        if (clip.Tracks.empty())
            continue;
        for (const AnimationJointTrack& track : clip.Tracks)
            if (!track.TimesSeconds.empty())
                clip.DurationSeconds = std::max(clip.DurationSeconds, track.TimesSeconds.back());
        out.Animations.push_back(std::move(imported));
    }

    return true;
}

std::vector<std::string_view> GltfMeshImporter::SourceExtensions() const
{
    return { ".glb", ".gltf" };
}

// Bump whenever this importer's output changes for the same source bytes.
// Every artifact it cooked is then stale and recooks on the next pass.
std::uint64_t GltfMeshImporter::CookIdentity() const
{
    return CookFingerprint("gltf_mesh", kGltfMeshCookVersion).Value();
}

ImportResult GltfMeshImporter::Import(const ImportInput& input, ICookOutputWriter& output)
{
    ImportedGltfScene scene;
    std::string error;
    if (!ImportGltfScene(input.Bytes, scene, &error))
        return ImportResult{ .Error = "gltf import: " + error };

    // Clip events come from the sidecar, by the name the clip's artifact
    // takes. One naming a clip the source does not export is an error: the
    // clip was renamed or removed, and its events would otherwise vanish.
    MeshImportSettings settings;
    if (!ParseMeshImportSettings(input.MetaBytes, settings, &error))
        return ImportResult{ .Error = "gltf import: " + std::string(input.SourceRelPath) + ".meta: " + error };
    for (auto& [clip, events] : settings.ClipEvents)
    {
        const auto animation = std::ranges::find_if(scene.Animations, [&clip](const ImportedAnimation& candidate) {
            return SanitizeMeshName(candidate.Name) == clip;
        });
        if (animation == scene.Animations.end())
        {
            std::string exported;
            for (const ImportedAnimation& candidate : scene.Animations)
                exported += (exported.empty() ? "" : ", ") + SanitizeMeshName(candidate.Name);
            return ImportResult{ .Error = std::format(
                "gltf import: {}.meta gives events to clip '{}', which the source does not export (it exports: {})",
                input.SourceRelPath, clip, exported.empty() ? "no clips" : exported) };
        }
        std::ranges::sort(events, [](const AnimationClipEvent& a, const AnimationClipEvent& b) {
            return a.Time != b.Time ? a.Time < b.Time : a.Key < b.Key;
        });
        animation->Data.Events = std::move(events);
    }

    const std::string source(input.SourceRelPath);
    const std::string virtualPrefix = "asset://" + source;
    const std::string fileBase = ".cooked/" + source;

    // A source that is one static mesh keeps the source's virtual path;
    // everything else takes a '#'-suffixed artifact name ('#' can't appear in
    // scanned paths, so cooked names never collide with real files).
    const bool singleStaticMesh = scene.StaticMeshes.size() == 1 && scene.Skeletons.empty();

    // Artifact names are the author's names, sanitized, with an index for an
    // unnamed element. Two elements landing on one name is an error rather
    // than a suffix by discovery order, so an artifact's name never depends
    // on what else the source contains.
    std::unordered_map<std::string, std::string> claimedNames;
    const auto claimName = [&claimedNames](std::string name, const std::string& origin,
                                           std::string& outError) -> std::optional<std::string> {
        const auto [it, inserted] = claimedNames.try_emplace(name, origin);
        if (inserted)
            return name;
        outError = std::format(
            "gltf import: {} and {} both name the artifact '{}'; rename one of them",
            it->second, origin, name);
        return std::nullopt;
    };

    // Skeleton artifact paths, indexed by skin, so meshes and clips can
    // reference them.
    std::vector<std::string> skeletonPaths(scene.Skeletons.size());

    ImportResult result;
    LoggingProvider silentLogging; // importers report, never log.

    // -- Skeletons --
    for (size_t i = 0; i < scene.Skeletons.size(); ++i)
    {
        const std::optional<std::string> unique = claimName(
            "skel:" + SanitizeMeshName(scene.Skeletons[i].Name), scene.Skeletons[i].Origin, error);
        if (!unique)
            return ImportResult{ .Error = error };

        std::vector<std::byte> bytes;
        if (!WriteSskelToBytes(scene.Skeletons[i].Data, bytes, &error))
            return ImportResult{ .Error = "gltf import: .sskel serialization failed: " + error };

        CookedArtifact artifact;
        artifact.Path = virtualPrefix + "#" + *unique;
        artifact.FileRelPath = fileBase + "." + *unique + ".sskel";
        artifact.Type = AssetType::Skeleton;
        skeletonPaths[i] = artifact.Path;

        if (!output.WriteBytes(artifact.FileRelPath, bytes))
            return ImportResult{ .Error = "gltf import: artifact write failed for '" + artifact.FileRelPath + "'" };
        result.Artifacts.push_back(std::move(artifact));
    }

    // -- Meshes --
    // A skeleton's model emits a `.skmesh` (AssetType::SkinnedMesh)
    // referencing the skeleton; a static mesh emits a `.smesh`
    // (AssetType::StaticMesh). The kind is path-level: the extension and
    // asset type distinguish them without reading the payload.
    const auto emit = [&](std::string_view fragment, std::string_view extension, AssetType type,
                          std::span<const std::byte> bytes) -> bool {
        CookedArtifact artifact;
        artifact.Path = fragment.empty() ? virtualPrefix : virtualPrefix + "#" + std::string(fragment);
        artifact.FileRelPath = fragment.empty()
            ? fileBase + std::string(extension)
            : fileBase + "." + std::string(fragment) + std::string(extension);
        artifact.Type = type;
        if (!output.WriteBytes(artifact.FileRelPath, bytes))
        {
            error = "gltf import: artifact write failed for '" + artifact.FileRelPath + "'";
            return false;
        }
        result.Artifacts.push_back(std::move(artifact));
        return true;
    };

    MeshSerializer serializer(silentLogging);
    for (ImportedSkinnedModel& model : scene.SkinnedModels)
    {
        const std::optional<std::string> unique =
            claimName("model:" + SanitizeMeshName(model.Name), model.Origin, error);
        if (!unique)
            return ImportResult{ .Error = error };

        model.Skinning.SkeletonPath = skeletonPaths[model.SkinIndex];
        SkinnedMeshData skinnedData{ std::move(model.Geometry), std::move(model.Skinning) };
        std::vector<std::byte> bytes;
        if (!serializer.WriteSkinnedToBytes(skinnedData, bytes))
            return ImportResult{ .Error = std::format(
                "gltf import: .skmesh serialization failed for {}", model.Origin) };
        if (!emit(*unique, ".skmesh", AssetType::SkinnedMesh, bytes))
            return ImportResult{ .Error = error };
    }

    for (const ImportedGltfMesh& mesh : scene.StaticMeshes)
    {
        std::vector<std::byte> bytes;
        if (!serializer.WriteToBytes(mesh.Geometry, bytes))
            return ImportResult{ .Error = std::format(
                "gltf import: .smesh serialization failed for {}", mesh.Origin) };

        std::string fragment;
        if (!singleStaticMesh)
        {
            const std::optional<std::string> unique =
                claimName(SanitizeMeshName(mesh.Name), mesh.Origin, error);
            if (!unique)
                return ImportResult{ .Error = error };
            fragment = *unique;
        }
        if (!emit(fragment, ".smesh", AssetType::StaticMesh, bytes))
            return ImportResult{ .Error = error };
    }

    // -- Animations --
    for (size_t animIndex = 0; animIndex < scene.Animations.size(); ++animIndex)
    {
        ImportedAnimation& animation = scene.Animations[animIndex];
        animation.Data.SkeletonPath = skeletonPaths[animation.SkinIndex];

        std::vector<std::byte> bytes;
        if (!WriteSanimToBytes(animation.Data, bytes, &error))
            return ImportResult{ .Error = "gltf import: .sanim serialization failed: " + error };

        const std::optional<std::string> unique =
            claimName(std::string(kMeshClipFragmentPrefix) + SanitizeMeshName(animation.Name), animation.Origin, error);
        if (!unique)
            return ImportResult{ .Error = error };
        CookedArtifact artifact;
        artifact.Path = virtualPrefix + "#" + *unique;
        artifact.FileRelPath = fileBase + "." + *unique + ".sanim";
        artifact.Type = AssetType::AnimationClip;

        if (!output.WriteBytes(artifact.FileRelPath, bytes))
            return ImportResult{ .Error = "gltf import: artifact write failed for '" + artifact.FileRelPath + "'" };
        result.Artifacts.push_back(std::move(artifact));
    }

    return result;
}
