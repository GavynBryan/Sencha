#include <gtest/gtest.h>

#include <assets/data/DataAssetSubtype.h>
#include <assets/data/DataDefinitionStaging.h>
#include <assets/cook/AssetImporter.h>
#include <assets/hotreload/AssetHotReloader.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonEquals.h>
#include <core/json/JsonParser.h>
#include <effects/EffectDefinitionData.h>
#include <equipment/EquipmentLayoutData.h>
#include <inventory/ItemDefinitionData.h>
#include <jobs/AsyncTaskQueue.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <string>

namespace
{
    constexpr std::string_view kArmorBase = R"({ "type": "effect.definition", "version": 1, "abstract": true,
        "data": { "duration": "infinite", "modifiers": [
            { "key": "m_armor", "attribute": "Armor", "op": "add", "magnitude": 10 },
            { "key": "m_speed", "attribute": "MoveSpeed", "op": "add", "magnitude": 1 } ] } })";

    struct Content
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path() / "sencha_data_definition_composition";
        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        RuntimeAssets Assets{ Logging, Serializers };

        Content()
        {
            std::filesystem::remove_all(Root);
            std::filesystem::create_directories(Root / "effects");
        }

        ~Content() { std::filesystem::remove_all(Root); }

        void Write(const std::string& name, std::string_view text)
        {
            std::ofstream(Root / "effects" / name) << text;
            (void)ScanAssetsDirectory(Root.generic_string(), Assets.Registry, Assets.Assets.Kinds());
        }

        [[nodiscard]] const EffectDeclaration* Effect(const AssetLease& lease) const
        {
            return Assets.DataAssets.TryGet<EffectDeclaration>(DataAssetHandle::FromToken(lease.OpaqueToken()),
                                                               kEffectDefinitionType);
        }

        void Changed(const std::string& name, std::string_view held = {})
        {
            AssetImporterRegistry importers;
            AsyncTaskQueue tasks{ 0 };
            AssetHotReloader reloader(Logging, Assets.Assets, Assets.Registry, importers, tasks, Root.generic_string());
            reloader.SetHostHeld([held](const AssetRecord& record) { return record.Path == held; });
            reloader.ReloadSource("effects/" + name);
            (void)tasks.PumpWork();
            (void)tasks.DrainCompletions();
        }

        [[nodiscard]] std::uint64_t ReloadVersion(const AssetLease& lease) const
        {
            return Assets.DataAssets.GetReloadVersion(DataAssetHandle::FromToken(lease.OpaqueToken()));
        }

        [[nodiscard]] std::string FailureOf(std::string_view path) const
        {
            for (const AssetSystem::LoadFailure& failure : Assets.Assets.LoadFailures())
                if (failure.Path == path)
                    return failure.Reason;
            return {};
        }
    };

    struct Memory
    {
        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        RuntimeAssets Assets{ Logging, Serializers };
        std::map<std::string, JsonValue, std::less<>> Sources;

        void Add(std::string path, std::string_view text) { Sources.emplace(std::move(path), *JsonParse(text)); }

        DataDefinitionStaging Stage(std::string_view text, bool provenance = false)
        {
            const JsonValue root = *JsonParse(text);
            const AssetPathsById current;
            return StageDataDefinition(root, DataDefinitionContext{
                .Types = Assets.DataTypes,
                .Schemas = &Assets.DataSchemas,
                .Current = current,
                .Read = [this](std::string_view path) -> const JsonValue* {
                    const auto found = Sources.find(path);
                    return found != Sources.end() ? &found->second : nullptr;
                },
                .Asset = "asset://effects/child.sdata",
                .Provenance = provenance });
        }
    };

    [[nodiscard]] const DataValidationError* ErrorAt(const DataDefinitionStaging& staged, std::string_view path)
    {
        const auto found = std::ranges::find(staged.Errors, path, &DataValidationError::Path);
        return found == staged.Errors.end() ? nullptr : &*found;
    }
}

