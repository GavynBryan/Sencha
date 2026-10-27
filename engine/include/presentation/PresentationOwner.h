#pragma once

#include <ecs/LeaseOwner.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class World;

// One system or verb that makes cosmetic changes. Numbered per World, never
// reused, never persisted: a writer names itself for diagnostics only.
struct PresentationWriterId
{
    std::uint32_t Value = 0;

    [[nodiscard]] bool IsValid() const { return Value != 0; }
    friend bool operator==(PresentationWriterId, PresentationWriterId) = default;
    friend auto operator<=>(PresentationWriterId, PresentationWriterId) = default;
};

// Who holds a cosmetic change. Compared whole, so two writers choosing the same
// holder key never collide and nothing is hashed or truncated.
struct PresentationOwner
{
    PresentationWriterId Writer;
    LeaseOwner Holder;

    friend bool operator==(const PresentationOwner&, const PresentationOwner&) = default;
};

// Deterministic order between owners, for breaking a tie the same way on every machine.
[[nodiscard]] bool PresentationOwnerPrecedes(const PresentationOwner& a, const PresentationOwner& b);

// A World resource.
class PresentationWriters
{
public:
    [[nodiscard]] PresentationWriterId Register(std::string_view debugName);
    [[nodiscard]] std::string_view NameOf(PresentationWriterId writer) const;

private:
    std::vector<std::string> Names;
};

[[nodiscard]] PresentationWriterId RegisterPresentationWriter(World& world, std::string_view debugName);
