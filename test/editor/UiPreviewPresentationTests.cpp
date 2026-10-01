#include "authoring/DocumentLibrary.h"
#include "authoring/ElementOutline.h"
#include "authoring/LibraryOutline.h"
#include "authoring/PreviewFraming.h"
#include "authoring/PreviewReports.h"
#include "authoring/UiPreviewModel.h"
#include "authoring/VocabularyOutline.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

// How the previewed document is framed in the content, and what about it is
// listed: its elements and the library's documents as outlines, the layer's
// diagnostics as problems, a raised action as a line of text.
namespace
{
UiElementInfo Element(std::uint32_t slot, std::uint32_t depth, std::string tag, std::uint32_t parent = 0)
{
    UiElementInfo element;
    element.Ref = UiElementRef{ UiScreenHandle{ 1, 1 }, slot, 1 };
    element.Tag = std::move(tag);
    element.Depth = depth;
    if (parent != 0)
        element.Parent = UiElementRef{ UiScreenHandle{ 1, 1 }, parent, 1 };
    return element;
}

// body > (div#menu > button.play, button.quit), span
std::vector<UiElementInfo> Tree()
{
    std::vector<UiElementInfo> tree{ Element(1, 0, "body"), Element(2, 1, "div", 1), Element(3, 2, "button", 2),
                                     Element(4, 2, "button", 2), Element(5, 1, "span", 1) };
    tree[1].Id = "menu";
    tree[2].Classes = { "play" };
    tree[3].Classes = { "quit" };
    return tree;
}

ExplorerQuery Page(std::string filter = {})
{
    return { .Explorer = "outline", .Filter = std::move(filter), .First = 0, .Count = 50 };
}

std::vector<std::string> Labels(const ExplorerPage& page)
{
    std::vector<std::string> labels;
    for (const ExplorerItem& item : page.Items)
        labels.push_back(std::string(static_cast<std::size_t>(item.Depth), ' ') + item.Label);
    return labels;
}
} // namespace

TEST(PreviewFraming, AFittedSurfaceFillsTheContentAndAZoomedOneIsMovedByThePan)
{
    const Rect2d content(100.0f, 50.0f, 1000.0f, 1000.0f);
    const Rect2d fitted = PlacePreview(content, RenderExtent{ 1920, 1080 }, PreviewZoom::Fit, Vec2d(300.0f, 0.0f));
    EXPECT_FLOAT_EQ(fitted.Width(), 1000.0f);
    EXPECT_FLOAT_EQ(fitted.Height(), 562.5f);
    EXPECT_FLOAT_EQ(fitted.Center().X, 600.0f) << "a fitted surface ignores the pan";

    const Rect2d half = PlacePreview(content, RenderExtent{ 1920, 1080 }, PreviewZoom::Half, Vec2d(30.0f, -20.0f));
    EXPECT_FLOAT_EQ(half.Width(), 960.0f);
    EXPECT_FLOAT_EQ(half.Center().X, 630.0f);
    EXPECT_FLOAT_EQ(half.Center().Y, 530.0f);
}

TEST(PreviewFraming, SafeAreasAndElementBoxesLandOnThePicture)
{
    const Rect2d picture(0.0f, 0.0f, 960.0f, 540.0f);
    const std::array<Rect2d, 2> safe = SafeAreas(picture);
    EXPECT_FLOAT_EQ(safe[0].Min().X, 48.0f);
    EXPECT_FLOAT_EQ(safe[1].Max().Y, 486.0f);

    const Rect2d box = BoxOnPicture(UiElementBox{ 100.0f, 200.0f, 300.0f, 40.0f }, picture, RenderExtent{ 1920, 1080 });
    EXPECT_FLOAT_EQ(box.Min().X, 50.0f) << "half size: surface pixels at half";
    EXPECT_FLOAT_EQ(box.Width(), 150.0f);
}

TEST(ElementOutline, ElementsSitUnderTheirParentsAndABranchCloses)
{
    const std::vector<UiElementInfo> tree = Tree();
    ElementOutline outline;
    EXPECT_EQ(Labels(outline.Explore(tree, {}, Page())),
              (std::vector<std::string>{ "body", " div#menu", "  button.play", "  button.quit", " span" }));
    outline.Flip(tree[1].Ref);
    const ExplorerPage closed = outline.Explore(tree, {}, Page());
    EXPECT_EQ(Labels(closed), (std::vector<std::string>{ "body", " div#menu", " span" }));
    EXPECT_EQ(closed.Items[1].Branch, ExplorerBranch::Closed);

    const ExplorerPage revealed = outline.Explore(tree, tree[3].Ref, Page());
    EXPECT_EQ(revealed.Items.size(), 5u) << "picking an element opens the branches above it";
    EXPECT_TRUE(revealed.Items[3].Current);
}

TEST(ElementOutline, AFilterKeepsWhatMatchesAndWhatHoldsIt)
{
    const std::vector<UiElementInfo> tree = Tree();
    ElementOutline outline;
    EXPECT_EQ(Labels(outline.Explore(tree, {}, Page("quit"))),
              (std::vector<std::string>{ "body", " div#menu", "  button.quit" }));
    const std::optional<UiElementRef> quit = ElementOutline::Resolve(tree, outline.Explore(tree, {}, Page("quit")).Items[2].Key);
    ASSERT_TRUE(quit.has_value());
    EXPECT_EQ(*quit, tree[3].Ref);
    EXPECT_FALSE(ElementOutline::Resolve(tree, "9:1").has_value()) << "a rebuilt element resolves to nothing";
}

