// A projectile prefab as a designer writes one: only the fields that differ from
// the defaults, every effect and tag name optional. The document loads it, which
// is the path the cook takes too.

#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"

#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/JsonArchive.h>
#include <projectile/Projectile.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/IComponentSerializer.h>
#include <world/serialization/SceneSerializationContext.h>

#include <gtest/gtest.h>

TEST(ProjectileAuthoring, AProjectileNamingOnlySomeFieldsLoadsWithTheRestAtTheirDefaults)
{
    RegisterDocumentSerializers();
    LoggingProvider logging;
    EditorDocument document(logging);
    Registry& registry = document.GetScene().GetRegistry();
    IComponentSerializer* serializer = EditorSceneSerializers().FindByJsonKey("Projectile");
    ASSERT_NE(serializer, nullptr);
    serializer->RegisterStorage(registry);

    const EntityId entity = registry.Components.CreateEntity();
    const std::optional<JsonValue> json =
        JsonParse(R"({"speed": 30, "hit_effect": "asset://effects/burn.sdata", "max_hits": 3})");
    ASSERT_TRUE(json.has_value());
    JsonReadArchive archive(*json);
    SceneSerializationContext context(logging, nullptr);
    ASSERT_TRUE(serializer->Load(archive, entity, registry, context));

    const Projectile* loaded = std::as_const(registry.Components).TryGet<Projectile>(entity);
    ASSERT_NE(loaded, nullptr);
    EXPECT_FLOAT_EQ(loaded->Speed, 30.0f);
    EXPECT_EQ(loaded->MaxHits, 3u);
    EXPECT_EQ(loaded->HitEffect.View(), "asset://effects/burn.sdata");
    EXPECT_TRUE(loaded->SplashEffect.Empty()) << "a name left out is none";
    EXPECT_FLOAT_EQ(loaded->Lifetime, Projectile{}.Lifetime);
    EXPECT_TRUE(loaded->HitsCharacters);
}
