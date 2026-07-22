#include <assets/cook/CookFingerprint.h>

#include <gtest/gtest.h>

TEST(CookFingerprintTest, FieldsAreOrderAndShapeSensitive)
{
    CookFingerprint first("test", 3);
    first.AddString("a", "bc").AddString("d", "e");

    CookFingerprint same("test", 3);
    same.AddString("a", "bc").AddString("d", "e");
    EXPECT_EQ(first.Value(), same.Value());

    CookFingerprint regrouped("test", 3);
    regrouped.AddString("a", "b").AddString("c", "de");
    EXPECT_NE(first.Value(), regrouped.Value());

    CookFingerprint reordered("test", 3);
    reordered.AddString("d", "e").AddString("a", "bc");
    EXPECT_NE(first.Value(), reordered.Value());
}

TEST(CookFingerprintTest, VersionAndDependenciesInvalidateIdentity)
{
    CookFingerprint versionOne("mesh", 1);
    versionOne.AddU32("count", 4).AddDependency("brush_cells", 10);

    CookFingerprint versionTwo("mesh", 2);
    versionTwo.AddU32("count", 4).AddDependency("brush_cells", 10);
    EXPECT_NE(versionOne.Value(), versionTwo.Value());

    CookFingerprint changedDependency("mesh", 1);
    changedDependency.AddU32("count", 4).AddDependency("brush_cells", 11);
    EXPECT_NE(versionOne.Value(), changedDependency.Value());
}

