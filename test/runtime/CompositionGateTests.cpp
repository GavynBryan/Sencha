// Real definitions built on others: each loads composed and follows its
// source's edits through hot reload (docs/assets/composition.md).

#include <gtest/gtest.h>

#include <abilities/AbilityDefinitionData.h>
#include <abilities/CuePresentationData.h>
#include <assets/cook/AssetImporter.h>
#include <assets/hotreload/AssetHotReloader.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <jobs/AsyncTaskQueue.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace
{
    struct GateContent
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path()
            / ("sencha_composition_gate_" + std::to_string(std::random_device{}()));
        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        RuntimeAssets Assets{ Logging, Serializers };

        GateContent()
        {
            std::filesystem::copy(std::filesystem::path(SENCHA_REPO_ROOT) / "test/fixtures/abilities/assets", Root,
                                  std::filesystem::copy_options::recursive);
            std::filesystem::remove_all(Root / ".cooked");
            Rescan();
        }

        ~GateContent()
        {
            std::error_code ec;
            std::filesystem::remove_all(Root, ec);
        }

        void Rescan() { (void)ScanAssetsDirectory(Root.generic_string(), Assets.Registry, Assets.Assets.Kinds()); }

        void Write(const std::string& relative, std::string_view text)
        {
            std::filesystem::create_directories((Root / relative).parent_path());
            std::ofstream(Root / relative, std::ios::trunc) << text;
            Rescan();
        }

        void Changed(const std::string& relative)
        {
            AssetImporterRegistry importers;
            AsyncTaskQueue tasks{ 0 };
            AssetHotReloader reloader(Logging, Assets.Assets, Assets.Registry, importers, tasks, Root.generic_string());
            reloader.ReloadSource(relative);
            (void)tasks.PumpWork();
            (void)tasks.DrainCompletions();
        }

        [[nodiscard]] std::string ReadFile(const std::string& relative) const
        {
            std::ifstream in(Root / relative);
            return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
        }

        template<typename T>
        [[nodiscard]] const T* Value(const AssetLease& lease, std::string_view type) const
        {
            return Assets.DataAssets.TryGet<T>(DataAssetHandle::FromToken(lease.OpaqueToken()), type);
        }

        [[nodiscard]] std::string FailureOf(std::string_view path) const
        {
            for (const AssetSystem::LoadFailure& failure : Assets.Assets.LoadFailures())
                if (failure.Path == path)
                    return failure.Reason;
            return {};
        }
    };

    const AbilityArgumentDecl* Argument(const AbilityDeclaration& ability, std::string_view phase, std::string_view op,
                                        std::string_view name)
    {
        const auto inPhase = std::ranges::find(ability.Phases, phase, &AbilityPhaseDecl::Key);
        if (inPhase == ability.Phases.end())
            return nullptr;
        const auto found = std::ranges::find(inPhase->Enter, op, &AbilityOpDecl::Key);
        if (found == inPhase->Enter.end())
            return nullptr;
        const auto argument = std::ranges::find(found->Arguments, name, &AbilityArgumentDecl::Name);
        return argument != found->Arguments.end() ? &*argument : nullptr;
    }

    double Range(const AbilityDeclaration& ability)
    {
        const AbilityArgumentDecl* range = Argument(ability, "p_fire", "o_trace", "range");
        return range != nullptr && range->Value.Constant.Literal.IsNumber() ? range->Value.Constant.Literal.AsNumber()
                                                                             : -1.0;
    }
}

TEST(CompositionGate, AnAbilityVariantPatchesOneOpsArgumentByKeyAndFollowsItsBase)
{
    GateContent content;
    content.Write("abilities/charge_long.sdata", R"({ "type": "ability.definition", "version": 1,
        "extends": "asset://abilities/charge_release.sdata",
        "data": { "phases": { "p_fire": { "enter": { "o_trace": { "arguments": { "range": { "const": 120.0 } } } } } } } })");

    AssetLease variant = content.Assets.Assets.LoadLease("asset://abilities/charge_long.sdata", AssetType::Data);
    ASSERT_TRUE(variant) << content.FailureOf("asset://abilities/charge_long.sdata");
    const AbilityDeclaration* ability = content.Value<AbilityDeclaration>(variant, kAbilityDefinitionType);
    ASSERT_NE(ability, nullptr);
    EXPECT_DOUBLE_EQ(Range(*ability), 120.0) << "the patched argument";
    ASSERT_NE(Argument(*ability, "p_fire", "o_trace", "origin"), nullptr) << "its sibling arguments are inherited";
    EXPECT_EQ(ability->Phases.size(), 3u);
    EXPECT_EQ(ability->Phases[0].ProgressTicks, 60u);

    std::string base = content.ReadFile("abilities/charge_release.sdata");
    const std::size_t at = base.find(R"("progress_ticks": 60)");
    ASSERT_NE(at, std::string::npos);
    base.replace(at, std::string_view(R"("progress_ticks": 60)").size(), R"("progress_ticks": 90)");
    content.Write("abilities/charge_release.sdata", base);
    content.Changed("abilities/charge_release.sdata");
    ability = content.Value<AbilityDeclaration>(variant, kAbilityDefinitionType);
    EXPECT_EQ(ability->Phases[0].ProgressTicks, 90u) << "the base's edit reached the variant";
    EXPECT_DOUBLE_EQ(Range(*ability), 120.0) << "and the variant's own patch held";
}

TEST(CompositionGate, ACuePresentationIncludesAFragmentAndFollowsIt)
{
    GateContent content;
    content.Write("cues/muzzle.sdata", R"({ "type": "cue.presentation", "version": 1, "abstract": true,
        "data": { "cues": [ { "key": "k_muzzle", "cue": "cue.muzzle", "sustain": [ { "lease": "test.flash" } ] } ] } })");
    content.Write("cues/rifle.sdata", R"({ "type": "cue.presentation", "version": 1,
        "includes": [ "asset://cues/muzzle.sdata" ],
        "data": { "cues": { "k_shot": { "cue": "cue.shot", "sustain": [ { "lease": "test.glow" } ] } } } })");

    AssetLease rifle = content.Assets.Assets.LoadLease("asset://cues/rifle.sdata", AssetType::Data);
    ASSERT_TRUE(rifle) << content.FailureOf("asset://cues/rifle.sdata");
    const CuePresentationDeclaration* cues = content.Value<CuePresentationDeclaration>(rifle, kCuePresentationType);
    ASSERT_NE(cues, nullptr);
    ASSERT_EQ(cues->Entries.size(), 2u);
    EXPECT_EQ(cues->Entries[0].Key, "k_muzzle");
    EXPECT_EQ(cues->Entries[1].Key, "k_shot");

    content.Write("cues/muzzle.sdata", R"({ "type": "cue.presentation", "version": 1, "abstract": true,
        "data": { "cues": [ { "key": "k_muzzle", "cue": "cue.muzzle_heavy", "sustain": [ { "lease": "test.flash" } ] } ] } })");
    content.Changed("cues/muzzle.sdata");
    cues = content.Value<CuePresentationDeclaration>(rifle, kCuePresentationType);
    ASSERT_EQ(cues->Entries.size(), 2u);
    EXPECT_EQ(cues->Entries[0].Cue, "cue.muzzle_heavy") << "the fragment's edit reached the presentation";
}
