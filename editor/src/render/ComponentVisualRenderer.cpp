#include "ComponentVisualRenderer.h"

#include "../EditorTheme.h"

#include <world/serialization/IComponentSerializer.h>
#include <world/serialization/SceneSerializer.h>

#ifdef SENCHA_ENABLE_COOK
#include <assets/cook/MeshCook.h>
#endif

#include <cstddef>
#include <fstream>
#include <set>
#include <vector>

namespace
{
std::vector<std::byte> ReadFileBytes(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream.is_open())
        return {};
    const std::streamoff size = stream.tellg();
    if (size <= 0)
        return {};
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    stream.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}
}

ComponentVisualRenderer::ComponentVisualRenderer(LevelScene& scene, EditorLinePipeline& lines)
    : Scene(scene)
    , Lines(lines)
{
}

const ComponentVisualRenderer::MeshEdges& ComponentVisualRenderer::EdgesFor(std::string_view assetPath)
{
    const std::string key(assetPath);
    if (const auto it = Cache.find(key); it != Cache.end())
        return it->second;

    MeshEdges edges; // empty on any failure — cached so we don't retry each frame

#ifdef SENCHA_ENABLE_COOK
    const std::vector<std::byte> bytes = ReadFileBytes(std::string(SENCHA_EDITOR_ASSET_DIR) + "/" + key);
    if (!bytes.empty())
    {
        std::vector<ImportedGltfMesh> meshes;
        if (ImportGltfMeshes(bytes, meshes))
        {
            std::set<std::pair<std::uint32_t, std::uint32_t>> unique;
            for (const ImportedGltfMesh& mesh : meshes)
            {
                const auto base = static_cast<std::uint32_t>(edges.Positions.size());
                for (const StaticMeshVertex& vertex : mesh.Geometry.Vertices)
                    edges.Positions.push_back(vertex.Position);

                const std::vector<std::uint32_t>& indices = mesh.Geometry.Indices;
                for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
                {
                    const std::uint32_t tri[3] = {
                        base + indices[i], base + indices[i + 1], base + indices[i + 2] };
                    for (int e = 0; e < 3; ++e)
                    {
                        std::uint32_t a = tri[e];
                        std::uint32_t b = tri[(e + 1) % 3];
                        if (a > b)
                            std::swap(a, b);
                        unique.emplace(a, b);
                    }
                }
            }
            edges.Edges.assign(unique.begin(), unique.end());
        }
    }
#endif

    return Cache.emplace(key, std::move(edges)).first->second;
}

void ComponentVisualRenderer::DrawViewport(const FrameContext& frame, const EditorViewport& viewport)
{
    const Registry& registry = Scene.GetRegistry();
    const auto& serializers = GetComponentSerializerEntries();

    std::vector<EditorLineVertex> vertices;
    for (EntityId entity : Scene.GetAllEntities())
    {
        if (!Scene.IsEntityVisible(entity))
            continue;

        const Transform3f* transform = Scene.TryGetTransform(entity);
        if (transform == nullptr)
            continue;

        for (const auto& serializer : serializers)
        {
            if (!serializer->HasComponent(entity, registry))
                continue;
            const std::optional<EditorVisual> visual = serializer->GetEditorVisual();
            if (!visual.has_value() || visual->VisualKind != EditorVisual::Kind::Mesh)
                continue;

            const MeshEdges& edges = EdgesFor(visual->AssetPath);
            for (const auto& [a, b] : edges.Edges)
            {
                vertices.push_back(EditorLineVertex{ .Position = transform->TransformPoint(edges.Positions[a]), .Color = EditorTheme::ComponentVisual });
                vertices.push_back(EditorLineVertex{ .Position = transform->TransformPoint(edges.Positions[b]), .Color = EditorTheme::ComponentVisual });
            }
        }
    }

    Lines.Submit(frame, viewport, vertices);
}
