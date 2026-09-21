// The previewer's view of the authored vocabulary: what a document can name,
// with a game module's names beside the engine's, and whether each record of a
// binding asset resolves -- all without a dispatcher, so nothing here can run.

#include "authoring/VocabularyCatalog.h"

#include <app/EngineVerbs.h>
#include <app/Game.h>
#include <authored/WorldVocabulary.h>
#include <ecs/World.h>

#include <gtest/gtest.h>

namespace
{
constexpr const char* kModuleVerb = "spike.grapple.fire";

// What a module's hook does, and nothing else: names into the World it is
// handed. It never sees an engine.
class SpikeGame final : public Game
{
public:
    void OnRegisterVocabulary(World& world) override
    {
        ++Calls;
        VerbRegistry* verbs = FindVerbRegistry(world);
        if (verbs == nullptr)
            return;
        VerbRegistrationScope scope(*verbs, "spike");
        VerbDefinition fire;
        fire.Name = kModuleVerb;
        fire.DisplayName = "Fire grapple";
        DataFieldSchema range;
        range.Key = "Range";
        range.Kind = DataFieldKind::Float;
        fire.Arguments.Children.push_back(std::move(range));
        (void)scope.Declare(std::move(fire));
        if (DeclareBadly)
        {
            VerbDefinition bad;
            bad.Name = "spike..broken";
            (void)scope.Declare(std::move(bad));
        }
        (void)scope.Commit();
    }

    int Calls = 0;
    bool DeclareBadly = false;
};

[[nodiscard]] VerbBindingDesc Record(std::string key, std::string verb)
{
    VerbBindingDesc desc;
    desc.Key = std::move(key);
    desc.KeyId = MakeVerbBindingKey(desc.Key);
    desc.VerbName = std::move(verb);
    return desc;
}
}

TEST(ShojiVocabulary, TheEngineVerbsAreThereBeforeAnyModuleIs)
{
    VocabularyCatalog catalog;
    EXPECT_TRUE(catalog.Errors().empty());
    const std::vector<VocabularyCatalog::VerbRow> verbs = catalog.ListVerbs();
    ASSERT_EQ(verbs.size(), 2u);
    EXPECT_EQ(verbs[0].Name, kRuntimeResumeVerb);
    EXPECT_EQ(verbs[0].Provider, "engine");
    EXPECT_EQ(verbs[1].Name, kApplicationQuitVerb);
}

TEST(ShojiVocabulary, ALoadedModulesNamesAppearBesideTheEnginesWithoutItsRuntime)
{
    VocabularyCatalog catalog;
    SpikeGame game;
    catalog.InstallModuleVocabulary(game);
    EXPECT_EQ(game.Calls, 1);
    EXPECT_TRUE(catalog.Errors().empty());

    const std::vector<VocabularyCatalog::VerbRow> verbs = catalog.ListVerbs();
    ASSERT_EQ(verbs.size(), 3u);
    EXPECT_EQ(verbs[2].Name, kModuleVerb);
    EXPECT_EQ(verbs[2].Provider, "spike");
    EXPECT_EQ(verbs[2].ArgumentCount, 1u);
}

TEST(ShojiVocabulary, ARecordIsReportedResolvedOrNotAndNeverLost)
{
    VocabularyCatalog catalog;
    SpikeGame game;
    catalog.InstallModuleVocabulary(game);

    VerbBindingLibrary library;
    library.Bindings.push_back(Record("menu.resume", std::string(kRuntimeResumeVerb)));
    VerbBindingDesc fire = Record("hook.fire", kModuleVerb);
    VerbBindingArgument range;
    range.Key = "Range";
    range.Source = VerbArgumentSource::Literal;
    range.Literal = JsonValue(12.0);
    fire.Arguments.push_back(std::move(range));
    library.Bindings.push_back(std::move(fire));
    library.Bindings.push_back(Record("orphan", "never.declared"));

    const std::vector<VocabularyCatalog::BindingRow> rows = catalog.Inspect(library);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_TRUE(rows[0].Resolved);
    EXPECT_TRUE(rows[1].Resolved);
    EXPECT_FALSE(rows[2].Resolved);
    EXPECT_NE(rows[2].Error.find("never.declared"), std::string::npos);
    // Inspection reads; the source records are exactly what they were.
    EXPECT_EQ(library.Bindings[2].VerbName, "never.declared");
    EXPECT_EQ(library.Bindings.size(), 3u);
}

TEST(ShojiVocabulary, AModulesRefusedDeclarationIsReportedNotHidden)
{
    VocabularyCatalog catalog;
    SpikeGame game;
    game.DeclareBadly = true;
    catalog.InstallModuleVocabulary(game);
    ASSERT_FALSE(catalog.Errors().empty());
    EXPECT_NE(catalog.Errors().front().find("spike..broken"), std::string::npos);
    // The whole batch was refused: the good name did not land either.
    EXPECT_EQ(catalog.ListVerbs().size(), 2u);
}

TEST(ShojiVocabulary, TwoCatalogsAreTwoWorlds)
{
    VocabularyCatalog first;
    VocabularyCatalog second;
    EXPECT_NE(first.Verbs().Catalog(), second.Verbs().Catalog());
}
