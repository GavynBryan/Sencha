#include "BenchRecorder.h"

#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

TEST(DataCompositionBench, Generate)
{
    const char* out = std::getenv("SENCHA_DATA_COMPOSITION_BENCH_OUT");
    if (out == nullptr)
        GTEST_SKIP() << "set SENCHA_DATA_COMPOSITION_BENCH_OUT to record the data composition bench";

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "sencha_data_composition_bench";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "effects");
    std::string modifiers;
    for (int i = 0; i < 40; ++i)
        modifiers += std::format(R"({}{{ "key": "m{}", "attribute": "Armor", "op": "add", "magnitude": {} }})",
                                 i == 0 ? "" : ", ", i, i);
    std::ofstream(root / "effects" / "base.sdata")
        << std::format(R"({{ "type": "effect.definition", "version": 1, "abstract": true,
            "data": {{ "duration": "infinite", "modifiers": [ {} ] }} }})", modifiers);
    constexpr int kVariants = 500;
    for (int i = 0; i < kVariants; ++i)
        std::ofstream(root / "effects" / std::format("variant{}.sdata", i))
            << std::format(R"({{ "type": "effect.definition", "version": 1, "extends": "asset://effects/base.sdata",
                "data": {{ "modifiers": {{ "m{}": {{ "magnitude": {} }} }} }} }})", i % 40, i);

    Bench::Recorder recorder;
    const int reps = Bench::RepsFromEnvironment("SENCHA_DATA_COMPOSITION_BENCH_REPS", 3);
    std::vector<double> samples;
    for (int rep = 0; rep < reps; ++rep)
    {
        LoggingProvider logging;
        ComponentSerializerRegistry serializers;
        RuntimeAssets assets(logging, serializers);
        (void)ScanAssetsDirectory(root.generic_string(), assets.Registry, assets.Assets.Kinds());
        std::vector<AssetLease> leases;
        const auto start = Bench::Clock::now();
        for (int i = 0; i < kVariants; ++i)
            leases.push_back(assets.Assets.LoadLease(std::format("asset://effects/variant{}.sdata", i), AssetType::Data));
        samples.push_back(Bench::MillisecondsSince(start));
        for (const AssetLease& lease : leases)
            ASSERT_TRUE(lease);
    }
    recorder.Record(std::format("data_composition.load_{}_variants_of_one_base", kVariants), "ms",
                    Bench::Median(samples));
    std::filesystem::remove_all(root);
    ASSERT_TRUE(recorder.WriteJson(out));
}
