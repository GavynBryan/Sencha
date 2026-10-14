#pragma once

#include <core/assets/AssetRef.h>
#include <world/scene/SmapFormat.h>

#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>

class ComponentSerializerRegistry;
class JsonValue;

// Every scene cook's output step: the scene's references and one level of .smat
// indirection plus `extraRefs`, named by id, compiled with the collision cells
// into one .smap (docs/plans/sencha-level-editor/05-level-cook.md §5). Cook-only.
[[nodiscard]] bool WriteCookedScene(
    const JsonValue& cookedScene,
    std::span<const std::string> extraRefs,
    std::span<const SmapCollisionCell> collisionCells,
    const ComponentSerializerRegistry& serializers,
    const std::function<std::filesystem::path(std::string_view)>& physicalPathFor,
    const std::filesystem::path& cookedScenePath,
    std::string* error = nullptr);

// The id a cooked scene's dependency goes by: the one its reference names,
// else the one its file states, else its path's; a part's derives from its
// file's.
[[nodiscard]] AssetId CookedDependencyId(const AssetRef& ref,
                                         const std::function<std::filesystem::path(std::string_view)>& physicalPathFor);