TEST(DataDefinitionComposition, AChildLoadsComposedOverItsBase)
{
    Content content;
    content.Write("armor_base.sdata", kArmorBase);
    content.Write("tunic.sdata", R"({ "type": "effect.definition", "version": 1,
        "extends": "asset://effects/armor_base.sdata",
        "data": { "modifiers": { "m_armor": { "magnitude": 14 }, "m_speed": null } } })");

    AssetLease tunic = content.Assets.Assets.LoadLease("asset://effects/tunic.sdata", AssetType::Data);
    ASSERT_TRUE(tunic) << content.FailureOf("asset://effects/tunic.sdata");
    const EffectDeclaration* effect = content.Effect(tunic);
    ASSERT_NE(effect, nullptr);
    EXPECT_EQ(effect->Duration, EffectDuration::Infinite) << "inherited from the base";
    ASSERT_EQ(effect->Modifiers.size(), 1u) << "the speed modifier was removed here";
    EXPECT_EQ(effect->Modifiers[0].Attribute, "Armor");
    EXPECT_EQ(effect->Modifiers[0].Magnitude, 14.0f);
}

TEST(DataDefinitionComposition, AChildSetsAnInheritedModifiersPrecedence)
{
    Content content;
    content.Write("armor_base.sdata", kArmorBase);
    content.Write("frozen_armor.sdata", R"({ "type": "effect.definition", "version": 1,
        "extends": "asset://effects/armor_base.sdata",
        "data": { "modifiers": { "m_armor": { "op": "override", "precedence": 50 } } } })");

    AssetLease frozen = content.Assets.Assets.LoadLease("asset://effects/frozen_armor.sdata", AssetType::Data);
    ASSERT_TRUE(frozen) << content.FailureOf("asset://effects/frozen_armor.sdata");
    const EffectDeclaration* effect = content.Effect(frozen);
    ASSERT_NE(effect, nullptr);
    const auto armor = std::ranges::find(effect->Modifiers, std::string("Armor"), &EffectDeclaration::Modifier::Attribute);
    ASSERT_NE(armor, effect->Modifiers.end());
    EXPECT_EQ(armor->Op, ModifierOp::Override);
    EXPECT_EQ(armor->Precedence, 50);
}

TEST(DataDefinitionComposition, AnAbstractDefinitionNeverLoadsAsAValueOrIsListed)
{
    Content content;
    content.Write("armor_base.sdata", kArmorBase);
    EXPECT_FALSE(content.Assets.Assets.LoadLease("asset://effects/armor_base.sdata", AssetType::Data));
    EXPECT_NE(content.FailureOf("asset://effects/armor_base.sdata").find("abstract"), std::string::npos);

    const AssetRecord* record = content.Assets.Registry.FindByPath("asset://effects/armor_base.sdata");
    ASSERT_NE(record, nullptr);
    const DataAssetEnvelopePeek peek = PeekDataAssetEnvelope(content.Assets.Assets.DefaultSource(), *record);
    EXPECT_EQ(peek.Subtype, kEffectDefinitionType);
    EXPECT_TRUE(peek.Abstract);
}

TEST(DataDefinitionComposition, AFragmentAddsToAKeyedListAndDisagreeingFragmentsAreAnError)
{
    Memory memory;
    memory.Add("asset://effects/armor_base.sdata", kArmorBase);
    memory.Add("asset://effects/fire.sdata", R"({ "type": "effect.definition", "version": 1, "abstract": true,
        "data": { "modifiers": [ { "key": "m_fire", "attribute": "FireResist", "op": "add", "magnitude": 5 } ] } })");
    memory.Add("asset://effects/brief.sdata", R"({ "type": "effect.definition", "version": 1, "abstract": true,
        "data": { "duration": "duration", "seconds": 3 } })");
    memory.Add("asset://effects/lasting.sdata", R"({ "type": "effect.definition", "version": 1, "abstract": true,
        "data": { "duration": "infinite" } })");

    const DataDefinitionStaging enchanted = memory.Stage(R"({ "type": "effect.definition", "version": 1,
        "extends": "asset://effects/armor_base.sdata", "includes": [ "asset://effects/fire.sdata" ], "data": {} })");
    ASSERT_TRUE(enchanted.Ok()) << enchanted.Errors.front().Path << " " << enchanted.Errors.front().Message;
    EXPECT_EQ(enchanted.Data.Find("modifiers")->Size(), 3u);
    EXPECT_EQ(enchanted.Sources, (std::vector<std::string>{ "asset://effects/armor_base.sdata", "asset://effects/fire.sdata" }));

    const DataDefinitionStaging torn = memory.Stage(R"({ "type": "effect.definition", "version": 1,
        "includes": [ "asset://effects/brief.sdata", "asset://effects/lasting.sdata" ], "data": {
          "modifiers": [ { "key": "m", "attribute": "A", "op": "add", "magnitude": 1 } ] } })");
    const DataValidationError* conflict = ErrorAt(torn, "$.data.duration");
    ASSERT_NE(conflict, nullptr);
    EXPECT_NE(conflict->Message.find("brief.sdata"), std::string::npos);
    EXPECT_NE(conflict->Message.find("lasting.sdata"), std::string::npos);
}

