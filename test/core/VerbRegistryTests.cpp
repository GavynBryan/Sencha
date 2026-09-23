// The authored vocabulary catalog: which names a World knows, which slot each
// one keeps, and what a provider is allowed to redeclare.
//
// Nothing here starts an engine. A catalog is a World resource with no service
// dependencies precisely so an editor can offer and validate a vocabulary
// without acquiring the power to run any of it.

#include <authored/VerbRegistry.h>

#include <gtest/gtest.h>

namespace
{
[[nodiscard]] DataFieldSchema NoArguments()
{
    DataFieldSchema root;
    root.Kind = DataFieldKind::Record;
    return root;
}

[[nodiscard]] DataFieldSchema OneInt(std::string key)
{
    DataFieldSchema field;
    field.Key = std::move(key);
    field.Kind = DataFieldKind::Int;
    DataFieldSchema root = NoArguments();
    root.Children.push_back(std::move(field));
    return root;
}

[[nodiscard]] VerbDefinition Verb(std::string name, DataFieldSchema arguments)
{
    VerbDefinition definition;
    definition.Name = std::move(name);
    definition.Arguments = std::move(arguments);
    return definition;
}

[[nodiscard]] bool DeclareOne(VerbRegistry& registry,
                              std::string provider,
                              VerbDefinition definition)
{
    VerbRegistrationScope scope(registry, std::move(provider));
    (void)scope.Declare(std::move(definition));
    return scope.Commit();
}
}

TEST(VerbNameTest, AcceptsDottedIdentifierSegmentsAndNothingElse)
{
    EXPECT_TRUE(IsValidAuthoredName("runtime.resume"));
    EXPECT_TRUE(IsValidAuthoredName("quit"));
    EXPECT_TRUE(IsValidAuthoredName("_private.step_2"));
    EXPECT_TRUE(IsValidAuthoredName("a.b.c.d"));

    EXPECT_FALSE(IsValidAuthoredName(""));
    EXPECT_FALSE(IsValidAuthoredName("."));
    EXPECT_FALSE(IsValidAuthoredName("runtime."));
    EXPECT_FALSE(IsValidAuthoredName(".resume"));
    EXPECT_FALSE(IsValidAuthoredName("runtime..resume"));
    EXPECT_FALSE(IsValidAuthoredName("2fast"));
    EXPECT_FALSE(IsValidAuthoredName("runtime resume"));
    EXPECT_FALSE(IsValidAuthoredName(" runtime.resume"));
    EXPECT_FALSE(IsValidAuthoredName("runtime.resume "));
    EXPECT_FALSE(IsValidAuthoredName("runtime-resume"));
}

TEST(VerbRegistryTest, EachCatalogMintsItsOwnIdentity)
{
    VerbRegistry first;
    VerbRegistry second;
    EXPECT_TRUE(first.Catalog().IsValid());
    EXPECT_NE(first.Catalog(), second.Catalog());

    // The same numeric slot means a different verb in each, which is exactly
    // why a compiled binding records the catalog beside the id.
    ASSERT_TRUE(DeclareOne(first, "engine", Verb("runtime.resume", NoArguments())));
    ASSERT_TRUE(DeclareOne(second, "engine", Verb("application.quit", NoArguments())));
    EXPECT_EQ(first.Find("runtime.resume"), second.Find("application.quit"));
    EXPECT_FALSE(second.Find("runtime.resume").IsValid());
}

TEST(VerbRegistryTest, AnInvalidBatchLeavesTheCatalogUntouched)
{
    VerbRegistry registry;
    ASSERT_TRUE(DeclareOne(registry, "engine", Verb("runtime.resume", NoArguments())));
    const std::size_t slotsBefore = registry.SlotCount();

    VerbRegistrationScope scope(registry, "game");
    EXPECT_TRUE(scope.Declare(Verb("game.good", NoArguments())));
    EXPECT_FALSE(scope.Declare(Verb("game..bad", NoArguments())));
    EXPECT_FALSE(scope.Commit());
    EXPECT_TRUE(scope.HasErrors());

    // Not even the well-formed half landed: a provider that declared a
    // vocabulary it got wrong has declared nothing.
    EXPECT_EQ(registry.SlotCount(), slotsBefore);
    EXPECT_FALSE(registry.Find("game.good").IsValid());
    EXPECT_TRUE(registry.Find("runtime.resume").IsValid());
}

