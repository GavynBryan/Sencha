#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The sources a composition is resolving, outermost first: the one cycle and
// depth policy every walker over extends, includes and placements shares.
class CompositionChain
{
public:
    static constexpr std::size_t MaxDepth = 16;

    // Enters `source`, or says why it cannot: it is already being resolved, or
    // the chain would be deeper than MaxDepth.
    [[nodiscard]] std::optional<std::string> Enter(std::string_view source);
    void Leave();

    [[nodiscard]] std::size_t Depth() const { return Sources.size(); }

private:
    std::vector<std::string> Sources;
};
