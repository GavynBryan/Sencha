#include <gtest/gtest.h>

#include "authoring/UiPreviewModel.h"
#include "authoring/UiPreviewSession.h"

#include <app/OptionsPage.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <ui/UiService.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <filesystem>
#include <random>
#include <string>

//=============================================================================
// The sidecar: what a document is previewed against, and the contract that it
// is also what the document's real host declares. A shipped document whose
// sidecar and host disagree is a document the previewer lies about.
//=============================================================================

namespace
{
UiPreviewModel Sample()
{
    UiPreviewModel m;
    m.ModelName = "sample";
    m.Modal = true;
    m.Properties = {
        UiModelProperty{ "title", UiValue(std::string("Hello")), false },
        UiModelProperty{ "count", UiValue(std::int64_t{ 3 }), false },
        UiModelProperty{ "ratio", UiValue(0.25), true },
        UiModelProperty{ "open", UiValue(true), false },
    };
    m.Arrays = { { "entries", { "a", "b" } } };
    UiRow range{ "Volume", "0.5", "", true };
    range.Control = UiRowControl::Range;
    range.Number = 0.5;
    range.Min = 0.0;
    range.Max = 1.0;
    range.Step = 0.1;
    UiRow choice{ "Mode", "Windowed", "", true };
    choice.Control = UiRowControl::Choice;
    choice.Choices = { "Windowed", "Fullscreen" };
    UiRow text{ "Name", "crate", "asset", false };
    m.Rows = { { "rows", { range, choice, text } } };
    m.Actions = { "activate", "close" };
    return m;
}

std::string Canonical(const UiPreviewModel& m) { return JsonStringify(m.ToJson(), true); }

// The shipped content, from the tree, cooked as committed.
class ShippedContent
{
public:
    ShippedContent()
        : Assets(Logging, Serializers, RuntimeAssets::ReferenceOnly{})
    {
        Mount(std::string(SENCHA_REPO_ROOT) + "/engine/assets");
        Mount(SENCHA_EDITOR_UI_DIR);
        Ui = std::make_unique<UiService>(Logging, Assets.Assets, Assets.UiPackages,
                                         Assets.Fonts, nullptr, nullptr);
    }
    ~ShippedContent() { Ui->Shutdown(); }
    UiService& Service() { return *Ui; }

private:
    void Mount(const std::string& root)
    {
        ScanAssetsDirectory(root, Assets.Registry, Assets.Assets.Kinds());
        ScanAssetsDirectory(root + "/.cooked", Assets.Registry, Assets.Assets.Kinds());
        RegisterCookedAssets(root, Assets.Registry);
    }

    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets;
    std::unique_ptr<UiService> Ui;
};
} // namespace

TEST(UiPreviewModel, RoundTripsThroughJsonWithEveryValueKind)
{
    const UiPreviewModel original = Sample();
    std::string error;
    const std::optional<UiPreviewModel> back = UiPreviewModel::Parse(original.ToJson(), &error);
    ASSERT_TRUE(back.has_value()) << error;
    EXPECT_EQ(Canonical(*back), Canonical(original));

    // Kinds survive: 3 is an int, 0.25 a float, "Hello" a string, true a bool.
    EXPECT_EQ(back->Properties[1].Initial.Kind(), UiValueKind::Int);
    EXPECT_EQ(back->Properties[2].Initial.Kind(), UiValueKind::Float);
    EXPECT_TRUE(back->Properties[2].Editable);
    EXPECT_EQ(back->Rows[0].Items[0].Control, UiRowControl::Range);
    EXPECT_DOUBLE_EQ(back->Rows[0].Items[0].Step, 0.1);
    EXPECT_EQ(back->Rows[0].Items[1].Choices.size(), 2u);
    EXPECT_EQ(back->Rows[0].Items[2].Control, UiRowControl::Text);
}

