#pragma once

#include <core/identity/Id.h>
#include <core/json/JsonValue.h>
#include <physics/SensorOverlaps.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// One zone's named lists, exactly as a save file holds them: docs/gameplay/saves.md, "Levels".

// Self-contained, immutable, shared between the zone store, a save being written and a save just read.
using EncodedZoneLists = std::shared_ptr<const std::vector<std::byte>>;

struct ZoneStateList
{
    std::string Name;
    std::uint32_t Version = 1;
    std::vector<std::byte> Payload;

    friend bool operator==(const ZoneStateList&, const ZoneStateList&) = default;
};

inline constexpr std::string_view kDestroyedZoneList = "destroyed";
inline constexpr std::string_view kChangedZoneList = "changed";
inline constexpr std::string_view kHeldZoneList = "held";
inline constexpr std::string_view kGainedZoneList = "gained";
inline constexpr std::string_view kOccupiedZoneList = "occupied";
// The zone's entities a level change carried away, gone from it for good: kept across ForgetLevel and every lifetime.
inline constexpr std::string_view kDepartedZoneList = "departed";
// The newest version of each known list this build reads, and the one it writes.
inline constexpr std::uint32_t kZoneListVersion = 1;

[[nodiscard]] bool IsKnownZoneList(std::string_view name);

[[nodiscard]] EncodedZoneLists EncodeZoneLists(std::span<const ZoneStateList> lists);
[[nodiscard]] std::optional<std::vector<ZoneStateList>> DecodeZoneLists(std::span<const std::byte> bytes);

// `destroyed` and `departed`: each id.
[[nodiscard]] std::vector<std::byte> EncodeIdList(std::span<const PersistentEntityId> ids);
[[nodiscard]] std::optional<std::vector<PersistentEntityId>> DecodeIdList(std::span<const std::byte> payload);

struct ZoneListEntity
{
    PersistentEntityId Id;
    JsonValue Value;
};

// `changed`: each entity's patch. `held`: each record's components.
[[nodiscard]] std::vector<std::byte> EncodeEntityList(std::span<const ZoneListEntity> entities);
[[nodiscard]] std::optional<std::vector<ZoneListEntity>> DecodeEntityList(std::span<const std::byte> payload);

// `occupied`: each sensor of the zone and what was inside it.
[[nodiscard]] std::vector<std::byte> EncodeOccupiedList(std::span<const SensorSeed> pairs);
[[nodiscard]] std::optional<std::vector<SensorSeed>> DecodeOccupiedList(std::span<const std::byte> payload);

struct GainedZoneMember
{
    std::string Form;
    std::vector<std::byte> Payload;

    friend bool operator==(const GainedZoneMember&, const GainedZoneMember&) = default;
};

[[nodiscard]] std::vector<std::byte> EncodeGainedList(std::span<const GainedZoneMember> members);
[[nodiscard]] std::optional<std::vector<GainedZoneMember>> DecodeGainedList(std::span<const std::byte> payload);

// Each form's payload is the member's runtime persistent id, then JSON text: its entity record for `whole`,
// its source, root transform and patch against the source's root for `from_source`.
inline constexpr std::string_view kWholeGainedForm = "whole";
inline constexpr std::string_view kSourceGainedForm = "from_source";

[[nodiscard]] bool IsMadeGainedForm(std::string_view form);

[[nodiscard]] std::vector<std::byte> EncodeGainedMember(const ZoneListEntity& entity);
[[nodiscard]] std::optional<ZoneListEntity> DecodeGainedMember(std::span<const std::byte> payload);
[[nodiscard]] std::optional<PersistentEntityId> GainedMemberId(std::span<const std::byte> payload);
