#include <anim/AnimBlendspaceData.h>

#include "AnimSchemaFields.h"

#include <cmath>
#include <format>
#include <memory>

namespace
{
    DataSchema MakeSchema()
    {
        using AnimSchema::ArrayOf;
        using AnimSchema::Field;
        using AnimSchema::Record;
        DataFieldSchema axis = Record({}, "Axis", {},
            {
                Field("fact", DataFieldKind::String, "Fact", "A float or int fact that places the mix."),
                Field("min", DataFieldKind::Float, "Minimum", "The fact is clamped to this from below."),
                Field("max", DataFieldKind::Float, "Maximum", "The fact is clamped to this from above."),
            });
        DataFieldSchema clip = Field("clip", DataFieldKind::AssetRef, "Clip", "What plays at this point.");
        clip.Reference.AssetTypeFilter = AssetType::AnimationClip;
        DataFieldSchema sample = Record({}, "Sample", {},
            {
                std::move(clip),
                ArrayOf("at", "At", "One coordinate per axis.", Field({}, DataFieldKind::Float, "Coordinate", {}),
                        true),
            });
        DataFieldSchema samples = ArrayOf("samples", "Samples", "Clips placed along the axes.", std::move(sample), true);
        samples.Editor.Widget = "cards";
        samples.Editor.TitleKey = "clip";

        DataSchema schema;
        schema.TypeName = std::string(kAnimBlendspaceType);
        schema.DisplayName = "Animation blendspace";
        schema.Description = "Clips placed along one or two fact axes and mixed at one phase.";
        schema.Root.Kind = DataFieldKind::Record;
        schema.Root.Children = {
            ArrayOf("axes", "Axes", "One or two facts.", std::move(axis), true),
            std::move(samples),
        };
        return schema;
    }

    DataAssetCompileResult Compile(const JsonValue& data)
    {
        DataAssetCompileResult result;
        auto space = std::make_shared<AnimBlendspaceData>();
        const auto fail = [&](std::string at, std::string message) {
            result.Error = at + " " + message;
            return result;
        };
        const auto number = [](const JsonValue& object, std::string_view key) {
            const JsonValue* value = object.Find(key);
            return value != nullptr && value->IsNumber() ? static_cast<float>(value->AsNumber()) : NAN;
        };

        const JsonValue::Array& axes = data.Find("axes")->AsArray();
        if (axes.empty() || axes.size() > kAnimBlendspaceMaxAxes)
            return fail("$.data.axes", std::format("A blendspace has 1 or {} axes.", kAnimBlendspaceMaxAxes));
        for (std::size_t a = 0; a < axes.size(); ++a)
        {
            const std::string at = std::format("$.data.axes[{}]", a);
            AnimBlendspaceAxisDecl axis;
            if (const JsonValue* fact = axes[a].Find("fact"); fact != nullptr && fact->IsString())
                axis.Fact = fact->AsString();
            axis.Min = number(axes[a], "min");
            axis.Max = number(axes[a], "max");
            if (axis.Fact.empty())
                return fail(at + ".fact", "An axis names the fact that places it.");
            if (!std::isfinite(axis.Min) || !std::isfinite(axis.Max) || !(axis.Min < axis.Max))
                return fail(at, "An axis runs from a finite minimum to a larger finite maximum.");
            space->Axes.push_back(std::move(axis));
        }

        const JsonValue::Array& samples = data.Find("samples")->AsArray();
        if (samples.size() < 2 || samples.size() > kAnimBlendspaceMaxSamples)
            return fail("$.data.samples",
                        std::format("A blendspace mixes 2 to {} samples.", kAnimBlendspaceMaxSamples));
        for (std::size_t s = 0; s < samples.size(); ++s)
        {
            const std::string at = std::format("$.data.samples[{}]", s);
            AnimBlendspaceSampleDecl sample;
            if (const JsonValue* clip = samples[s].Find("clip"); clip != nullptr && clip->IsString())
                sample.Clip = clip->AsString();
            if (sample.Clip.empty())
                return fail(at + ".clip", "A sample names its clip.");
            const JsonValue* coordinates = samples[s].Find("at");
            if (coordinates == nullptr || !coordinates->IsArray() || coordinates->AsArray().size() != axes.size())
                return fail(at + ".at", std::format("A sample has one coordinate per axis, {} here.", axes.size()));
            for (std::size_t a = 0; a < axes.size(); ++a)
            {
                const JsonValue& value = coordinates->AsArray()[a];
                const float coordinate = value.IsNumber() ? static_cast<float>(value.AsNumber()) : NAN;
                const AnimBlendspaceAxisDecl& axis = space->Axes[a];
                if (!std::isfinite(coordinate) || coordinate < axis.Min || coordinate > axis.Max)
                    return fail(std::format("{}.at[{}]", at, a),
                                std::format("A coordinate lies on its axis, from {} to {}.", axis.Min, axis.Max));
                sample.At[a] = coordinate;
            }
            for (std::size_t o = 0; o < space->Samples.size(); ++o)
            {
                bool same = true;
                for (std::size_t a = 0; a < axes.size(); ++a)
                    same = same && space->Samples[o].At[a] == sample.At[a];
                if (same)
                    return fail(at + ".at", std::format("Samples {} and {} sit at the same point.", o, s));
            }
            result.Dependencies.push_back(AssetRef{ AssetType::AnimationClip, sample.Clip });
            space->Samples.push_back(std::move(sample));
        }
        result.Value = std::move(space);
        return result;
    }
}

void RegisterAnimBlendspaceData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (!types.Register({ std::string(kAnimBlendspaceType), 1, Compile }))
        return;
    if (!schemas.Register(MakeSchema()))
        (void)types.Unregister(kAnimBlendspaceType);
}