TEST(UiPreviewModel, SavesBesideTheDocumentAndLoadsBack)
{
    std::random_device rd;
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("sencha_preview_model_" + std::to_string(rd()));
    std::filesystem::create_directories(dir);
    const std::filesystem::path document = dir / "pause.rml";
    const std::filesystem::path sidecar = UiPreviewModel::SidecarFor(document);
    EXPECT_EQ(sidecar.filename().string(), "pause.preview.json");

    std::string error;
    ASSERT_TRUE(Sample().Save(sidecar, &error)) << error;
    const std::optional<UiPreviewModel> loaded = UiPreviewModel::Load(sidecar, &error);
    ASSERT_TRUE(loaded.has_value()) << error;
    EXPECT_EQ(Canonical(*loaded), Canonical(Sample()));

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(UiPreviewModel, RefusesWhatItCannotRepresentAndSaysWhy)
{
    std::string error;
    EXPECT_FALSE(UiPreviewModel::Parse(*JsonParse(R"({"properties":[{"kind":"string","value":"x"}]})"), &error));
    EXPECT_NE(error.find("name"), std::string::npos);
    EXPECT_FALSE(UiPreviewModel::Parse(*JsonParse(R"({"properties":[{"name":"a","kind":"int","value":"x"}]})"), &error));
    EXPECT_NE(error.find("kind"), std::string::npos);
    EXPECT_FALSE(UiPreviewModel::Parse(*JsonParse(R"({"rows":[{"name":"r","items":[{"control":"dial"}]}]})"), &error));
    EXPECT_NE(error.find("dial"), std::string::npos);
}

TEST(UiPreviewModel, TheOptionsSidecarDeclaresWhatTheOptionsPageDeclares)
{
    // The drift guard, against the one host descriptor that is public.
    std::string error;
    const std::optional<UiPreviewModel> sidecar = UiPreviewModel::Load(
        std::filesystem::path(SENCHA_REPO_ROOT) / "engine/assets/ui/options.preview.json", &error);
    ASSERT_TRUE(sidecar.has_value()) << error;

    const UiScreenDesc host = OptionsPage{}.Describe("asset://ui/options.rml");
    const UiScreenDesc previewed = sidecar->Describe("asset://ui/options.rml");
    EXPECT_EQ(previewed.ModelName, host.ModelName);
    EXPECT_EQ(previewed.Modal, host.Modal);
    EXPECT_EQ(previewed.RowLists, host.RowLists);
    EXPECT_EQ(previewed.Arrays, host.Arrays);
    EXPECT_EQ(previewed.Actions, host.Actions);
    ASSERT_EQ(previewed.Properties.size(), host.Properties.size());
    for (std::size_t i = 0; i < host.Properties.size(); ++i)
        EXPECT_EQ(previewed.Properties[i].Path, host.Properties[i].Path);
}

TEST(UiPreviewModel, EveryShippedDocumentOpensAgainstItsSidecarWithNothingMissing)
{
    // Four documents, four sidecars, zero misses: the previewer shows each one
    // the way its host would, and the sidecar is a complete declaration.
    ShippedContent content;
    ASSERT_TRUE(content.Service().IsReady());

    struct Shipped { const char* Source; const char* Package; };
    const Shipped shipped[] = {
        { "/engine/assets/ui/pause.rml", "asset://ui/pause.rml" },
        { "/engine/assets/ui/options.rml", "asset://ui/options.rml" },
    };
    const Shipped editorShipped[] = {
        { "/inspector.rml", "asset://inspector.rml" },
        { "/cook_profiles.rml", "asset://cook_profiles.rml" },
    };

    UiPreviewSession session(content.Service());
    const auto check = [&](const std::filesystem::path& source, const char* package) {
        std::string error;
        const std::optional<UiPreviewModel> model =
            UiPreviewModel::Load(UiPreviewModel::SidecarFor(source), &error);
        ASSERT_TRUE(model.has_value()) << source << ": " << error;
        ASSERT_TRUE(session.Open(package, *model)) << package << " did not open";
        content.Service().Update();
        content.Service().Update();
        session.Poll();
        for (const UiDiagnostic& d : session.Diagnostics())
        {
            EXPECT_NE(d.Kind, UiDiagnosticKind::BindingMissing)
                << package << " reads '" << d.Variable.value_or("?") << "', which its sidecar lacks";
            EXPECT_NE(d.Kind, UiDiagnosticKind::MemberMissing)
                << package << " reads member '" << d.Variable.value_or("?") << "', which its rows lack";
            EXPECT_NE(d.Kind, UiDiagnosticKind::EventCallbackMissing)
                << package << " raises '" << d.Variable.value_or("?") << "', which its sidecar lacks";
            EXPECT_NE(d.Kind, UiDiagnosticKind::ModelRefused) << d.Message;
        }
        session.ClearDiagnostics();
    };
    for (const Shipped& s : shipped)
        check(std::filesystem::path(SENCHA_REPO_ROOT) / (std::string(".") + s.Source), s.Package);
    for (const Shipped& s : editorShipped)
        check(std::filesystem::path(SENCHA_EDITOR_UI_DIR) / (std::string(".") + s.Source), s.Package);
}
