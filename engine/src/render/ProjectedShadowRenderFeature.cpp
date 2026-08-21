#include <render/ProjectedShadowRenderFeature.h>

#include <render/static_mesh/StaticMeshVertex.h>

#include <profiling/RenderInstrumentation.h>
#include <profiling/RenderStats.h>

ProjectedShadowRenderFeature::ProjectedShadowRenderFeature(
    ProjectedShadowSet& casters,
    const RenderLightSet& lights,
    const RenderQueue& queue,
    const CameraRenderData& camera,
    StaticMeshCache& meshes,
    const SkinnedMeshCache& skinnedMeshes,
    const ProjectedShadowBudgets& budgets,
    std::shared_ptr<ProjectedShadowFrameData> output)
    : Casters(&casters)
    , Lights(&lights)
    , Queue(&queue)
    , Camera(&camera)
    , Meshes(&meshes)
    , SkinnedMeshes(&skinnedMeshes)
    , Budgets(&budgets)
    , Output(std::move(output))
{
}

bool ProjectedShadowRenderFeature::Setup(const RendererServices& services)
{
    Instrumentation = services.Instrumentation;
    Silhouettes.Setup(services, sizeof(StaticMeshVertex));
    return true;
}

void ProjectedShadowRenderFeature::Teardown()
{
    Silhouettes.Teardown();
}

void ProjectedShadowRenderFeature::OnDraw(const FrameContext& frame)
{
    Output->Reset();
    RenderStats* stats = Instrumentation != nullptr ? Instrumentation->Stats : nullptr;
    if (stats != nullptr)
    {
        stats->ProjectedCastersGathered =
            static_cast<std::uint32_t>(Casters->Casters.size());
        stats->ProjectedCastersRendered = 0;
        stats->ProjectedCastersDropped = 0;
        stats->ProjectedReceiverDraws = 0;
    }
    if (!Lights->ProjectedShadowsEnabled || Casters->Casters.empty())
        return;

    const std::uint32_t dropped =
        RankAndClampProjectedCasters(*Casters, Camera->Position, Budgets->MaxCasters);
    if (stats != nullptr)
        stats->ProjectedCastersDropped = dropped;
    const ProjectedShadowTileGrid grid =
        MakeProjectedShadowTileGrid(Budgets->MaxCasters, Budgets->TilePixels);

    // Silhouette assembly: geometry resolved once, view-projections kept for
    // the projection uniforms below.
    CasterDraws.clear();
    SectionDraws.clear();
    CasterViewProjections.clear();
    for (const ProjectedShadowCaster& caster : Casters->Casters)
    {
        const GpuStaticMesh* mesh = SkinnedMeshes->Get(caster.Mesh);
        if (mesh == nullptr)
            continue;

        const Mat4 viewProjection =
            MakeProjectedShadowViewProjection(caster, Budgets->MaxDistance);

        ProjectedSilhouetteCasterDraw draw;
        draw.Mvp = viewProjection * caster.WorldMatrix;
        draw.FirstSection = static_cast<std::uint32_t>(SectionDraws.size());
        for (std::uint32_t section = 0; section < mesh->Sections.size(); ++section)
        {
            if ((caster.SectionMask & (1u << section)) == 0)
                continue;
            SectionDraws.push_back(ProjectedSilhouetteSectionDraw{
                .Vertex = mesh->VertexBuffer,
                .Index = mesh->IndexBuffer,
                .IndexCount = mesh->Sections[section].IndexCount,
                .IndexOffset = mesh->Sections[section].IndexOffset,
            });
        }
        draw.SectionCount =
            static_cast<std::uint32_t>(SectionDraws.size()) - draw.FirstSection;
        if (draw.SectionCount == 0)
            continue;
        CasterDraws.push_back(draw);
        CasterViewProjections.push_back(viewProjection);
    }
    if (CasterDraws.empty())
        return;

    ProjectedSilhouetteInput silhouettes;
    silhouettes.TilesPerRow = grid.TilesPerRow;
    silhouettes.TilePixels = grid.TilePixels;
    silhouettes.Casters = CasterDraws;
    silhouettes.Sections = SectionDraws;
    if (!Silhouettes.Draw(frame, silhouettes))
        return;
    const std::uint32_t atlasIndex = Silhouettes.AtlasBindlessIndex();

    // Projection assembly, fully baked for the game's one view: camera matrix
    // and scissor included, so the mesh feature only records.
    Output->VertexStride = sizeof(StaticMeshVertex);
    std::uint32_t tile = 0;
    std::uint32_t drawIndex = 0;
    for (const ProjectedShadowCaster& caster : Casters->Casters)
    {
        const GpuStaticMesh* mesh = SkinnedMeshes->Get(caster.Mesh);
        if (mesh == nullptr)
            continue;
        const Mat4& viewProjection = CasterViewProjections[drawIndex];
        ++drawIndex;

        const Aabb3d swept =
            ProjectedShadowSweptBounds(caster, Budgets->MaxDistance);
        const ProjectedShadowScreenRect rect = ComputeProjectedShadowScreenRect(
            swept, Camera->ViewProjection,
            frame.TargetExtent.width, frame.TargetExtent.height);
        const std::uint32_t thisTile = tile++;
        if (rect.Width == 0 || rect.Height == 0)
            continue;

        ReceiverIndices.clear();
        GatherProjectedShadowReceivers(Queue->Opaque(), swept,
                                       Budgets->MaxReceiversPerCaster,
                                       ReceiverIndices);
        if (ReceiverIndices.empty())
            continue;

        ProjectedShadowProjection projection;
        projection.Uniform.CameraViewProjection = Camera->ViewProjection;
        projection.Uniform.ShadowViewProjection = viewProjection;
        projection.Uniform.TileScaleBias =
            ProjectedShadowTileUvScaleBias(grid, thisTile);
        projection.Uniform.Params = Vec4(Lights->ProjectedShadowDarkness,
                                         Lights->ProjectedShadowFadeStart,
                                         static_cast<float>(atlasIndex), 0.0f);
        projection.ScissorX = rect.X;
        projection.ScissorY = rect.Y;
        projection.ScissorWidth = rect.Width;
        projection.ScissorHeight = rect.Height;
        projection.FirstReceiver =
            static_cast<std::uint32_t>(Output->Receivers.size());

        for (const std::uint32_t itemIndex : ReceiverIndices)
        {
            const RenderQueueItem& item = Queue->Opaque()[itemIndex];
            const GpuStaticMesh* receiverMesh = Meshes->Get(item.Mesh);
            if (receiverMesh == nullptr
                || item.SectionIndex >= receiverMesh->Sections.size())
                continue;
            const StaticMeshSection& section =
                receiverMesh->Sections[item.SectionIndex];
            Output->Receivers.push_back(ProjectedReceiverDraw{
                .Vertex = receiverMesh->VertexBuffer,
                .Index = receiverMesh->IndexBuffer,
                .IndexCount = section.IndexCount,
                .IndexOffset = section.IndexOffset,
                .World = item.WorldMatrix,
            });
        }
        projection.ReceiverCount =
            static_cast<std::uint32_t>(Output->Receivers.size())
            - projection.FirstReceiver;
        if (projection.ReceiverCount > 0)
            Output->Casters.push_back(projection);
    }

    Output->Ready = !Output->Casters.empty();
    if (stats != nullptr)
    {
        stats->ProjectedCastersRendered =
            static_cast<std::uint32_t>(Output->Casters.size());
        stats->ProjectedReceiverDraws =
            static_cast<std::uint32_t>(Output->Receivers.size());
    }
}
