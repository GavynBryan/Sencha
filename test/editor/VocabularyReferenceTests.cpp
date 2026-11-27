#include <app/EngineVocabulary.h>
#include <authored/VocabularyReference.h>
#include <authored/WorldVocabulary.h>
#include <ecs/World.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
const std::filesystem::path kReferencePath = std::filesystem::path(SENCHA_REPO_ROOT) / "docs/reference/vocabulary.md";

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

DataFieldSchema EntityArgument(std::string key)
{
    return MakeDataField(DataFieldKind::Entity, std::move(key), "Target");
}

VerbDefinition Verb(std::string name, std::string description, std::vector<DataFieldSchema> arguments = {})
{
    VerbDefinition verb;
    verb.Name = std::move(name);
    verb.Description = std::move(description);
    verb.Arguments.Children = std::move(arguments);
    return verb;
}

std::string RenderScratch(std::vector<DataFieldSchema> arguments)
{
    World names;
    InstallAuthoredVocabulary(names);
    VerbRegistrationScope scope(*FindVerbRegistry(names), "scratch");
    EXPECT_TRUE(scope.Declare(Verb("scratch.poke", "Pokes the target.", std::move(arguments))));
    EXPECT_TRUE(scope.Commit());
    return RenderVocabularyReference(names, "scratch");
}
}

TEST(VocabularyReference, TheEnginesReferenceIsWhatItsCatalogDeclares)
{
    World names;
    InstallAuthoredVocabulary(names);
    ASSERT_TRUE(InstallEngineVocabulary(names));
    const std::string rendered = RenderVocabularyReference(names, "engine");

    if (std::getenv("SENCHA_WRITE_VOCABULARY_REFERENCE") != nullptr)
    {
        std::filesystem::create_directories(kReferencePath.parent_path());
        std::ofstream(kReferencePath, std::ios::binary) << rendered;
    }
    EXPECT_EQ(rendered, ReadFile(kReferencePath))
        << "docs/reference/vocabulary.md differs from the engine's catalog; review the change and rerun this test "
           "with SENCHA_WRITE_VOCABULARY_REFERENCE=1 to rewrite it";
}

TEST(VocabularyReference, ARenamedArgumentChangesTheReference)
{
    const std::string before = RenderScratch({ EntityArgument("target") });
    const std::string after = RenderScratch({ EntityArgument("victim") });

    EXPECT_NE(before, after);
    EXPECT_NE(before.find("| `scratch.poke` | instant | gameplay / authority_only | `target: entity` |"),
              std::string::npos)
        << before;
    EXPECT_NE(after.find("`victim: entity`"), std::string::npos) << after;
}

TEST(VocabularyReference, AnArgumentShowsItsDefaultRangeAndWhetherItMayBeLeftOut)
{
    DataFieldSchema count = MakeDataField(DataFieldKind::Int, "count", "Count");
    count.Default = std::int64_t{ 1 };
    count.Required = false;
    count.Numeric.Minimum = 1.0;
    DataFieldSchema at = MakeDataField(DataFieldKind::Vector, "at", "At");
    at.Required = false;
    DataFieldSchema bus = MakeDataField(DataFieldKind::String, "bus", "Bus");
    bus.Default = std::string("Sfx|Music");

    const std::string rendered = RenderScratch({ count, at, bus });
    EXPECT_NE(rendered.find("`count: int = 1 [1 ..]`<br>`at?: vector3`<br>`bus: string = \"Sfx\\|Music\"`"),
              std::string::npos)
        << rendered;
}

TEST(VocabularyReference, OnlyTheNamedProvidersEntriesAreListedInNameOrder)
{
    World names;
    InstallAuthoredVocabulary(names);
    VerbRegistrationScope mine(*FindVerbRegistry(names), "mine");
    EXPECT_TRUE(mine.Declare(Verb("mine.zeta", "Last.")));
    EXPECT_TRUE(mine.Declare(Verb("mine.alpha", "First.")));
    VerbRegistrationScope theirs(*FindVerbRegistry(names), "theirs");
    EXPECT_TRUE(theirs.Declare(Verb("theirs.beta", "Not mine.")));
    ASSERT_TRUE(mine.Commit());
    ASSERT_TRUE(theirs.Commit());

    const std::string rendered = RenderVocabularyReference(names, "mine");
    EXPECT_EQ(rendered.find("theirs.beta"), std::string::npos);
    ASSERT_NE(rendered.find("mine.alpha"), std::string::npos);
    EXPECT_LT(rendered.find("mine.alpha"), rendered.find("mine.zeta"));
    EXPECT_NE(RenderVocabularyReference(names, "").find("theirs.beta"), std::string::npos);
}
