#include "data/DataDocumentStore.h"
#include "documents/DocumentSourceSet.h"

#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonParser.h>
#include <effects/EffectDefinitionData.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

namespace
{
    constexpr std::string_view kBase = "asset://effects/armor_base.sdata";
    constexpr std::string_view kTunic = "asset://effects/tunic.sdata";

    struct ComposedDocuments : testing::Test
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path()
            / ("sencha_data_document_composition_" + std::to_string(std::random_device{}()));
        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        RuntimeAssets Assets{ Logging, Serializers };
        DocumentSourceSet Sources;
        DataDocumentStore Store{ Assets, Sources };

        void SetUp() override
        {
            std::filesystem::create_directories(Root / "effects");
            std::ofstream(Root / "effects" / "armor_base.sdata") << Base("10");
            std::ofstream(Root / "effects" / "tunic.sdata") << R"({ "type": "effect.definition", "version": 1,
                "extends": "asset://effects/armor_base.sdata", "data": {} })";
            (void)ScanAssetsDirectory(Root.generic_string(), Assets.Registry, Assets.Assets.Kinds());
        }

        void TearDown() override
        {
            Sources.DiscardAll();
            std::error_code ec;
            std::filesystem::remove_all(Root, ec);
        }

        [[nodiscard]] static std::string Base(std::string_view magnitude)
        {
            return R"({ "type": "effect.definition", "version": 1, "abstract": true, "data": {
                "duration": "infinite", "modifiers": [
                { "key": "m_armor", "attribute": "Armor", "op": "add", "magnitude": )"
                + std::string(magnitude) + " } ] } }";
        }

        DataDocument& Open(std::string_view path)
        {
            std::string error;
            DataDocument* document = Store.Open(path, error);
            EXPECT_NE(document, nullptr) << error;
            return *document;
        }

        void Commit(DataDocument& document, std::string_view text)
        {
            document.BeginEdit();
            document.PreviewRoot(*JsonParse(text));
            Store.CommitEdit(document);
        }
    };
}

TEST_F(ComposedDocuments, AnOpenChildRecomposesWhenItsOpenBaseChanges)
{
    DataDocument& base = Open(kBase);
    DataDocument& tunic = Open(kTunic);
    const AssetLease resident = Assets.Assets.LoadLease(kTunic, AssetType::Data);
    ASSERT_TRUE(resident);
    ASSERT_TRUE(tunic.IsSemanticallyValid());

    Commit(base, Base("\"lots\""));
    ASSERT_FALSE(tunic.IsSemanticallyValid()) << "the child's problems did not follow its base";
    EXPECT_EQ(tunic.ValidationErrors().front().Path, "$.data.modifiers{m_armor}.magnitude");

    Commit(base, Base("25"));
    EXPECT_TRUE(tunic.IsSemanticallyValid());
    const EffectDeclaration* effect = Assets.DataAssets.TryGet<EffectDeclaration>(
        DataAssetHandle::FromToken(resident.OpaqueToken()), kEffectDefinitionType);
    ASSERT_NE(effect, nullptr);
    EXPECT_EQ(effect->Modifiers[0].Magnitude, 25.0f) << "the resident child composes the base's working version";
}

TEST_F(ComposedDocuments, AnOpenChildRecomposesWhenItsClosedBaseChangesOnDisk)
{
    DataDocument& tunic = Open(kTunic);
    ASSERT_TRUE(tunic.IsSemanticallyValid());

    std::ofstream(Root / "effects" / "armor_base.sdata", std::ios::trunc) << Base("\"lots\"");
    EXPECT_EQ(Store.FileChangedOnDisk(Root / "effects" / "armor_base.sdata"), ExternalChange::NotOpen)
        << "the base itself is not open";
    ASSERT_FALSE(tunic.IsSemanticallyValid()) << "the child still shows what its base used to be";
    EXPECT_EQ(tunic.ValidationErrors().front().Path, "$.data.modifiers{m_armor}.magnitude");
}

TEST_F(ComposedDocuments, AnOpenChildWhoseBaseDidNotComposeIsCheckedAgainWhenTheBaseIsFixed)
{
    std::ofstream(Root / "effects" / "armor_base.sdata", std::ios::trunc) << "{ not a definition";
    DataDocument& tunic = Open(kTunic);
    ASSERT_FALSE(tunic.IsSemanticallyValid());

    std::ofstream(Root / "effects" / "armor_base.sdata", std::ios::trunc) << Base("10");
    (void)Store.FileChangedOnDisk(Root / "effects" / "armor_base.sdata");
    EXPECT_TRUE(tunic.IsSemanticallyValid()) << "the child still reports the base it could not read";
}
