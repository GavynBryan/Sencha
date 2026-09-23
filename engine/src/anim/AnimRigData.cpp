#include <anim/AnimRigData.h>

#include "AnimSchemaFields.h"

#include <anim/AnimBehaviorSet.h>
#include <anim/AnimFactSchema.h>
#include <anim/AnimRequestSchema.h>
#include <anim/AnimSelectorData.h>
#include <anim/AnimSlotMapData.h>
#include <authored/VerbBindingData.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <format>
#include <memory>
#include <utility>

namespace
{
    using AnimSchema::DataRef;
    using AnimSchema::Field;

    DataSchema MakeSchema()
    {
        DataFieldSchema skeleton = Field("skeleton", DataFieldKind::AssetRef, "Skeleton",
                                         "The joint hierarchy this rig poses.", false);
        skeleton.Reference.AssetTypeFilter = AssetType::Skeleton;

        DataFieldSchema capacity = Field("fact_capacity", DataFieldKind::Enum, "Fact storage",
                                         "Small holds 16 slots, Large 64.", false);
        capacity.Default = std::string("small");
        capacity.EnumChoices = { { "small", "Small (16)", "Simple-tier entities" },
                                 { "large", "Large (64)", "Character-tier entities" } };

        DataFieldSchema layer = Field({}, DataFieldKind::Record, "Layer", {}, true);
        layer.Children.push_back(Field("name", DataFieldKind::GameplayTag, "Name",
                                       "The layer's tag, e.g. Anim.Layer.Base.", true));
        DataFieldSchema mode = Field("mode", DataFieldKind::Enum, "Mode",
                                     "How the layer composes over those below it.", false);
        mode.Default = std::string("override");
        mode.EnumChoices = { { "override", "Override", "Replaces the pose under its mask" },
                             { "additive", "Additive", "Adds onto the pose under its mask" } };
        layer.Children.push_back(std::move(mode));
        DataFieldSchema weight = Field("weight", DataFieldKind::Float, "Weight",
                                       "Constant weight.", false);
        weight.Default = 1.0;
        weight.Numeric.Minimum = 0.0;
        weight.Numeric.Maximum = 1.0;
        layer.Children.push_back(std::move(weight));
        layer.Children.push_back(DataRef("selector", "Selector",
                                         "Chooses the layer's behavior. None makes it request-keyed.",
                                         kAnimSelectorType));
        layer.Children.push_back(Field("idle", DataFieldKind::GameplayTag, "Idle behavior",
                                       "Played when nothing is selected or requested.", false));
        DataFieldSchema layers = Field("layers", DataFieldKind::Array, "Layers",
                                       "Composed in order, at most eight.", true);
        layers.Editor.Widget = "cards";
        layers.Editor.TitleKey = "name";
        layers.Children.push_back(std::move(layer));

        DataSchema schema;
        schema.TypeName = std::string(kAnimRigType);
        schema.DisplayName = "Animation rig";
        schema.Description = "What one kind of animated entity reads, stores and composes.";
        schema.Root.Kind = DataFieldKind::Record;
        schema.Root.Children = {
            std::move(skeleton),
            DataRef("facts", "Fact schema", "The facts this rig's rules read. None makes a Prop rig.",
                    kAnimFactSchemaType),
            DataRef("requests", "Request schema", "The intents and params requests may carry.",
                    kAnimRequestSchemaType),
            std::move(capacity),
            std::move(layers),
            AnimSchema::ArrayOf("behaviors", "Behavior sets",
                                "In order; a later set overrides an earlier one by tag.",
                                DataRef({}, "Behavior set", {}, kAnimBehaviorSetType, true)),
            AnimSchema::ArrayOf("slot_maps", "Slot maps", "The base map first, then overlays.",
                                DataRef({}, "Slot map", {}, kAnimSlotMapType, true)),
            AnimSchema::ArrayOf("bindings", "Event bindings",
                                "The authored bindings this rig's events name, in order.",
                                DataRef({}, "Bindings", {}, kVerbBindingsTypeName, true)),
            AnimSchema::ArrayOf("extensions", "Extension bindings",
                                "Selectors bound to the extension points rules expose.",
                                AnimSchema::Record({}, "Extension", {},
                                    {
                                        Field("name", DataFieldKind::String, "Name",
                                              "The extension point's name.", true),
                                        DataRef("selector", "Selector", "The rules it contributes.",
                                                kAnimSelectorType, true),
                                    })),
        };
        return schema;
    }

