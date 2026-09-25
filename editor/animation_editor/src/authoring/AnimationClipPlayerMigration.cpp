#include "authoring/AnimationClipPlayerMigration.h"

#include <anim/AnimationClipCache.h>
#include <core/json/JsonParser.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>

namespace
{
    constexpr std::string_view kPlayerKey = "AnimationClipPlayer";

    std::optional<JsonValue> ReadJson(const std::filesystem::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return JsonParse(text.str());
    }

    double NumberOr(const JsonValue& object, std::string_view key, double fallback)
    {
        const JsonValue* value = object.Find(key);
        return value != nullptr && value->IsNumber() ? value->AsNumber() : fallback;
    }

    JsonValue Document(std::string type, JsonValue data)
    {
        return JsonValue(JsonValue::Object{ { "type", JsonValue(std::move(type)) },
                                            { "version", JsonValue(1.0) },
                                            { "data", std::move(data) } });
    }

    // A clip's name as a tag segment: letters, digits and underscores.
    std::string Segment(const std::string& clipPath)
    {
        std::string name = AnimationRigBehaviorFor(clipPath);
        return name.substr(name.find('.') + 1);
    }
}

std::vector<AnimationClipPlayerUse> FindAnimationClipPlayers(const std::filesystem::path& root,
                                                             std::vector<std::string>& problems)
{
    std::vector<AnimationClipPlayerUse> uses;
    std::vector<std::filesystem::path> scenes;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
    {
        if (ec)
            break;
        // Cooked output is rebuilt from its source.
        if (it->is_directory() && it->path().filename() == ".cooked")
        {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file() && it->path().extension() == ".sscene")
            scenes.push_back(it->path());
    }
    std::ranges::sort(scenes);
    for (const std::filesystem::path& file : scenes)
    {
        const std::string relative = std::filesystem::relative(file, root).generic_string();
        const std::optional<JsonValue> scene = ReadJson(file);
        const JsonValue* entities = scene ? scene->Find("entities") : nullptr;
        if (entities == nullptr || !entities->IsArray())
        {
            problems.push_back(std::format("{} is not a scene this can read.", relative));
            continue;
        }
        for (const JsonValue& entity : entities->AsArray())
        {
            const JsonValue* components = entity.Find("components");
            const JsonValue* player = components != nullptr ? components->Find(kPlayerKey) : nullptr;
            if (player == nullptr || !player->IsObject())
                continue;
            AnimationClipPlayerUse use;
            use.Scene = relative;
            if (const JsonValue* id = entity.Find("id"); id != nullptr && id->IsString())
                use.Entity = id->AsString();
            if (const JsonValue* clip = player->Find("clip"); clip != nullptr && clip->IsString())
                use.Clip = clip->AsString();
            use.TimeSeconds = NumberOr(*player, "time_seconds", 0.0);
            use.Rate = NumberOr(*player, "rate", 1.0);
            if (const JsonValue* loop = player->Find("loop"); loop != nullptr && loop->IsBool())
                use.Loop = loop->AsBool();
            uses.push_back(std::move(use));
        }
    }
    return uses;
}

