#include "input/InputRouter.h"
#include "tools/RadialMenuFace.h"
#include "tools/RadialMenuModel.h"
#include "tools/RadialMenuSession.h"

#include <SDL3/SDL_keycode.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

// What an open wheel shows: keys where the session hit-tests, the one under
// the pointer hot, the hot entry's variants on the outer ring, and nothing at
// all once the key is let go.
namespace
{
class Menu : public IRadialMenuModel
{
public:
    std::vector<MenuItem> Items = { { .Label = "Select", .Icon = IconId::Pointer },
                                    { .Label = "Brush", .Icon = IconId::Box },
                                    { .Label = "Clip", .Icon = IconId::None } };
    std::vector<MenuItem> BrushShapes = { { .Label = "Box", .Icon = IconId::Box },
                                          { .Label = "Cylinder", .Icon = IconId::Cylinder } };

    int Count() const override { return static_cast<int>(Items.size()); }
    MenuItem Item(int index) const override { return Items[static_cast<std::size_t>(index)]; }
    int ActiveIndex() const override { return 0; }
    std::span<const MenuItem> Variants(int index) const override
    {
        return index == 1 ? std::span<const MenuItem>(BrushShapes) : std::span<const MenuItem>{};
    }
    int ActiveVariant(int) const override { return 1; }
    void Select(int, int) override {}
};

class RadialMenuFaceTest : public testing::Test
{
protected:
    RadialMenuFaceTest()
    {
        Wheel = std::make_unique<RadialMenuSession>(
            Items, KeyChord{ .Key = SDLK_Q, .Mods = {} },
            [] { return RadialMenu::Frame{ .Scale = 1.0f, .Min = { 0.0f, 0.0f }, .Max = { 1600.0f, 900.0f } }; },
            [](Vec2d) { return true; });
        Router.AddHandler([this](const InputEvent& e, PointerCapture& cap) { return Wheel->OnInput(e, cap); });
    }

    Menu Items;
    InputRouter Router;
    std::unique_ptr<RadialMenuSession> Wheel;
};
} // namespace

TEST_F(RadialMenuFaceTest, TheKeysSitWhereTheSessionHitTests)
{
    EXPECT_FALSE(FaceOf(*Wheel, Items).Open) << "nothing shows while the key is up";

    (void)Router.Route(KeyDownEvent{ .Key = SDLK_Q, .Modifiers = {}, .Pointer = { 800.0f, 450.0f } });
    const RadialMenu::Layout& layout = Wheel->GetLayout();
    const Vec2d brush = RadialMenu::SlotCenter(layout, 1);
    (void)Router.Route(PointerMoveEvent{ .Position = brush, .Delta = {}, .Modifiers = {} });

    const RadialMenuFace face = FaceOf(*Wheel, Items);
    ASSERT_TRUE(face.Open);
    ASSERT_EQ(face.Keys.size(), 3u + 2u) << "three entries and the hot entry's two variants";
    for (int index = 0; index < 3; ++index)
    {
        const RadialMenuKey& key = face.Keys[static_cast<std::size_t>(index)];
        EXPECT_EQ(key.Centre, RadialMenu::SlotCenter(layout, index));
        EXPECT_EQ(key.Hot, index == 1) << "only the entry under the pointer is hot";
        EXPECT_EQ(key.Active, index == 0) << "the entry in effect is marked";
        EXPECT_FALSE(key.Variant);
    }
    for (int v = 0; v < 2; ++v)
    {
        const RadialMenuKey& key = face.Keys[3u + static_cast<std::size_t>(v)];
        EXPECT_TRUE(key.Variant);
        EXPECT_EQ(key.Centre, RadialMenu::VariantSlotCenter(layout, 1, v, 2));
        EXPECT_EQ(key.Active, v == 1);
    }
    EXPECT_EQ(face.Caption, "Brush");
    EXPECT_FALSE(face.CaptionNamesCurrent);
    EXPECT_GT(face.CaptionTop, face.Centre.Y + face.PlateRadius) << "the caption sits under the wheel";

    (void)Router.Route(KeyUpEvent{ .Key = SDLK_Q, .Modifiers = {}, .Pointer = brush });
    EXPECT_FALSE(FaceOf(*Wheel, Items).Open) << "letting the key go takes the wheel away";
}

TEST_F(RadialMenuFaceTest, WithNothingHotTheCaptionNamesWhatIsOn)
{
    (void)Router.Route(KeyDownEvent{ .Key = SDLK_Q, .Modifiers = {}, .Pointer = { 800.0f, 450.0f } });
    const RadialMenuFace face = FaceOf(*Wheel, Items);
    ASSERT_TRUE(face.Open);
    EXPECT_EQ(face.Caption, "Select");
    EXPECT_TRUE(face.CaptionNamesCurrent);
    EXPECT_EQ(face.Keys.size(), 3u) << "no fan without a hot entry";
}
