#include <debug/MovementStatePanel.h>

#include <assets/data/DataAssetCache.h>
#include <attributes/AttributeSet.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementDefs.h>
#include <movement/MovementProfileBindingCache.h>
#include <movement/MovementProfileRuntime.h>

#include <imgui.h>

#include <cmath>
#include <utility>
#include <vector>

namespace
{
    constexpr float kFallbackMoveSpeed = 7.0f;

    const char* SupportText(SupportKind kind)
    {
        switch (kind)
        {
        case SupportKind::None: return "in the air";
        case SupportKind::Stable: return "on stable ground";
        case SupportKind::Steep: return "on steep ground";
        }
        return "unknown";
    }

    float ReadMoveSpeed(const World& world, EntityId entity)
    {
        if (!world.IsRegistered<AttributeSet>())
            return kFallbackMoveSpeed;
        const AttributeSet* attributes = world.TryGet<AttributeSet>(entity);
        const MovementDefs* defs = world.TryGetResource<MovementDefs>();
        if (attributes == nullptr || defs == nullptr || !defs->MoveSpeed.IsValid())
            return kFallbackMoveSpeed;
        return attributes->GetCurrent(defs->MoveSpeed, kFallbackMoveSpeed);
    }

    // Assembled the same way the tuning resolution system does. The shared part
    // is ResolveMovementTuning itself; only the read of each fact differs,
    // because a panel looks at one entity and the system walks chunks.
    MovementResolveContext MakeContext(const World& world,
                                       EntityId entity,
                                       const CharacterMovement& movement)
    {
        MovementResolveContext context;
        context.Mode = movement.Mode;
        if (const SupportState* support = world.TryGet<SupportState>(entity))
            context.Support = support->Kind;
        if (const Immersion* immersion = world.TryGet<Immersion>(entity))
            context.Immersion = immersion->Fraction;
        context.Tags = world.TryGet<GameplayTagContainer>(entity);
        return context;
    }
}

MovementStatePanel::MovementStatePanel(World& world, const DataAssetCache* dataAssets)
    : WorldState(world)
    , DataAssets(dataAssets)
{
}

void MovementStatePanel::Draw()
{
    if (!ImGui::Begin("Movement"))
    {
        ImGui::End();
        return;
    }

    if (!WorldState.IsRegistered<CharacterMovement>())
    {
        ImGui::TextDisabled("This world has no movement characters.");
        ImGui::End();
        return;
    }

    DrawSelector();
    if (!WorldState.IsAlive(Selected))
    {
        ImGui::TextDisabled("Select a character.");
        ImGui::End();
        return;
    }

    DrawFacts(Selected);
    DrawReloadStatus();
    DrawResolvedTuning(Selected);
    ImGui::End();
}

void MovementStatePanel::DrawSelector()
{
    if (QueryWorld != &WorldState)
    {
        Characters.emplace(WorldState);
        QueryWorld = &WorldState;
    }

    std::vector<EntityId> entities;
    Characters->ForEachChunk([&entities](auto& view)
    {
        for (std::uint32_t i = 0; i < view.Count(); ++i)
            entities.push_back(view.Entity(i));
    });

    if (entities.empty())
    {
        ImGui::TextDisabled("No characters are spawned.");
        Selected = EntityId{};
        return;
    }

    // Identity is generational, so a stale selection is dropped rather than
    // silently following whatever now occupies the index.
    if (!WorldState.IsAlive(Selected))
        Selected = entities.front();

    if (entities.size() > 1)
    {
        const std::string current = std::to_string(Selected.Index);
        if (ImGui::BeginCombo("Character", current.c_str()))
        {
            for (const EntityId entity : entities)
            {
                const std::string label = std::to_string(entity.Index);
                if (ImGui::Selectable(label.c_str(), entity == Selected))
                    Selected = entity;
            }
            ImGui::EndCombo();
        }
    }
}

