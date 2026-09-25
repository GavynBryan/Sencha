#include "authoring/AnimationRigRecipe.h"

#include <anim/AnimationClipCache.h>
#include <anim/SkeletonCache.h>

#include <algorithm>
#include <cctype>
#include <format>

namespace
{
    JsonValue Document(std::string type, JsonValue data)
    {
        return JsonValue(JsonValue::Object{ { "type", JsonValue(std::move(type)) },
                                            { "version", JsonValue(1.0) },
                                            { "data", std::move(data) } });
    }

    JsonValue Strings(const std::vector<std::string>& values)
    {
        JsonValue::Array array;
        for (const std::string& value : values)
            array.emplace_back(value);
        return JsonValue(std::move(array));
    }
}

std::string AnimationRigBehaviorFor(const std::string& clipPath)
{
    // The name after '#anim:' for a clip cooked from a mesh source, else the
    // file's stem.
    std::string name;
    if (const std::size_t anim = clipPath.rfind("#anim:"); anim != std::string::npos)
        name = clipPath.substr(anim + 6);
    else
    {
        const std::size_t slash = clipPath.find_last_of('/');
        name = clipPath.substr(slash == std::string::npos ? 0 : slash + 1);
        name = name.substr(0, name.find('.'));
    }
    for (char& c : name)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            c = '_';
    if (name.empty() || !std::isalpha(static_cast<unsigned char>(name.front())))
        name = "Clip_" + name;
    name.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(name.front())));
    return "Anim." + name;
}

std::string_view AnimationRigPresetName(AnimationRigPreset preset)
{
    switch (preset)
    {
    case AnimationRigPreset::Prop: return "Prop";
    case AnimationRigPreset::Simple: return "Simple";
    case AnimationRigPreset::Character: return "Character";
    }
    return "Prop";
}

namespace
{
    constexpr std::string_view kEngineFacts = "asset://animation/engine.facts.sdata";
    constexpr std::string_view kUpperLayer = "anim.layer.upper";

    JsonValue Object(JsonValue::Object object) { return JsonValue(std::move(object)); }

    JsonValue Rule(std::string name, int priority, JsonValue::Array enter, std::string behavior)
    {
        JsonValue::Object rule{ { "name", JsonValue(std::move(name)) },
                                { "priority", JsonValue(static_cast<double>(priority)) },
                                { "enter", JsonValue(std::move(enter)) } };
        if (!behavior.empty())
            rule.emplace_back("behavior", JsonValue(std::move(behavior)));
        return Object(std::move(rule));
    }

    JsonValue RequestTest(const std::string& intent) { return Object({ { "request", JsonValue(intent) } }); }
}

