#pragma once

#include "LevelScene.h"

#include <core/assets/AssetRef.h>
#include <world/registry/Registry.h>

#include <string>
#include <string_view>

class LevelDocument
{
public:
    LevelDocument();

    [[nodiscard]] std::string_view GetDisplayName() const;
    [[nodiscard]] bool IsDirty() const;
    bool Save();
    bool SaveAs(std::string_view path);
    bool Load(std::string_view path);
    void New();

    [[nodiscard]] bool HasFilePath() const;

    void MarkDirty(bool dirty = true);
    [[nodiscard]] LevelScene& GetScene();
    [[nodiscard]] const LevelScene& GetScene() const;
    [[nodiscard]] const Registry& GetRegistry() const;

    // Level-wide fallback material applied to any face that carries no explicit
    // one (a fresh brush is never "no material"). A level setting. (04-§2)
    [[nodiscard]] const AssetRef& GetDefaultMaterial() const;
    void SetDefaultMaterial(AssetRef material);

private:
    std::string FilePath;
    bool Dirty = false;
    Registry Registry_;
    LevelScene Scene;
    AssetRef DefaultMaterial{ AssetType::Material, "asset://materials/dev/gray.smat" };
};