TEST(VerbRegistryTest, ADeclarationIsCheckedBeforeItIsAccepted)
{
    VerbRegistry registry;

    DataFieldSchema duplicateKeys = NoArguments();
    duplicateKeys.Children.push_back(OneInt("Amount").Children.front());
    duplicateKeys.Children.push_back(OneInt("Amount").Children.front());

    DataFieldSchema emptyEnum = NoArguments();
    DataFieldSchema choice;
    choice.Key = "Mode";
    choice.Kind = DataFieldKind::Enum;
    emptyEnum.Children.push_back(choice);

    DataFieldSchema badRange = NoArguments();
    DataFieldSchema ranged;
    ranged.Key = "Scale";
    ranged.Kind = DataFieldKind::Float;
    ranged.Numeric.Minimum = 1.0;
    ranged.Numeric.Maximum = 0.0;
    badRange.Children.push_back(ranged);

    DataFieldSchema listWithoutElement = NoArguments();
    DataFieldSchema list;
    list.Key = "Targets";
    list.Kind = DataFieldKind::Array;
    listWithoutElement.Children.push_back(list);

    DataFieldSchema notARecord;
    notARecord.Kind = DataFieldKind::Int;

    for (const DataFieldSchema& bad :
         { duplicateKeys, emptyEnum, badRange, listWithoutElement, notARecord })
    {
        VerbRegistrationScope scope(registry, "game");
        EXPECT_FALSE(scope.Declare(Verb("game.op", bad)));
        EXPECT_FALSE(scope.Commit());
    }
    EXPECT_EQ(registry.SlotCount(), 0u);
}

TEST(VerbRegistryTest, TwoProvidersCannotClaimOneName)
{
    VerbRegistry registry;
    ASSERT_TRUE(DeclareOne(registry, "engine", Verb("runtime.resume", NoArguments())));

    VerbRegistrationScope scope(registry, "game");
    EXPECT_TRUE(scope.Declare(Verb("runtime.resume", NoArguments())));
    EXPECT_FALSE(scope.Commit());
    ASSERT_FALSE(scope.Errors().empty());
    // The diagnostic names the verb and both providers -- neither first-wins
    // nor last-wins would tell the author which declaration they meant.
    EXPECT_NE(scope.Errors().front().find("runtime.resume"), std::string::npos);
    EXPECT_NE(scope.Errors().front().find("engine"), std::string::npos);
    EXPECT_NE(scope.Errors().front().find("game"), std::string::npos);

    EXPECT_EQ(registry.Provider(registry.Find("runtime.resume")), "engine");
}

TEST(VerbRegistryTest, RedeclaringAnIdenticalContractIsIdempotent)
{
    VerbRegistry registry;
    ASSERT_TRUE(DeclareOne(registry, "engine", Verb("game.score", OneInt("Amount"))));
    const VerbId id = registry.Find("game.score");
    const VerbContractRevision revision = registry.Revision(id);

    // Same provider, same shape, a reworded label: a binding compiled last week
    // is still valid, so the revision must not move.
    VerbDefinition relabelled = Verb("game.score", OneInt("Amount"));
    relabelled.DisplayName = "Add score";
    relabelled.Description = "Adds points to the running total.";
    relabelled.Arguments.Children.front().DisplayName = "Points";
    relabelled.Arguments.Children.front().Numeric.Step = 5.0;
    ASSERT_TRUE(DeclareOne(registry, "engine", std::move(relabelled)));

    EXPECT_EQ(registry.Find("game.score"), id);
    EXPECT_EQ(registry.Revision(id), revision);
    EXPECT_EQ(registry.SlotCount(), 1u);
    EXPECT_EQ(registry.Get(id)->DisplayName, "Add score");
}

TEST(VerbRegistryTest, AChangedArgumentContractMovesTheRevision)
{
    VerbRegistry registry;
    ASSERT_TRUE(DeclareOne(registry, "engine", Verb("game.score", OneInt("Amount"))));
    const VerbId id = registry.Find("game.score");
    const VerbContractRevision before = registry.Revision(id);

    ASSERT_TRUE(DeclareOne(registry, "engine", Verb("game.score", OneInt("Points"))));
    EXPECT_EQ(registry.Find("game.score"), id);
    EXPECT_NE(registry.Revision(id), before);
}

TEST(VerbRegistryTest, ARetiredNameKeepsItsSlotAndResolvesToNothing)
{
    VerbRegistry registry;
    VerbRegistrationScope scope(registry, "module");
    EXPECT_TRUE(scope.Declare(Verb("module.first", NoArguments())));
    EXPECT_TRUE(scope.Declare(Verb("module.second", OneInt("Amount"))));
    ASSERT_TRUE(scope.Commit());

    const VerbId first = registry.Find("module.first");
    const VerbId second = registry.Find("module.second");
    ASSERT_TRUE(first.IsValid());
    ASSERT_TRUE(second.IsValid());

    registry.RetireProvider("module");
    EXPECT_FALSE(registry.Find("module.first").IsValid());
    EXPECT_FALSE(registry.IsLive(first));
    EXPECT_EQ(registry.Get(first), nullptr);
    EXPECT_FALSE(registry.Revision(first).IsValid());
    EXPECT_TRUE(registry.Live().empty());
    // The slots stay: a cached id must not come to mean whatever is declared
    // next.
    EXPECT_EQ(registry.SlotCount(), 2u);

    // A different provider declaring a new name gets a new slot, not a
    // retired one.
    ASSERT_TRUE(DeclareOne(registry, "other", Verb("other.thing", NoArguments())));
    EXPECT_EQ(registry.SlotCount(), 3u);
    EXPECT_NE(registry.Find("other.thing"), first);
}

