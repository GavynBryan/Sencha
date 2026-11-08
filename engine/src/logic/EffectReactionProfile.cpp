#include <logic/EffectReactionProfile.h>

#include <assets/data/DataAssetTypeRegistry.h>
#include <attributes/AttributeRegistry.h>
#include <camera/CameraShakeProfile.h>
#include <core/metadata/DataSchema.h>
#include <effects/EffectApplicationLog.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <gameplay_tags/GameplayTagSet.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>

namespace
{
    DataFieldSchema Amount(std::string key, std::string displayName, std::string summary)
    {
        DataFieldSchema field = MakeDataField(DataFieldKind::Float, std::move(key), std::move(displayName),
                                              std::move(summary));
        field.Default = 0.0;
        field.Required = false;
        field.Numeric.Minimum = 0.0;
        return field;
    }

    DataSchema MakeSchema()
    {
        DataFieldSchema change = MakeDataField(DataFieldKind::Enum, "change", "Change", "Which way the attribute moves.");
        change.Required = false;
        change.Default = std::string("decrease");
        change.EnumChoices = { { "decrease", "Decrease", "" }, { "increase", "Increase", "" } };

        DataFieldSchema impulse = MakeDataField(DataFieldKind::Record, "impulse", "Push");
        impulse.Required = false;
        impulse.Children.push_back(Amount("per_unit", "Per unit", "Metres per second of push per unit of change."));
        impulse.Children.push_back(Amount("max", "Most", "The push's cap, in metres per second."));
        impulse.Children.push_back(Amount("lift", "Lift", "Share of the push added straight up."));

        DataFieldSchema shakeProfile = MakeDataField(DataFieldKind::DataAssetRef, "profile", "Profile",
                                                     "How the view shakes.");
        shakeProfile.Reference.DataSubtype = std::string(kCameraShakeType);
        DataFieldSchema shake = MakeDataField(DataFieldKind::Record, "shake", "Shake",
                                              "Shakes the view the entity owns, if any.");
        shake.Required = false;
        shake.Children.push_back(std::move(shakeProfile));
        shake.Children.push_back(Amount("per_unit", "Per unit", "Shake scale per unit of change."));
        shake.Children.push_back(Amount("max", "Most", "The shake scale's cap."));

        const auto intent = [](std::string key, std::string name, std::string summary) {
            DataFieldSchema field = MakeDataField(DataFieldKind::GameplayTag, std::move(key), std::move(name),
                                                  std::move(summary));
            field.Required = false;
            return field;
        };
        DataFieldSchema ticks = MakeDataField(DataFieldKind::Int, "ticks", "Ticks", "How long the intent is asked for.");
        ticks.Required = false;
        ticks.Numeric.Minimum = 1.0;
        DataFieldSchema animation = MakeDataField(DataFieldKind::Record, "animation", "Animation",
                                                  "Asks the entity's rig to flinch, by the side the blow came from.");
        animation.Required = false;
        animation.Children.push_back(intent("front", "Front", "Struck from the front, or from no direction."));
        animation.Children.push_back(intent("back", "Back", "Struck from behind; the front when empty."));
        animation.Children.push_back(intent("left", "Left", "Struck from its left; the front when empty."));
        animation.Children.push_back(intent("right", "Right", "Struck from its right; the front when empty."));
        animation.Children.push_back(std::move(ticks));

        DataFieldSchema reaction = MakeDataField(DataFieldKind::Record, "reaction", "Reaction");
        reaction.Children.push_back(MakeTagQueryNamesField("kinds", "Kinds"));
        reaction.Children.push_back(MakeTagQueryNamesField("recipient", "Recipient"));
        reaction.Children.push_back(MakeDataField(DataFieldKind::String, "attribute", "Attribute",
                                                  "The attribute whose change it answers, by name."));
        reaction.Children.push_back(std::move(change));
        reaction.Children.push_back(Amount("threshold", "Threshold", "The least change answered at all."));
        reaction.Children.push_back(std::move(impulse));
        reaction.Children.push_back(std::move(shake));
        reaction.Children.push_back(std::move(animation));

        DataFieldSchema reactions = MakeDataField(DataFieldKind::Array, "reactions", "Reactions",
                                                  "The first that matches an application answers it.");
        reactions.Editor.Widget = "cards";
        reactions.Editor.TitleKey = "attribute";
        reactions.Children.push_back(std::move(reaction));

        DataSchema schema;
        schema.TypeName = std::string(kEffectReactionProfileType);
        schema.DisplayName = "Effect reactions";
        schema.Description = "How an entity answers the effects applied to it: a push and a shake of its view, in "
                             "proportion to the change, and a flinch.";
        schema.Root.Kind = DataFieldKind::Record;
        schema.Root.Children.push_back(std::move(reactions));
        return schema;
    }

