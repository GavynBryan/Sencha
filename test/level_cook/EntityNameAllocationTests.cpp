// Default entity names: every EditorScene factory stamps the smallest unused
// name on its base ("Brush", "Brush 1", "Brush 2"), deletions free their
// suffix for reuse, and the names survive a document round trip. The
// allocator itself is NextEntityName; the factories are its only callers
// besides duplication.

#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/EntityNameComponent.h"

#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
    class EntityNameAllocationTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterDocumentSerializers(); }

        [[nodiscard]] std::string NameOf(EntityId entity) const
        {
            const auto* name = Document.GetRegistry()
                                   .Components.TryGet<EntityNameComponent>(entity);
            return name != nullptr ? std::string(name->Value.View()) : std::string();
        }

        LoggingProvider Logging;
        EditorDocument  Document{ Logging };
        EditorScene&    Scene = Document.GetScene();
    };

    TEST_F(EntityNameAllocationTest, FactoriesStampIncrementingDefaults)
    {
        EXPECT_EQ(NameOf(Scene.CreateBrush({}, { 1, 1, 1 })), "Brush");
        EXPECT_EQ(NameOf(Scene.CreateBrush({}, { 1, 1, 1 })), "Brush 1");
        EXPECT_EQ(NameOf(Scene.CreateBrush({}, { 1, 1, 1 })), "Brush 2");
        EXPECT_EQ(NameOf(Scene.CreateEntity({})), "Entity");
        EXPECT_EQ(NameOf(Scene.CreateEntity({})), "Entity 1");
        EXPECT_EQ(NameOf(Scene.CreateCamera({})), "Camera");
    }

    TEST_F(EntityNameAllocationTest, ADeletedSuffixIsReused)
    {
        (void)Scene.CreateBrush({}, { 1, 1, 1 });
        const EntityId middle = Scene.CreateBrush({}, { 1, 1, 1 });
        (void)Scene.CreateBrush({}, { 1, 1, 1 });
        ASSERT_EQ(NameOf(middle), "Brush 1");

        Scene.DestroyEntity(middle);
        EXPECT_EQ(NameOf(Scene.CreateBrush({}, { 1, 1, 1 })), "Brush 1");
    }

    TEST_F(EntityNameAllocationTest, DefaultNamesRoundTripThroughTheDocument)
    {
        (void)Scene.CreateBrush({}, { 1, 1, 1 });
        (void)Scene.CreateBrush({}, { 1, 1, 1 });

        EditorDocument reloaded(Logging);
        ASSERT_TRUE(reloaded.LoadFromSceneText(Document.ToSceneText()));
        // The reloaded names are taken: the next brush continues the family.
        EXPECT_EQ(reloaded.GetScene().GetEntityCount(), 2u);
        const EntityId next = reloaded.GetScene().CreateBrush({}, { 1, 1, 1 });
        const auto* name = reloaded.GetRegistry()
                               .Components.TryGet<EntityNameComponent>(next);
        ASSERT_NE(name, nullptr);
        EXPECT_EQ(std::string(name->Value.View()), "Brush 2");
    }

    TEST_F(EntityNameAllocationTest, ALongBaseCannotCollideThroughTruncation)
    {
        const std::string longBase(80, 'x');
        const std::string first = Scene.NextEntityName(longBase);
        const EntityId a = Scene.CreateEntity({});
        Scene.GetRegistry().Components.TryGet<EntityNameComponent>(a)->Value =
            InlineString<64>(first);
        const std::string second = Scene.NextEntityName(longBase);
        EXPECT_NE(first, second);
        // Both fit the component's 63-char storage untruncated.
        EXPECT_LE(first.size(), 63u);
        EXPECT_LE(second.size(), 63u);
    }
}
