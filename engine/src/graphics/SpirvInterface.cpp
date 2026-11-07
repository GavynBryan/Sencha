#include <graphics/SpirvInterface.h>

#include <algorithm>
#include <map>
#include <utility>

namespace
{
constexpr std::uint32_t kMagic = 0x07230203;
constexpr std::size_t kHeaderWords = 5;

constexpr std::uint32_t kOpTypePointer = 32;
constexpr std::uint32_t kOpVariable = 59;
constexpr std::uint32_t kOpDecorate = 71;
constexpr std::uint32_t kOpMemberDecorate = 72;

constexpr std::uint32_t kStorageInput = 1;
constexpr std::uint32_t kStorageOutput = 3;
constexpr std::uint32_t kStoragePushConstant = 9;

constexpr std::uint32_t kDecorationBuiltIn = 11;
constexpr std::uint32_t kDecorationInvariant = 18;
constexpr std::uint32_t kDecorationLocation = 30;
constexpr std::uint32_t kDecorationOffset = 35;
constexpr std::uint32_t kBuiltInPosition = 0;
} // namespace

SpirvInterface ReadSpirvInterface(std::span<const std::uint32_t> words)
{
    SpirvInterface result;
    if (words.size() < kHeaderWords || words[0] != kMagic)
        return result;

    std::map<std::uint32_t, std::uint32_t> locations;
    std::map<std::uint32_t, std::uint32_t> storage;
    std::map<std::uint32_t, std::uint32_t> variableTypes;
    std::map<std::uint32_t, std::uint32_t> pointees;
    std::map<std::uint32_t, std::uint32_t> lastMemberOffsets;
    std::set<std::pair<std::uint32_t, std::uint32_t>> positions;
    std::set<std::pair<std::uint32_t, std::uint32_t>> invariants;

    for (std::size_t at = kHeaderWords; at < words.size();)
    {
        const std::uint32_t length = words[at] >> 16;
        if (length == 0 || at + length > words.size())
            return result;
        const std::uint32_t op = words[at] & 0xFFFFu;
        const auto operand = [&](std::size_t index) { return index < length ? words[at + index] : ~0u; };

        if (op == kOpDecorate && operand(2) == kDecorationLocation)
            locations[operand(1)] = operand(3);
        else if (op == kOpMemberDecorate && operand(3) == kDecorationInvariant)
            invariants.insert({ operand(1), operand(2) });
        else if (op == kOpMemberDecorate && operand(3) == kDecorationBuiltIn && operand(4) == kBuiltInPosition)
            positions.insert({ operand(1), operand(2) });
        else if (op == kOpMemberDecorate && operand(3) == kDecorationOffset)
            lastMemberOffsets[operand(1)] = std::max(lastMemberOffsets[operand(1)], operand(4));
        else if (op == kOpTypePointer)
            pointees[operand(1)] = operand(3);
        else if (op == kOpVariable)
        {
            variableTypes[operand(2)] = operand(1);
            storage[operand(2)] = operand(3);
        }
        at += length;
    }

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
    for (const auto& [id, storageClass] : storage)
    {
        if (storageClass != kStoragePushConstant)
            continue;
        const auto block = lastMemberOffsets.find(pointees[variableTypes[id]]);
        if (block != lastMemberOffsets.end())
            result.LastPushMemberOffset = block->second;
    }
    result.Valid = true;
    return result;
}
