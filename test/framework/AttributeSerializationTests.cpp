#include <framework/attributes/AttributeRegistry.h>
#include <framework/attributes/AttributeSerialization.h>
#include <framework/attributes/AttributeSet.h>
#include <core/serialization/JsonArchive.h>

#include <gtest/gtest.h>

// Attributes persist by name and reload against a registry that assigns different
// ids (different registration order) — base values ride on names, not ids.
TEST(AttributeSerialization, RoundTripsByNameAcrossDifferingRegistries)
{
    AttributeRegistry saveReg;
    const AttributeId hp  = saveReg.RegisterAttribute("Health", 0.0f, 100.0f);
    const AttributeId sta = saveReg.RegisterAttribute("Stamina", 0.0f, 50.0f);

    AttributeSet s{};
    s.Add(hp, 73.0f);
    s.Add(sta, 20.0f);

    JsonWriteArchive wa;
    ASSERT_TRUE(WriteAttributes(wa, s, saveReg));
    const JsonValue jv = wa.TakeValue();

    AttributeRegistry loadReg; // same names, different order -> different ids
    loadReg.RegisterAttribute("Unrelated");
    loadReg.RegisterAttribute("Stamina", 0.0f, 50.0f);
    loadReg.RegisterAttribute("Health", 0.0f, 100.0f);
    EXPECT_NE(saveReg.FindAttribute("Health").Value, loadReg.FindAttribute("Health").Value);

    AttributeSet s2{};
    JsonReadArchive ra(jv);
    ASSERT_TRUE(ReadAttributes(ra, s2, loadReg));

    EXPECT_EQ(s2.Size(), 2u);
    EXPECT_FLOAT_EQ(s2.GetBase(loadReg.FindAttribute("Health")), 73.0f);
    EXPECT_FLOAT_EQ(s2.GetBase(loadReg.FindAttribute("Stamina")), 20.0f);
}

TEST(AttributeSerialization, UnknownAttributesAreSkippedNotFatal)
{
    AttributeRegistry saveReg;
    AttributeSet s{};
    s.Add(saveReg.RegisterAttribute("Health"), 10.0f);
    s.Add(saveReg.RegisterAttribute("Stamina"), 5.0f);

    JsonWriteArchive wa;
    ASSERT_TRUE(WriteAttributes(wa, s, saveReg));
    const JsonValue jv = wa.TakeValue();

    AttributeRegistry sparse; // knows only Health
    sparse.RegisterAttribute("Health");

    AttributeSet s2{};
    JsonReadArchive ra(jv);
    EXPECT_TRUE(ReadAttributes(ra, s2, sparse));
    EXPECT_EQ(s2.Size(), 1u);
    EXPECT_TRUE(s2.Has(sparse.FindAttribute("Health")));
}
