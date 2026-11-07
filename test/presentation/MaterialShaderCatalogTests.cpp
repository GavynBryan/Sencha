#include <gtest/gtest.h>

#include <render/MaterialShaderCatalog.h>
#include <shaders/kMeshForwardFragSpv.h>
#include <shaders/kMeshForwardVertSpv.h>

#include <array>
#include <cstdint>
#include <span>

namespace
{
    MaterialShaderDesc Desc(std::string_view name, const std::uint32_t* words, std::size_t count)
    {
        return MaterialShaderDesc{ .Name = name, .FragmentSpirv = std::span<const std::uint32_t>(words, count) };
    }
}

TEST(MaterialShaderCatalog, AcceptsAShaderWrittenAgainstTheMeshInterface)
{
    MaterialShaderCatalog catalog;
    const MaterialShaderId id =
        catalog.Register(Desc("forward", kMeshForwardFragSpv, kMeshForwardFragSpvWordCount));
    EXPECT_TRUE(id.IsValid());
    EXPECT_EQ(catalog.Find("forward"), id);
    EXPECT_TRUE(catalog.Refusals().empty());
}

// A vertex shader reads the mesh attributes, which the vertex stage never outputs.
TEST(MaterialShaderCatalog, RefusesAShaderReadingInputsTheVertexStageDoesNotWrite)
{
    MaterialShaderCatalog catalog;
    EXPECT_FALSE(catalog.Register(Desc("vertex", kMeshForwardVertSpv, kMeshForwardVertSpvWordCount)).IsValid());
    ASSERT_EQ(catalog.Refusals().size(), 1u);
    EXPECT_EQ(catalog.Refusals()[0],
              "material shader 'vertex' refused: it reads an input the mesh vertex stage does not write");
}

TEST(MaterialShaderCatalog, RefusesWordsThatAreNotSpirv)
{
    MaterialShaderCatalog catalog;
    const std::array<std::uint32_t, 6> words{ 1, 2, 3, 4, 5, 6 };
    EXPECT_FALSE(catalog.Register(Desc("noise", words.data(), words.size())).IsValid());
    ASSERT_EQ(catalog.Refusals().size(), 1u);
    EXPECT_EQ(catalog.Refusals()[0], "material shader 'noise' refused: it is not SPIR-V");
}

TEST(MaterialShaderCatalog, AReplacedCatalogIsToldApartFromTheOneItReplaced)
{
    MaterialShaderCatalog catalog;
    const std::uint64_t first = catalog.Stamp();
    catalog = MaterialShaderCatalog{};
    EXPECT_NE(catalog.Stamp(), first);
}
