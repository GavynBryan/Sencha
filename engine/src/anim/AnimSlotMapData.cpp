#include <anim/AnimSlotMapData.h>

#include "AnimSchemaFields.h"

#include <anim/AnimBlendspaceData.h>
#include <anim/AnimFlowData.h>

#include <gameplay_tags/GameplayTagRegistry.h>

#include <format>
#include <memory>

namespace
{
    DataSchema MakeSchema()
    {
        using AnimSchema::ArrayOf;
        using AnimSchema::DataRef;
        using AnimSchema::Enum;
        using AnimSchema::Field;
        using AnimSchema::Record;
        DataFieldSchema clip = Field("clip", DataFieldKind::AssetRef, "Clip", "The clip this row plays.", false);
        clip.Reference.AssetTypeFilter = AssetType::AnimationClip;

        DataFieldSchema row = Record({}, "Row", {},
            {
                Field("behavior", DataFieldKind::GameplayTag, "Behavior", "The behavior this row resolves."),
                Field("priority", DataFieldKind::Int, "Priority",
                      "Higher rows are tried first; overlays insert rows by priority.", false),
                AnimPredicateSchema("when", "When", "Facts that must hold for this row; empty always matches."),
                std::move(clip),
                DataRef("flow", "Flow", "The flow this row plays, for a behavior whose content is a sequence.",
                        kAnimFlowType),
                DataRef("blendspace", "Blendspace",
                        "The blendspace this row plays: clips mixed by where facts place them.", kAnimBlendspaceType),
            });
        DataFieldSchema rows = ArrayOf("rows", "Rows", "First match per behavior.", std::move(row), true);
        rows.Editor.Widget = "cards";
        rows.Editor.TitleKey = "behavior";

        DataSchema schema;
        schema.TypeName = std::string(kAnimSlotMapType);
        schema.DisplayName = "Animation slot map";
        schema.Description = "Resolves a behavior plus facts to the content one rig plays.";
        schema.Root.Kind = DataFieldKind::Record;
        schema.Root.Children.push_back(std::move(rows));
        return schema;
    }

    DataAssetCompileResult Compile(const JsonValue& data)
    {
        DataAssetCompileResult result;
        auto map = std::make_shared<AnimSlotMapData>();
        GameplayTagRegistry tagSyntax;

        const JsonValue::Array& rows = data.Find("rows")->AsArray();
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const JsonValue& entry = rows[i];
            const std::string at = std::format("$.data.rows[{}]", i);
            AnimSlotRowDecl row;
            row.Behavior = entry.Find("behavior")->AsString();
            GameplayTagError error;
            if (!tagSyntax.RegisterTag(row.Behavior, &error))
            {
                result.Error = at + ".behavior " + error.Message;
                return result;
            }
            if (const JsonValue* priority = entry.Find("priority"); priority != nullptr && priority->IsNumber())
                row.Priority = static_cast<std::int32_t>(priority->AsNumber());
            if (!ReadAnimPredicate(entry.Find("when"), at + ".when", row.When, result.Error))
                return result;
            if (const JsonValue* clip = entry.Find("clip"); clip != nullptr && clip->IsString())
                row.Clip = clip->AsString();
            if (const JsonValue* flow = entry.Find("flow"); flow != nullptr && flow->IsString())
                row.Flow = flow->AsString();
            if (const JsonValue* space = entry.Find("blendspace"); space != nullptr && space->IsString())
                row.Blendspace = space->AsString();
            if (!row.Clip.empty() + !row.Flow.empty() + !row.Blendspace.empty() != 1)
            {
                result.Error = at + " A row plays exactly one of a clip, a flow or a blendspace.";
                return result;
            }
            if (!row.Clip.empty())
                result.Dependencies.push_back(AssetRef{ AssetType::AnimationClip, row.Clip });
            else
                result.Dependencies.push_back(AssetRef{ AssetType::Data, row.Flow.empty() ? row.Blendspace : row.Flow });
            map->Rows.push_back(std::move(row));
        }
        result.Value = std::move(map);
        return result;
    }
}

void RegisterAnimSlotMapData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (!types.Register({ std::string(kAnimSlotMapType), 1, Compile }))
        return;
    if (!schemas.Register(MakeSchema()))
        (void)types.Unregister(kAnimSlotMapType);
}
