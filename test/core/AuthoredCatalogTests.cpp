// What every authored catalog shares -- validating a batch without changing
// anything -- and what a provider declaring into all three at once is
// promised: every batch publishes, or none does.

#include <authored/AuthoredApi.h>
#include <authored/WorldVocabulary.h>

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

namespace
{
[[nodiscard]] VerbDefinition Verb(std::string name)
{
    VerbDefinition definition;
    definition.Name = std::move(name);
    return definition;
}

[[nodiscard]] AuthoredQueryDefinition QueryNamed(std::string name)
{
    AuthoredQueryDefinition definition;
    definition.Name = std::move(name);
    definition.Result.Kind = DataFieldKind::Bool;
    return definition;
}

[[nodiscard]] AuthoredEventDefinition Event(std::string name)
{
    AuthoredEventDefinition definition;
    definition.Name = std::move(name);
    return definition;
}
} // namespace

TEST(AuthoredCatalog, ValidatingAPublishChangesNothing)
{
    AuthoredQueryRegistry queries;
    {
        AuthoredQueryRegistrationScope scope(queries, "first");
        ASSERT_TRUE(scope.Declare(QueryNamed("thing.lit")));
        ASSERT_TRUE(scope.Commit());
    }
    const std::uint64_t generation = queries.Generation();
    const std::size_t slots = queries.SlotCount();

    std::vector<std::string> errors;
    const std::vector<AuthoredQueryDefinition> fresh{ QueryNamed("thing.hot") };
    EXPECT_TRUE(queries.ValidatePublish("second", fresh, errors));
    const std::vector<AuthoredQueryDefinition> conflicting{ QueryNamed("thing.lit") };
    EXPECT_FALSE(queries.ValidatePublish("second", conflicting, errors));
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors.front().find("first"), std::string::npos);

    // Asked twice, answered twice, and nothing moved.
    EXPECT_EQ(queries.Generation(), generation);
    EXPECT_EQ(queries.SlotCount(), slots);
    EXPECT_FALSE(queries.Find("thing.hot").IsValid());
    EXPECT_TRUE(queries.InstallationErrors().empty());
}

TEST(AuthoredCatalog, EachKindValidatesItsOwnShape)
{
    AuthoredQueryRegistry queries;
    AuthoredQueryRegistrationScope queryScope(queries, "test");
    AuthoredQueryDefinition optional = QueryNamed("thing.maybe");
    optional.Result.Kind = DataFieldKind::Optional;
    EXPECT_FALSE(queryScope.Declare(std::move(optional)))
        << "a query with no answer reports Unavailable; its result is never optional";

    AuthoredEventRegistry events;
    AuthoredEventRegistrationScope eventScope(events, "test");
    AuthoredEventDefinition flat = Event("thing.happened");
    flat.Payload.Kind = DataFieldKind::Int;
    EXPECT_FALSE(eventScope.Declare(std::move(flat)));

    // A target's expected component belongs on an entity and nowhere else.
    VerbRegistry verbs;
    VerbRegistrationScope verbScope(verbs, "test");
    VerbDefinition misplaced = Verb("thing.poke");
    DataFieldSchema count;
    count.Key = "Count";
    count.Kind = DataFieldKind::Int;
    count.Reference.ComponentIdentity = "game.door";
    misplaced.Arguments.Children.push_back(std::move(count));
    EXPECT_FALSE(verbScope.Declare(std::move(misplaced)));
}

TEST(AuthoredCatalog, AnExpectedComponentIsMetadataNotContract)
{
    VerbRegistry verbs;
    const auto declare = [&verbs](std::string component) {
        VerbDefinition open = Verb("door.open");
        DataFieldSchema door;
        door.Key = "door";
        door.Kind = DataFieldKind::Entity;
        door.Reference.ComponentIdentity = std::move(component);
        open.Arguments.Children.push_back(std::move(door));
        VerbRegistrationScope scope(verbs, "doors");
        (void)scope.Declare(std::move(open));
        return scope.Commit();
    };
    ASSERT_TRUE(declare("game.door"));
    const VerbContractRevision before = verbs.Revision(verbs.Find("door.open"));
    ASSERT_TRUE(declare("game.gate"));
    // Refining which entities an author is offered does not invalidate a
    // binding compiled against the old expectation.
    EXPECT_EQ(verbs.Revision(verbs.Find("door.open")), before);
}

namespace
{
class VocabularyScopeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        InstallAuthoredVocabulary(Entities);
        // Another provider already owns a query name.
        AuthoredQueryRegistrationScope queries(*FindAuthoredQueryRegistry(Entities), "other");
        (void)queries.Declare(QueryNamed("taken.query"));
        ASSERT_TRUE(queries.Commit());
        Generations = CurrentGenerations();
    }

    [[nodiscard]] std::array<std::uint64_t, 3> CurrentGenerations() const
    {
        return { FindVerbRegistry(Entities)->Generation(),
                 FindAuthoredQueryRegistry(Entities)->Generation(),
                 FindAuthoredEventRegistry(Entities)->Generation() };
    }

    World Entities;
    std::array<std::uint64_t, 3> Generations{};
};
} // namespace

// A tiny hand-written API, shaped the way the generator writes one, so the
// scope's Declare has something to route.
struct CatalogProbeVerbs
{
};
template<>
struct AuthoredApiDefinition<CatalogProbeVerbs>
{
    using Target = CatalogProbeVerbs;
    static VerbDefinition Describe_Go() { return Verb("probe.go"); }
    static VerbAdmission Invoke_Go(CatalogProbeVerbs&, const VerbInvocation&)
    {
        return VerbAdmission::Accepted;
    }
    static constexpr std::array<AuthoredVerbEntry<CatalogProbeVerbs>, 1> Verbs{ {
        { "probe.go", &Describe_Go, &Invoke_Go },
    } };
};

