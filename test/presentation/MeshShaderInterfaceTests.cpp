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

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <utility>

namespace
{
    struct ShaderInterface
    {
        std::set<std::uint32_t> Inputs;
        std::set<std::uint32_t> Outputs;
        bool InvariantPosition = false;
    };

    ShaderInterface ReadInterface(const std::uint32_t* words, std::size_t count)
    {
        constexpr std::uint32_t kOpVariable = 59;
        constexpr std::uint32_t kOpDecorate = 71;
        constexpr std::uint32_t kOpMemberDecorate = 72;
        constexpr std::uint32_t kStorageInput = 1;
        constexpr std::uint32_t kStorageOutput = 3;
        constexpr std::uint32_t kDecorationInvariant = 18;
        constexpr std::uint32_t kDecorationBuiltIn = 11;
        constexpr std::uint32_t kDecorationLocation = 30;
        constexpr std::uint32_t kBuiltInPosition = 0;

        std::map<std::uint32_t, std::uint32_t> locations;
        std::set<std::pair<std::uint32_t, std::uint32_t>> positions;
        std::set<std::pair<std::uint32_t, std::uint32_t>> invariants;
        std::map<std::uint32_t, std::uint32_t> storage;

        // Past the five word header, each instruction leads with its length and opcode.
        for (std::size_t at = 5; at < count; at += words[at] >> 16)
        {
            const std::uint32_t op = words[at] & 0xFFFFu;
            if (op == kOpDecorate && words[at + 2] == kDecorationLocation)
                locations[words[at + 1]] = words[at + 3];
            else if (op == kOpMemberDecorate && words[at + 3] == kDecorationInvariant)
                invariants.insert({ words[at + 1], words[at + 2] });
            else if (op == kOpMemberDecorate && words[at + 3] == kDecorationBuiltIn
                     && words[at + 4] == kBuiltInPosition)
                positions.insert({ words[at + 1], words[at + 2] });
            else if (op == kOpVariable)
                storage[words[at + 2]] = words[at + 3];
        }

        ShaderInterface result;
        for (const auto& [id, location] : locations)
        {
            const auto found = storage.find(id);
            if (found != storage.end() && found->second == kStorageInput)
                result.Inputs.insert(location);
            else if (found != storage.end() && found->second == kStorageOutput)
                result.Outputs.insert(location);
        }
        for (const auto& position : positions)
            result.InvariantPosition |= invariants.contains(position);
        return result;
    }
}

TEST(MeshShaderInterface, ForwardVertexWritesOnlyWhatItsFragmentReads)
{
    const ShaderInterface vertex = ReadInterface(kMeshForwardVertSpv, kMeshForwardVertSpvWordCount);
    const ShaderInterface fragment = ReadInterface(kMeshForwardFragSpv, kMeshForwardFragSpvWordCount);
    EXPECT_FALSE(vertex.Outputs.empty());
    EXPECT_EQ(vertex.Outputs, fragment.Inputs);
}

TEST(MeshShaderInterface, OutlineMaskVertexWritesOnlyWhatItsFragmentReads)
{
    const ShaderInterface vertex = ReadInterface(kOutlineMaskVertSpv, kOutlineMaskVertSpvWordCount);
    const ShaderInterface fragment = ReadInterface(kOutlineMaskFragSpv, kOutlineMaskFragSpvWordCount);
    EXPECT_FALSE(vertex.Outputs.empty());
    EXPECT_EQ(vertex.Outputs, fragment.Inputs);
}

TEST(MeshShaderInterface, ForwardAndOutlineMaskBothHoldThePositionInvariant)
{
    EXPECT_TRUE(ReadInterface(kMeshForwardVertSpv, kMeshForwardVertSpvWordCount).InvariantPosition);
    EXPECT_TRUE(ReadInterface(kOutlineMaskVertSpv, kOutlineMaskVertSpvWordCount).InvariantPosition);
}
