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
    if (std::optional<std::string> refusal = RefusesBeneath(source, 1))
        return refusal;
    Sources.emplace_back(source);
    return std::nullopt;
}

std::optional<std::string> CompositionChain::RefusesBeneath(std::string_view source, std::size_t height) const
{
    if (Sources.size() + height <= MaxDepth)
        return std::nullopt;
    return std::format("composition is deeper than {} sources at '{}'", MaxDepth, source);
}

void CompositionChain::Leave()
{
    Sources.pop_back();
}
