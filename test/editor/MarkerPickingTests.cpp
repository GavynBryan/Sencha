#include "WorkspaceFixture.h"

#include "authoring/EditorComponentAdapter.h"
#include "document/EditorScene.h"
#include "viewport/EditorViewport.h"
#include "viewport/ViewportProjection.h"

#include <render/PointLightComponent.h>

#include <gtest/gtest.h>

// A light has no shape to click: it stands in the views as a marker at its
// origin, drawn over everything, and the pointer on the marker picks it even
// with a brush behind.
namespace
{
class MarkerPickingTest : public WorkspaceTest
{
protected:
    EntityId AddLight(Vec3d position)
    {
        const EntityId entity = Scene().CreateEntity(position);
        Scene().GetRegistry().Components.AddComponent(entity, PointLightComponent{});
        return entity;
    }

    static EditorViewport TopView()
    {
        EditorViewport viewport;
        viewport.ApplyOrientation(ViewportOrientation::Top);
        viewport.Id = ViewportId{ 1 };
        viewport.RegionMin = Vec2d(0.0f, 0.0f);
        viewport.RegionMax = Vec2d(400.0f, 400.0f);
        return viewport;
    }

    SelectableRef PickAt(const EditorViewport& viewport, Vec2d point)
    {
        return Workspace.Picking.Pick(viewport, point, Scene());
    }
};
} // namespace

TEST_F(MarkerPickingTest, ALightInsideABrushIsPickedByItsMarker)
{
    const EntityId brush = AddBrush(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(2.0f, 2.0f, 2.0f));
    const EntityId light = AddLight(Vec3d(0.0f, 0.0f, 0.0f));
    const EditorViewport top = TopView();
    const Vec2d marker = ViewportProjection(top).WorldToPixel(Vec3d{})->Pixel;

    ViewportAffordanceOutput output;
    Workspace.Affordances->Build(output);
    ASSERT_EQ(output.Markers.size(), 1u);
    EXPECT_EQ(output.Markers[0].Entity, light);
    EXPECT_EQ(output.Markers[0].Icon, IconId::Light);

    const SelectableRef onMarker = PickAt(top, marker);
    EXPECT_TRUE(onMarker.IsEntity());
    EXPECT_EQ(onMarker.Entity, light) << "the marker is drawn over the brush, so it is what the pointer is on";

    const Vec2d insideBrush = ViewportProjection(top).WorldToPixel(Vec3d(1.5f, 0.0f, 1.5f))->Pixel;
    ASSERT_GT((insideBrush - marker).Magnitude(), 20.0f) << "the test needs a point clear of the marker";
    const SelectableRef beside = PickAt(top, insideBrush);
    EXPECT_EQ(beside.Entity, brush) << "off the marker, the brush behind it is still there to click";
}

TEST_F(MarkerPickingTest, AHiddenLightHasNoMarker)
{
    const EntityId light = AddLight(Vec3d(3.0f, 0.0f, 0.0f));
    Scene().SetEntityVisible(light, false);
    ViewportAffordanceOutput output;
    Workspace.Affordances->Build(output);
    EXPECT_TRUE(output.Markers.empty());
}
