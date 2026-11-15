#include <sencha/testing/UiLayoutTest.h>

#include <math/Angle.h>

#include <cmath>
#include <string>
#include <vector>

// `<sector>`: a vector shape a box cannot be, drawn antialiased. What a
// document gets is the band and the wedge it asked for, painted by its fill and
// stroke, every edge with a fringe of falling coverage rather than a hard step.
namespace
{
class UiSectorTest : public UiLayoutTest
{
protected:
    // Draws `sector` (its attributes and style) in a 100px box at the surface's
    // corner, and returns every vertex recorded.
    std::vector<UiVertex> Draw(std::string_view attributes, std::string_view style)
    {
        if (!OpenMarkup(R"(<sector id="s" )" + std::string(attributes) + "/>",
                        "#s { display: block; position: absolute; left: 0px; top: 0px; "
                        "width: 100px; height: 100px; " + std::string(style) + " }",
                        RenderExtent{ 200, 200 }).IsValid())
        {
            return {};
        }
        Ui().Update();
        Ui().ExtractRender();
        std::vector<UiVertex> vertices;
        for (const UiDrawFrame& frame : Ui().Frames())
            for (const UiDrawCommand& command : frame.Commands)
                if (command.Geometry)
                    vertices.insert(vertices.end(), command.Geometry->Vertices.begin(),
                                    command.Geometry->Vertices.end());
        return vertices;
    }
};

float Radius(const UiVertex& vertex)
{
    return std::hypot(vertex.Position.X - 50.0f, vertex.Position.Y - 50.0f);
}

// Degrees clockwise from straight up.
float Bearing(const UiVertex& vertex)
{
    const float degrees = ToDegrees(std::atan2(vertex.Position.X - 50.0f, -(vertex.Position.Y - 50.0f)));
    return degrees < 0.0f ? degrees + 360.0f : degrees;
}

std::uint8_t Alpha(const UiVertex& vertex)
{
    return vertex.ColorSrgbPremultiplied[3];
}
} // namespace

TEST_F(UiSectorTest, ASectorIsTheBandAndWedgeItNamesWithAnAntialiasedEdge)
{
    const std::vector<UiVertex> vertices = Draw(R"(start="90" sweep="90" inner="0.5")", "fill-color: #ff0000;");
    ASSERT_FALSE(vertices.empty()) << "the sector drew nothing";
    bool opaque = false;
    bool fringe = false;
    for (const UiVertex& vertex : vertices)
    {
        EXPECT_LE(Radius(vertex), 50.0f + 0.6f) << "outside the outer radius";
        EXPECT_GE(Radius(vertex), 25.0f - 0.6f) << "inside the inner radius";
        EXPECT_GE(Bearing(vertex), 90.0f - 1.5f) << "before the start";
        EXPECT_LE(Bearing(vertex), 180.0f + 1.5f) << "past the end";
        opaque |= Alpha(vertex) == 255;
        fringe |= Alpha(vertex) == 0;
    }
    EXPECT_TRUE(opaque);
    EXPECT_TRUE(fringe) << "no edge falls off: the shape would be aliased";
}

TEST_F(UiSectorTest, AStrokeIsDrawnInsideTheEdgeAndTheFillInsideIt)
{
    const std::vector<UiVertex> vertices =
        Draw(R"(inner="0")", "fill-color: #ff0000; stroke-color: #00ff00; stroke-width: 6px;");
    bool stroke = false;
    bool fill = false;
    for (const UiVertex& vertex : vertices)
    {
        if (Alpha(vertex) != 255)
            continue;
        EXPECT_LE(Radius(vertex), 50.0f) << "a full turn with no inner radius is a disc of the box";
        if (vertex.ColorSrgbPremultiplied[1] == 255)
            stroke = true;
        if (vertex.ColorSrgbPremultiplied[0] == 255)
        {
            fill = true;
            EXPECT_LE(Radius(vertex), 44.0f) << "the fill reaches under the stroke";
        }
    }
    EXPECT_TRUE(stroke);
    EXPECT_TRUE(fill);
}

TEST_F(UiSectorTest, NothingIsDrawnWithoutAFill)
{
    EXPECT_TRUE(Draw(R"(sweep="180")", "").empty()) << "the box itself is not the shape";
}
