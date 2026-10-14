#include "authoring/PlaceIcons.h"

#include <core/metadata/ComponentDefinition.h>
#include <core/metadata/ComponentPlacement.h>
#include <core/metadata/Field.h>
#include <core/serialization/FourCC.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <memory>
#include <string_view>
#include <tuple>

// A component names the icon it is placed by as data; one that names no icon
// is told so, rather than placed with none in silence.
namespace
{
struct WellMarked
{
    float Value = 0.0f;
};
struct Misnamed
{
    float Value = 0.0f;
};
} // namespace

template <>
struct ComponentDefinition<WellMarked>
{
    static constexpr std::string_view Identity = "test.well_marked";
    static constexpr std::string_view SchemaName = "WellMarked";
    static constexpr std::uint32_t SceneChunk = MakeFourCC('W', 'M', 'R', 'K');
    static constexpr std::string_view PlaceableLabel = "Well Marked";
    static constexpr std::string_view PlaceableIcon = "flag";
    static auto Fields() { return std::tuple{ MakeField("value", &WellMarked::Value).Default(0.0f) }; }
};

template <>
struct ComponentDefinition<Misnamed>
{
    static constexpr std::string_view Identity = "test.misnamed";
    static constexpr std::string_view SchemaName = "Misnamed";
    static constexpr std::uint32_t SceneChunk = MakeFourCC('M', 'S', 'N', 'M');
    static constexpr std::string_view PlaceableLabel = "Misnamed";
    static constexpr std::string_view PlaceableIcon = "flagg";
    static auto Fields() { return std::tuple{ MakeField("value", &Misnamed::Value).Default(0.0f) }; }
};

TEST(PlaceIcons, AnIconNoneIsNamedIsAProblemNotASilence)
{
    ComponentSerializerRegistry serializers;
    (void)serializers.Register(std::make_unique<ComponentSerializer<WellMarked>>());
    (void)serializers.Register(std::make_unique<ComponentSerializer<Misnamed>>());

    EXPECT_EQ(PlaceIconOf(*serializers.FindByJsonKey("WellMarked")), IconId::Flag);
    EXPECT_EQ(PlaceIconOf(*serializers.FindByJsonKey("Misnamed")), IconId::None);

    const std::vector<Problem> problems = UnknownPlaceIcons(serializers);
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems.front().Location, "Misnamed");
    EXPECT_NE(problems.front().Message.find("flagg"), std::string::npos);
}
