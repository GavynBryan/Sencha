#pragma once

#include <core/metadata/TypeSchema.h>
#include <ecs/EntityId.h>
#include <net/ReplicationImage.h>
#include <net/ReplicationLayout.h>
#include <net/ReplicationWireContext.h>

#include <cstdint>
#include <span>
#include <string_view>
#include <tuple>
#include <vector>

inline constexpr std::size_t kRosterMost = 32;
inline constexpr std::size_t kRosterHolds = kRosterMost + 1;

struct RosterEntry
{
    std::uint32_t Value = 0;
    std::uint32_t Secret = 0;
    EntityId Target;
};

struct Roster
{
    std::uint32_t Label = 0;
    std::uint32_t Count = 0;
    RosterEntry Entries[kRosterHolds] = {};
};

struct RosterHeadWire
{
    std::uint32_t Label = 0;
};

struct RosterEntryWire
{
    std::uint32_t Value = 0;
    std::uint32_t Secret = 0;
    std::uint64_t Target = 0;
};

struct LedgerLineWire
{
    std::uint32_t Amount = 0;
};

struct Ledger
{
    std::uint32_t Count = 0;
    std::uint32_t Amounts[8] = {};
};

struct HugeElementWire
{
    std::uint64_t A = 0;
    std::uint64_t B = 0;
    std::uint64_t C = 0;
    std::uint64_t D = 0;
};

struct Huge
{
    std::uint32_t Count = 0;
};

template <>
struct TypeSchema<Roster>
{
    static constexpr std::string_view Name = "test.Roster";
    static auto Fields() { return std::tuple{ MakeField("label", &Roster::Label), MakeField("count", &Roster::Count) }; }
};

template <>
struct TypeSchema<RosterHeadWire>
{
    static constexpr std::string_view Name = "test.RosterHeadWire";
    static auto Fields() { return std::tuple{ MakeField("label", &RosterHeadWire::Label) }; }
};

template <>
struct TypeSchema<RosterEntryWire>
{
    static constexpr std::string_view Name = "test.RosterEntryWire";
    static auto Fields()
    {
        return std::tuple{ MakeField("value", &RosterEntryWire::Value),
                           MakeField("secret", &RosterEntryWire::Secret).OwnerOnly(),
                           MakeField("target", &RosterEntryWire::Target) };
    }
};

template <>
struct TypeSchema<Ledger>
{
    static constexpr std::string_view Name = "test.Ledger";
    static auto Fields() { return std::tuple{ MakeField("count", &Ledger::Count) }; }
};

template <>
struct TypeSchema<LedgerLineWire>
{
    static constexpr std::string_view Name = "test.LedgerLineWire";
    static auto Fields() { return std::tuple{ MakeField("amount", &LedgerLineWire::Amount).OwnerOnly() }; }
};

template <>
struct TypeSchema<Huge>
{
    static constexpr std::string_view Name = "test.Huge";
    static auto Fields() { return std::tuple{ MakeField("count", &Huge::Count) }; }
};

template <>
struct TypeSchema<HugeElementWire>
{
    static constexpr std::string_view Name = "test.HugeElementWire";
    static auto Fields()
    {
        return std::tuple{ MakeField("a", &HugeElementWire::A), MakeField("b", &HugeElementWire::B),
                           MakeField("c", &HugeElementWire::C), MakeField("d", &HugeElementWire::D) };
    }
};

template <>
struct ReplicationCodec<Roster>
{
    using Wire = RosterHeadWire;
    using Lists = std::tuple<ReplicatedListOf<RosterEntryWire, kRosterMost>>;
    static constexpr std::string_view ListNames[] = { "entries" };

    static void ToWire(const ReplicationWireContext& context, const Roster& local, ReplicationImage& wire)
    {
        wire.HeadAs<RosterHeadWire>().Label = local.Label;
        const std::span<RosterEntryWire> out = wire.Resize<RosterEntryWire>(0, local.Count);
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = RosterEntryWire{ .Value = local.Entries[i].Value,
                                      .Secret = local.Entries[i].Secret,
                                      .Target = context.WireEntity(local.Entries[i].Target) };
    }

    static void FromWire(const ReplicationWireContext& context, const ReplicationImage& wire, Roster& local)
    {
        local.Label = wire.HeadAs<RosterHeadWire>().Label;
        const std::span<const RosterEntryWire> in = wire.List<RosterEntryWire>(0);
        local.Count = static_cast<std::uint32_t>(in.size());
        for (std::size_t i = 0; i < in.size(); ++i)
            local.Entries[i] = RosterEntry{ .Value = in[i].Value,
                                            .Secret = in[i].Secret,
                                            .Target = context.LocalEntity(in[i].Target) };
    }
};

template <>
struct ReplicationCodec<Ledger>
{
    using Wire = ReplicationNoHead;
    using Lists = std::tuple<ReplicatedListOf<LedgerLineWire, 8>>;
    static constexpr std::string_view ListNames[] = { "lines" };

    static void ToWire(const ReplicationWireContext&, const Ledger& local, ReplicationImage& wire)
    {
        const std::span<LedgerLineWire> out = wire.Resize<LedgerLineWire>(0, local.Count);
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i].Amount = local.Amounts[i];
    }

    static void FromWire(const ReplicationWireContext&, const ReplicationImage& wire, Ledger& local)
    {
        const std::span<const LedgerLineWire> in = wire.List<LedgerLineWire>(0);
        local.Count = static_cast<std::uint32_t>(in.size());
        for (std::size_t i = 0; i < in.size(); ++i)
            local.Amounts[i] = in[i].Amount;
    }
};

template <>
struct ReplicationCodec<Huge>
{
    using Wire = ReplicationNoHead;
    using Lists = std::tuple<ReplicatedListOf<HugeElementWire, 200>>;
    static constexpr std::string_view ListNames[] = { "elements" };
    static void ToWire(const ReplicationWireContext&, const Huge&, ReplicationImage&) {}
    static void FromWire(const ReplicationWireContext&, const ReplicationImage&, Huge&) {}
};

inline std::vector<std::uint64_t> EveryElementRun(const ReplicatedComponent& component, const ReplicationImage& image,
                                                  std::uint64_t fields)
{
    std::vector<std::uint64_t> elements;
    for (std::size_t list = 0; list < component.Lists.size(); ++list)
    {
        if ((fields & (std::uint64_t{ 1 } << (component.Fields.size() + list))) == 0)
            continue;
        const std::uint64_t runs = (std::uint64_t{ 1 } << component.Lists[list].Fields.size()) - 1;
        elements.insert(elements.end(), image.Count(list), runs);
    }
    return elements;
}
