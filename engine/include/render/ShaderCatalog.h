#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

template <typename Id, typename Type>
    requires requires(Type type, Id id) {
        type.Name;
        type.FragmentSpirv;
        id.Value;
    }
class ShaderCatalog
{
public:
    [[nodiscard]] Id Find(std::string_view name) const
    {
        const auto found = std::ranges::find_if(Types, [&](const Type& type) { return type.Name == name; });
        return found != Types.end() ? Id{ static_cast<std::uint32_t>(found - Types.begin()) + 1 } : Id{};
    }

    [[nodiscard]] const Type* Get(Id id) const
    {
        return id.IsValid() && id.Value <= Types.size() ? &Types[id.Value - 1] : nullptr;
    }

    [[nodiscard]] std::size_t Count() const { return Types.size(); }
    [[nodiscard]] std::span<const std::string> Refusals() const { return Refusals_; }

protected:
    Id Add(std::string_view kind, Type type, std::string_view conflict = {})
    {
        const auto refuse = [&](std::string_view why) {
            Refusals_.push_back(std::string(kind) + " '" + type.Name + "' refused: " + std::string(why));
            return Id{};
        };
        if (type.Name.empty())
            return refuse("it has no name");
        if (Find(type.Name).IsValid())
            return refuse("the name is taken");
        if (!conflict.empty())
            return refuse(conflict);
        Types.push_back(std::move(type));
        return Id{ static_cast<std::uint32_t>(Types.size()) };
    }

    [[nodiscard]] std::span<const Type> All() const { return Types; }

private:
    std::vector<Type> Types;
    std::vector<std::string> Refusals_;
};
