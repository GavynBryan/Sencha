#include "ArenaScore.h"

#include <authored/AuthoredApi.h>
#include <authored/WorldVocabulary.h>
#include <ecs/World.h>

#include <gtest/gtest.h>

// The arena's verb was declared by hand before it was declared by annotation.
// Content -- the game's binding asset, the placed relay -- was authored against
// that contract, so the generated one must mean the same thing: same keys, same
// kinds, same range, same default, same optionality. Held here against the old
// declaration, written out as it was.
namespace
{
[[nodiscard]] DataFieldSchema HandWrittenAwardScoreArguments()
{
    DataFieldSchema side;
    side.Key = "Side";
    side.Kind = DataFieldKind::Enum;
    side.EnumChoices = { DataEnumChoice{ .Value = "red", .DisplayName = "Red", .Description = {} },
                         DataEnumChoice{ .Value = "blue", .DisplayName = "Blue", .Description = {} } };

    DataFieldSchema amount;
    amount.Key = "Amount";
    amount.Kind = DataFieldKind::Int;
    amount.Numeric.Minimum = 1.0;
    amount.Numeric.Maximum = 1000.0;
    amount.Default = std::int64_t{ 1 };

    DataFieldSchema source;
    source.Key = "Source";
    source.Kind = DataFieldKind::Optional;
    source.Required = false;
    DataFieldSchema entity;
    entity.Kind = DataFieldKind::Entity;
    source.Children.push_back(std::move(entity));

    DataFieldSchema root = EmptyVerbArguments();
    root.Children = { std::move(side), std::move(amount), std::move(source) };
    return root;
}
} // namespace

TEST(ArenaScoreContract, TheGeneratedVerbMeansWhatContentWasAuthoredAgainst)
{
    World world;
    InstallAuthoredVocabulary(world);
    DeclareArenaVocabulary(world);
    ASSERT_TRUE(AuthoredInstallationErrors(world).empty());

    const VerbRegistry& verbs = *FindVerbRegistry(world);
    const VerbDefinition* award = verbs.Get(verbs.Find("arena.award_score"));
    ASSERT_NE(award, nullptr);
    EXPECT_TRUE(AuthoredContractsMatch(award->Arguments, HandWrittenAwardScoreArguments()));
    EXPECT_EQ(award->DisplayName, "Award score");
    EXPECT_EQ(award->Category, "Arena");

    const AuthoredQueryRegistry& queries = *FindAuthoredQueryRegistry(world);
    EXPECT_TRUE(queries.Find("arena_scoreboard.red").IsValid());
    EXPECT_TRUE(queries.Find("arena_scoreboard.blue").IsValid());

    const AuthoredEventRegistry& events = *FindAuthoredEventRegistry(world);
    const AuthoredEventDefinition* changed = events.Get(events.Find("arena.score_changed"));
    ASSERT_NE(changed, nullptr);
    EXPECT_EQ(changed->SourceComponent, "arena_scoreboard");
}
