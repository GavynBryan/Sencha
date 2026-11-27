#include "BenchRecorder.h"

#include "scene_source/SceneComposition.h"

#include <core/metadata/DataSchema.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <format>
#include <map>
#include <string>

namespace
{
class BenchSources final : public ISceneSourceLookup
{
public:
    void Add(std::string path, std::string_view text)
    {
        std::string error;
        std::optional<SceneSourceDocument> parsed = ParseSceneSource(text, &error);
        ASSERT_TRUE(parsed.has_value()) << path << ": " << error;
        Docs.emplace(std::move(path), std::move(*parsed));
    }

    const SceneSourceDocument* Find(std::string_view assetPath) override
    {
        const auto found = Docs.find(std::string(assetPath));
        return found != Docs.end() ? &found->second : nullptr;
    }

private:
    std::map<std::string, SceneSourceDocument> Docs;
};

class BenchRules final : public ISceneComponentRules
{
public:
    BenchRules()
    {
        Schemas.emplace("Attributes", MakeKeyedListRecordSchema("attributes", "name"));
        Schemas.emplace("GameplayTags", MakeKeyedListRecordSchema("tags", "name"));
        for (auto& [key, schema] : Schemas)
        {
            DataFieldSchema component = schema;
            component.Key = key;
            component.Required = false;
            Entity.Children.push_back(std::move(component));
        }
    }

    const DataFieldSchema& EntitySchema() const override { return Entity; }

    std::vector<std::string> OwedBy(std::string_view) const override { return {}; }
    bool IsRemovable(std::string_view) const override { return true; }

private:
    std::map<std::string, DataFieldSchema> Schemas;
    DataFieldSchema Entity = MakeDataField(DataFieldKind::Record, {}, {});
};

std::string Id(std::uint64_t value)
{
    return std::format("{:016x}", value);
}

std::string BaseScene()
{
    std::string entities;
    for (std::uint64_t i = 0; i < 10; ++i)
        entities += std::format(
            "{{ id: '{}', {} components: {{ Transform: {{ local: {{ position: [{}, 0, 0] }} }}, "
            "Attributes: {{ attributes: [ {{ name: 'Health', base: 100 }}, {{ name: 'Stealth', base: 5 }}, "
            "{{ name: 'Speed', base: 4 }}, {{ name: 'Armor', base: 2 }} ] }}, "
            "GameplayTags: {{ tags: [ {{ name: 'Creature.Humanoid', stacks: 1 }}, {{ name: 'Creature.Alive', stacks: 1 }} ] }}, "
            "Light: {{ intensity: 3, range: 5, falloff: {{ curve: 'smooth', power: 2 }} }} }} }},\n",
            Id(0xa0 + i), i == 0 ? std::string() : std::format("parent: '{}',", Id(0xa0)), i);
    return "{ format_version: 3, entities: [\n" + entities + "] }";
}

std::string Level(std::size_t placements)
{
    std::string instances;
    for (std::size_t p = 0; p < placements; ++p)
    {
        const std::uint64_t instance = 0x100000 + p * 0x20;
        std::string ids;
        for (std::uint64_t i = 0; i < 10; ++i)
            ids += std::format("'{}': '{}', ", Id(0xa0 + i), Id(instance + 1 + i));
        instances += std::format(
            "{{ id: '{}', source: 'asset://bench/top.sscene', entity_ids: {{ {} }}, "
            "patch: {{ '{}': {{ Attributes: {{ attributes: {{ Health: {{ base: {} }} }} }} }} }} }},\n",
            Id(instance), ids, Id(0xa3), 100 + p % 50);
    }
    return "{ format_version: 3, entities: [], instances: [\n" + instances + "] }";
}

double ResolveMilliseconds(const SceneSourceDocument& level, BenchSources& sources, const BenchRules& rules, int reps)
{
    std::vector<double> samples;
    for (int i = 0; i < reps; ++i)
    {
        std::string error;
        const auto start = Bench::Clock::now();
        const std::optional<SceneCompositionResult> result = ResolveSceneComposition(level, sources, &error, &rules);
        samples.push_back(Bench::MillisecondsSince(start));
        EXPECT_TRUE(result.has_value()) << error;
    }
    return Bench::Median(samples);
}
} // namespace

TEST(SceneCompositionBench, Generate)
{
    const char* out = std::getenv("SENCHA_SCENE_COMPOSITION_BENCH_OUT");
    if (out == nullptr)
        GTEST_SKIP() << "set SENCHA_SCENE_COMPOSITION_BENCH_OUT to record the scene composition bench";

    BenchSources sources;
    sources.Add("asset://bench/base.sscene", BaseScene());
    sources.Add("asset://bench/fragment.sscene", R"({ format_version: 3, abstract: true,
        entities: [ { id: '00000000000000f1', components: { Alertable: { radius: 5 },
            GameplayTags: { tags: [ { name: 'Creature.Alert', stacks: 1 } ] } } } ] })");
    sources.Add("asset://bench/mid.sscene", std::format(R"({{ format_version: 3, extends: 'asset://bench/base.sscene',
        includes: [ {{ fragment: 'asset://bench/fragment.sscene' }} ],
        patch: {{ '{}': {{ Attributes: {{ attributes: {{ Health: {{ base: 150 }} }} }} }},
                  '{}': {{ Light: {{ intensity: 6 }} }} }} }})", Id(0xa0), Id(0xa5)));
    sources.Add("asset://bench/top.sscene", std::format(R"({{ format_version: 3, extends: 'asset://bench/mid.sscene',
        patch: {{ '{}': {{ GameplayTags: {{ tags: {{ 'Creature.Alive': null }} }} }} }} }})", Id(0xa1)));
    const BenchRules rules;

    Bench::Recorder recorder;
    const int reps = Bench::RepsFromEnvironment("SENCHA_SCENE_COMPOSITION_BENCH_REPS", 5);
    for (const std::size_t placements : { std::size_t{ 200 }, std::size_t{ 2000 } })
    {
        std::string error;
        const std::optional<SceneSourceDocument> level = ParseSceneSource(Level(placements), &error);
        ASSERT_TRUE(level.has_value()) << error;
        recorder.Record(std::format("scene_composition.resolve_{}_placements", placements), "ms",
                        ResolveMilliseconds(*level, sources, rules, reps));
    }
    ASSERT_TRUE(recorder.WriteJson(out));
}
