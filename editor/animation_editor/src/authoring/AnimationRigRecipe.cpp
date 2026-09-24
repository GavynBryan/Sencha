#include "authoring/AnimationRigRecipe.h"

#include <anim/AnimationClipCache.h>

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

AnimationRigPlan PlanAnimationRig(const AnimationRigRecipe& recipe, const AnimationClipCache& clips)
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

    const std::string folder = "animation/" + recipe.Name + "/";
    const std::string asset = "asset://" + folder + recipe.Name;
    JsonValue::Array behaviorDecls;
    JsonValue::Array rows;
    JsonValue::Array intents;
    for (std::size_t i = 0; i < behaviors.size(); ++i)
    {
        behaviorDecls.emplace_back(JsonValue::Object{ { "tag", JsonValue(behaviors[i]) },
                                                      { "kind", JsonValue("cyclic") } });
        rows.emplace_back(JsonValue::Object{ { "behavior", JsonValue(behaviors[i]) },
                                             { "clip", JsonValue(recipe.Clips[i]) } });
        if (i > 0)
            intents.emplace_back(JsonValue::Object{ { "intent", JsonValue(behaviors[i]) },
                                                    { "params", JsonValue(JsonValue::Array{}) } });
    }

    plan.Documents.push_back({ folder + recipe.Name + ".behaviors.sdata",
                               Document("animation.behavior_set",
                                        JsonValue(JsonValue::Object{ { "behaviors", JsonValue(std::move(behaviorDecls)) } })) });
    plan.Documents.push_back({ folder + recipe.Name + ".slots.sdata",
                               Document("animation.slot_map",
                                        JsonValue(JsonValue::Object{ { "rows", JsonValue(std::move(rows)) } })) });
    JsonValue::Object rig{
        { "behaviors", Strings({ asset + ".behaviors.sdata" }) },
        { "slot_maps", Strings({ asset + ".slots.sdata" }) },
        { "layers", JsonValue(JsonValue::Array{ JsonValue(JsonValue::Object{
                        { "name", JsonValue("anim.layer.base") }, { "idle", JsonValue(behaviors.front()) } }) }) },
    };
    if (!skeleton.empty())
        rig.emplace(rig.begin(), "skeleton", JsonValue(skeleton));
    if (!intents.empty())
    {
        // Every clip past the idle plays while a request of its name is held.
        plan.Documents.push_back({ folder + recipe.Name + ".requests.sdata",
                                   Document("animation.request_schema",
                                            JsonValue(JsonValue::Object{ { "intents", JsonValue(std::move(intents)) } })) });
        rig.emplace_back("requests", JsonValue(asset + ".requests.sdata"));
    }
    plan.Documents.push_back({ folder + recipe.Name + ".rig.sdata", Document("animation.rig", JsonValue(std::move(rig))) });
    plan.RigPath = asset + ".rig.sdata";

    plan.Scenario.RelativePath = folder + recipe.Name + ".rig.sanimscenario";
    plan.Scenario.Root = JsonValue(JsonValue::Object{
        { "type", JsonValue("animation.preview_scenario") },
        { "version", JsonValue(1.0) },
        { "name", JsonValue(recipe.Name) },
        { "rig", JsonValue(plan.RigPath) },
        { "tick_rate", JsonValue(60.0) },
        { "participants", Strings({ "player" }) },
        { "declared_tags", Strings(behaviors) },
    });
    return plan;
}