TEST(DataDefinitionComposition, AStructuralFailureNamesTheFieldThatCausedIt)
{
    Memory memory;
    memory.Add("asset://effects/left.sdata", R"({ "type": "effect.definition", "version": 1, "abstract": true,
        "extends": "asset://effects/right.sdata", "data": {} })");
    memory.Add("asset://effects/right.sdata", R"({ "type": "effect.definition", "version": 1, "abstract": true,
        "extends": "asset://effects/left.sdata", "data": {} })");
    memory.Add("asset://effects/attributes.sdata", R"({ "type": "gameplay.attribute_declarations", "version": 1,
        "data": { "attributes": [] } })");

    const DataValidationError* missing =
        ErrorAt(memory.Stage(R"({ "type": "effect.definition", "version": 1,
            "extends": "asset://effects/absent.sdata", "data": {} })"), "$.extends");
    ASSERT_NE(missing, nullptr);
    EXPECT_NE(missing->Message.find("does not exist"), std::string::npos);

    const DataDefinitionStaging cycle = memory.Stage(R"({ "type": "effect.definition", "version": 1,
        "extends": "asset://effects/left.sdata", "data": {} })");
    ASSERT_NE(ErrorAt(cycle, "$.extends"), nullptr);
    EXPECT_NE(ErrorAt(cycle, "$.extends")->Message.find("itself"), std::string::npos);

    const DataValidationError* wrongKind =
        ErrorAt(memory.Stage(R"({ "type": "effect.definition", "version": 1,
            "includes": [ "asset://effects/attributes.sdata" ], "data": {} })"), "$.includes[0]");
    ASSERT_NE(wrongKind, nullptr);
    EXPECT_NE(wrongKind->Message.find("gameplay.attribute_declarations"), std::string::npos);

    for (int i = 0; i < 17; ++i)
        memory.Add(std::format("asset://effects/link{}.sdata", i),
                   std::format(R"({{ "type": "effect.definition", "version": 1, "abstract": true,
                       "extends": "asset://effects/link{}.sdata", "data": {{}} }})", i + 1));
    memory.Add("asset://effects/link17.sdata", R"({ "type": "effect.definition", "version": 1, "abstract": true, "data": {} })");
    const DataDefinitionStaging deep = memory.Stage(R"({ "type": "effect.definition", "version": 1,
        "extends": "asset://effects/link0.sdata", "data": {} })");
    ASSERT_FALSE(deep.Ok());
    EXPECT_NE(deep.Errors.front().Message.find("deeper than 16"), std::string::npos);
}

TEST(DataDefinitionComposition, AFragmentThatExtendsIsRefusedAtItsInclude)
{
    Memory memory;
    memory.Add("asset://effects/armor_base.sdata", kArmorBase);
    memory.Add("asset://effects/heavy.sdata", R"({ "type": "effect.definition", "version": 1, "abstract": true,
        "extends": "asset://effects/armor_base.sdata", "data": {} })");
    memory.Add("asset://effects/fire.sdata", R"({ "type": "effect.definition", "version": 1, "abstract": true,
        "includes": [ "asset://effects/heavy.sdata" ], "data": {} })");

    const DataValidationError* direct = ErrorAt(memory.Stage(R"({ "type": "effect.definition", "version": 1,
        "extends": "asset://effects/armor_base.sdata", "includes": [ "asset://effects/heavy.sdata" ],
        "data": {} })"), "$.includes[0]");
    ASSERT_NE(direct, nullptr);
    EXPECT_NE(direct->Message.find("extends another definition"), std::string::npos) << direct->Message;

    const DataDefinitionStaging nested = memory.Stage(R"({ "type": "effect.definition", "version": 1,
        "extends": "asset://effects/armor_base.sdata", "includes": [ "asset://effects/fire.sdata" ], "data": {} })");
    ASSERT_NE(ErrorAt(nested, "$.includes[0]"), nullptr) << "a fragment's own fragment is held to the same rule";
}