void MovementStatePanel::DrawFacts(EntityId entity)
{
    const World& world = std::as_const(WorldState);

    if (const SupportState* support = world.TryGet<SupportState>(entity))
        ImGui::Text("Support: %s", SupportText(support->Kind));
    if (const Immersion* immersion = world.TryGet<Immersion>(entity))
    {
        if (immersion->Fraction > 0.0f)
            ImGui::Text("Immersion: %.0f%%", immersion->Fraction * 100.0f);
    }

    if (const CharacterMovement* movement = world.TryGet<CharacterMovement>(entity))
    {
        if (const auto* modes = world.TryGetResource<LocomotionModeRegistry>())
        {
            if (const LocomotionModeEntry* mode = modes->Find(movement->Mode))
                ImGui::Text("Mode: %s", mode->Name.c_str());
        }
    }

    if (const KinematicState* kinematic = world.TryGet<KinematicState>(entity))
    {
        const Vec3d up(0.0f, 1.0f, 0.0f);
        const float vertical = kinematic->Velocity.Dot(up);
        const Vec3d planar = kinematic->Velocity - up * vertical;
        const float speed = planar.Magnitude();
        ImGui::Text("Speed: %.2f m/s planar, %.2f m/s vertical", speed, vertical);

        PlanarSpeed[SampleCursor] = speed;
        VerticalSpeed[SampleCursor] = vertical;
        SampleCursor = (SampleCursor + 1) % PlanarSpeed.size();

        ImGui::PlotLines("##planar", PlanarSpeed.data(),
                         static_cast<int>(PlanarSpeed.size()), static_cast<int>(SampleCursor),
                         "planar speed", 0.0f, FLT_MAX, ImVec2(-1.0f, 50.0f));
        ImGui::SetItemTooltip("Sampled once per drawn frame, not per fixed tick.");
    }

    if (const GameplayTagContainer* tags = world.TryGet<GameplayTagContainer>(entity))
    {
        if (const auto* registry = world.TryGetResource<GameplayTagRegistry>())
        {
            std::string held;
            for (std::uint8_t i = 0; i < tags->Count; ++i)
            {
                const std::string_view name = registry->GetName(tags->Tags[i]);
                if (!name.starts_with("movement."))
                    continue;
                if (!held.empty())
                    held += ", ";
                held += name;
            }
            ImGui::TextWrapped("Tags: %s", held.empty() ? "none" : held.c_str());
        }
    }
}

void MovementStatePanel::DrawReloadStatus()
{
    const MovementTuningSource* source =
        std::as_const(WorldState).TryGet<MovementTuningSource>(Selected);
    if (source == nullptr || DataAssets == nullptr)
        return;

    ImGui::Separator();
    if (!source->Profile.IsValid())
    {
        ImGui::TextDisabled("No movement profile: defaults plus the MoveSpeed attribute.");
        return;
    }

    ImGui::Text("Profile: %.*s",
                static_cast<int>(DataAssets->GetName(source->Profile.Value).size()),
                DataAssets->GetName(source->Profile.Value).data());

    const uint64_t version = DataAssets->GetReloadVersion(source->Profile.Value);
    if (version != LastReloadVersion)
    {
        // Counted rather than shown raw: what a designer wants to know is that
        // the save they just made reached the running game.
        if (LastReloadVersion != 0)
            ++ReloadsSeen;
        LastReloadVersion = version;
    }
    ImGui::Text("Hot reloads seen: %u", ReloadsSeen);

    if (const auto* bindings = WorldState.TryGetResource<MovementProfileBindingCache>())
        ImGui::Text("Bindings rebuilt: %llu", static_cast<unsigned long long>(bindings->RebuildCount()));

    if (!LastBindError.empty())
        ImGui::TextColored(ImVec4(0.93f, 0.62f, 0.15f, 1.0f), "%s", LastBindError.c_str());
}

void MovementStatePanel::DrawResolvedTuning(EntityId entity)
{
    const World& world = std::as_const(WorldState);
    const CharacterMovement* movement = world.TryGet<CharacterMovement>(entity);
    if (movement == nullptr)
        return;

    ImGui::Separator();
    if (const ResolvedMovementTuning* tuning = world.TryGet<ResolvedMovementTuning>(entity))
    {
        if (ImGui::BeginTable("tuning", 2, ImGuiTableFlags_RowBg))
        {
            for (const MovementTuningField& field : MovementTuningFields())
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(std::string(field.Key).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", tuning->*field.Resolved);
            }
            ImGui::EndTable();
        }
    }

    auto* bindings = WorldState.TryGetResource<MovementProfileBindingCache>();
    if (bindings == nullptr)
        return;

    const MovementTuningSource* source = world.TryGet<MovementTuningSource>(entity);

    std::string error;
    const BoundMovementProfile* profile = bindings->Get(
        source != nullptr ? source->Profile : MovementProfileHandle{}, &error);
    if (!error.empty())
        LastBindError = error;
    if (profile == nullptr)
        return;

    // Collected here and only here: the fixed tick keeps resolving without a
    // trace, so an open panel costs one extra resolve for one entity.
    const MovementResolveResult resolved = ResolveMovementTuning(
        *profile, MakeContext(world, entity, *movement),
        ReadMoveSpeed(world, entity), /*collectTrace*/ true);

    ImGui::Separator();
    ImGui::TextUnformatted("Rules");
    for (std::size_t index = 0; index < resolved.Trace.size(); ++index)
    {
        const MovementLayerTrace& layer = resolved.Trace[index];
        const std::string name = layer.Name.empty()
            ? layer.SourcePath : layer.Name;
        if (layer.Matched)
        {
            ImGui::BulletText("%s", name.c_str());
        }
        else
        {
            ImGui::TextDisabled("   %s (%s)", name.c_str(),
                                layer.Failure.empty() ? "off" : layer.Failure.c_str());
        }
    }
}
