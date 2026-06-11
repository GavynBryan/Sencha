#include "LevelDocument.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <render/Camera.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformComponents.h>

#include <fstream>
#include <sstream>

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

    const JsonValue root = SaveSceneJson(Registry_);
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
