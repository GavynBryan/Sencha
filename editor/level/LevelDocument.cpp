#include "LevelDocument.h"

#include "brush/BrushValidation.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <render/Camera.h>
#include <world/serialization/IComponentSerializer.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformComponents.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
    // Brushes serialize as a sidecar block keyed by BrushId; the entity's
    // BrushComponent (serialized in the scene) carries the matching id. (03-§5)
    JsonValue BrushMeshToJson(const BrushMesh& mesh)
    {
        JsonValue::Array vertices;
        vertices.reserve(mesh.Vertices.size());
        for (const BrushVertex& vertex : mesh.Vertices)
        {
            vertices.push_back(JsonValue(JsonValue::Array{
                JsonValue(static_cast<double>(vertex.Position.X)),
                JsonValue(static_cast<double>(vertex.Position.Y)),
                JsonValue(static_cast<double>(vertex.Position.Z)) }));
        }

        JsonValue::Array faces;
        faces.reserve(mesh.Faces.size());
        for (const BrushFace& face : mesh.Faces)
        {
            JsonValue::Array loop;
            loop.reserve(face.Loop.size());
            for (std::uint32_t index : face.Loop)
                loop.push_back(JsonValue(static_cast<int>(index)));
            faces.push_back(JsonValue(std::move(loop)));
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
                const JsonValue::Array& a = p.AsArray();
                mesh.Vertices.push_back(BrushVertex{ Vec3d{
                    static_cast<float>(a[0].AsNumber()),
                    static_cast<float>(a[1].AsNumber()),
                    static_cast<float>(a[2].AsNumber()) } });
            }
        }
        if (const JsonValue* faces = value.Find("faces"); faces && faces->IsArray())
        {
            for (const JsonValue& f : faces->AsArray())
            {
                if (!f.IsArray())
                    continue;
                BrushFace face;
                for (const JsonValue& index : f.AsArray())
                    face.Loop.push_back(static_cast<std::uint32_t>(index.AsNumber()));
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
}

LevelDocument::LevelDocument()
    : Registry_()
    , Scene(Registry_)
{
    Registry_.Id = { 2, 1 };
    Registry_.Kind = RegistryKind::Transient;
    Registry_.Zone = ZoneId::Invalid();

    Registry_.Resources.Register<ActiveCameraService>();

    // Component registration must happen before any entity is created.
    World& world = Registry_.Components;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<BrushComponent>();
    world.RegisterComponent<CameraComponent>();

    // Register storage for every serializer the registry knows — engine, editor,
    // and any game module loaded at startup — so game components are available
    // (and inspectable/addable) before any entity exists. Idempotent.
    for (const auto& serializer : GetComponentSerializerEntries())
        serializer->RegisterStorage(Registry_);
}

std::string_view LevelDocument::GetDisplayName() const
{
    return FilePath.empty() ? std::string_view("Untitled") : std::string_view(FilePath);
}

bool LevelDocument::IsDirty() const
{
    return Dirty;
}

bool LevelDocument::Save()
{
    if (FilePath.empty())
        return false;

    JsonValue root = SaveSceneJson(Registry_);
    if (root.IsObject())
        root.AsObject().emplace_back("brush_meshes", SerializeBrushMeshes(Scene.GetBrushMeshStore()));
    const std::string text = JsonStringify(root, /*pretty*/ true);

    std::ofstream file(FilePath, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;

    file << text;
    if (!file.good())
        return false;

    Dirty = false;
    return true;
}

bool LevelDocument::SaveAs(std::string_view path)
{
    if (path.empty())
        return false;

    FilePath.assign(path);
    return Save();
}

bool LevelDocument::Load(std::string_view path)
{
    std::ifstream file{ std::string(path), std::ios::binary };
    if (!file.is_open())
        return false;

    std::ostringstream buffer;
    buffer << file.rdbuf();

    JsonParseError parseError;
    const std::optional<JsonValue> root = JsonParse(buffer.str(), &parseError);
    if (!root.has_value())
        return false;

    Scene.Clear();

    SceneLoadError loadError;
    if (!LoadSceneJson(*root, Registry_, &loadError))
    {
        Scene.SyncFromRegistry();
        return false;
    }

    if (const JsonValue* meshes = root->Find("brush_meshes"))
        DeserializeBrushMeshes(*meshes, Scene.GetBrushMeshStore());

    Scene.SyncFromRegistry();
    FilePath.assign(path);
    Dirty = false;
    return true;
}

void LevelDocument::New()
{
    Scene.Clear();
    FilePath.clear();
    Dirty = false;
}

bool LevelDocument::HasFilePath() const
{
    return !FilePath.empty();
}

void LevelDocument::MarkDirty(bool dirty)
{
    Dirty = dirty;
}

LevelScene& LevelDocument::GetScene()
{
    return Scene;
}

const LevelScene& LevelDocument::GetScene() const
{
    return Scene;
}

const Registry& LevelDocument::GetRegistry() const
{
    return Registry_;
}