struct CatalogProbeEvent
{
};
template<>
struct AuthoredApiDefinition<CatalogProbeEvent>
{
    static constexpr std::string_view EventName = "probe.went";
    static AuthoredEventDefinition DescribeEvent() { return Event("probe.went"); }
    static void Encode(const CatalogProbeEvent&, AuthoredArguments& payload) { payload.Resize(0); }
};

struct CatalogProbeQueries
{
};
template<>
struct AuthoredApiDefinition<CatalogProbeQueries>
{
    using Target = CatalogProbeQueries;
    static AuthoredQueryDefinition Describe_Ok() { return QueryNamed("probe.ok"); }
    static AuthoredQueryStatus Evaluate(const CatalogProbeQueries&, std::span<const AuthoredValue>,
                                        AuthoredValue&)
    {
        return AuthoredQueryStatus::Unavailable;
    }
    static constexpr std::array<AuthoredQueryEntry<CatalogProbeQueries>, 1> Queries{ {
        { "probe.ok", &Describe_Ok, &Evaluate },
    } };
};

struct CatalogProbeConflict
{
};
template<>
struct AuthoredApiDefinition<CatalogProbeConflict>
{
    using Target = CatalogProbeConflict;
    static AuthoredQueryDefinition Describe_Taken() { return QueryNamed("taken.query"); }
    static AuthoredQueryStatus Evaluate(const CatalogProbeConflict&, std::span<const AuthoredValue>,
                                        AuthoredValue&)
    {
        return AuthoredQueryStatus::Unavailable;
    }
    static constexpr std::array<AuthoredQueryEntry<CatalogProbeConflict>, 1> Queries{ {
        { "taken.query", &Describe_Taken, &Evaluate },
    } };
};

TEST_F(VocabularyScopeTest, ACleanCommitPublishesEveryKind)
{
    AuthoredVocabularyScope vocabulary(Entities, "probe");
    vocabulary.Declare<CatalogProbeVerbs>();
    vocabulary.Declare<CatalogProbeQueries>();
    vocabulary.Declare<CatalogProbeEvent>();
    ASSERT_TRUE(vocabulary.Commit());

    EXPECT_TRUE(FindVerbRegistry(Entities)->Find("probe.go").IsValid());
    EXPECT_TRUE(FindAuthoredQueryRegistry(Entities)->Find("probe.ok").IsValid());
    EXPECT_TRUE(FindAuthoredEventRegistry(Entities)->Find("probe.went").IsValid());
    EXPECT_TRUE(AuthoredInstallationErrors(Entities).empty());
}

TEST_F(VocabularyScopeTest, AConflictInOneCatalogPublishesNothingInAny)
{
    AuthoredVocabularyScope vocabulary(Entities, "probe");
    vocabulary.Declare<CatalogProbeVerbs>();
    vocabulary.Declare<CatalogProbeConflict>();
    vocabulary.Declare<CatalogProbeEvent>();
    EXPECT_FALSE(vocabulary.Commit());

    // Not half a module: the verb and the event are absent too, and no
    // catalog so much as moved its generation.
    EXPECT_FALSE(FindVerbRegistry(Entities)->Find("probe.go").IsValid());
    EXPECT_FALSE(FindAuthoredEventRegistry(Entities)->Find("probe.went").IsValid());
    EXPECT_EQ(CurrentGenerations(), Generations);

    // The reason is where the host reads it, in the catalog it concerns.
    const std::vector<std::string> errors = AuthoredInstallationErrors(Entities);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors.front().find("taken.query"), std::string::npos);
    EXPECT_EQ(FindAuthoredQueryRegistry(Entities)->InstallationErrors().size(), 1u);
}

TEST_F(VocabularyScopeTest, ASchemaErrorInOneBatchBlocksTheOthers)
{
    AuthoredVocabularyScope vocabulary(Entities, "probe");
    vocabulary.Declare<CatalogProbeVerbs>();
    vocabulary.Declare<CatalogProbeQueries>();
    // Declared twice: the event batch is malformed on its own.
    vocabulary.Declare<CatalogProbeEvent>();
    vocabulary.Declare<CatalogProbeEvent>();
    EXPECT_FALSE(vocabulary.Commit());
    EXPECT_FALSE(FindVerbRegistry(Entities)->Find("probe.go").IsValid());
    EXPECT_FALSE(FindAuthoredQueryRegistry(Entities)->Find("probe.ok").IsValid());
    EXPECT_EQ(CurrentGenerations(), Generations);
}

TEST_F(VocabularyScopeTest, AScopeCommitsOnce)
{
    AuthoredVocabularyScope vocabulary(Entities, "probe");
    vocabulary.Declare<CatalogProbeVerbs>();
    ASSERT_TRUE(vocabulary.Commit());
    EXPECT_FALSE(vocabulary.Commit());
}

TEST(AuthoredVocabularyScope, AWorldWithoutCatalogsTakesNothing)
{
    World bare;
    AuthoredVocabularyScope vocabulary(bare, "probe");
    vocabulary.Declare<CatalogProbeVerbs>();
    EXPECT_FALSE(vocabulary.Commit());
    EXPECT_EQ(FindVerbRegistry(bare), nullptr);
}
