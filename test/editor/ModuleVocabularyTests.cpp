// A game module's gameplay vocabulary reaching the editor.
//
// Components travel to a document through the serializer registry; names do
// not. Tags, attributes, abilities, and locomotion modes are registration-order
// values that each World installs for itself, so the module's declaration has
// to be replayed into every document rather than run once at load. Without it
// the inspector's pickers offer only the engine's names, and a scene carrying
// the game's tags reads back with them missing.

#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"

#include <abilities/AbilityDefinition.h>
#include <app/EngineVerbs.h>
#include <authored/WorldVocabulary.h>
#include <abilities/AbilityRegistry.h>
#include <attributes/AttributeRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/LocomotionMode.h>

#include <gtest/gtest.h>

namespace
{
constexpr const char* kModuleTag = "Spike.Sprinting";
constexpr const char* kModuleAttribute = "SprintDrain";
constexpr const char* kModuleAbility = "Sprint";

// The installer a host sets at module load. Names only here: a real module's
// locomotion mode carries closures out of its own image, which is why the hook
// documents that a world must not outlive the mapping.
constexpr const char* kModuleVerb = "spike.sprint";

void InstallSpikeVocabulary(World& world)
{
    if (VerbRegistry* verbs = FindVerbRegistry(world))
    {
        VerbRegistrationScope scope(*verbs, "spike");
        VerbDefinition sprint;
        sprint.Name = kModuleVerb;
        (void)scope.Declare(std::move(sprint));
        (void)scope.Commit();
    }
    if (auto* tags = world.TryGetResource<GameplayTagRegistry>())
        (void)tags->RegisterTag(kModuleTag);
    if (auto* attributes = world.TryGetResource<AttributeRegistry>())
        (void)attributes->RegisterAttribute(kModuleAttribute, 0.0f, 10.0f, 3.0f);
    if (auto* abilities = world.TryGetResource<AbilityRegistry>())
        (void)abilities->Register(kModuleAbility, AbilityDefinition{});
}

class ModuleVocabularyTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    // Process-global, like the serializer registry it sits beside: leaving it
    // set would hand every later document in this binary the module's names.
    void TearDown() override { SetEditorModuleVocabulary({}); }

    [[nodiscard]] static bool CarriesSpikeVocabulary(EditorDocument& document)
    {
        World& world = document.GetScene().GetRegistry().Components;
        const auto* tags = world.TryGetResource<GameplayTagRegistry>();
        const auto* attributes = world.TryGetResource<AttributeRegistry>();
        const auto* abilities = world.TryGetResource<AbilityRegistry>();
        const VerbRegistry* verbs = FindVerbRegistry(world);
        return tags != nullptr && attributes != nullptr && abilities != nullptr
            && verbs != nullptr && verbs->Find(kModuleVerb).IsValid()
            && tags->FindTag(kModuleTag).IsValid()
            && attributes->FindAttribute(kModuleAttribute).IsValid()
            && abilities->Find(kModuleAbility).IsValid();
    }

    LoggingProvider Logging;
};
}

TEST_F(ModuleVocabularyTest, EveryDocumentBuiltWhileTheModuleIsLoadedCarriesItsNames)
{
    EditorDocument beforeLoad(Logging);
    EXPECT_FALSE(CarriesSpikeVocabulary(beforeLoad));

    SetEditorModuleVocabulary(InstallSpikeVocabulary);

    // Replayed per document, not shared: two open zones are two Worlds, and
    // each resolves the names in the content it holds against its own.
    EditorDocument first(Logging);
    EditorDocument second(Logging);
    EXPECT_TRUE(CarriesSpikeVocabulary(first));
    EXPECT_TRUE(CarriesSpikeVocabulary(second));

    // What the registration said, not just that a name exists.
    const AttributeRegistry& attributes =
        *first.GetScene().GetRegistry().Components.TryGetResource<AttributeRegistry>();
    const AttributeId drain = attributes.FindAttribute(kModuleAttribute);
    EXPECT_FLOAT_EQ(attributes.DefaultBase(drain), 3.0f);
    EXPECT_FLOAT_EQ(attributes.Max(drain), 10.0f);

    // A document built before the module loaded is unaffected: installing does
    // not reach back into worlds that already exist.
    EXPECT_FALSE(CarriesSpikeVocabulary(beforeLoad));
}

TEST_F(ModuleVocabularyTest, ClearingTheInstallerStopsTheNamesAtTheNextDocument)
{
    SetEditorModuleVocabulary(InstallSpikeVocabulary);
    EditorDocument loaded(Logging);
    ASSERT_TRUE(CarriesSpikeVocabulary(loaded));

    // The host clears it before unmapping the module, because the callable's
    // target lives in the module's image.
    SetEditorModuleVocabulary({});
    EditorDocument afterUnload(Logging);
    EXPECT_FALSE(CarriesSpikeVocabulary(afterUnload));
    // The document that already has them keeps them: they are values in its
    // own world, not a view onto the module.
    EXPECT_TRUE(CarriesSpikeVocabulary(loaded));
}

// The engine's own verbs are in every document, whether or not a module is
// loaded, so a picker can offer Resume beside a game's names. Declarations
// only: a document has no dispatcher, and nothing installed here can run.
TEST_F(ModuleVocabularyTest, EveryDocumentCarriesTheEngineVerbsWithoutADispatcher)
{
    EditorDocument document(Logging);
    const VerbRegistry* verbs =
        FindVerbRegistry(document.GetScene().GetRegistry().Components);
    ASSERT_NE(verbs, nullptr);
    EXPECT_TRUE(verbs->Find(kRuntimeResumeVerb).IsValid());
    EXPECT_TRUE(verbs->Find(kApplicationQuitVerb).IsValid());
    EXPECT_EQ(verbs->Provider(verbs->Find(kRuntimeResumeVerb)), "engine");
    EXPECT_TRUE(verbs->InstallationErrors().empty());

    // Two documents are two catalogs.
    EditorDocument other(Logging);
    EXPECT_NE(FindVerbRegistry(other.GetScene().GetRegistry().Components)->Catalog(),
              verbs->Catalog());
}
