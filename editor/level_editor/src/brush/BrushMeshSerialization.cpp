#include "BrushMeshSerialization.h"

#include "BrushModifierSerialization.h"
#include "BrushValidation.h"

#include <core/assets/AssetRefCodec.h>
#include <core/json/JsonValue.h>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace
{
    JsonValue Vec3ToJson(Vec3d v)
    {
        return JsonValue(JsonValue::Array{
            JsonValue(static_cast<double>(v.X)),
            JsonValue(static_cast<double>(v.Y)),
            JsonValue(static_cast<double>(v.Z)) });
    }

    JsonValue Vec2ToJson(Vec2d v)
    {
        return JsonValue(JsonValue::Array{
            JsonValue(static_cast<double>(v.X)),
            JsonValue(static_cast<double>(v.Y)) });
    }

    bool LeadingNumbers(const JsonValue& v, std::size_t count)
    {
        if (!v.IsArray() || v.Size() < count)
            return false;
        for (std::size_t i = 0; i < count; ++i)
            if (!v.AsArray()[i].IsNumber())
                return false;
        return true;
    }

    std::optional<Vec3d> Vec3FromJson(const JsonValue& v)
    {
        if (!LeadingNumbers(v, 3))
            return std::nullopt;
        const JsonValue::Array& a = v.AsArray();
        return Vec3d{ static_cast<float>(a[0].AsNumber()),
                      static_cast<float>(a[1].AsNumber()),
                      static_cast<float>(a[2].AsNumber()) };
    }

    Vec3d Vec3FromJson(const JsonValue& v, Vec3d fallback)
    {
        return Vec3FromJson(v).value_or(fallback);
    }

    Vec2d Vec2FromJson(const JsonValue& v, Vec2d fallback)
    {
        if (!LeadingNumbers(v, 2))
            return fallback;
        const JsonValue::Array& a = v.AsArray();
        return Vec2d{ static_cast<float>(a[0].AsNumber()),
                      static_cast<float>(a[1].AsNumber()) };
    }

    std::optional<std::uint32_t> IndexFromJson(const JsonValue& v)
    {
        if (!v.IsNumber())
            return std::nullopt;
        const double number = v.AsNumber();
        if (!(number >= 0.0) || number > std::numeric_limits<std::uint32_t>::max()
            || std::floor(number) != number)
            return std::nullopt;
        return static_cast<std::uint32_t>(number);
    }

    // False when any entry is not the index of an existing vertex.
    bool LoopFromJson(const JsonValue& v, std::size_t vertexCount, std::vector<std::uint32_t>& loop)
    {
        if (!v.IsArray())
            return false;
        for (const JsonValue& entry : v.AsArray())
        {
            const std::optional<std::uint32_t> index = IndexFromJson(entry);
            if (!index || *index >= vertexCount)
                return false;
            loop.push_back(*index);
        }
        return true;
    }

    void AppendProblem(std::string* problems, const std::string& problem)
    {
        if (problems == nullptr)
            return;
        if (!problems->empty())
            problems->push_back('\n');
        problems->append(problem);
    }

    JsonValue FaceMaterialToJson(const FaceMaterial& fm)
    {
        JsonValue::Object uv;
        uv.emplace_back("axis_u", Vec3ToJson(fm.Uv.AxisU));
        uv.emplace_back("axis_v", Vec3ToJson(fm.Uv.AxisV));
        uv.emplace_back("scale", Vec2ToJson(fm.Uv.Scale));
        uv.emplace_back("offset", Vec2ToJson(fm.Uv.Offset));
        uv.emplace_back("rotation", JsonValue(static_cast<double>(fm.Uv.Rotation)));
        uv.emplace_back("world_aligned", JsonValue(fm.Uv.WorldAligned));

        JsonValue::Object obj;
        if (!fm.Material.Path.empty() || fm.Material.Id.IsValid()) // none => inherit the level default
            obj.emplace_back("material", AssetRefToJson(fm.Material));
        obj.emplace_back("uv", JsonValue(std::move(uv)));
        return JsonValue(std::move(obj));
    }

    FaceMaterial FaceMaterialFromJson(const JsonValue& value)
    {
        FaceMaterial fm;
        if (const JsonValue* material = value.Find("material"))
            if (std::optional<AssetRef> ref = AssetRefFromJson(*material))
            {
                fm.Material = std::move(*ref);
                fm.Material.Type = AssetType::Material;
            }
        if (const JsonValue* uv = value.Find("uv"); uv && uv->IsObject())
        {
            const UvProjection def;
            if (const JsonValue* v = uv->Find("axis_u"))
                fm.Uv.AxisU = Vec3FromJson(*v, def.AxisU);
            if (const JsonValue* v = uv->Find("axis_v"))
                fm.Uv.AxisV = Vec3FromJson(*v, def.AxisV);
            if (const JsonValue* v = uv->Find("scale"))
                fm.Uv.Scale = Vec2FromJson(*v, def.Scale);
            if (const JsonValue* v = uv->Find("offset"))
                fm.Uv.Offset = Vec2FromJson(*v, def.Offset);
            if (const JsonValue* v = uv->Find("rotation"); v && v->IsNumber())
                fm.Uv.Rotation = static_cast<float>(v->AsNumber());
            if (const JsonValue* v = uv->Find("world_aligned"); v && v->IsBool())
                fm.Uv.WorldAligned = v->AsBool();
        }
        return fm;
    }
}

