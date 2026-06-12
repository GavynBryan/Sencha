#include <assets/cook/MeshCook.h>

#include <assets/static_mesh/StaticMeshSerializer.h>
#include <core/hash/ContentHash.h>
#include <core/logging/LoggingProvider.h>
#include <render/static_mesh/StaticMeshValidation.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <mikktspace.h>

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

    bool ReadPrimitive(const cgltf_primitive& primitive,
                       std::string_view meshName,
                       size_t primitiveIndex,
                       std::vector<StaticMeshVertex>& vertices,
                       std::vector<uint32_t>& indices,
                       std::string* error)
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

        const cgltf_size vertexCount = positions->count;
        vertices.resize(vertexCount);
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

        // Decision M: every cooked vertex carries a tangent.
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

            StaticMeshData& mesh = imported.Data;
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

        RecomputeStaticMeshBounds(imported.Data);
        const StaticMeshValidationResult validation = ValidateStaticMeshData(imported.Data);
        if (!validation.IsValid())
        {
            std::string joined;
            for (const StaticMeshValidationError& validationError : validation.Errors)
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

std::vector<std::string_view> GltfMeshImporter::SourceExtensions() const
{
    return { ".glb", ".gltf" };
}

ImportResult GltfMeshImporter::Import(const ImportInput& input, ICookOutputWriter& output)
{
    std::vector<ImportedGltfMesh> meshes;
    std::string error;
    if (!ImportGltfMeshes(input.Bytes, meshes, &error))
        return ImportResult{ .Error = "gltf import: " + error };

    // The meshes were validated by ImportGltfMeshes; the serializer re-checks
    // through a sink-less local provider because importers do not log.
    LoggingProvider silentLogging;
    StaticMeshSerializer serializer(silentLogging);

    ImportResult result;
    std::unordered_set<std::string> usedNames;
    for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex)
    {
        const ImportedGltfMesh& mesh = meshes[meshIndex];

        std::vector<std::byte> smeshBytes;
        if (!serializer.WriteToBytes(mesh.Data, smeshBytes))
            return ImportResult{ .Error = std::format("gltf import: .smesh serialization failed for mesh {}", meshIndex) };

        std::string name = SanitizeMeshName(mesh.Name);
        if (name.empty())
            name = std::format("mesh{}", meshIndex);
        while (!usedNames.insert(name).second)
            name += std::format("_{}", meshIndex);

        CookedArtifact artifact;
        if (meshes.size() == 1)
        {
            // The common one-prop-per-file case keeps the source's virtual
            // path, so references never churn as the cooked format evolves.
            artifact.Path = "asset://" + std::string(input.SourceRelPath);
            artifact.FileRelPath = ".cooked/" + std::string(input.SourceRelPath) + ".smesh";
        }
        else
        {
            artifact.Path = "asset://" + std::string(input.SourceRelPath) + "#" + name;
            artifact.FileRelPath = ".cooked/" + std::string(input.SourceRelPath) + "." + name + ".smesh";
        }
        artifact.Type = AssetType::StaticMesh;

        if (!output.WriteBytes(artifact.FileRelPath, smeshBytes))
            return ImportResult{ .Error = "gltf import: artifact write failed for '" + artifact.FileRelPath + "'" };

        result.Artifacts.push_back(std::move(artifact));
    }

    return result;
}
