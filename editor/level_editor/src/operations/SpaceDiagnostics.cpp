#include "SpaceDiagnostics.h"

#include "SpaceRelations.h"

#include "document/EditorScene.h"
#include "document/SpaceComponents.h"

#include <world/registry/Registry.h>

#include <string>

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
}

std::vector<ContentRiskRecord> SpaceDiagnostics(const EditorScene& scene, const std::optional<OpeningClearance>& clearance)
{
    std::vector<ContentRiskRecord> records;
    const World& components = scene.GetRegistry().Components;
    for (const EntityId entity : scene.GetAllEntities())
    {
        if (components.HasComponent<Shell>(entity) && scene.RoleOf(entity) == BrushRole::Volume)
        {
            (void)scene.TryGetBrushPieces(entity);
            const std::span<const PersistentEntityId> openings = scene.ShellOpenings(entity);
            for (const ShellProblem& problem : scene.ShellProblems(entity))
            {
                const Rule rule = RuleFor(problem.Kind);
                if (problem.Kind == ShellProblemKind::OpeningRefused && problem.Element < openings.size())
                    records.push_back({ rule.Severity, ContentRiskSourceKind::Entity, openings[problem.Element].Value,
                                        rule.Id, problem.Message });
                else
                    records.push_back(About(scene, entity, rule, problem.Message));
            }
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
