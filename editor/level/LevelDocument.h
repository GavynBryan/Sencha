#pragma once

#include "LevelScene.h"

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
    bool Load(std::string_view path);

    void MarkDirty(bool dirty = true);
    [[nodiscard]] LevelScene& GetScene();
    [[nodiscard]] const LevelScene& GetScene() const;
    [[nodiscard]] const Registry& GetRegistry() const;

private:
    std::string FilePath;
    bool Dirty = false;
    Registry Registry_;
    LevelScene Scene;
};
