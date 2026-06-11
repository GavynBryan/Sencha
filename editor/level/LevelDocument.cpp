#include "LevelDocument.h"

#include <render/Camera.h>
#include <world/transform/TransformComponents.h>

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
    return false;
}

bool LevelDocument::Load(std::string_view path)
{
    FilePath.assign(path);
    Dirty = false;
    return false;
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
