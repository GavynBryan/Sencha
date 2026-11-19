#include <core/composition/CompositionChain.h>

#include <algorithm>
#include <format>

std::optional<std::string> CompositionChain::Enter(std::string_view source)
{
    if (const auto first = std::ranges::find(Sources, source); first != Sources.end())
    {
        std::string through;
        for (auto at = first; at != Sources.end(); ++at)
            through += *at + " -> ";
        return std::format("source cycle: '{}' composes from itself through {}{}", source, through, source);
    }
    if (Sources.size() >= MaxDepth)
        return std::format("composition is deeper than {} sources at '{}'", MaxDepth, source);
    Sources.emplace_back(source);
    return std::nullopt;
}

void CompositionChain::Leave()
{
    Sources.pop_back();
}
