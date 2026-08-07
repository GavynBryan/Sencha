#pragma once

#include "document/EntitySnapshot.h"

#include "selection/SelectableRef.h"

#include <math/Vec.h>
#include <zone/WorldConnectionComponents.h>
#include <zone/WorldPartitionIds.h>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

class WorldDocument;

// Mints fresh persistent identities in a duplication batch and remaps references
// between members of that batch. It runs once when snapshots are first captured;
// load, paste, restore, and redo never re-run creation defaults.
void RemapWorldConnectionDuplicateSnapshots(
    WorldDocument& world, std::vector<EntitySnapshot>& snapshots);

struct EditorCreateContext
{
    WorldDocument* World = nullptr;
    ZoneId ActiveZone{};
    GraphId ActiveGraph{};
    Vec3d PlacementPoint{};
    Vec3d PlacementNormal = Vec3d::Forward();
    std::span<const SelectableRef> Selection{};
};

class IEditorEntityRecipe
{
public:
    virtual ~IEditorEntityRecipe() = default;
    [[nodiscard]] virtual EntitySnapshot Build(const EditorCreateContext& context) const = 0;
};

class EditorEntityRecipeRegistry
{
public:
    bool Register(std::string name, std::unique_ptr<IEditorEntityRecipe> recipe);
    [[nodiscard]] const IEditorEntityRecipe* Find(std::string_view name) const;

private:
    std::unordered_map<std::string, std::unique_ptr<IEditorEntityRecipe>> Recipes;
};

class WorldDockRecipe final : public IEditorEntityRecipe
{
public:
    [[nodiscard]] EntitySnapshot Build(const EditorCreateContext& context) const override;
};

class WorldLinkRecipe final : public IEditorEntityRecipe
{
public:
    explicit WorldLinkRecipe(ZoneId zoneB,
                             uint8_t directions = DockDirectionBoth)
        : ZoneB(zoneB)
        , Directions(directions)
    {
    }

    [[nodiscard]] EntitySnapshot Build(const EditorCreateContext& context) const override;

private:
    ZoneId ZoneB;
    uint8_t Directions = DockDirectionBoth;
};