AnimationClipPlayerMigrationPlan PlanAnimationClipPlayerMigration(const std::filesystem::path& root,
                                                                  const std::vector<AnimationClipPlayerUse>& uses,
                                                                  const AnimationClipCache& clips)
{
    AnimationClipPlayerMigrationPlan plan;
    if (uses.empty())
        return plan;

    // One rig per clip and settings; the same clip played two ways is two
    // rigs, numbered in the order first met.
    using Settings = std::tuple<std::string, double, double, bool>;
    std::map<Settings, std::string> rigs;
    std::map<std::string, int> variants;
    std::vector<std::string> tags;
    for (const AnimationClipPlayerUse& use : uses)
    {
        const Settings key{ use.Clip, use.TimeSeconds, use.Rate, use.Loop };
        if (rigs.contains(key))
            continue;
        const AnimationClipData* clip = clips.Get(clips.Find(use.Clip));
        if (clip == nullptr)
        {
            plan.Error = std::format("{} entity {} plays '{}', which is not a loaded clip.", use.Scene, use.Entity,
                                     use.Clip);
            return plan;
        }
        const std::string segment = Segment(use.Clip);
        const int variant = ++variants[segment];
        const std::string name = variant == 1 ? segment : std::format("{}_{}", segment, variant);
        const std::string stem = "animation/migrated/" + name;
        const std::string tag = "Anim.Migrated." + name;
        tags.push_back(tag);

        JsonValue::Object behavior{ { "tag", JsonValue(tag) },
                                    { "kind", JsonValue(use.Loop ? "cyclic" : "one_shot") } };
        if (use.Rate != 1.0)
            behavior.emplace_back("rate", JsonValue(use.Rate));
        if (use.TimeSeconds != 0.0)
            behavior.emplace_back("start_seconds", JsonValue(use.TimeSeconds));
        plan.Documents.push_back({ stem + ".behaviors.sdata",
                                   Document("animation.behavior_set",
                                            JsonValue(JsonValue::Object{ { "behaviors", JsonValue(JsonValue::Array{
                                                                                            JsonValue(std::move(behavior)) }) } })) });
        plan.Documents.push_back(
            { stem + ".slots.sdata",
              Document("animation.slot_map",
                       JsonValue(JsonValue::Object{ { "rows", JsonValue(JsonValue::Array{ JsonValue(JsonValue::Object{
                                                                  { "behavior", JsonValue(tag) },
                                                                  { "clip", JsonValue(use.Clip) } }) }) } })) });
        JsonValue::Object rig{
            { "behaviors", JsonValue(JsonValue::Array{ JsonValue("asset://" + stem + ".behaviors.sdata") }) },
            { "slot_maps", JsonValue(JsonValue::Array{ JsonValue("asset://" + stem + ".slots.sdata") }) },
            { "layers", JsonValue(JsonValue::Array{ JsonValue(JsonValue::Object{
                            { "name", JsonValue("anim.layer.base") }, { "idle", JsonValue(tag) } }) }) },
        };
        if (!clip->SkeletonPath.empty())
            rig.emplace(rig.begin(), "skeleton", JsonValue(clip->SkeletonPath));
        plan.Documents.push_back({ stem + ".rig.sdata", Document("animation.rig", JsonValue(std::move(rig))) });
        rigs.emplace(key, "asset://" + stem + ".rig.sdata");
    }
    JsonValue::Array declared;
    for (const std::string& tag : tags)
        declared.emplace_back(tag);
    plan.Documents.push_back({ "animation/migrated/migrated.tags.sdata",
                               Document("gameplay.tag_declarations",
                                        JsonValue(JsonValue::Object{ { "tags", JsonValue(std::move(declared)) } })) });

    // Each scene, with every player's entity carrying its rig instead.
    std::vector<std::string> scenes;
    for (const AnimationClipPlayerUse& use : uses)
        if (std::ranges::find(scenes, use.Scene) == scenes.end())
            scenes.push_back(use.Scene);
    for (const std::string& relative : scenes)
    {
        std::optional<JsonValue> scene = ReadJson(root / relative);
        if (!scene)
        {
            plan.Error = std::format("{} changed and no longer parses.", relative);
            return plan;
        }
        for (JsonValue& entity : scene->Find("entities")->AsArray())
        {
            JsonValue* components = entity.Find("components");
            const JsonValue* player = components != nullptr ? components->Find(kPlayerKey) : nullptr;
            if (player == nullptr || !player->IsObject())
                continue;
            const Settings key{ player->Find("clip") != nullptr && player->Find("clip")->IsString()
                                    ? player->Find("clip")->AsString()
                                    : std::string(),
                                NumberOr(*player, "time_seconds", 0.0), NumberOr(*player, "rate", 1.0),
                                player->Find("loop") == nullptr || !player->Find("loop")->IsBool()
                                    || player->Find("loop")->AsBool() };
            const auto rig = rigs.find(key);
            if (rig == rigs.end())
                continue;
            JsonValue::Object& fields = components->AsObject();
            std::erase_if(fields, [](const auto& field) { return field.first == kPlayerKey; });
            fields.emplace_back("anim_rig", JsonValue(JsonValue::Object{ { "rig", JsonValue(rig->second) } }));
        }
        plan.Scenes.push_back({ relative, std::move(*scene) });
    }
    return plan;
}
