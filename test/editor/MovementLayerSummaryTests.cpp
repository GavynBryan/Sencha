#include <gtest/gtest.h>

#include "movement/MovementLayerSummary.h"

#include <movement/MovementProfileData.h>

#include <string>
#include <vector>

//=============================================================================
// The synthesized wording is what an author reads instead of JSON, so the exact
// strings are pinned. A failure here means the phrasing changed: update the
// expectation deliberately rather than loosening the assertion.
//=============================================================================

namespace
{
    MovementLayerCondition AirCondition()
    {
        MovementLayerCondition condition;
        condition.Support = MovementSupportCondition::None;
        return condition;
    }

    std::vector<MovementCoefficientLabel> TemplateLabels()
    {
        DataAssetTypeRegistry types;
        DataSchemaRegistry schemas;
        RegisterMovementProfileData(types, schemas);
        const DataSchema* schema = schemas.Find("movement.profile");
        return schema ? MovementCoefficientLabels(*schema)
                      : std::vector<MovementCoefficientLabel>{};
    }
}

TEST(MovementLayerSummary, ConditionsReadAsSentences)
{
    struct Case
    {
        MovementLayerCondition Condition;
        const char* Sentence;
        const char* Name;
    };

    MovementLayerCondition unconditional;

    MovementLayerCondition steep;
    steep.Support = MovementSupportCondition::Steep;

    MovementLayerCondition jumping;
    jumping.AllTags = { "movement.jump.requested" };

    MovementLayerCondition combined;
    combined.Support = MovementSupportCondition::None;
    combined.AllTags = { "movement.gliding" };

    MovementLayerCondition moded;
    moded.Mode = "movement.mode.flight";

    MovementLayerCondition immersed;
    immersed.ImmersionAtLeast = 0.6f;

    MovementLayerCondition excluded;
    excluded.NoneTags = { "movement.jump.cooldown" };

    const Case cases[] = {
        { unconditional, "Always", "Always" },
        { AirCondition(), "When in the air", "In the air" },
        { steep, "When on steep ground", "On steep ground" },
        { jumping, "When while movement.jump.requested", "While movement.jump.requested" },
        { combined, "When in the air and while movement.gliding",
          "In the air and while movement.gliding" },
        { moded, "When in mode 'movement.mode.flight'", "In mode 'movement.mode.flight'" },
        { immersed, "When immersed at least 60%", "Immersed at least 60%" },
        { excluded, "When unless movement.jump.cooldown", "Unless movement.jump.cooldown" },
    };

    for (const Case& item : cases)
    {
        EXPECT_EQ(DescribeMovementCondition(item.Condition), item.Sentence);
        EXPECT_EQ(SynthesizeMovementLayerName(item.Condition), item.Name);
    }
}

TEST(MovementLayerSummary, AuthoredNameWinsOverSynthesis)
{
    MovementProfileLayer layer;
    layer.When = AirCondition();
    EXPECT_EQ(MovementLayerName(layer), "In the air");

    layer.Name = "Air control";
    EXPECT_EQ(MovementLayerName(layer), "Air control");
}

TEST(MovementLayerSummary, PatchesReadInApplicationOrder)
{
    const std::vector<MovementCoefficientLabel> labels = TemplateLabels();
    ASSERT_EQ(labels.size(), 8u);

    MovementProfileLayer layer;
    layer.Set.Friction = 0.0f;
    layer.Set.WishSpeedCap = 0.76f;
    layer.Scale.MaxSpeed = 0.5f;
    layer.Add.Drag = 2.0f;

    // Set, then scale, then add, matching how the runtime folds them together.
    EXPECT_EQ(DescribeMovementPatches(layer, labels),
              "Friction = 0 1/s, Wish speed cap = 0.76 m/s, "
              "Maximum speed x 0.5, Drag + 2 1/s");
}

TEST(MovementLayerSummary, EmptyLayerSummarizesAsNoChange)
{
    const std::vector<MovementCoefficientLabel> labels = TemplateLabels();
    const MovementProfileLayer layer;
    EXPECT_TRUE(DescribeMovementPatches(layer, labels).empty());
}

TEST(MovementLayerSummary, CoefficientsTrimTrailingZeros)
{
    EXPECT_EQ(FormatMovementCoefficient(0.0f), "0");
    EXPECT_EQ(FormatMovementCoefficient(4.5f), "4.5");
    EXPECT_EQ(FormatMovementCoefficient(0.76f), "0.76");
    EXPECT_EQ(FormatMovementCoefficient(2.07f), "2.07");
    EXPECT_EQ(FormatMovementCoefficient(10.0f), "10");
}

TEST(MovementLayerSummary, LabelsComeFromTheRegisteredSchema)
{
    const std::vector<MovementCoefficientLabel> labels = TemplateLabels();
    ASSERT_EQ(labels.size(), 8u);
    EXPECT_EQ(labels[0].Key, "max_speed");
    EXPECT_EQ(labels[0].Display, "Maximum speed");
    EXPECT_EQ(labels[0].Units, "m/s");
    EXPECT_EQ(labels[6].Key, "gravity_scale");
    EXPECT_EQ(labels[6].Units, "multiplier");
}