TEST(DataDefinitionComposition, AChainOfSixteenDefinitionsComposesAndSeventeenDoesNot)
{
    const auto chain = [](std::size_t bases) {
        Memory memory;
        for (std::size_t i = 0; i + 1 < bases; ++i)
            memory.Add(std::format("asset://effects/link{}.sdata", i),
                       std::format(R"({{ "type": "effect.definition", "version": 1, "abstract": true,
                           "extends": "asset://effects/link{}.sdata", "data": {{}} }})", i + 1));
        memory.Add(std::format("asset://effects/link{}.sdata", bases - 1), kArmorBase);
        return memory.Stage(R"({ "type": "effect.definition", "version": 1,
            "extends": "asset://effects/link0.sdata", "data": {} })");
    };
    const DataDefinitionStaging sixteen = chain(15);
    EXPECT_TRUE(sixteen.Ok()) << sixteen.Errors.front().Path << " " << sixteen.Errors.front().Message;
    const DataDefinitionStaging seventeen = chain(16);
    ASSERT_FALSE(seventeen.Ok());
    EXPECT_NE(seventeen.Errors.front().Message.find("deeper than 16"), std::string::npos);
}

TEST(DataDefinitionComposition, ADerivedRigPatchesOneSocketOfItsBaseByName)
{
    Memory memory;
    memory.Add("asset://anim/hero.rig.sdata", R"({ "type": "animation.rig", "version": 1, "data": {
        "behaviors": [ "asset://anim/s.behaviors.sdata" ], "slot_maps": [ "asset://anim/s.slots.sdata" ],
        "layers": [ { "name": "anim.layer.base", "idle": "Anim.Idle" } ],
        "sockets": [ { "name": "hand_r", "joint": "hand_r" }, { "name": "head", "joint": "head" } ] } })");

    const DataDefinitionStaging rig = memory.Stage(R"({ "type": "animation.rig", "version": 1,
        "extends": "asset://anim/hero.rig.sdata",
        "data": { "sockets": { "hand_r": { "position": [ 0, 0.1, 0 ] }, "back": { "joint": "spine" } } } })");
    ASSERT_TRUE(rig.Ok()) << rig.Errors.front().Path << " " << rig.Errors.front().Message;
    const JsonValue::Array& sockets = rig.Data.Find("sockets")->AsArray();
    ASSERT_EQ(sockets.size(), 3u);
    EXPECT_EQ(sockets[0].Find("name")->AsString(), "hand_r");
    EXPECT_EQ(sockets[0].Find("joint")->AsString(), "hand_r") << "the patched socket keeps what it does not restate";
    ASSERT_NE(sockets[0].Find("position"), nullptr);
    EXPECT_EQ(sockets[1].Find("name")->AsString(), "head");
    EXPECT_EQ(sockets[2].Find("name")->AsString(), "back") << "a new socket follows the inherited ones";
}

TEST(DataDefinitionComposition, AnErrorAtAnInheritedValueNamesItsSourceAndItsKey)
{
    Memory memory;
    memory.Add("asset://effects/flawed.sdata", R"({ "type": "effect.definition", "version": 1, "abstract": true,
        "data": { "duration": "infinite", "modifiers": [
            { "key": "m_ok", "attribute": "Armor", "op": "add", "magnitude": 1 },
            { "key": "m_bad", "attribute": "Armor", "op": "add", "magnitude": "lots" } ] } })");

    const DataDefinitionStaging staged = memory.Stage(R"({ "type": "effect.definition", "version": 1,
        "extends": "asset://effects/flawed.sdata", "data": { "modifiers": { "m_ok": { "magnitude": 2 } } } })");
    const DataValidationError* inherited = ErrorAt(staged, "$.data.modifiers{m_bad}.magnitude");
    ASSERT_NE(inherited, nullptr) << (staged.Errors.empty() ? "" : staged.Errors.front().Path);
    EXPECT_NE(inherited->Message.find("inherited from 'asset://effects/flawed.sdata'"), std::string::npos);

    const DataDefinitionStaging mismatched = memory.Stage(R"({ "type": "effect.definition", "version": 1,
        "extends": "asset://effects/flawed.sdata", "data": { "modifiers": { "m_ok": { "key": "m_other" } } } })");
    EXPECT_NE(ErrorAt(mismatched, "$.data.modifiers{m_ok}.key"), nullptr);
}

