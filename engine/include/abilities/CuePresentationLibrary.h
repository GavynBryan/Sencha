#pragma once

#include <abilities/CuePresentationData.h>
#include <authored/VerbBinding.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

class World;
struct VerbBindingEnvironment;

// The cue presentations a World's content declares, compiled against its verbs
// and leases and kept by cue. Only cosmetic operations are kept: a presentation
// never changes what the simulation does.
class CuePresentationLibrary
{
public:
    struct Presentation
    {
        std::vector<CompiledVerbBinding> Once;
        std::vector<CompiledVerbBinding> Sustain;
    };

    // `path`'s presentations in place of what it declared before; a cue it names
    // that `world` does not, or an operation that is not cosmetic, is reported and left out.
    void Declare(std::string_view path, const CuePresentationDeclaration& declaration, const World& world,
                 std::vector<std::string>& errors);
    // The same, compiled against `environment`: content's declarations give it the
    // mounted assets, so an asset an operation names is checked to be there.
    void Declare(std::string_view path, const CuePresentationDeclaration& declaration,
                 const VerbBindingEnvironment& environment, std::vector<std::string>& errors);
    void Undeclare(std::string_view path);

    // Every asset's presentation of `cue`, in asset path order; null when none presents it.
    [[nodiscard]] const Presentation* Find(GameplayTagId cue) const;

private:
    void Rebuild();

    std::map<std::string, std::vector<std::pair<GameplayTagId, Presentation>>, std::less<>> ByPath;
    std::unordered_map<std::uint32_t, Presentation> ByCue;
};