    float AmountOr(const JsonValue* record, std::string_view key)
    {
        const JsonValue* value = record != nullptr ? record->Find(key) : nullptr;
        return value != nullptr && value->IsNumber() ? static_cast<float>(value->AsNumber()) : 0.0f;
    }

    DataAssetCompileResult Compile(const JsonValue& data)
    {
        auto profile = std::make_shared<EffectReactionProfile>();
        std::vector<AssetRef> dependencies;
        const JsonValue* reactions = data.Find("reactions");
        if (reactions == nullptr || !reactions->IsArray())
            return { {}, {}, "$.data.reactions A profile lists its reactions." };
        for (std::size_t i = 0; i < reactions->AsArray().size(); ++i)
        {
            const JsonValue& entry = reactions->AsArray()[i];
            const std::string at = std::format("$.data.reactions[{}]", i);
            EffectReactionDeclaration reaction;
            if (const JsonValue* kinds = entry.Find("kinds"))
                reaction.Kinds = ReadTagQueryNames(*kinds);
            if (const JsonValue* recipient = entry.Find("recipient"))
                reaction.Recipient = ReadTagQueryNames(*recipient);
            const JsonValue* attribute = entry.Find("attribute");
            if (attribute == nullptr || !attribute->IsString() || attribute->AsString().empty())
                return { {}, {}, at + ".attribute A reaction names the attribute whose change it answers." };
            reaction.Attribute = attribute->AsString();
            if (const JsonValue* change = entry.Find("change"); change != nullptr && change->IsString())
            {
                if (change->AsString() != "decrease" && change->AsString() != "increase")
                    return { {}, {}, at + ".change A change is a decrease or an increase." };
                reaction.Decrease = change->AsString() == "decrease";
            }
            reaction.Threshold = AmountOr(&entry, "threshold");
            const JsonValue* impulse = entry.Find("impulse");
            reaction.ImpulsePerUnit = AmountOr(impulse, "per_unit");
            reaction.ImpulseMax = AmountOr(impulse, "max");
            reaction.Lift = AmountOr(impulse, "lift");
            const JsonValue* shake = entry.Find("shake");
            if (const JsonValue* path = shake != nullptr ? shake->Find("profile") : nullptr;
                path != nullptr && path->IsString() && !path->AsString().empty())
            {
                reaction.ShakeProfile = path->AsString();
                dependencies.push_back(AssetRef{ AssetType::Data, reaction.ShakeProfile });
            }
            reaction.ShakePerUnit = AmountOr(shake, "per_unit");
            reaction.ShakeMax = AmountOr(shake, "max");
            if (const JsonValue* animation = entry.Find("animation"))
            {
                const auto name = [&](std::string_view key) {
                    const JsonValue* value = animation->Find(key);
                    return value != nullptr && value->IsString() ? value->AsString() : std::string{};
                };
                reaction.AnimationFront = name("front");
                reaction.AnimationBack = name("back");
                reaction.AnimationLeft = name("left");
                reaction.AnimationRight = name("right");
                const JsonValue* ticks = animation->Find("ticks");
                if (reaction.AnimationFront.empty() || ticks == nullptr || !ticks->IsNumber() || ticks->AsNumber() < 1.0)
                    return { {}, {}, at + ".animation An animation names at least its front, and its ticks." };
                reaction.AnimationTicks = static_cast<std::uint32_t>(ticks->AsNumber());
            }
            for (const float amount : { reaction.Threshold, reaction.ImpulsePerUnit, reaction.ImpulseMax,
                                        reaction.Lift, reaction.ShakePerUnit, reaction.ShakeMax })
                if (!std::isfinite(amount) || amount < 0.0f)
                    return { {}, {}, at + " Every amount is a number no less than zero." };
            profile->Reactions.push_back(std::move(reaction));
        }
        return { std::move(profile), std::move(dependencies), {} };
    }

    void ResolveQuery(const GameplayTagQueryNames& names, const GameplayTagRegistry& tags, GameplayTagQuery& out,
                      std::vector<std::string>& errors)
    {
        std::vector<std::string> missing;
        out = ResolveTagQuery(names, tags, missing);
        for (const std::string& name : missing)
            errors.push_back(std::format("no gameplay tag '{}'", name));
    }

    // Which way the case pushes, flattened onto the ground plane; zero when it names none.
    Vec3d PushDirection(const EffectReactionCase& application)
    {
        Vec3d way = Vec3d::Zero();
        if (application.Source.HasDirection)
            way = application.Source.Direction;
        else if (application.Source.HasPoint)
            way = application.TargetPosition - application.Source.Point;
        else if (application.InstigatorPosition)
            way = application.TargetPosition - *application.InstigatorPosition;
        const Vec3d up = application.Up.Normalized();
        way = way - up * way.Dot(up);
        const float length = way.Magnitude();
        return std::isfinite(length) && length > 1.0e-4f ? way / length : Vec3d::Zero();
    }