AnimationRigPlan PlanAnimationRig(const AnimationRigRecipe& recipe, const AnimationClipCache& clips,
                                  const SkeletonCache* skeletons)
{
    AnimationRigPlan plan;
    const bool validName = !recipe.Name.empty() && std::ranges::all_of(recipe.Name, [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    });
    if (!validName)
    {
        plan.Error = "A rig's name is letters, digits and underscores.";
        return plan;
    }
    if (recipe.Clips.empty())
    {
        plan.Error = "Choose the clips the rig plays; the first is its idle.";
        return plan;
    }
    const AnimationRigPreset preset = recipe.Preset;
    if (preset == AnimationRigPreset::Character && recipe.Clips.size() < 3)
    {
        plan.Error = "A character plays an idle, a locomotion clip and at least one action; choose three clips "
                     "or more.";
        return plan;
    }

    std::string skeleton;
    std::vector<std::string> behaviors;
    for (const std::string& path : recipe.Clips)
    {
        const AnimationClipData* clip = clips.Get(clips.Find(path));
        if (clip == nullptr)
        {
            plan.Error = std::format("'{}' is not a loaded clip.", path);
            return plan;
        }
        if (skeleton.empty())
            skeleton = clip->SkeletonPath;
        else if (clip->SkeletonPath != skeleton)
        {
            plan.Error = std::format("'{}' animates '{}' and the first clip '{}'; one rig poses one skeleton.",
                                     path, clip->SkeletonPath, skeleton);
            return plan;
        }
        const std::string behavior = AnimationRigBehaviorFor(path);
        if (std::ranges::find(behaviors, behavior) != behaviors.end())
        {
            plan.Error = std::format("Two clips would both play as {}; choose one of them.", behavior);
            return plan;
        }
        behaviors.push_back(behavior);
    }
    if (preset == AnimationRigPreset::Character)
    {
        if (recipe.UpperBodyJoint.empty())
        {
            plan.Error = "Name the joint the upper body starts at: the actions play from there up.";
            return plan;
        }
        if (skeletons != nullptr)
        {
            const SkeletonData* data = skeletons->Get(skeletons->Find(skeleton));
            if (data != nullptr
                && std::ranges::none_of(data->Joints,
                                        [&](const SkeletonJoint& joint) { return joint.Name == recipe.UpperBodyJoint; }))
            {
                plan.Error = std::format("'{}' has no joint named '{}'.", skeleton, recipe.UpperBodyJoint);
                return plan;
            }
        }
    }

    // What each clip is: the idle, locomotion (a selector tier's second
    // clip), and actions.
    const bool selects = preset != AnimationRigPreset::Prop;
    const std::size_t firstAction = selects && behaviors.size() > 1 ? 2 : 1;
    const std::string folder = "animation/" + recipe.Name + "/";
    const std::string asset = "asset://" + folder + recipe.Name;

    JsonValue::Array behaviorDecls;
    JsonValue::Array rows;
    JsonValue::Array intents;
    for (std::size_t i = 0; i < behaviors.size(); ++i)
    {
        const bool action = i >= firstAction;
        JsonValue::Object decl{ { "tag", JsonValue(behaviors[i]) },
                                { "kind", JsonValue(selects && action ? "one_shot" : "cyclic") } };
        // A selector's action plays once through even if its request is let
        // go; a prop's plays for as long as it is held.
        if (selects && action)
            decl.emplace_back("latch", Object({ { "mode", JsonValue("until_complete") } }));
        behaviorDecls.emplace_back(std::move(decl));
        rows.emplace_back(Object({ { "behavior", JsonValue(behaviors[i]) }, { "clip", JsonValue(recipe.Clips[i]) } }));
        if (action)
            intents.emplace_back(Object({ { "intent", JsonValue(behaviors[i]) },
                                          { "params", JsonValue(JsonValue::Array{}) } }));
    }

    plan.Documents.push_back({ folder + recipe.Name + ".behaviors.sdata",
                               Document("animation.behavior_set",
                                        Object({ { "behaviors", JsonValue(std::move(behaviorDecls)) } })) });
    plan.Documents.push_back({ folder + recipe.Name + ".slots.sdata",
                               Document("animation.slot_map", Object({ { "rows", JsonValue(std::move(rows)) } })) });

    JsonValue::Array layers;
    if (!selects)
    {
        layers.emplace_back(Object({ { "name", JsonValue("anim.layer.base") }, { "idle", JsonValue(behaviors.front()) } }));
    }
    else
    {
        // The base selector: idle, locomotion on speed, and -- for a Simple
        // rig -- the actions over both.
        JsonValue::Array base;
        base.emplace_back(Rule("idle", 0, {}, behaviors.front()));
        if (behaviors.size() > 1)
            base.emplace_back(Rule("move", 10,
                                   JsonValue::Array{ Object({ { "fact", JsonValue("Speed") },
                                                              { "compare", JsonValue("gt") },
                                                              { "value", JsonValue(0.1) } }) },
                                   behaviors[1]));
        JsonValue::Array upper;
        JsonValue::Array anyAction;
        for (std::size_t i = firstAction; i < behaviors.size(); ++i)
        {
            const std::string name = behaviors[i].substr(behaviors[i].find('.') + 1);
            JsonValue rule = Rule(name, 50, JsonValue::Array{ RequestTest(behaviors[i]) }, behaviors[i]);
            (preset == AnimationRigPreset::Character ? upper : base).push_back(std::move(rule));
            anyAction.push_back(RequestTest(behaviors[i]));
        }
        plan.Documents.push_back({ folder + recipe.Name + ".selector.sdata",
                                   Document("animation.selector", Object({ { "rules", JsonValue(std::move(base)) } })) });
        layers.emplace_back(Object({ { "name", JsonValue("anim.layer.base") },
                                     { "selector", JsonValue(asset + ".selector.sdata") },
                                     { "idle", JsonValue(behaviors.front()) } }));
        if (preset == AnimationRigPreset::Character)
        {
            // Shown only while an action is asked for; otherwise the base
            // layer's locomotion shows through.
            upper.emplace(upper.begin(), Rule("rest", 0, {}, behaviors.front()));
            JsonValue shown = Rule("shown", 10, JsonValue::Array{ Object({ { "any", JsonValue(std::move(anyAction)) } }) }, {});
            shown.AsObject().emplace_back("weight", JsonValue(1.0));
            upper.push_back(std::move(shown));
            JsonValue hidden = Rule("hidden", 0, {}, {});
            hidden.AsObject().emplace_back("weight", JsonValue(0.0));
            upper.push_back(std::move(hidden));
            plan.Documents.push_back({ folder + recipe.Name + ".upper.selector.sdata",
                                       Document("animation.selector",
                                                Object({ { "rules", JsonValue(std::move(upper)) } })) });
            layers.emplace_back(Object({ { "name", JsonValue(std::string(kUpperLayer)) },
                                         { "selector", JsonValue(asset + ".upper.selector.sdata") },
                                         { "idle", JsonValue(behaviors.front()) },
                                         { "mask", JsonValue(JsonValue::Array{ Object(
                                                       { { "joint", JsonValue(recipe.UpperBodyJoint) } }) }) } }));
        }
    }

    JsonValue::Object rig{
        { "behaviors", Strings({ asset + ".behaviors.sdata" }) },
        { "slot_maps", Strings({ asset + ".slots.sdata" }) },
        { "layers", JsonValue(std::move(layers)) },
    };
    if (selects)
        rig.emplace(rig.begin(), "facts", JsonValue(std::string(kEngineFacts)));
    if (!skeleton.empty())
        rig.emplace(rig.begin(), "skeleton", JsonValue(skeleton));
    if (!intents.empty())
    {
        plan.Documents.push_back({ folder + recipe.Name + ".requests.sdata",
                                   Document("animation.request_schema",
                                            Object({ { "intents", JsonValue(std::move(intents)) } })) });
        rig.emplace_back("requests", JsonValue(asset + ".requests.sdata"));
    }
    plan.Documents.push_back({ folder + recipe.Name + ".rig.sdata", Document("animation.rig", JsonValue(std::move(rig))) });
    plan.RigPath = asset + ".rig.sdata";

    JsonValue::Object scenario{
        { "type", JsonValue("animation.preview_scenario") },
        { "version", JsonValue(1.0) },
        { "name", JsonValue(recipe.Name) },
        { "rig", JsonValue(plan.RigPath) },
        { "tick_rate", JsonValue(60.0) },
        { "participants", Strings({ "player" }) },
        { "declared_tags", Strings(behaviors) },
    };
    // A selector reads facts; the scenario starts them standing still.
    if (selects)
        scenario.emplace_back("inputs", Object({ { "Speed", JsonValue(0.0) }, { "Grounded", JsonValue(true) } }));
    plan.Scenario.RelativePath = folder + recipe.Name + ".rig.sanimscenario";
    plan.Scenario.Root = JsonValue(std::move(scenario));
    return plan;
}