TEST(LibraryOutline, DocumentsAreListedUnderTheirLibrary)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path()
                                       / ("sencha_library_outline_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(root / "ui");
    std::ofstream(root / "ui" / "pause.rml") << R"(<rml><body data-model="pause"/></rml>)";
    std::ofstream(root / "ui" / "hud.rml") << "<rml><body/></rml>";
    DocumentLibrary library;
    library.AddRoot("Project", root.generic_string());
    library.Rescan();

    LibraryOutline outline;
    const ExplorerPage page = outline.Explore(library, "asset://ui/pause.rml", Page());
    ASSERT_EQ(page.Items.size(), 3u);
    EXPECT_EQ(page.Items[0].Label, "Project");
    EXPECT_EQ(page.Items[0].Branch, ExplorerBranch::Open);
    EXPECT_TRUE(page.Items[2].Current) << page.Items[2].Key;
    EXPECT_EQ(Labels(outline.Explore(library, {}, Page("pause"))).size(), 2u);

    EXPECT_TRUE(outline.Flip(page.Items[0].Key));
    EXPECT_EQ(outline.Explore(library, {}, Page()).Items.size(), 1u);
    EXPECT_FALSE(outline.Flip(page.Items[1].Key)) << "a document is opened, not folded";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST(PreviewReports, OnlyTheDocumentsWarningsAreProblemsAndEachOnce)
{
    const UiSurfaceId preview{ 1, 1 };
    const UiSurfaceId other{ 2, 1 };
    std::deque<UiDiagnostic> diagnostics(4);
    diagnostics[0].Severity = UiDiagnosticSeverity::Info;
    diagnostics[0].Message = "rebuilt";
    diagnostics[1].Severity = UiDiagnosticSeverity::Warning;
    diagnostics[1].Kind = UiDiagnosticKind::ResourceUnresolved;
    diagnostics[1].Message = "no such image";
    diagnostics[1].Path = "ui/pause.rml";
    diagnostics[1].Line = 12;
    diagnostics[1].Surface = preview;
    diagnostics[2] = diagnostics[1];
    diagnostics[3] = diagnostics[1];
    diagnostics[3].Surface = other;

    const std::vector<Problem> problems = PreviewProblems(diagnostics, preview);
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems[0].Location, "ui/pause.rml:12");
    EXPECT_EQ(problems[0].Severity, ProblemSeverity::Warning);
}

TEST(PreviewReports, ARaisedActionReadsAsItsNameAndArguments)
{
    UiPreviewModel model;
    model.Actions = { "open", "volume" };
    UiAction action;
    action.Id = UiActionIdAt(1);
    action.Arguments = { UiValue(std::int64_t{ 3 }), UiValue(std::string("music")) };
    EXPECT_EQ(DescribeRaisedAction(action, model), "volume (int 3, \"music\")");
    action.Id = UiActionIdAt(5);
    action.Arguments.clear();
    EXPECT_EQ(DescribeRaisedAction(action, model), "action 6") << "undeclared, said by its number";
}

TEST(VocabularyOutline, TheVocabularyReadsAsBranchesAndAnUnresolvedRecordIsAProblem)
{
    const VocabularyCatalog catalog;
    BindingAsset asset{ .Path = "asset://data/menu.bindings.sdata", .Records = {} };
    VocabularyCatalog::BindingRow play;
    play.Key = "play";
    play.VerbName = "runtime.resume";
    play.Resolved = true;
    VocabularyCatalog::BindingRow nope;
    nope.Key = "nope";
    nope.VerbName = "no.such.verb";
    nope.Error = "unknown verb";
    asset.Records = { play, nope };
    const std::vector<BindingAsset> bindings{ asset };

    VocabularyOutline outline;
    const ExplorerPage page = outline.Explore(catalog, bindings, Page());
    ASSERT_FALSE(page.Items.empty());
    EXPECT_EQ(page.Items.front().Label, "Verbs");
    const auto bindingsBranch = std::ranges::find(page.Items, std::string("Bindings"), &ExplorerItem::Label);
    ASSERT_NE(bindingsBranch, page.Items.end());
    EXPECT_EQ(page.Items.back().Label, "nope > no.such.verb · does not resolve");

    const ExplorerPage filtered = outline.Explore(catalog, bindings, Page("no.such"));
    EXPECT_EQ(Labels(filtered), (std::vector<std::string>{ "Bindings", " asset://data/menu.bindings.sdata",
                                                           "  nope > no.such.verb · does not resolve" }));

    EXPECT_TRUE(outline.Flip(bindingsBranch->Key));
    const ExplorerPage closed = outline.Explore(catalog, bindings, Page());
    EXPECT_EQ(closed.Items.back().Label, "Bindings") << "a closed branch hides what is under it";

    const std::vector<Problem> problems = VocabularyProblems(catalog, bindings);
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems[0].Location, asset.Path);
    EXPECT_NE(problems[0].Message.find("unknown verb"), std::string::npos);
}
