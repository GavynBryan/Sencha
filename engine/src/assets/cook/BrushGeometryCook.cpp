#include <assets/cook/BrushGeometryCook.h>

#include <assets/cook/MeshCook.h> // GenerateSectionTangents

#include <cstdint>

namespace
{
    // Material identity for grouping/slotting is the virtual path: two faces share
    // a section iff they resolve to the same material asset.
    bool SameMaterial(const AssetRef& a, const AssetRef& b)
    {
        return a.Path == b.Path;
    }
}

std::vector<AssetRef> CollectMaterialOrder(std::span<const CookFace> faces)
{
    std::vector<AssetRef> order;
    for (const CookFace& face : faces)
    {
        bool seen = false;
        for (const AssetRef& known : order)
        {
            if (SameMaterial(known, face.Material))
            {
                seen = true;
                break;
            }
        }
        if (!seen)
            order.push_back(face.Material);
    }
    return order;
}

bool BakeBrushFacesToStaticMesh(std::span<const CookFace> faces,
                                std::span<const AssetRef> materialOrder,
                                MeshGeometry& out,
                                std::string* error)
{
    out = MeshGeometry{};

    // Every face's material must have a slot, or the section grouping is silently
    // lossy — reject rather than drop geometry.
    for (const CookFace& face : faces)
    {
        bool found = false;
        for (const AssetRef& mat : materialOrder)
        {
            if (SameMaterial(mat, face.Material))
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            if (error)
                *error = "BakeBrushFacesToStaticMesh: face material '" + face.Material.Path
                    + "' is absent from the material order";
            return false;
        }
    }

    Aabb3d meshBounds = Aabb3d::Empty();

    for (std::uint32_t slot = 0; slot < materialOrder.size(); ++slot)
    {
        const AssetRef& material = materialOrder[slot];

        // Gather this material's triangles into a de-indexed run.
        std::vector<StaticMeshVertex> vertices;
        for (const CookFace& face : faces)
        {
            if (!SameMaterial(face.Material, material))
                continue;
            vertices.insert(vertices.end(), face.Triangles.begin(), face.Triangles.end());
        }
        if (vertices.empty())
            continue; // a slotted-but-unused material contributes no section

        std::vector<std::uint32_t> indices(vertices.size());
        for (std::uint32_t i = 0; i < indices.size(); ++i)
            indices[i] = i;

        // MikkTSpace tangents from the baked UVs; this also welds exact-duplicate
        // vertices, so the section's stream is indexed, not a raw triangle soup.
        if (!GenerateSectionTangents(vertices, indices, error))
            return false;

        Aabb3d sectionBounds = Aabb3d::Empty();
        for (const StaticMeshVertex& v : vertices)
            sectionBounds.ExpandToInclude(v.Position);
        meshBounds.ExpandToInclude(sectionBounds);

        StaticMeshSection section;
        section.VertexOffset = static_cast<std::uint32_t>(out.Vertices.size());
        section.VertexCount = static_cast<std::uint32_t>(vertices.size());
        section.IndexOffset = static_cast<std::uint32_t>(out.Indices.size());
        section.IndexCount = static_cast<std::uint32_t>(indices.size());
        section.MaterialSlot = slot;
        section.LocalBounds = sectionBounds;

        // Indices are section-local from GenerateSectionTangents; rebase onto the
        // shared vertex stream as we append.
        const std::uint32_t base = section.VertexOffset;
        out.Vertices.insert(out.Vertices.end(), vertices.begin(), vertices.end());
        for (std::uint32_t idx : indices)
            out.Indices.push_back(base + idx);
        out.Sections.push_back(section);
    }

    out.LocalBounds = meshBounds;
    return true;
}
