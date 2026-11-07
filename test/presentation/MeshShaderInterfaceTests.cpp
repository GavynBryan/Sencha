// What the mesh shaders hand each other, read from the SPIR-V that ships.
//
// A vertex output no fragment shader reads is legal, so nothing fails when one
// appears: the work is just thrown away on every vertex. And the outline mask
// only lines up with the scene if both passes compute one invariant position,
// which no image shows until a driver happens to round differently.

#include <gtest/gtest.h>

#include <shaders/kMeshForwardFragSpv.h>
#include <shaders/kMeshForwardVertSpv.h>
#include <shaders/kOutlineMaskFragSpv.h>
#include <shaders/kOutlineMaskVertSpv.h>

#include <graphics/SpirvInterface.h>

namespace
{
    SpirvInterface ReadInterface(const std::uint32_t* words, std::size_t count)
    {
        return ReadSpirvInterface({ words, count });
    }
}

TEST(MeshShaderInterface, ForwardVertexWritesOnlyWhatItsFragmentReads)
{
    const SpirvInterface vertex = ReadInterface(kMeshForwardVertSpv, kMeshForwardVertSpvWordCount);
    const SpirvInterface fragment = ReadInterface(kMeshForwardFragSpv, kMeshForwardFragSpvWordCount);
    EXPECT_FALSE(vertex.Outputs.empty());
    EXPECT_EQ(vertex.Outputs, fragment.Inputs);
}

TEST(MeshShaderInterface, OutlineMaskVertexWritesOnlyWhatItsFragmentReads)
{
    const SpirvInterface vertex = ReadInterface(kOutlineMaskVertSpv, kOutlineMaskVertSpvWordCount);
    const SpirvInterface fragment = ReadInterface(kOutlineMaskFragSpv, kOutlineMaskFragSpvWordCount);
    EXPECT_FALSE(vertex.Outputs.empty());
    EXPECT_EQ(vertex.Outputs, fragment.Inputs);
}

TEST(MeshShaderInterface, ForwardAndOutlineMaskBothHoldThePositionInvariant)
{
    EXPECT_TRUE(ReadInterface(kMeshForwardVertSpv, kMeshForwardVertSpvWordCount).InvariantPosition);
    EXPECT_TRUE(ReadInterface(kOutlineMaskVertSpv, kOutlineMaskVertSpvWordCount).InvariantPosition);
}
