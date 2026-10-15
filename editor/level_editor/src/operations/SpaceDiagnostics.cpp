#include "SpaceDiagnostics.h"

#include "SpaceRelations.h"

#include "document/EditorScene.h"
#include "document/EntityNameComponent.h"
#include "document/SpaceComponents.h"
#include "document/SpaceWalls.h"

#include <world/registry/Registry.h>

#include <string>
#include <string_view>
#include <unordered_map>

namespace
{
    struct Rule
    {
        const char*         Id;
        ContentRiskSeverity Severity;
    };

    Rule RuleFor(ShellProblemKind kind)
    {
        switch (kind)
        {
        case ShellProblemKind::OpenVolume:
        case ShellProblemKind::NonPlanarFace:
        case ShellProblemKind::InvertedSlab:
            return { "shell.unsupported", ContentRiskSeverity::Error };
        case ShellProblemKind::SpaceOverlap:
            return { "space.overlap", ContentRiskSeverity::Error };
        case ShellProblemKind::VertexJoin:
            return { "shell.vertex_join", ContentRiskSeverity::Warning };
        case ShellProblemKind::SharedWallNotConvex:
            return { "shell.shared_not_convex", ContentRiskSeverity::Warning };
        case ShellProblemKind::WallsOverlap:
            return { "shell.overlap", ContentRiskSeverity::Warning };
        case ShellProblemKind::OpeningRefused:
            return { "opening.refused", ContentRiskSeverity::Error };
        }
        return { "shell.unsupported", ContentRiskSeverity::Error };
    }

    ContentRiskRecord About(const EditorScene& scene, EntityId entity, Rule rule, std::string message)
    {
        return { rule.Severity, ContentRiskSourceKind::Entity, scene.PersistentIdOf(entity).Value, rule.Id,
                 std::move(message) };
    }

    // Walls that are not what the space as it stands calls for: none at all
    // for a level saved before its space was walled, and a kept wall the
    // space has changed under since.
    void WallDiagnostics(const EditorScene& scene, EntityId space, const SpaceShell& shell,
                         std::vector<ContentRiskRecord>& records)
    {
        const SpaceWallDrift drift = CompareSpaceWalls(scene, space, shell);
        for (const EntityId wall : drift.KeptBehind)
            records.push_back(About(scene, wall, { "wall.kept_stale", ContentRiskSeverity::Warning },
                                    "reshaped by hand and kept, and its space has changed since; "
                                    "shell.rebuild takes it back"));
        if (drift.Stale)
            records.push_back(About(scene, space, { "shell.stale", ContentRiskSeverity::Warning },
                                    "its walls are not what it calls for; the next edit to the level, or "
                                    "shell.rebuild, builds them again"));
    }
}

std::vector<ContentRiskRecord> SpaceDiagnostics(const EditorScene& scene, const std::optional<OpeningClearance>& clearance)
{
    std::vector<ContentRiskRecord> records;
    const World& components = scene.GetRegistry().Components;
    // Operations keep a space's name its own; an inspector edit or a hand-edited file may not.
    std::unordered_map<std::string_view, std::size_t> named;
    for (const EntityId entity : scene.GetAllEntities())
        if (const auto* name = components.TryGet<EntityNameComponent>(entity))
            ++named[name->Value.View()];
    for (const EntityId entity : scene.GetAllEntities())
        if (const auto* name = components.TryGet<EntityNameComponent>(entity);
            name != nullptr && components.HasComponent<Space>(entity) && named[name->Value.View()] > 1)
            records.push_back(About(scene, entity, { "space.name_shared", ContentRiskSeverity::Warning },
                                    "another entity is also called '" + std::string(name->Value.View())
                                        + "', so @" + std::string(name->Value.View()) + " names neither"));
    for (const EntityId entity : scene.GetAllEntities())
    {
        if (const std::optional<SpaceShell> shell = BuildSpaceShell(scene, entity))
        {
            for (const ShellProblem& problem : shell->Result.Problems)
            {
                const Rule rule = RuleFor(problem.Kind);
                if (problem.Kind == ShellProblemKind::OpeningRefused && problem.Element < shell->Openings.size())
                    records.push_back({ rule.Severity, ContentRiskSourceKind::Entity,
                                        shell->Openings[problem.Element].Value, rule.Id, problem.Message });
                else
                    records.push_back(About(scene, entity, rule, problem.Message));
            }
            WallDiagnostics(scene, entity, *shell, records);
        }

        const Opening* opening = components.TryGet<Opening>(entity);
        if (opening == nullptr)
            continue;
        const std::optional<EntityId> owner = OwnerOf(scene, entity);
        if (!owner)
            records.push_back(About(scene, entity, { "opening.no_wall", ContentRiskSeverity::Warning },
                                    "it stands in no space's generated wall"));
        else if (!FarSideOf(scene, *owner, entity))
            records.push_back(About(scene, entity, { "opening.joins_one_space", ContentRiskSeverity::Warning },
                                    "no space lies on its far side"));
        if (clearance && opening->Width < 2.0f * clearance->Radius)
            records.push_back(About(scene, entity, { "opening.too_narrow", ContentRiskSeverity::Warning },
                                    "narrower than the navigation profile's capsule"));
        if (clearance && opening->Height < clearance->Height)
            records.push_back(About(scene, entity, { "opening.too_low", ContentRiskSeverity::Warning },
                                    "lower than the navigation profile's capsule"));
    }
    return records;
}