JsonValue BrushMeshToJson(const BrushMesh& mesh)
{
    JsonValue::Array vertices;
    vertices.reserve(mesh.Vertices.size());
    for (const BrushVertex& vertex : mesh.Vertices)
        vertices.push_back(Vec3ToJson(vertex.Position));

    JsonValue::Array faces;
    faces.reserve(mesh.Faces.size());
    for (const BrushFace& face : mesh.Faces)
    {
        JsonValue::Array loop;
        loop.reserve(face.Loop.size());
        for (std::uint32_t index : face.Loop)
            loop.push_back(JsonValue(static_cast<int>(index)));

        JsonValue obj = FaceMaterialToJson(face.Material);
        obj.AsObject().emplace_back("loop", JsonValue(std::move(loop)));
        faces.push_back(std::move(obj));
    }

    JsonValue::Object obj;
    obj.emplace_back("vertices", JsonValue(std::move(vertices)));
    obj.emplace_back("faces", JsonValue(std::move(faces)));
    if (!mesh.SoftEdges.empty())
    {
        JsonValue::Array soft;
        soft.reserve(mesh.SoftEdges.size() * 2);
        for (const std::array<std::uint32_t, 2>& edge : mesh.SoftEdges)
        {
            soft.push_back(JsonValue(static_cast<int>(edge[0])));
            soft.push_back(JsonValue(static_cast<int>(edge[1])));
        }
        obj.emplace_back("soft_edges", JsonValue(std::move(soft)));
    }
    return JsonValue(std::move(obj));
}

BrushMesh BrushMeshFromJson(const JsonValue& value, std::string* problems)
{
    BrushMesh mesh;
    if (const JsonValue* vertices = value.Find("vertices"); vertices && vertices->IsArray())
    {
        for (const JsonValue& p : vertices->AsArray())
        {
            // A skipped vertex would renumber every later one under the faces
            // that index them, so a malformed vertex rejects the whole mesh.
            const std::optional<Vec3d> position = Vec3FromJson(p);
            if (!position)
            {
                AppendProblem(problems, "vertex " + std::to_string(mesh.Vertices.size())
                                            + " is not three numbers; brush dropped");
                return {};
            }
            mesh.Vertices.push_back(BrushVertex{ *position });
        }
    }
    if (const JsonValue* faces = value.Find("faces"); faces && faces->IsArray())
    {
        const JsonValue::Array& entries = faces->AsArray();
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            const JsonValue& f = entries[i];
            BrushFace face;
            const JsonValue* loop = f.IsArray() ? &f : (f.IsObject() ? f.Find("loop") : nullptr);
            if (loop == nullptr || !LoopFromJson(*loop, mesh.Vertices.size(), face.Loop))
            {
                AppendProblem(problems, "face " + std::to_string(i)
                                            + " has a loop entry that is not a vertex index; face dropped");
                continue;
            }
            if (f.IsObject())
                face.Material = FaceMaterialFromJson(f);
            mesh.Faces.push_back(std::move(face));
        }
    }
    // Flat pair list [a0, b0, a1, b1, ...]; absent in older documents (all hard).
    // A pair naming no edge of the mesh is pruned by the repair below.
    if (const JsonValue* soft = value.Find("soft_edges"); soft && soft->IsArray())
    {
        const JsonValue::Array& a = soft->AsArray();
        for (std::size_t i = 0; i + 1 < a.size(); i += 2)
        {
            const std::optional<std::uint32_t> first = IndexFromJson(a[i]);
            const std::optional<std::uint32_t> second = IndexFromJson(a[i + 1]);
            if (first && second)
                mesh.SoftEdges.push_back(BrushSoftEdgeKey(*first, *second));
        }
    }
    // Part of parsing, not a courtesy for the caller: the cached face normals
    // are data the tessellator reads, and a mesh without them lights as black
    // rather than failing. Every route from serialized data to a live mesh
    // goes through here, so the repair has exactly one owner.
    BrushValidateAndRepair(mesh);
    return mesh;
}

JsonValue SerializeBrushMeshes(const BrushMeshStore& store)
{
    JsonValue::Object obj;
    for (const auto& [id, record] : store.All())
        obj.emplace_back(std::to_string(id), BrushRecordToJson(record));
    return JsonValue(std::move(obj));
}

void DeserializeBrushMeshes(const JsonValue& value, BrushMeshStore& store, std::string* error)
{
    if (!value.IsObject())
        return;
    for (const auto& [idText, recordJson] : value.AsObject())
    {
        std::uint32_t id = 0;
        const char* end = idText.data() + idText.size();
        const auto [stop, status] = std::from_chars(idText.data(), end, id);
        if (status != std::errc{} || stop != end || id == 0)
        {
            AppendProblem(error, "brush_meshes key '" + idText + "' is not a brush id; entry skipped");
            continue;
        }
        std::string problems;
        store.Set(BrushId{ id }, BrushRecordFromJson(recordJson, &problems));
        std::size_t lineStart = 0;
        while (lineStart < problems.size())
        {
            std::size_t lineEnd = problems.find('\n', lineStart);
            if (lineEnd == std::string::npos)
                lineEnd = problems.size();
            if (lineEnd > lineStart)
                AppendProblem(error, "brush " + idText + ": " + problems.substr(lineStart, lineEnd - lineStart));
            lineStart = lineEnd + 1;
        }
    }
}
