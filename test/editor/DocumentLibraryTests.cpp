#include <gtest/gtest.h>

#include "authoring/DocumentLibrary.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace
{
void Write(const std::filesystem::path& p, std::string_view text)
{
    std::filesystem::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
}

struct Roots
{
    std::filesystem::path Dir;
    Roots()
    {
        std::random_device rd;
        Dir = std::filesystem::temp_directory_path() / ("sencha_doclib_" + std::to_string(rd()));
        Write(Dir / "game" / "ui" / "hud.rml", "<rml><body data-model='hud'><div/></body></rml>");
        Write(Dir / "game" / "ui" / "hud.preview.json", "{}");
        Write(Dir / "game" / "ui" / ".cooked" / "hud.rml.sui", "x");     // wrong place: cooked lives at the root
        Write(Dir / "game" / ".cooked" / "ui" / "hud.rml.sui", "x");
        Write(Dir / "game" / "ui" / "splash.rml", "<rml><body><div/></body></rml>");
        Write(Dir / "game" / "ui" / "notes.txt", "not a document");
        Write(Dir / "engine" / "ui" / "pause.rml", "<rml><body data-model=\"pause\"><div/></body></rml>");
    }
    ~Roots()
    {
        std::error_code ec;
        std::filesystem::remove_all(Dir, ec);
    }
};
}

TEST(DocumentLibrary, ListsEveryDocumentUnderItsRootsWithWhatTheScanCanTell)
{
    Roots roots;
    DocumentLibrary library;
    library.AddRoot("Project", (roots.Dir / "game").generic_string());
    library.AddRoot("Engine", (roots.Dir / "engine").generic_string());
    library.Rescan();

    const std::vector<DocumentEntry>& docs = library.Documents();
    ASSERT_EQ(docs.size(), 3u) << "three .rml files, nothing else, and nothing under .cooked";
    // Ordered by library then path.
    EXPECT_EQ(docs[0].Library, "Engine");
    EXPECT_EQ(docs[0].RelPath, "ui/pause.rml");
    EXPECT_EQ(docs[0].PackagePath, "asset://ui/pause.rml");
    EXPECT_EQ(docs[0].ModelName, "pause");
    EXPECT_FALSE(docs[0].Cooked);

    EXPECT_EQ(docs[1].Library, "Project");
    EXPECT_EQ(docs[1].RelPath, "ui/hud.rml");
    EXPECT_EQ(docs[1].ModelName, "hud");
    EXPECT_TRUE(docs[1].Cooked) << "the cooked package sits at <root>/.cooked/<rel>.sui";
    EXPECT_TRUE(docs[1].HasPreviewModel);

    EXPECT_EQ(docs[2].RelPath, "ui/splash.rml");
    EXPECT_TRUE(docs[2].ModelName.empty()) << "a static document names no model";
    EXPECT_FALSE(docs[2].HasPreviewModel);

    ASSERT_NE(library.Find("asset://ui/hud.rml"), nullptr);
    EXPECT_EQ(library.Find("asset://ui/hud.rml")->SourcePath(), roots.Dir / "game" / "ui/hud.rml");
    EXPECT_EQ(library.Find("asset://nope.rml"), nullptr);
}

TEST(DocumentLibrary, ARescanPicksUpWhatAppeared)
{
    Roots roots;
    DocumentLibrary library;
    library.AddRoot("Project", (roots.Dir / "game").generic_string());
    library.Rescan();
    ASSERT_EQ(library.Documents().size(), 2u);
    Write(roots.Dir / "game" / "ui" / "later.rml", "<rml><body/></rml>");
    EXPECT_EQ(library.Documents().size(), 2u);
    library.Rescan();
    EXPECT_EQ(library.Documents().size(), 3u);
}

TEST(ScanDataModelName, ReadsTheAttributeHoweverItIsQuoted)
{
    EXPECT_EQ(ScanDataModelName("<body data-model=\"pause\">"), "pause");
    EXPECT_EQ(ScanDataModelName("<body class='x' data-model='hud' id='b'>"), "hud");
    EXPECT_EQ(ScanDataModelName("<body>"), "");
    EXPECT_EQ(ScanDataModelName("<body data-model=>"), "");
}