TEST(VerbRegistryTest, AReturningNameRevivesItsSlotWithANewRevision)
{
    VerbRegistry registry;
    ASSERT_TRUE(DeclareOne(registry, "module", Verb("module.op", OneInt("Amount"))));
    const VerbId id = registry.Find("module.op");
    const VerbContractRevision before = registry.Revision(id);

    registry.RetireProvider("module");
    ASSERT_TRUE(DeclareOne(registry, "module", Verb("module.op", OneInt("Amount"))));

    EXPECT_EQ(registry.Find("module.op"), id);
    EXPECT_EQ(registry.SlotCount(), 1u);
    // Identical spelling, but the catalog cannot know the module in between was
    // the same build, so a binding compiled before the retirement recompiles.
    EXPECT_NE(registry.Revision(id), before);
}

TEST(VerbRegistryTest, EnumerationIsByIdAndNeverHashOrder)
{
    VerbRegistry registry;
    VerbRegistrationScope scope(registry, "engine");
    EXPECT_TRUE(scope.Declare(Verb("zulu.op", NoArguments())));
    EXPECT_TRUE(scope.Declare(Verb("alpha.op", NoArguments())));
    EXPECT_TRUE(scope.Declare(Verb("mike.op", NoArguments())));
    ASSERT_TRUE(scope.Commit());

    const std::vector<VerbId> live = registry.Live();
    ASSERT_EQ(live.size(), 3u);
    EXPECT_EQ(registry.Get(live[0])->Name, "zulu.op");
    EXPECT_EQ(registry.Get(live[1])->Name, "alpha.op");
    EXPECT_EQ(registry.Get(live[2])->Name, "mike.op");

    // Adding a name later does not move the ones already minted.
    ASSERT_TRUE(DeclareOne(registry, "engine", Verb("bravo.op", NoArguments())));
    const std::vector<VerbId> after = registry.Live();
    ASSERT_EQ(after.size(), 4u);
    EXPECT_EQ(after[0], live[0]);
    EXPECT_EQ(after[1], live[1]);
    EXPECT_EQ(after[2], live[2]);
}

TEST(VerbRegistryTest, AnIdFromAnotherCatalogIsNotAccepted)
{
    VerbRegistry crowded;
    VerbRegistrationScope scope(crowded, "engine");
    EXPECT_TRUE(scope.Declare(Verb("a.one", NoArguments())));
    EXPECT_TRUE(scope.Declare(Verb("a.two", NoArguments())));
    ASSERT_TRUE(scope.Commit());

    VerbRegistry sparse;
    ASSERT_TRUE(DeclareOne(sparse, "engine", Verb("a.one", NoArguments())));

    const VerbId two = crowded.Find("a.two");
    EXPECT_FALSE(sparse.IsLive(two));
    EXPECT_EQ(sparse.Get(two), nullptr);
}

TEST(VerbRegistryTest, AScopeCommitsOnce)
{
    VerbRegistry registry;
    VerbRegistrationScope scope(registry, "engine");
    EXPECT_TRUE(scope.Declare(Verb("engine.op", NoArguments())));
    EXPECT_TRUE(scope.Commit());
    EXPECT_FALSE(scope.Commit());
    EXPECT_EQ(registry.SlotCount(), 1u);
}

TEST(VerbRegistryTest, AScopeWithoutAProviderPublishesNothing)
{
    VerbRegistry registry;
    VerbRegistrationScope scope(registry, "");
    EXPECT_TRUE(scope.HasErrors());
    (void)scope.Declare(Verb("engine.op", NoArguments()));
    EXPECT_FALSE(scope.Commit());
    EXPECT_EQ(registry.SlotCount(), 0u);
}

TEST(VerbContractTest, PresentationDiffersFromContract)
{
    DataFieldSchema left = OneInt("Amount");
    DataFieldSchema right = OneInt("Amount");
    EXPECT_TRUE(AuthoredContractsMatch(left, right));

    right.Children.front().DisplayName = "Points";
    right.Children.front().Summary = "How many";
    right.Children.front().Description = "The number of points to add.";
    right.Children.front().Units = "points";
    right.Children.front().Advanced = true;
    right.Children.front().Numeric.Step = 10.0;
    EXPECT_TRUE(AuthoredContractsMatch(left, right));

    right.Children.front().Numeric.Maximum = 100.0;
    EXPECT_FALSE(AuthoredContractsMatch(left, right));
}