    // The intent for the side a blow travelling along `way` struck, the front when it has none.
    GameplayTagId FlinchIntent(const EffectReactionRule& rule, const EffectReactionCase& application, Vec3d way)
    {
        if (!application.Forward || way.Magnitude() <= 0.0f)
            return rule.AnimationFront;
        const Vec3d forward = application.Forward->Normalized();
        const Vec3d right = forward.Cross(application.Up.Normalized());
        const float along = way.Dot(forward);
        const float across = way.Dot(right);
        GameplayTagId side;
        if (std::abs(along) >= std::abs(across))
            side = along > 0.0f ? rule.AnimationBack : rule.AnimationFront;
        else
            side = across > 0.0f ? rule.AnimationLeft : rule.AnimationRight;
        return side.IsValid() ? side : rule.AnimationFront;
    }
}

void RegisterEffectReactionProfileData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (!types.Register({ std::string(kEffectReactionProfileType), 1, Compile }))
        return;
    if (!schemas.Register(MakeSchema()))
        (void)types.Unregister(kEffectReactionProfileType);
}

bool ResolveEffectReactionProfile(const EffectReactionProfile& profile, const AttributeRegistry& attributes,
                                  const GameplayTagRegistry& tags, std::vector<EffectReactionRule>& out,
                                  std::vector<std::string>& errors)
{
    const std::size_t before = errors.size();
    out.clear();
    for (const EffectReactionDeclaration& reaction : profile.Reactions)
    {
        EffectReactionRule rule;
        ResolveQuery(reaction.Kinds, tags, rule.Kinds, errors);
        ResolveQuery(reaction.Recipient, tags, rule.Recipient, errors);
        rule.Attribute = attributes.FindAttribute(reaction.Attribute);
        if (!rule.Attribute.IsValid())
            errors.push_back(std::format("no attribute '{}'", reaction.Attribute));
        rule.Decrease = reaction.Decrease;
        rule.Threshold = reaction.Threshold;
        rule.ImpulsePerUnit = reaction.ImpulsePerUnit;
        rule.ImpulseMax = reaction.ImpulseMax;
        rule.Lift = reaction.Lift;
        rule.ShakeProfile = reaction.ShakeProfile;
        rule.ShakePerUnit = reaction.ShakePerUnit;
        rule.ShakeMax = reaction.ShakeMax;
        const auto intent = [&](const std::string& name) {
            if (name.empty())
                return GameplayTagId{};
            const GameplayTagId id = tags.FindTag(name);
            if (!id.IsValid())
                errors.push_back(std::format("no gameplay tag '{}'", name));
            return id;
        };
        rule.AnimationFront = intent(reaction.AnimationFront);
        rule.AnimationBack = intent(reaction.AnimationBack);
        rule.AnimationLeft = intent(reaction.AnimationLeft);
        rule.AnimationRight = intent(reaction.AnimationRight);
        rule.AnimationTicks = reaction.AnimationTicks;
        out.push_back(std::move(rule));
    }
    return errors.size() == before;
}

std::optional<EffectReactionOutcome> ReactToEffect(std::span<const EffectReactionRule> rules,
                                                   const EffectReactionCase& application,
                                                   const GameplayTagRegistry& tags)
{
    static const GameplayTagSet kNoKinds;
    static const GameplayTagContainer kNoTags{};
    const GameplayTagSet& kinds = application.Kinds != nullptr ? *application.Kinds : kNoKinds;
    const GameplayTagContainer& recipient = application.Recipient != nullptr ? *application.Recipient : kNoTags;
    for (std::size_t index = 0; index < rules.size(); ++index)
    {
        const EffectReactionRule& rule = rules[index];
        if (!rule.Kinds.Matches(kinds, tags) || !rule.Recipient.Matches(recipient, tags))
            continue;
        const auto change = std::ranges::find(application.Changes, rule.Attribute, &EffectAttributeChange::Attribute);
        if (change == application.Changes.end())
            continue;
        const float moved = change->After - change->Before;
        const float units = rule.Decrease ? -moved : moved;
        if (!(units > 0.0f) || units < rule.Threshold)
            continue;

        EffectReactionOutcome outcome;
        outcome.Rule = index;
        outcome.Units = units;
        const Vec3d way = PushDirection(application);
        const float speed = std::min(units * rule.ImpulsePerUnit, rule.ImpulseMax);
        if (speed > 0.0f && way.Magnitude() > 0.0f)
            outcome.Impulse = way * speed + application.Up.Normalized() * (speed * rule.Lift);
        if (rule.AnimationFront.IsValid())
        {
            outcome.AnimationIntent = FlinchIntent(rule, application, way);
            outcome.AnimationTicks = rule.AnimationTicks;
        }
        if (!rule.ShakeProfile.empty())
            outcome.ShakeScale = std::min(units * rule.ShakePerUnit, rule.ShakeMax);
        return outcome;
    }
    return std::nullopt;
}
