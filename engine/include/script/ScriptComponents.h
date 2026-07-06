#pragma once

#include <assets/script/ScriptCache.h>
#include <core/assets/AssetRef.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// ScriptSource (docs/plans/t-language.md, milestone 5 integration)
//
// The authoring-facing script component: a designer attaches this to a level
// entity and points its `script` field at a cooked T script, exactly the way
// a mesh field points at a mesh. Being schema-driven, it serializes through
// the cook and shows up editable in the editor inspector (with a script-asset
// picker) with no editor code naming it.
//
// This is the serialized form. At load a resolve step reads the linked
// module behind the handle and attaches the runtime ScriptBehavior that the
// behavior bridge ticks. ScriptSource carries entity behavior; abilities
// arrive through AbilityKit activation, not through a level component.
//=============================================================================
struct ScriptSource
{
    ScriptHandle Script;
};

template <>
struct TypeSchema<ScriptSource>
{
    static constexpr std::string_view Name = "ScriptSource";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('T', 'S', 'R', 'C');

    static auto Fields()
    {
        return std::tuple{
            MakeField("script", &ScriptSource::Script).AsAsset(AssetType::Script),
        };
    }
};