TEST(DataDefinitionComposition, AComposedChildsCompileErrorIsSplitAtItsPath)
{
    const DataValidationError split = SplitCompileError("$.data.modifiers[0].attribute unknown attribute");
    EXPECT_EQ(split.Path, "$.data.modifiers[0].attribute");
    EXPECT_EQ(split.Message, "unknown attribute");
    const DataValidationError whole = SplitCompileError("an ability needs a phase");
    EXPECT_EQ(whole.Path, "$.data") << "a message with no path belongs to the definition, not an invented field";
}

TEST(DataDefinitionComposition, DefinitionsWithoutSourcesStageAsTheyAlwaysHave)
{
    Memory memory;
    std::size_t staged = 0;
    for (const char* contentRoot : { "engine/assets", "test/fixtures", "templates" })
    {
        const std::filesystem::path base = std::filesystem::path(SENCHA_REPO_ROOT) / contentRoot;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(base))
        {
            if (entry.path().extension() != ".sdata" || entry.path().generic_string().find("/.cooked/") != std::string::npos)
                continue;
            std::ifstream in(entry.path());
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const std::optional<JsonValue> root = JsonParse(text);
            if (!root || !root->IsObject() || root->Find("extends") != nullptr || root->Find("includes") != nullptr)
                continue;
            const JsonValue* type = root->Find("type");
            if (type == nullptr || !type->IsString() || memory.Assets.DataTypes.Find(type->AsString()) == nullptr)
                continue;
            const DataDefinitionStaging result = memory.Stage(text);
            EXPECT_FALSE(result.Composed) << entry.path();
            EXPECT_TRUE(result.Sources.empty()) << entry.path();
            EXPECT_TRUE(JsonEquals(result.Data, *root->Find("data"))) << entry.path();
            ++staged;
        }
    }
    EXPECT_GT(staged, 20u) << "the gate should cover the tree's content, not an empty set";
}

// An abstract base is never resident, so nothing would reload because it changed.
TEST(DataDefinitionComposition, EditingANonResidentBaseRecomposesItsResidentChild)
{
    Content content;
    content.Write("armor_base.sdata", kArmorBase);
    content.Write("tunic.sdata", R"({ "type": "effect.definition", "version": 1,
        "extends": "asset://effects/armor_base.sdata", "data": { "modifiers": { "m_armor": { "magnitude": 14 } } } })");
    AssetLease tunic = content.Assets.Assets.LoadLease("asset://effects/tunic.sdata", AssetType::Data);
    ASSERT_TRUE(tunic) << content.FailureOf("asset://effects/tunic.sdata");
    ASSERT_FALSE(content.Assets.Assets.IsResident("asset://effects/armor_base.sdata", AssetType::Data));
    const std::uint64_t loaded = content.ReloadVersion(tunic);

    content.Write("armor_base.sdata", R"({ "type": "effect.definition", "version": 1, "abstract": true,
        "data": { "duration": "infinite", "modifiers": [
            { "key": "m_armor", "attribute": "Armor", "op": "add", "magnitude": 10 },
            { "key": "m_speed", "attribute": "MoveSpeed", "op": "add", "magnitude": 3 } ] } })");
    content.Changed("armor_base.sdata");
    const std::uint64_t recomposed = content.ReloadVersion(tunic);
    EXPECT_NE(recomposed, loaded);
    const EffectDeclaration* effect = content.Effect(tunic);
    ASSERT_EQ(effect->Modifiers.size(), 2u);
    EXPECT_EQ(effect->Modifiers[0].Magnitude, 14.0f) << "the child's own override survives the base's edit";
    EXPECT_EQ(effect->Modifiers[1].Magnitude, 3.0f) << "the base's edit reached the child";

    content.Write("armor_base.sdata", R"({ "type": "effect.definition", "version": 1, "abstract": true,
        "data": { "duration": "infinite", "modifiers": [
            { "key": "m_armor", "attribute": "Armor", "op": "add", "magnitude": 10 },
            { "key": "m_speed", "attribute": "MoveSpeed", "op": "add", "magnitude": "fast" } ] } })");
    content.Changed("armor_base.sdata");
    EXPECT_EQ(content.ReloadVersion(tunic), recomposed) << "a broken recomposition must not replace the value";
    EXPECT_EQ(content.Effect(tunic)->Modifiers[1].Magnitude, 3.0f) << "the last good value stays";
    const std::string failure = content.FailureOf("asset://effects/tunic.sdata");
    EXPECT_NE(failure.find("$.data.modifiers{m_speed}.magnitude"), std::string::npos) << failure;
    EXPECT_NE(failure.find("inherited from 'asset://effects/armor_base.sdata'"), std::string::npos) << failure;
}

