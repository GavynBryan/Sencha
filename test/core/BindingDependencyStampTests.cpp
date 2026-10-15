// A dependency stamp matches until the thing it was taken from moves in a way
// that could change what a compiled binding resolves to.

#include <assets/data/DataAssetCache.h>
#include <authored/BindingDependencyStamp.h>
#include <authored/VerbRegistry.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <gtest/gtest.h>

TEST(BindingDependencyStamp, CatalogStampMovesOnPublishRetireAndIdentity)
{
    VerbRegistry verbs;
    const CatalogStamp empty = CatalogStamp::Of(verbs);
    EXPECT_TRUE(empty.Matches(verbs));

    {
        VerbRegistrationScope scope(verbs, "test");
        VerbDefinition verb;
        verb.Name = "test.go";
        ASSERT_TRUE(scope.Declare(std::move(verb)));
        ASSERT_TRUE(scope.Commit());
    }
    EXPECT_FALSE(empty.Matches(verbs));
    const CatalogStamp published = CatalogStamp::Of(verbs);
    EXPECT_TRUE(published.Matches(verbs));

    verbs.RetireProvider("test");
    EXPECT_FALSE(published.Matches(verbs));

    VerbRegistry other;
    EXPECT_FALSE(CatalogStamp::Of(other).Matches(verbs));
}

TEST(BindingDependencyStamp, TagStampMovesOnlyWhenTheVocabularyGrows)
{
    GameplayTagRegistry tags;
    (void)tags.RegisterTag("a.b");
    const TagVocabularyStamp stamp = TagVocabularyStamp::Of(tags);
    (void)tags.RegisterTag("a.b");
    EXPECT_TRUE(stamp.Matches(tags));
    (void)tags.RegisterTag("a.c");
    EXPECT_FALSE(stamp.Matches(tags));
}

TEST(BindingDependencyStamp, AnUnknownAssetMatchesItsOwnAbsence)
{
    DataAssetCache cache;
    const DataAssetStamp stamp = DataAssetStamp::Of(cache, DataAssetHandle{});
    EXPECT_TRUE(stamp.Matches(cache));
}
