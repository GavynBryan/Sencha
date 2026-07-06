#include "BrushMeshSerialization.h"

#include "BrushValidation.h"

#include <cstdint>
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

    Vec3d Vec3FromJson(const JsonValue& v, Vec3d fallback)
    {
        if (!v.IsArray() || v.Size() < 3)
            return fallback;
        const JsonValue::Array& a = v.AsArray();
        return Vec3d{ static_cast<float>(a[0].AsNumber()),
                      static_cast<float>(a[1].AsNumber()),
                      static_cast<float>(a[2].AsNumber()) };
    }

    Vec2d Vec2FromJson(const JsonValue& v, Vec2d fallback)
    {
        if (!v.IsArray() || v.Size() < 2)
            return fallback;
        const JsonValue::Array& a = v.AsArray();
        return Vec2d{ static_cast<float>(a[0].AsNumber()),
                      static_cast<float>(a[1].AsNumber()) };
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
        if (!fm.Material.Path.empty()) // empty => inherit the level default
            obj.emplace_back("material", JsonValue(fm.Material.Path));
        obj.emplace_back("uv", JsonValue(std::move(uv)));
        return JsonValue(std::move(obj));
    }

    FaceMaterial FaceMaterialFromJson(const JsonValue& value)
    {
        FaceMaterial fm;
        if (const JsonValue* material = value.Find("material");
            material && material->IsString())
        {
            fm.Material.Type = AssetType::Material;
            fm.Material.Path = material->AsString();
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
    return JsonValue(std::move(obj));
}

BrushMesh BrushMeshFromJson(const JsonValue& value)
{
    BrushMesh mesh;
    if (const JsonValue* vertices = value.Find("vertices"); vertices && vertices->IsArray())
    {
        for (const JsonValue& p : vertices->AsArray())
        {
            if (!p.IsArray() || p.Size() < 3)
                continue;
            mesh.Vertices.push_back(BrushVertex{ Vec3FromJson(p, Vec3d{}) });
        }
    }
    if (const JsonValue* faces = value.Find("faces"); faces && faces->IsArray())
    {
        for (const JsonValue& f : faces->AsArray())
        {
            BrushFace face;
            if (f.IsArray()) // pre-texturing form: bare loop array
            {
                for (const JsonValue& index : f.AsArray())
                    face.Loop.push_back(static_cast<std::uint32_t>(index.AsNumber()));
            }
            else if (f.IsObject())
            {
                if (const JsonValue* loop = f.Find("loop"); loop && loop->IsArray())
                    for (const JsonValue& index : loop->AsArray())
                        face.Loop.push_back(static_cast<std::uint32_t>(index.AsNumber()));
                face.Material = FaceMaterialFromJson(f);
            }
            else
            {
                continue;
            }
            mesh.Faces.push_back(std::move(face));
        }
    }
    return mesh;
}

JsonValue SerializeBrushMeshes(const BrushMeshStore& store)
{
    JsonValue::Object obj;
    for (const auto& [id, mesh] : store.All())
        obj.emplace_back(std::to_string(id), BrushMeshToJson(mesh));
    return JsonValue(std::move(obj));
}

void DeserializeBrushMeshes(const JsonValue& value, BrushMeshStore& store)
{
    if (!value.IsObject())
        return;
    for (const auto& [idText, meshJson] : value.AsObject())
    {
        BrushMesh mesh = BrushMeshFromJson(meshJson);
        BrushValidateAndRepair(mesh); // never accept a corrupt brush silently (03-§5)
        store.Set(BrushId{ static_cast<std::uint32_t>(std::stoul(idText)) }, std::move(mesh));
    }
}