TEST(DataDefinitionComposition, EditingTheRootOfAChainRecomposesThroughTheMiddle)
{
    Content content;
    content.Write("armor_base.sdata", kArmorBase);
    content.Write("heavy.sdata", R"({ "type": "effect.definition", "version": 1, "abstract": true,
        "extends": "asset://effects/armor_base.sdata", "data": { "modifiers": { "m_armor": { "magnitude": 20 } } } })");
    content.Write("plate.sdata", R"({ "type": "effect.definition", "version": 1,
        "extends": "asset://effects/heavy.sdata", "data": {} })");
    AssetLease plate = content.Assets.Assets.LoadLease("asset://effects/plate.sdata", AssetType::Data);
    ASSERT_TRUE(plate) << content.FailureOf("asset://effects/plate.sdata");
    const std::uint64_t loaded = content.ReloadVersion(plate);

    content.Write("armor_base.sdata", R"({ "type": "effect.definition", "version": 1, "abstract": true,
        "data": { "duration": "infinite", "modifiers": [
            { "key": "m_armor", "attribute": "Armor", "op": "add", "magnitude": 10 },
            { "key": "m_speed", "attribute": "MoveSpeed", "op": "add", "magnitude": -2 } ] } })");
    content.Changed("armor_base.sdata");
    EXPECT_NE(content.ReloadVersion(plate), loaded);
    const EffectDeclaration* effect = content.Effect(plate);
    ASSERT_EQ(effect->Modifiers.size(), 2u);
    EXPECT_EQ(effect->Modifiers[0].Magnitude, 20.0f) << "the middle's override holds";
    EXPECT_EQ(effect->Modifiers[1].Magnitude, -2.0f) << "the root's edit came through the middle";
}

TEST(DataDefinitionComposition, OnlyAResidentChildIsNamedAsComposedFromItsBase)
{
    Content content;
    content.Write("armor_base.sdata", kArmorBase);
    content.Write("tunic.sdata", R"({ "type": "effect.definition", "version": 1,
        "extends": "asset://effects/armor_base.sdata", "data": {} })");
    const AssetKindRegistration* data = content.Assets.Assets.Kinds().Find(AssetType::Data);
    ASSERT_NE(data, nullptr);
    ASSERT_TRUE(data->ComposedFrom);

    AssetLease tunic = content.Assets.Assets.LoadLease("asset://effects/tunic.sdata", AssetType::Data);
    ASSERT_TRUE(tunic);
    EXPECT_EQ(data->ComposedFrom("asset://effects/armor_base.sdata"),
              (std::vector<std::string>{ "asset://effects/tunic.sdata" }));
    tunic = {};
    EXPECT_TRUE(data->ComposedFrom("asset://effects/armor_base.sdata").empty());
}

TEST(DataDefinitionComposition, AChildTheHostHoldsIsLeftToTheHostWhenItsBaseChanges)
{
    Content content;
    content.Write("armor_base.sdata", kArmorBase);
    content.Write("tunic.sdata", R"({ "type": "effect.definition", "version": 1,
        "extends": "asset://effects/armor_base.sdata", "data": {} })");
    AssetLease tunic = content.Assets.Assets.LoadLease("asset://effects/tunic.sdata", AssetType::Data);
    ASSERT_TRUE(tunic);
    const std::uint64_t loaded = content.ReloadVersion(tunic);

    content.Write("armor_base.sdata", R"({ "type": "effect.definition", "version": 1, "abstract": true,
        "data": { "duration": "infinite", "modifiers": [] } })");
    content.Changed("armor_base.sdata", "asset://effects/tunic.sdata");
    EXPECT_EQ(content.ReloadVersion(tunic), loaded) << "an open document's working version must not be replaced by its file";
}