    DataAssetCompileResult Compile(const JsonValue& data)
    {
        DataAssetCompileResult result;
        auto rig = std::make_shared<AnimRigData>();

        const auto optionalPath = [&](std::string_view key, AssetType type, std::string& out) {
            if (const JsonValue* value = data.Find(key);
                value != nullptr && value->IsString() && !value->AsString().empty())
            {
                out = value->AsString();
                result.Dependencies.push_back(AssetRef{ type, out });
            }
        };
        optionalPath("skeleton", AssetType::Skeleton, rig->SkeletonPath);
        optionalPath("facts", AssetType::Data, rig->FactSchemaPath);
        optionalPath("requests", AssetType::Data, rig->RequestSchemaPath);

        if (const JsonValue* capacity = data.Find("fact_capacity"))
            rig->FactCapacity = capacity->AsString() == "large" ? AnimFactCapacity::Large
                                                                : AnimFactCapacity::Small;

        const JsonValue::Array& layers = data.Find("layers")->AsArray();
        if (layers.empty())
        {
            result.Error = "$.data.layers A rig composes at least one layer.";
            return result;
        }
        if (layers.size() > kAnimMaxLayers)
        {
            result.Error = std::format("$.data.layers A rig has at most {} layers.", kAnimMaxLayers);
            return result;
        }

        // Syntax only: the tag's id is the World's, resolved when the rig binds.
        GameplayTagRegistry tagSyntax;
        for (std::size_t i = 0; i < layers.size(); ++i)
        {
            const std::string path = std::format("$.data.layers[{}]", i);
            AnimRigLayer layer;
            layer.Name = layers[i].Find("name")->AsString();
            GameplayTagError error;
            if (!tagSyntax.RegisterTag(layer.Name, &error))
            {
                result.Error = path + ".name " + error.Message;
                return result;
            }
            if (std::any_of(rig->Layers.begin(), rig->Layers.end(),
                            [&](const AnimRigLayer& other) { return other.Name == layer.Name; }))
            {
                result.Error = path + ".name Two layers share a name.";
                return result;
            }
            if (const JsonValue* mode = layers[i].Find("mode"))
                layer.Mode = mode->AsString() == "additive" ? AnimLayerMode::Additive
                                                            : AnimLayerMode::Override;
            if (const JsonValue* weight = layers[i].Find("weight"))
                layer.Weight = static_cast<float>(weight->AsNumber());
            if (const JsonValue* selector = layers[i].Find("selector");
                selector != nullptr && selector->IsString() && !selector->AsString().empty())
            {
                layer.SelectorPath = selector->AsString();
                result.Dependencies.push_back(AssetRef{ AssetType::Data, layer.SelectorPath });
            }
            if (const JsonValue* idle = layers[i].Find("idle"); idle != nullptr && idle->IsString())
            {
                layer.Idle = idle->AsString();
                if (!layer.Idle.empty() && !tagSyntax.RegisterTag(layer.Idle, &error))
                {
                    result.Error = path + ".idle " + error.Message;
                    return result;
                }
            }
            rig->Layers.push_back(std::move(layer));
        }

        const auto paths = [&](std::string_view key, std::vector<std::string>& out) {
            if (const JsonValue* list = data.Find(key); list != nullptr && list->IsArray())
            {
                for (const JsonValue& path : list->AsArray())
                {
                    out.push_back(path.AsString());
                    result.Dependencies.push_back(AssetRef{ AssetType::Data, out.back() });
                }
            }
        };
        paths("behaviors", rig->BehaviorSetPaths);
        paths("slot_maps", rig->SlotMapPaths);
        paths("bindings", rig->BindingSetPaths);

        if (const JsonValue* extensions = data.Find("extensions"); extensions != nullptr && extensions->IsArray())
        {
            for (std::size_t i = 0; i < extensions->AsArray().size(); ++i)
            {
                const JsonValue& entry = extensions->AsArray()[i];
                AnimRigExtension extension{ entry.Find("name")->AsString(),
                                            entry.Find("selector")->AsString() };
                if (std::any_of(rig->Extensions.begin(), rig->Extensions.end(),
                                [&](const AnimRigExtension& other) { return other.Name == extension.Name; }))
                {
                    result.Error = std::format("$.data.extensions[{}].name '{}' is bound twice.", i,
                                               extension.Name);
                    return result;
                }
                result.Dependencies.push_back(AssetRef{ AssetType::Data, extension.SelectorPath });
                rig->Extensions.push_back(std::move(extension));
            }
        }

        result.Value = std::move(rig);
        return result;
    }
}

void RegisterAnimRigData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (!types.Register({ std::string(kAnimRigType), 1, Compile }))
        return;
    if (!schemas.Register(MakeSchema()))
        (void)types.Unregister(kAnimRigType);
}

void UnregisterAnimRigData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (types.Unregister(kAnimRigType))
        (void)schemas.Unregister(kAnimRigType);
}
