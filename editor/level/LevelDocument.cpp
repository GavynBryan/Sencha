#include "LevelDocument.h"

#include <render/Camera.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagationOrderService.h>

LevelDocument::LevelDocument()
    : Registry_()
    , Scene(Registry_)
{
    Registry_.Id = { 2, 1 };
    Registry_.Kind = RegistryKind::Transient;
    Registry_.Zone = ZoneId::Invalid();

    auto& order = Registry_.Resources.Register<TransformPropagationOrderService>();
    Registry_.Resources.Register<TransformHierarchyService>();
    Registry_.Resources.Register<ActiveCameraService>();
    Registry_.Components.Register<TransformStore<Transform3f>>(order);
    Registry_.Components.Register<BrushComponentStore>();
    Registry_.Components.Register<CameraStore>();
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