TEST(DataDefinitionComposition, AnItemsEquipFieldsComposeOverItsBase)
{
    Content content;
    content.Write("fire.sdata", R"({ "type": "effect.definition", "version": 1, "data": { "duration": "infinite" } })");
    content.Write("ring_base.sdata", R"({ "type": "item.definition", "version": 1, "abstract": true,
        "data": { "name": "Ring", "tags": [ "Item.Ring" ],
                  "equip": { "occupies": [ "ring" ],
                             "effects": [ { "key": "e_cast", "effect": { "data": "asset://effects/fire.sdata" } } ] },
                  "grants": [ { "key": "g_bolt", "parameters": [ { "key": "power", "value": 10 } ] } ] } })");
    content.Write("ring_strong.sdata", R"({ "type": "item.definition", "version": 1,
        "extends": "asset://effects/ring_base.sdata",
        "data": { "name": "Strong Ring", "equip": { "effects": { "e_cast": { "scale": 2 } } },
                  "grants": { "g_bolt": { "parameters": { "power": { "value": 25 } } } } } })");

    AssetLease strong = content.Assets.Assets.LoadLease("asset://effects/ring_strong.sdata", AssetType::Data);
    ASSERT_TRUE(strong) << content.FailureOf("asset://effects/ring_strong.sdata");
    const ItemDeclaration* item = content.Assets.DataAssets.TryGet<ItemDeclaration>(
        DataAssetHandle::FromToken(strong.OpaqueToken()), kItemDefinitionType);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->Occupies, std::vector<std::string>{ "ring" });
    ASSERT_EQ(item->EquipEffects.size(), 1u);
    EXPECT_EQ(item->EquipEffects[0].Effect, "asset://effects/fire.sdata");
    EXPECT_FLOAT_EQ(item->EquipEffects[0].Scale, 2.0f);
    ASSERT_EQ(item->GrantParameters.size(), 1u);
    EXPECT_EQ(item->GrantParameters[0].Parameters,
              (std::vector<AbilityGrantParameter>{ { .Key = "power", .Value = 25.0 } }));
}

TEST(DataDefinitionComposition, AnEquipmentLayoutComposesItsSlotsByKey)
{
    Content content;
    content.Write("humanoid.sdata", R"({ "type": "equipment.layout", "version": 1, "abstract": true,
        "data": { "slots": [ { "key": "hand.main", "accepts": "Item.Weapon", "action": "Input.Fire", "priority": 10 },
                             { "key": "ring.left", "accepts": "Item.Ring", "priority": 20 } ],
                  "lock_tags": [ "State.Attacking" ] } })");
    content.Write("mage.sdata", R"({ "type": "equipment.layout", "version": 1,
        "extends": "asset://effects/humanoid.sdata",
        "data": { "slots": { "ring.right": { "key": "ring.right", "accepts": "Item.Ring", "priority": 21 },
                             "hand.main": { "on_depleted": "hold" } } } })");

    AssetLease mage = content.Assets.Assets.LoadLease("asset://effects/mage.sdata", AssetType::Data);
    ASSERT_TRUE(mage) << content.FailureOf("asset://effects/mage.sdata");
    const EquipmentLayoutDeclaration* layout = content.Assets.DataAssets.TryGet<EquipmentLayoutDeclaration>(
        DataAssetHandle::FromToken(mage.OpaqueToken()), kEquipmentLayoutType);
    ASSERT_NE(layout, nullptr);
    ASSERT_EQ(layout->Slots.size(), 3u);
    EXPECT_EQ(layout->Slots[0].Key, "hand.main");
    EXPECT_EQ(layout->Slots[0].OnDepleted, SlotDepletion::Hold) << "patched by key";
    EXPECT_EQ(layout->Slots[0].Action, std::optional<std::string>("Input.Fire")) << "the rest inherited";
    EXPECT_EQ(layout->Slots[2].Key, "ring.right");
    EXPECT_EQ(layout->Slots[2].Priority, std::optional<std::int32_t>(21));
    EXPECT_EQ(layout->LockTags, std::vector<std::string>{ "State.Attacking" });
}
