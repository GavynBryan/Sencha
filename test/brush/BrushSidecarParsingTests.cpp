// Hand-written or damaged brush sidecars load as diagnostics, never as a crash
// or a silently renumbered mesh.

#include "brush/BrushMeshSerialization.h"
#include "brush/BrushMeshStore.h"

#include <core/json/JsonParser.h>

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>

namespace
{
    JsonValue Parse(std::string_view text)
    {
        const std::optional<JsonValue> parsed = JsonParse(text);
        EXPECT_TRUE(parsed.has_value()) << text;
        return parsed.value_or(JsonValue{});
    }

    // A unit tetrahedron with outward faces, written as a sidecar entry.
    constexpr std::string_view kVertices =
        R"("vertices": [[0,0,0],[1,0,0],[0,1,0],[0,0,1]])";
    constexpr std::string_view kGoodFaces = R"([0,2,1],[0,1,3],[0,3,2],[1,2,3])";

    std::string Mesh(std::string_view vertices, std::string_view faces)
    {
        return "{" + std::string(vertices) + R"(, "faces": [)" + std::string(faces) + "]}";
    }
}

TEST(BrushSidecarParsing, AWellFormedMeshReportsNothing)
{
    std::string problems;
    const BrushMesh mesh = BrushMeshFromJson(Parse(Mesh(kVertices, kGoodFaces)), &problems);
    EXPECT_TRUE(problems.empty()) << problems;
    EXPECT_EQ(mesh.Vertices.size(), 4u);
    EXPECT_EQ(mesh.Faces.size(), 4u);
}

TEST(BrushSidecarParsing, AFaceWithABadLoopEntryIsDroppedAndNamed)
{
    struct Case
    {
        const char* Name;
        const char* BadFace;
    };
    const Case cases[] = {
        { "index past the last vertex", "[1,2,9]" },
        { "negative index", "[1,2,-1]" },
        { "fractional index", "[1,2,0.5]" },
        { "index that is not a number", R"([1,2,"three"])" },
        { "object face with a bad loop", R"({"loop": [1,2,4096]})" },
        { "object face without a loop", R"({"material": "asset://m.smat"})" },
    };
    for (const Case& c : cases)
    {
        const std::string faces = std::string("[0,2,1],[0,1,3],[0,3,2],") + c.BadFace;
        std::string problems;
        const BrushMesh mesh = BrushMeshFromJson(Parse(Mesh(kVertices, faces)), &problems);
        EXPECT_EQ(mesh.Faces.size(), 3u) << c.Name;
        EXPECT_NE(problems.find("face 3"), std::string::npos) << c.Name << ": " << problems;
    }
}

TEST(BrushSidecarParsing, AMalformedVertexRejectsTheMeshRatherThanRenumberingIt)
{
    const std::string vertices = R"("vertices": [[0,0,0],["x",0,0],[0,1,0],[0,0,1]])";
    std::string problems;
    const BrushMesh mesh = BrushMeshFromJson(Parse(Mesh(vertices, kGoodFaces)), &problems);
    EXPECT_TRUE(mesh.Vertices.empty());
    EXPECT_TRUE(mesh.Faces.empty());
    EXPECT_NE(problems.find("vertex 1"), std::string::npos) << problems;
}

TEST(BrushSidecarParsing, MalformedSoftEdgePairsAreIgnored)
{
    const std::string text = "{" + std::string(kVertices) + R"(, "faces": [)"
        + std::string(kGoodFaces) + R"(], "soft_edges": [0, "a", -3, 1.5, 0, 1]})";
    const BrushMesh mesh = BrushMeshFromJson(Parse(text));
    ASSERT_EQ(mesh.SoftEdges.size(), 1u);
    EXPECT_EQ(mesh.SoftEdges.front(), BrushSoftEdgeKey(0, 1));
}

TEST(BrushSidecarParsing, KeysThatAreNotBrushIdsAreSkippedAndNamed)
{
    const std::string entry = Mesh(kVertices, kGoodFaces);
    const std::string text = R"({"abc": )" + entry + R"(, "0": )" + entry + R"(, "7x": )" + entry
        + R"(, "5": )" + entry + "}";
    BrushMeshStore store;
    std::string error;
    DeserializeBrushMeshes(Parse(text), store, &error);

    EXPECT_EQ(store.All().size(), 1u);
    EXPECT_NE(store.FindRecord(BrushId{ 5 }), nullptr);
    for (const char* key : { "'abc'", "'0'", "'7x'" })
        EXPECT_NE(error.find(key), std::string::npos) << key << ": " << error;
}

TEST(BrushSidecarParsing, StoreProblemsNameTheirBrush)
{
    const std::string text = R"({"12": )" + Mesh(kVertices, "[0,2,1],[0,1,3],[0,3,2],[1,2,99]") + "}";
    BrushMeshStore store;
    std::string error;
    DeserializeBrushMeshes(Parse(text), store, &error);
    EXPECT_NE(error.find("brush 12: face 3"), std::string::npos) << error;
}
