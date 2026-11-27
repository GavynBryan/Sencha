#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The sources a composition is resolving, outermost first (docs/assets/composition.md, "Chains of sources").
class CompositionChain
{
public:
    static constexpr std::size_t MaxDepth = 16;

    [[nodiscard]] std::optional<std::string> Enter(std::string_view source);
    void Leave();

    // Why `source`, whose own chain is `height` sources deep, cannot go beneath this one.
    [[nodiscard]] std::optional<std::string> RefusesBeneath(std::string_view source, std::size_t height) const;
    [[nodiscard]] std::size_t Depth() const { return Sources.size(); }

private:
    std::vector<std::string> Sources;
};
